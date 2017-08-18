#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<dbAccess.h>           /* EPICS Database access definitions                              */
#include<devSup.h>             /* EPICS Device support layer structures and symbols              */
#include<epicsExport.h>        /* EPICS Symbol exporting macro definitions                       */
#include<longoutRecord.h>
#include<eventRecord.h>
#include<waveformRecord.h>
#include<longinRecord.h>
#include<biRecord.h>
#include"drvTpr.h"

#define MAX_EVENT 256
#define MAX_TIQ   256
static struct timeInfo {
    tprCardStruct *pCard;
    int            chan;
    long long      idx;
    tprEvent       message[MAX_TIQ];
} ti[MAX_EVENT];

struct dpvtStruct {
    tprCardStruct *pCard;
    int            chan;
    int            idx;
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

static struct dpvtStruct *makeDpvt(tprCardStruct *pCard, int chan, int idx)
{
    struct dpvtStruct *p = (struct dpvtStruct *)malloc(sizeof(struct dpvtStruct));
    p->pCard = pCard;
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
        pRec->dpvt = makeDpvt(pCard, chan, l->index);                         \
        return 0;                                                             \
    }

static epicsStatus tprProclongoutRecord(longoutRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    if (dpvt->idx == ENUM) {
        if (pRec->mlst >= 0 && pRec->mlst < MAX_EVENT)
            ti[pRec->mlst].pCard = NULL;
        if (pRec->val < 0 || pRec->mlst >= MAX_EVENT)
            return 0;
        ti[pRec->val].pCard = pCard;
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
    if (evt < 0 || evt >= MAX_EVENT)
        return 1;
    if (!ti[evt].idx)
        return 1;
    if (dpvt->idx == EVT) {
        tprEvent *e = &ti[evt].message[(ti[evt].idx-1)&(MAX_TIQ-1)];
        pRec->time.secPastEpoch = e->seconds;
        pRec->time.nsec = e->nanosecs;
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
        tprEvent *e = &ti[evt].message[(ti[evt].idx-1)&(MAX_TIQ-1)];
        memcpy(pRec->bptr, e, sizeof(tprEvent));
        pRec->nord = sizeof(tprEvent) / sizeof(u32);
        pRec->time.secPastEpoch = e->seconds;
        pRec->time.nsec = e->nanosecs;
    } else
        return 1;
    return 0;
}

static epicsStatus tprProclonginRecord(longinRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    int evt = pCard->client[dpvt->chan].longoutRecord[0]->val;
    tprEvent *e;
    if (evt < 0 || evt >= MAX_EVENT)
        return 1;
    if (!ti[evt].idx)
        return 1;
    e = &ti[evt].message[(ti[evt].idx-1)&(MAX_TIQ-1)];
    switch (dpvt->idx) {
    case PIDL:
        pRec->val = e->pulseID;
        break;
    case PIDH:
        pRec->val = e->pulseID >> 32;
        break;
    default:
        return 1;
    }
    pRec->time.secPastEpoch = e->seconds;
    pRec->time.nsec = e->nanosecs;
    return 0;
}

static epicsStatus tprProcbiRecord(biRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    tprEvent *e;
    int evt = pCard->client[dpvt->chan].longoutRecord[0]->val;
    if (evt < 0 || evt >= MAX_EVENT)
        return 1;
    if (!ti[evt].idx)
        return 1;
    if (dpvt->idx == TMODE) {
        e = &ti[evt].message[(ti[evt].idx-1)&(MAX_TIQ-1)];
        pRec->val = pCard->client[dpvt->chan].mode;
        pRec->time.secPastEpoch = e->seconds;
        pRec->time.nsec = e->nanosecs;
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

int tprTimeGet(epicsTimeStamp *epicsTime_ps, int eventCode)
{
    if (eventCode < 0 || eventCode >= MAX_EVENT || !ti[eventCode].pCard || !ti[eventCode].idx) {
        printf("tTG! bad %d\n", eventCode);
        return epicsTimeERROR;
    } else {
        tprEvent *e = &ti[eventCode].message[(ti[eventCode].idx-1)&(MAX_TIQ-1)];
        epicsTime_ps->secPastEpoch = e->seconds;
        epicsTime_ps->nsec = e->nanosecs;
        printf("tTG! OK\n");
        return epicsTimeOK;
    }
}

void tprMessageProcess(tprCardStruct *pCard, int chan, tprHeader *message)
{
    if (pCard->config.mode < 0)
        return;
    switch (message->tag & TAG_TYPE_MASK) {
    case TAG_EVENT: {
        tprEvent *e = (tprEvent *)message;
        int evt = pCard->client[chan].longoutRecord[0]->val;
        if (evt < 0 || evt >= MAX_EVENT)
            return;
        memcpy(&ti[evt].message[ti[evt].idx++ & (MAX_TIQ-1)], e, sizeof(tprEvent));
        pCard->client[chan].mode = (message->tag & TAG_LCLS1) ? 0 : 1;
        post_event(evt);
        scanIoRequest(pCard->client[chan].ioscan);
        break;
    }
    case TAG_BSA_CTRL: {
        tprBSAControl *bc = (tprBSAControl *)message;
        printf("BSActrl %p\n", bc);
        break;
    }
    case TAG_BSA_EVENT: {
        tprBSAEvent *be = (tprBSAEvent *)message;
        printf("BSAevent %p\n", be);
        break;
    }
    }
}
