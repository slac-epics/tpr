#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<dbAccess.h>           /* EPICS Database access definitions                   */
#include<devSup.h>             /* EPICS Device support layer structures and symbols   */
#include<epicsExport.h>        /* EPICS Symbol exporting macro definitions            */
#include<longoutRecord.h>
#include<eventRecord.h>
#include<waveformRecord.h>
#include<longinRecord.h>
#include<biRecord.h>
#include"HiResTime.h"
#include"drvTpr.h"
#include"timingFifoApi.h"
#include"bsaCallbackApi.h"

static BsaTimingCallback        gpBsaTimingCallback   = NULL;
static void                    *gpBsaTimingUserPvt    = NULL;

typedef struct fifoInfoStruct {
    tprEvent        event;
    long long       fifo_tsc;
} fifoInfo;

#define MAX_TS_QUEUE           512
#define MAX_TS_QUEUE_MASK      511
#define MAX_EVENT 256
static struct timeInfo {
    tprCardStruct *pCard;
    int            card;
    int            chan;
    long long      idx;
    fifoInfo       message[MAX_TS_QUEUE];
} ti[MAX_EVENT];

#define MAX_ALLTS_QUEUE           2048
#define MAX_ALLTS_QUEUE_MASK      2047
static EventTimingData   allTimingInfo[MAX_ALLTS_QUEUE];
static long long        allTimingInfoIdx = 0;

struct dpvtStruct {
    tprCardStruct *pCard;
    int            card;
    int            chan;
    int            idx;
};

static struct BsaTimingData pattern = {
    0LL,
    {0, 0},
    0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL
};

static struct lookup {
    char *name;
    int   index;
} fieldnames[] = {
    {"ENUM",    ENUM},
    {"EVT",     EVT},
    {"MESSAGE", MESSAGE},
    {"PIDL",    PIDL},
    {"PIDH",    PIDH},
    {"TMODE",   TMODE},
    {NULL,      -1}
};

static struct lookup *do_lookup(DBLINK *l, int *card, int *chan)
{
    char buf[64], c;
    int i;
    if (sscanf(l->value.instio.string, "%d %c %s", card, &c, buf) != 3) {
        fprintf(stderr, "Illegal link format: %s\n", l->value.instio.string);
        return NULL;
    }
    if (c >= '0' && c <= '9')
        *chan = c - '0';
    else if (c >= 'A' && c <= 'B')
        *chan = c - 'A' + 10;
    else if (c == '-')
        *chan = -1;
    else {
        fprintf(stderr, "Illegal channel: %c (%s)\n", c, l->value.instio.string);
        return NULL;
    }
    for (i = 0; fieldnames[i].name != NULL; i++) {
        if (!strcmp(fieldnames[i].name, buf))
            return &fieldnames[i];
    }
    fprintf(stderr, "Unknown field name: %s\n", buf);
    return NULL;
}

static struct dpvtStruct *makeDpvt(tprCardStruct *pCard, int card, int chan, int idx)
{
    struct dpvtStruct *p = (struct dpvtStruct *)malloc(sizeof(struct dpvtStruct));
    p->pCard = pCard;
    p->card = card;
    p->chan = chan;
    p->idx = idx;
    return p;
}

#define INITDECL(TYPE,FIELD)                                                  \
    static epicsStatus tprInit##TYPE(TYPE *pRec)                              \
    {                                                                         \
        int card, chan;                                                       \
        tprCardStruct *pCard;                                                 \
        struct lookup *l = do_lookup(&pRec->FIELD, &card, &chan);             \
        if (!l)                                                               \
            return S_dev_badSignal;                                           \
        pCard = tprGetCard(card);                                             \
        if (!pCard)                                                           \
            return S_dev_badCard;                                             \
        pCard->client[chan].TYPE[l->index & 255] = pRec;                      \
        pRec->dpvt = makeDpvt(pCard, card, chan, l->index);                   \
        return 0;                                                             \
    }

