#include <epicsStdlib.h>        /* EPICS Standard C library support routines                      */
#include <epicsStdio.h>         /* EPICS Standard C I/O support routines                          */
#include <epicsTypes.h>         /* EPICS Architecture-independent type definitions                */
#include <epicsInterrupt.h>     /* EPICS Interrupt context support routines                       */
#include <epicsMutex.h>         /* EPICS Mutex support library                                    */
#include <errno.h>
#include <string.h>

#include <alarm.h>              /* EPICS Alarm status and severity definitions                    */
#include <dbAccess.h>           /* EPICS Database access definitions                              */
#include <dbEvent.h>            /* EPICS Event monitoring routines and definitions                */
#include <dbScan.h>             /* EPICS Database scan routines and definitions                   */
#include <devLib.h>             /* EPICS Device hardware addressing support library               */
#include <devSup.h>             /* EPICS Device support layer structures and symbols              */
#include <ellLib.h>             /* EPICS Linked list support library                              */
#include <errlog.h>             /* EPICS Error logging support library                            */
#include <recGbl.h>             /* EPICS Record Support global routine definitions                */
#include <registryFunction.h>   /* EPICS Registry support library                                 */
#include <epicsExport.h>        /* EPICS Symbol exporting macro definitions                       */

#include <boRecord.h>
#include <biRecord.h>
#include <mbboRecord.h>
#include <mbboDirectRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include "drvTpr.h"

struct dpvtStruct {
    tprCardStruct *pCard;
    int            lcls;
    int            chan;
    int            idx;
};

static struct lookup {
    char *name;
    int   index;
    int   isglobal;
} fieldnames[] = {
    {"CRESET",  CRESET, 1},
    {"XBAR",    XBAR, 1},
    {"MODE",    MODE, 1},
    {"FRAME",   FRAME, 1},
    {"RXLINK",  RXLINK, 1},
    {"ENABLE",  ENABLE, 0},
    {"BSAEN",   BSAEN, 0},
    {"TRIGEN",  TRIGEN, 0},
    {"POL",     POL, 0},
    {"EVENT",   EVENT, 0},
    {"DMASK",   DMASK, 0},
    {"DMODE",   DMODE, 0},
    {"TSMASK",  TSMASK, 0},
    {"SEQ",     SEQ, 0},
    {"BSADEL",  BSADEL, 0},
    {"BSAWID",  BSAWID, 0},
    {"TRGDEL",  TRGDEL, 0},
    {"TRGWID",  TRGWID, 0},
    {"TRGFDEL", TRGFDEL, 0},
    {"CHANGE",  CHANGE, 0},
    {"COUNT",   COUNT, 0},
    {NULL,      -1, 0}
};

static struct {
    int id;
    char *name;
} initorder[] = {
    { POL,       "POL" },
    { EVENT,     "EVENT" },
    { DMASK,     "DMASK" },
    { DMODE,     "DMODE" },
    { TSMASK,    "TSMASK" },
    { SEQ,       "SEQ" },
    { BSADEL,    "BSADEL" },
    { BSAWID,    "BSAWID" },
    { TRGDEL,    "TRGDEL" },
    { TRGWID,    "TRGWID" },
    { TRGFDEL,   "TRGFDEL" },
    { BSAEN,     "BSAEN" },          // Put the enables last!!!
    { ENABLE,    "ENABLE" },
    { TRIGEN,    "TRIGEN" },
    { -1,        NULL },
};