static epicsStatus tprProclongoutRecord(longoutRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    if (dpvt->idx == ENUM) {
        if (pRec->mlst >= 0 && pRec->mlst < MAX_EVENT)
            ti[pRec->mlst].pCard = NULL;
        if (pRec->val < 0 || pRec->val >= MAX_EVENT)
            return 0;
        ti[pRec->val].pCard = pCard;
        ti[pRec->val].card = dpvt->card;
        ti[pRec->val].chan = dpvt->chan;
    } else
        return 1;
    return 0;
}

static epicsStatus tprProceventRecord(eventRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    int evt = pCard->client[dpvt->chan].longoutRecord[0]->val;
    if (evt < 0 || evt >= MAX_EVENT || !ti[evt].idx) {
        return 1;
    }
    if (dpvt->idx == EVT) {
        fifoInfo	*pFifoInfo = &ti[evt].message[(ti[evt].idx-1) & MAX_TS_QUEUE_MASK];
        pRec->time.secPastEpoch = pFifoInfo->event.seconds;
        pRec->time.nsec = pFifoInfo->event.nanosecs;
    } else
        return 1;
    return 0;
}

static epicsStatus tprProcwaveformRecord(waveformRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    int evt = pCard->client[dpvt->chan].longoutRecord[0]->val;
    if (evt < 0 || evt >= MAX_EVENT || !ti[evt].idx)
        return 1;
    if (dpvt->idx == MESSAGE) {
        fifoInfo	*pFifoInfo = &ti[evt].message[(ti[evt].idx-1) & MAX_TS_QUEUE_MASK];
        memcpy(pRec->bptr, &pFifoInfo->event, sizeof(tprEvent));
        pRec->nord = sizeof(tprEvent) / sizeof(u32);
        pRec->time.secPastEpoch = pFifoInfo->event.seconds;
        pRec->time.nsec = pFifoInfo->event.nanosecs;
    } else
        return 1;
    return 0;
}

static epicsStatus tprProclonginRecord(longinRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    int evt = pCard->client[dpvt->chan].longoutRecord[0]->val;
    fifoInfo    *   pFifoInfo;
    if (evt < 0 || evt >= MAX_EVENT)
        return 1;
    if (!ti[evt].idx)
        return 1;
    pFifoInfo = &ti[evt].message[(ti[evt].idx-1) & MAX_TS_QUEUE_MASK];
    switch (dpvt->idx) {
    case PIDL:
        pRec->val = pFifoInfo->event.pulseID;
        break;
    case PIDH:
        pRec->val = pFifoInfo->event.pulseID >> 32;
        break;
    default:
        return 1;
    }
    pRec->time.secPastEpoch = pFifoInfo->event.seconds;
    pRec->time.nsec = pFifoInfo->event.nanosecs;
    return 0;
}

static epicsStatus tprProcbiRecord(biRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    fifoInfo    *   pFifoInfo;
    int evt = pCard->client[dpvt->chan].longoutRecord[0]->val;
    if (evt < 0 || evt >= MAX_EVENT)
        return 1;
    if (!ti[evt].idx)
        return 1;
    if (dpvt->idx == TMODE) {
        pFifoInfo = &ti[evt].message[(ti[evt].idx-1) & MAX_TS_QUEUE_MASK];
        pRec->val = pCard->client[dpvt->chan].mode;
        pRec->time.secPastEpoch = pFifoInfo->event.seconds;
        pRec->time.nsec = pFifoInfo->event.nanosecs;
        pRec->udf = 0;
    }
    return 2;
}

#define DSETDECL(TYPE)                                                        \
static dsetStruct devTprC##TYPE = {                                           \
    5, (DEVSUPFUN)NULL, (DEVSUPFUN)NULL, (DEVSUPFUN)tprInit##TYPE,            \
    (DEVSUPFUN)NULL, (DEVSUPFUN)tprProc##TYPE                                 \
};                                                                            \
epicsExportAddress (dset, devTprC##TYPE);

INITDECL(longoutRecord,out);
INITDECL(eventRecord,inp);
INITDECL(waveformRecord,inp);
INITDECL(longinRecord,inp);
INITDECL(biRecord,inp);

DSETDECL(longoutRecord);
DSETDECL(waveformRecord);
DSETDECL(longinRecord);
DSETDECL(biRecord);

static epicsStatus tprIointeventRecord(int cmd, eventRecord *pRec, IOSCANPVT *iopvt)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    *iopvt = pCard->client[dpvt->chan].ioscan;
    return 0;
}