static struct lookup *do_lookup(DBLINK *l, int *card, int *lcls, int *chan)
{
    char buf[64], c;
    int i;
    if (sscanf(l->value.instio.string, "%d %d %c %s", card, lcls, &c, buf) != 4) {
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
    (*lcls)--;
    for (i = 0; fieldnames[i].name != NULL; i++) {
        if (!strcmp(fieldnames[i].name, buf))
            return &fieldnames[i];
    }
    fprintf(stderr, "Unknown field name: %s\n", buf);
    return NULL;
}

static struct dpvtStruct *makeDpvt(tprCardStruct *pCard, int lcls, int chan, int idx)
{
    struct dpvtStruct *p = (struct dpvtStruct *)malloc(sizeof(struct dpvtStruct));
    p->pCard = pCard;
    p->lcls = lcls;
    p->chan = chan;
    p->idx = idx;
    return p;
}

int tprGetConfig(tprCardStruct *pCard, int chan, int reg)
{
    switch (reg & 0xffffff00) {
    case 0x000:
        return pCard->config.global.boRecord[reg & 255]->val;
    case 0x100:
        return pCard->config.lcls[pCard->config.mode][chan].boRecord[reg & 255]->val;
    case 0x200:
        return pCard->config.lcls[pCard->config.mode][chan].mbboRecord[reg & 255]->rval;
    case 0x300:
        return pCard->config.lcls[pCard->config.mode][chan].mbboDirectRecord[reg & 255]->val;
    case 0x400:
        return pCard->config.lcls[pCard->config.mode][chan].longoutRecord[reg & 255]->val;
    case 0x500:
        return pCard->config.lcls[pCard->config.mode][chan].longinRecord[reg & 255]->val;
    }
    return 0;
}

// Return 1 if we need to flag a change, 0 otherwise.
int tprWrite(tprCardStruct *pCard, int reg, int chan, int value)
{
    if (!pCard->r)    // Not the master, so we really shouldn't be calling this at all!
        return 0;
    switch (reg) {
    case CRESET:
        WDEBUG(countReset, 1);
        pCard->r->countReset = 1;
        WDEBUG(countReset, 0);
        pCard->r->countReset = 0;
        pCard->config.global.boRecord[CRESET]->val = 0;
        // Monitor?  Or RVAL?
        return 0;
    case XBAR:
        WDEBUG(xbarOut[2], value ? 0 : 1);
        WDEBUG(xbarOut[3], value ? 1 : 0);
        pCard->r->xbarOut[2] = value ? 0 : 1;
        pCard->r->xbarOut[3] = value ? 1 : 0;
        return 0;
    case MODE: {
        /* The big one: we're switching modes! */
        int i, j, csr;

        if (value == pCard->config.mode) /* Wishful thinking! */
            return 0;
        for (i = 0; i < 12; i++) {       /* Everything off. */
            if (tprDebug & TPR_DEBUG_WRITE) printf("i=%d\n", i);
            WDEBUG(channel[i].control, 0);
            pCard->r->channel[i].control = 0;
            WDEBUG(trigger[i].control, i);
            pCard->r->trigger[i].control = i;
        }
        pCard->config.mode = value;      /* Switch modes */
        csr = pCard->r->CSR;
        if (value)
            csr |= 0x10;
        else
            csr &= ~0x10;
        WDEBUG(CSR, csr);
        pCard->r->CSR = csr;
        for (i = 0; i < 12; i++) {       /* Tell everything there has been a change. */
            dbProcess((dbCommon *)pCard->config.lcls[0][i].longinRecord[0]);
            dbProcess((dbCommon *)pCard->config.lcls[1][i].longinRecord[0]);
        }
        for (i = 0; i < 12; i++) {       /* Set the parameters */
            if (tprDebug & TPR_DEBUG_WRITE) printf("i=%d\n", i);
            for (j = 0; initorder[j].id >= 0; j++) {
                if (tprDebug & TPR_DEBUG_WRITE) printf("%s\n", initorder[j].name);
                tprWrite(pCard, initorder[j].id, i, tprGetConfig(pCard, i, initorder[j].id));
            }
        }
        return 0;
    }
    case ENABLE:
        if (value)
            value = pCard->r->channel[chan].control | 5;
        else
            value = pCard->r->channel[chan].control & ~5;
        WDEBUG(channel[chan].control, value);
        pCard->r->channel[chan].control = value;
        return 1;
    case BSAEN:
        if (value)
            value = pCard->r->channel[chan].control | 2;
        else
            value = pCard->r->channel[chan].control & ~2;
        WDEBUG(channel[chan].control, value);
        pCard->r->channel[chan].control = value;
        return 0;
    case TRIGEN:
        if (value)
            value = pCard->r->trigger[chan].control | 0x80000000;
        else
            value = pCard->r->trigger[chan].control & 0x7fffffff;
        value |= chan;
        WDEBUG(trigger[chan].control, value);
        pCard->r->trigger[chan].control = value;
        return 1;
    case POL:
        if (value)
            value = pCard->r->trigger[chan].control | 0x00010000;
        else
            value = pCard->r->trigger[chan].control & 0xfffeffff;
        value |= chan;
        WDEBUG(trigger[chan].control, value);
        pCard->r->trigger[chan].control = value;
        return 1;
    case EVENT: {
        u32 eventSelect = pCard->r->channel[chan].eventSelect;
        if (value < 7) {           // Fixed rate
            eventSelect = ES_DESTSELECT(eventSelect) | ES_FIXED(value);
        } else if (value < 13) {   // AC rate
            eventSelect = ES_DESTSELECT(eventSelect) | ES_AC(value - 7, tprGetConfig(pCard, chan, TSMASK));
        } else if (value == 13) {  // Sequence
            eventSelect = ES_DESTSELECT(eventSelect) | ES_SEQ(tprGetConfig(pCard, chan, SEQ));
        }
        WDEBUG(channel[chan].eventSelect, eventSelect);
        pCard->r->channel[chan].eventSelect = eventSelect;
        return 1;
    }
    case DMASK: {
        u32 eventSelect = pCard->r->channel[chan].eventSelect;
        eventSelect = ES_RATESELECT(eventSelect) | ES_DMODESELECT(eventSelect) | ES_DMASK(value);
        WDEBUG(channel[chan].eventSelect, eventSelect);
        pCard->r->channel[chan].eventSelect = eventSelect;
        return 1;
    }
    case DMODE: {
        u32 eventSelect = pCard->r->channel[chan].eventSelect;
        eventSelect = ES_RATESELECT(eventSelect) | ES_DMODE(value) | ES_DMASKSELECT(eventSelect);
        WDEBUG(channel[chan].eventSelect, eventSelect);
        pCard->r->channel[chan].eventSelect = eventSelect;
        return 1;
    }
    case TSMASK: {
        u32 eventSelect = pCard->r->channel[chan].eventSelect;
        if (ES_TYPESELECT(eventSelect) != ES_TYPE_AC)
            return 0;
        eventSelect = ES_DESTSELECT(eventSelect) | ES_ACSELECT(eventSelect) | ES_TS(value);
        WDEBUG(channel[chan].eventSelect, eventSelect);
        pCard->r->channel[chan].eventSelect = eventSelect;
        return 1;
    }
    case SEQ: {
        u32 eventSelect = pCard->r->channel[chan].eventSelect;
        if (ES_TYPESELECT(eventSelect) != ES_TYPE_SEQ)
            return 0;
        eventSelect = ES_DESTSELECT(eventSelect) | ES_SEQ(value);
        WDEBUG(channel[chan].eventSelect, eventSelect);
        pCard->r->channel[chan].eventSelect = eventSelect;
        return 1;
    }
    case BSADEL:
        if (value < 0) {
            WDEBUG(channel[chan].bsadelay, ES_BSANEG(value));
            pCard->r->channel[chan].bsadelay = ES_BSANEG(value);
        } else {
            WDEBUG(channel[chan].bsadelay, ES_BSAPOS(value));
            pCard->r->channel[chan].bsadelay = ES_BSAPOS(value);
        }
        return 0;
    case BSAWID:
        WDEBUG(channel[chan].bsawidth, value);
        pCard->r->channel[chan].bsawidth = value;
        return 0;
    case TRGDEL:
        WDEBUG(trigger[chan].delay, value);
        pCard->r->trigger[chan].delay = value;
        return 1;
    case TRGWID:
        WDEBUG(trigger[chan].width, value);
        pCard->r->trigger[chan].width = value;
        return 1;
    case TRGFDEL:
        if (pCard->config.mode == 0) /* Not for LCLS-I! */
            return 0;
        WDEBUG(trigger[chan].delayTap, value);
        pCard->r->trigger[chan].delayTap = value;
        return 1;
    }
    return 0;
}

#define INITDECL(TYPE,FIELD)                                                  \
    static epicsStatus tprInit##TYPE(TYPE *pRec)                              \
    {                                                                         \
        int card, lcls, chan;                                                 \
        tprCardStruct *pCard;                                                 \
        struct lookup *l = do_lookup(&pRec->FIELD, &card, &lcls, &chan);      \
        if (!l)                                                               \
            return S_dev_badSignal;                                           \
        pCard = tprGetCard(card);                                             \
        if (!pCard)                                                           \
            return S_dev_badCard;                                             \
        if (l->isglobal)                                                      \
            pCard->config.global.TYPE[l->index & 255] = pRec;                 \
        else                                                                  \
            pCard->config.lcls[lcls][chan].TYPE[l->index & 255] = pRec;       \
        pRec->dpvt = makeDpvt(pCard, lcls, chan, l->index);                   \
        return 0;                                                             \
    }

/*
 * Logic here:
 *    dpvt->lcls == -2                 --> always write!
 *    pCard->config.mode == -1         --> We aren't initialized, don't write 
 *                                         anything except the "always write" values.
 *    dpvt->lcls >= 0                  --> Not universal (LCLS-I or LCLS-II, not both).
 *    dpvt->lcls != pCard->config.mode --> This is for the "other" set of values.
 */
#define PROCDECL(TYPE, FIELD)                                                 \
    static epicsStatus tprProc##TYPE(TYPE *pRec)                              \
    {                                                                         \
        struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;            \
        tprCardStruct *pCard = dpvt->pCard;                                   \
        int mode = pCard->config.mode;                                        \
        if ((dpvt->lcls != -2) && (mode < 0 || (dpvt->lcls >= 0 && dpvt->lcls != mode))) \
            return 0;                                                         \
        if (tprWrite(pCard, dpvt->idx, dpvt->chan, pRec->FIELD))              \
            scanIoRequest(pCard->config.lcls[mode][dpvt->chan].ioscan);       \
        return 0;                                                             \
    }

#define DSETDECL(TYPE)                                                        \
static dsetStruct devTpr##TYPE = {                                            \
    5, (DEVSUPFUN)NULL, (DEVSUPFUN)NULL, (DEVSUPFUN)tprInit##TYPE,            \
    (DEVSUPFUN)NULL, (DEVSUPFUN)tprProc##TYPE                                 \
};                                                                            \
epicsExportAddress (dset, devTpr##TYPE);

static epicsStatus tprProcbiRecord(biRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    int mode = pCard->config.mode;
    unsigned int csr;
    if (mode < 0 || (dpvt->lcls >= 0 && dpvt->lcls != mode))
            return 0;
    if (dpvt->lcls > 0) {
    } else {
        switch (dpvt->idx) {
        case RXLINK:
            csr = pCard->r->CSR;
            pRec->val = (csr >> 1) & 1;
            pRec->udf = 0;
            break;
        }
    }
    return 2;
}

INITDECL(boRecord, out)
INITDECL(mbboRecord, out)
INITDECL(mbboDirectRecord, out)
INITDECL(longoutRecord, out)
INITDECL(longinRecord, inp)
INITDECL(biRecord, inp)

PROCDECL(boRecord,val)
PROCDECL(mbboRecord,rval)
PROCDECL(mbboDirectRecord,val)
PROCDECL(longoutRecord,val)

DSETDECL(boRecord)
DSETDECL(mbboRecord)
DSETDECL(mbboDirectRecord)
DSETDECL(longoutRecord)
DSETDECL(biRecord)

static epicsStatus tprIointlonginRecord(int cmd, longinRecord *pRec, IOSCANPVT *iopvt)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    *iopvt = pCard->config.lcls[dpvt->lcls][dpvt->chan].ioscan;
    return 0;
}

static epicsStatus tprProclonginRecord(longinRecord *pRec)
{
    struct dpvtStruct *dpvt = (struct dpvtStruct *)pRec->dpvt;
    tprCardStruct *pCard = dpvt->pCard;
    int mode = pCard->config.mode;
    if (mode < 0 || (dpvt->lcls >= 0 && dpvt->lcls != mode))
            return 0;
    if (dpvt->lcls >= 0) {
        switch (dpvt->idx) {
        case CHANGE:
            pRec->val++;
            pRec->udf = 0;
            break;
        case COUNT:
            pRec->val = pCard->r->channel[dpvt->chan].eventCount;
            pRec->udf = 0;
            break;
        }
    } else {
        switch (dpvt->idx) {
        case FRAME:
            pRec->val = pCard->r->frameCount & 0x7fffffff;
            pRec->udf = 0;
            break;
        }
    }
    return 0;
}

static dsetStruct devTprlonginRecord = {
    5, (DEVSUPFUN)NULL, (DEVSUPFUN)NULL, (DEVSUPFUN)tprInitlonginRecord,
    (DEVSUPFUN)tprIointlonginRecord, (DEVSUPFUN)tprProclonginRecord
};

epicsExportAddress (dset, devTprlonginRecord);