static dsetStruct devTprCeventRecord = {
    5, (DEVSUPFUN)NULL, (DEVSUPFUN)NULL, (DEVSUPFUN)tprIniteventRecord,
    (DEVSUPFUN)tprIointeventRecord, (DEVSUPFUN)tprProceventRecord
};
epicsExportAddress (dset, devTprCeventRecord);

int tprCurrentTimeStamp(epicsTimeStamp *epicsTime_ps, int eventCode)
{
    EventTimingData  *pInfo      = &allTimingInfo[(allTimingInfoIdx-1)&MAX_ALLTS_QUEUE_MASK];
    epicsTime_ps->secPastEpoch  = pInfo->fifo_time.secPastEpoch;
    epicsTime_ps->nsec          = pInfo->fifo_time.nsec;
    return epicsTimeOK;
}

int timingGetCurTimeStamp(epicsTimeStamp  *pTimeStampDest)
{
    return tprCurrentTimeStamp(pTimeStampDest, 0);
}

/* timingGetEventTimeStamp() needed to support timingFifoApi */
int timingGetEventTimeStamp(epicsTimeStamp *epicsTime_ps, int eventCode)
{
    if (eventCode <= 0) {
        tprCurrentTimeStamp( epicsTime_ps, eventCode );
        return epicsTimeOK;
    }
    if (eventCode >= MAX_EVENT || !ti[eventCode].pCard || !ti[eventCode].idx) {
        return epicsTimeERROR;
    }

    /* TODO: This doesn't look threadsafe for 32bit cpus.  */
    long long       msgIndex    = ti[eventCode].idx - 1;
    fifoInfo    *   pFifoInfo   = &ti[eventCode].message[msgIndex & MAX_TS_QUEUE_MASK];
    epicsTime_ps->secPastEpoch  = pFifoInfo->event.seconds;
    epicsTime_ps->nsec          = pFifoInfo->event.nanosecs;
    return epicsTimeOK;
}


void tprMessageProcess(tprCardStruct *pCard, int chan, tprHeader *message)
{
    if (pCard->r && pCard->config.mode < 0) {
        return;
    }
    switch (message->tag & TAG_TYPE_MASK) {
    case TAG_EVENT: {
        long long tsc = GetHiResTicks();
        tprEvent *e = (tprEvent *)message; 
        EventTimingData  *pInfo      = &allTimingInfo[(allTimingInfoIdx-1)&MAX_ALLTS_QUEUE_MASK];
        /* We'd like to check e->pulseID > lastfid, but this doesn't work for LCLS1! */
        if (!allTimingInfoIdx || 
            (e->seconds > pInfo->fifo_time.secPastEpoch ||
             (e->seconds == pInfo->fifo_time.secPastEpoch &&
              e->nanosecs > pInfo->fifo_time.nsec))) {
            EventTimingData  *pTimingFifoInfo        = &allTimingInfo[allTimingInfoIdx&MAX_ALLTS_QUEUE_MASK];
            pTimingFifoInfo->fifo_tsc               = tsc;
            pTimingFifoInfo->fifo_fid               = e->pulseID;
            pTimingFifoInfo->fifo_time.secPastEpoch = e->seconds;
            pTimingFifoInfo->fifo_time.nsec         = e->nanosecs;
            allTimingInfoIdx++;
        }
        if (!pCard->client[chan].longoutRecord[0])
            return;
        /* Add timing info for this event to its fifo */
        int evt = pCard->client[chan].longoutRecord[0]->val;
        if (evt < 0 || evt >= MAX_EVENT)
            return;
        fifoInfo *pFifoInfo = &ti[evt].message[ti[evt].idx++ & MAX_TS_QUEUE_MASK];
        pFifoInfo->fifo_tsc = tsc;
        memcpy(&pFifoInfo->event, e, sizeof(tprEvent));

        /* Generate the approprite ioscan for this channel */
        pCard->client[chan].mode = (message->tag & TAG_LCLS1) ? 0 : 1;
        /* Generate the approprite ioscan for this channel */
        scanIoRequest(pCard->client[chan].ioscan);
        break;
    }
    case TAG_BSA_CTRL: 
        if (gpBsaTimingCallback) {
            tprBSAControl *bc = (tprBSAControl *)message;
            pattern.edefInitMask = bc->init;
            pattern.edefActiveMask = 0;
            pattern.edefAvgDoneMask = 0;
            pattern.edefAllDoneMask = 0;
            pattern.edefUpdateMask = 0;
            pattern.edefMinorMask = ((pattern.edefMinorMask & ~bc->init) |
                                     (bc->init & bc->minor));
            pattern.edefMajorMask = ((pattern.edefMajorMask & ~bc->init) |
                                     (bc->init & bc->major));
            pattern.timeStamp.nsec = bc->nanosecs;
            pattern.timeStamp.secPastEpoch = bc->seconds;
            pattern.pulseId = 0;  /* This will be added to the message. */
            (*gpBsaTimingCallback)(gpBsaTimingUserPvt, &pattern);
        }
        break;
    case TAG_BSA_EVENT:
        if (gpBsaTimingCallback) {
            tprBSAEvent *be = (tprBSAEvent *)message;
            pattern.edefInitMask = 0;
            pattern.edefActiveMask = be->active;
            pattern.edefAvgDoneMask = be->avgdone;
            /* pattern.edefAllDoneMask = ?? */
            pattern.edefUpdateMask = be->update;
            pattern.timeStamp.nsec = be->nanosecs;
            pattern.timeStamp.secPastEpoch = be->seconds;
            pattern.pulseId = be->PID;
            (*gpBsaTimingCallback)(gpBsaTimingUserPvt, &pattern);
        }
        break;
    }
}

/**
 * RegisterBsaTimingCallback is called by the BSA client to register a callback function
 * for new BsaTimingData.
 *
 * The pUserPvt pointer can be used to establish context or set to 0 if not needed.
 *
 * Timing services must support this RegisterBsaTimingCallback() function and call the
 * callback function once for each new BsaTimingData to be compliant w/ this timing BSA API.
 *
 * The Timing service may support more than one BSA client, but is allowed to refuse
 * attempts to register multiple BSA callbacks.
 *
 * Each timing service should provide it's timing BSA code using a unique library name
 * so we can have EPICS IOC's that build applications for multiple timing service types.
 */
int RegisterBsaTimingCallback( BsaTimingCallback callback, void * pUserPvt )
{
    if ( gpBsaTimingCallback != NULL ) {
        fprintf(stderr, "BsaTimingCallback already set!\n");
        return -1;
    }

    gpBsaTimingCallback = callback;
    gpBsaTimingUserPvt  = pUserPvt;
    return 0;
}

/**
 * The timingGetFifoInfo() call allows a timingFifo client to access a
 * FIFO queue of the last MAX_TS_QUEUE eventCode arrival timestamps.
 * Each client has their own index position in the queue which can be controlled w/ the incr argument.
 * Clients should not write directly to the index value.
 *   - incr==TS_INDEX_INIT: index set to the position of the most recent eventCode arrival.
 *   - incr!=TS_INDEX_INIT: index set to index+incr.  Use +1/-1 to advance up and down the queue
 *   - incr==1: Set to 1 once synced to fetch one synced timestamp per sample indefinitely as
 *     long as you don't overrun or underrun.
 *
 * The index is adjusted according to the incr argument before reading the
 * eventCode arrival time info from the FIFO
 *
 * FIFO overruns occur by trying to fetch another timestamp when you've already fetched
 * the most recent one.
 *
 * FIFO underruns occur by trying to fetch the next timestamp after it's
 * position in the FIFO is reused by a new eventCode arrival time.
 *
 * The timing service must guarantee that all values returned via this call are for the same pulse.
 *
 * Return value:
 *   - 0 on success
 *   - epicsTimeError for NULL return ptrs, invalid index, or FIFO overflow/underflow.
 *
 * These values are returned via ptr:
 *   - index ptr: 64 bit FIFO position. Adjusted according to the incr
 *     argument before reading the info from the FIFO
 *   - pFifoInfoDest ptr: epicsTimestamp, 64 bit fiducial, 64 bit tsc, and status
 */
int timingGetFifoInfo(
    unsigned int           eventCode,
    int                    incr,
    unsigned long long    *idx,
    EventTimingData        *pFifoInfoDest)
{
    fifoInfo    *pFifoInfo;
    if (!pFifoInfoDest || !idx || eventCode >= MAX_EVENT)
        return epicsTimeERROR;
    /* TODO: 64 bit ti[eventCode].idx access isn't threadsafe for 32bit cpus.  */
    if (incr == TS_INDEX_INIT)
        *idx = ti[eventCode].idx - 1;
    else
        *idx += incr;
    if (*idx + MAX_TS_QUEUE < ti[eventCode].idx || *idx >= ti[eventCode].idx)
        return epicsTimeERROR;
    pFifoInfo = &ti[eventCode].message[*idx & MAX_TS_QUEUE_MASK];
    pFifoInfoDest->fifo_time.secPastEpoch    = pFifoInfo->event.seconds;
    pFifoInfoDest->fifo_time.nsec            = pFifoInfo->event.nanosecs;
    pFifoInfoDest->fifo_fid                  = pFifoInfo->event.pulseID;
    pFifoInfoDest->fifo_tsc                  = pFifoInfo->fifo_tsc;
    return 0;
}

TimingPulseId timingGetLastFiducial()
{
    TimingPulseId pulseID = allTimingInfo[(allTimingInfoIdx-1)&MAX_ALLTS_QUEUE_MASK].fifo_fid;
    if (pulseID == 0x1ffff)
        return TIMING_PULSEID_INVALID;   /* Convert old LCLS1 invalid to new! */
    else
        return pulseID;
}

/* TODO: Add support for this timingFifoApi function */
TimingPulseId timingGetFiducialForTimeStamp(epicsTimeStamp timeStamp)
{
    int i = (allTimingInfoIdx-1) & MAX_ALLTS_QUEUE_MASK;
    int cnt;

    /* Why 10?  Well, we need at least 1, and let's just be a bit conservative here. */
    for (cnt = 0; cnt < MAX_ALLTS_QUEUE - 10; cnt++) {
        /* Found a match! */
        if (allTimingInfo[i].fifo_time.secPastEpoch == timeStamp.secPastEpoch &&
            allTimingInfo[i].fifo_time.nsec == timeStamp.nsec)
            return allTimingInfo[i].fifo_fid;
        /* If we've gone back too far, there is no match! */
        if (allTimingInfo[i].fifo_time.secPastEpoch > timeStamp.secPastEpoch ||
            (allTimingInfo[i].fifo_time.secPastEpoch == timeStamp.secPastEpoch &&
             allTimingInfo[i].fifo_time.nsec > timeStamp.nsec))
            return TIMING_PULSEID_INVALID;
        /* Go back one, rolling around if necessary. */
        if (--i < 0)
            i = MAX_ALLTS_QUEUE - 1;
    }
    return TIMING_PULSEID_INVALID;
}

void timingPrintNow(int e)
{
    EventTimingData  *pInfo      = &allTimingInfo[(allTimingInfoIdx-1)&MAX_ALLTS_QUEUE_MASK];
    printf("0x%lx: %08x.%08x\n", pInfo->fifo_fid, pInfo->fifo_time.secPastEpoch, pInfo->fifo_time.nsec);
    if (e >= 0) {
        int i;
        long long l = ti[e].idx;
        printf("Event %d --> index %lld & MASK = %lld\n", e, l - 1, (l - 1) & MAX_TS_QUEUE_MASK);
        for (i = 1; i <= 3; i++) {
            fifoInfo    *p = &ti[e].message[(l - i) & MAX_TS_QUEUE_MASK];
            printf("%8d: 0x%lx: %08x.%08x (%lld)\n", (int)((l - i) & MAX_TS_QUEUE_MASK), p->event.pulseID, p->event.seconds, p->event.nanosecs, p->fifo_tsc);
        }
    }
}
