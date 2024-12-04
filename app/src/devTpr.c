//////////////////////////////////////////////////////////////////////////////
// This file is part of 'tpr'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'tpr', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
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
#include "tprdev.h"

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
    {"MSGDLY",  MSGDLY, 1},
    {"FRAME",   FRAME, 1},
    {"RXLINK",  RXLINK, 1},

    {"ENABLE",  ENABLE, 0},
    {"BSAEN",   BSAEN, 0},
    {"TRIGEN",  TRIGEN, 0},
    {"POL",     POL, 0},

    {"RMODE",   RMODE, 0},
    {"FRATE",   FRATE, 0},
    {"ACRATE",  ACRATE, 0},
    {"DMODE",   DMODE, 0},

    {"TSMASK",  TSMASK, 0},
    {"DMASK",   DMASK, 0},

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
    { RMODE,     "RMODE" },
    { FRATE,     "FRATE" },
    { ACRATE,    "ACRATE" },
    { TSMASK,    "TSMASK" },
    { DMASK,     "DMASK" },
    { DMODE,     "DMODE" },
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
    if (pCard->config.mode == 0) {    /* Defaults for non-existant LCLS-I PVs! */
        switch (reg) {
        case RMODE:
            return TPR_ES_TYPE_SEQ;
        case FRATE:
        case ACRATE:
        case TSMASK:
        case DMASK:
            return 0;
        case DMODE:
            return TPR_ES_DMODE_DK;
        }
    }
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
    if (!tprMaster(pCard->devpvt))    // Not the master, so we really shouldn't be calling this at all!
        return 0;
    switch (reg) {
    case CRESET:
        WDEBUG(TPR_COUNTRESET, 1);
        tprRegWrite(pCard->devpvt, TPR_COUNTRESET, 1);
        WDEBUG(TPR_COUNTRESET, 0);
        tprRegWrite(pCard->devpvt, TPR_COUNTRESET, 0);
        pCard->config.global.boRecord[CRESET]->val = 0;
        // Monitor?  Or RVAL?
        return 0;
    case XBAR:
        WDEBUG(TPR_XBAROUT(2), value ? 0 : 1);
        WDEBUG(TPR_XBAROUT(3), value ? 1 : 0);
        tprRegWrite(pCard->devpvt, TPR_XBAROUT(2), value ? 0 : 1);
        tprRegWrite(pCard->devpvt, TPR_XBAROUT(3), value ? 1 : 0);
        return 0;
    case MODE: {
        /* The big one: we're switching modes! */
        int i, j, csr;

        if (value == pCard->config.mode) /* Wishful thinking! */
            return 0;
        for (i = 0; i < 12; i++) {       /* Everything off. */
            if (tprDebug & TPR_DEBUG_WRITE) printf("i=%d\n", i);
            WDEBUG(TPR_CH_CONTROL(i), 0);
            tprRegWrite(pCard->devpvt, TPR_CH_CONTROL(i), 0);
            WDEBUG(TPR_TR_CONTROL(i), i);
            tprRegWrite(pCard->devpvt, TPR_TR_CONTROL(i), i);
        }
        pCard->config.mode = value;      /* Switch modes */
        csr = tprRegRead(pCard->devpvt, TPR_CSR);
        if (value)
            csr |= TPR_CSR_CLKSEL;
        else
            csr &= ~TPR_CSR_CLKSEL;
        WDEBUG(TPR_CSR, csr);
        tprRegWrite(pCard->devpvt, TPR_CSR, csr);
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
            value = tprRegRead(pCard->devpvt, TPR_CH_CONTROL(chan)) | TPR_CC_ENABLE;
        else
            value = tprRegRead(pCard->devpvt, TPR_CH_CONTROL(chan)) & ~TPR_CC_ENABLE;
        WDEBUG(TPR_CH_CONTROL(chan), value);
        tprRegWrite(pCard->devpvt, TPR_CH_CONTROL(chan), value);
        return 1;
    case BSAEN:
        if (value)
            value = tprRegRead(pCard->devpvt, TPR_CH_CONTROL(chan)) | TPR_CC_BSAEN;
        else
            value = tprRegRead(pCard->devpvt, TPR_CH_CONTROL(chan)) & ~TPR_CC_BSAEN;
        WDEBUG(TPR_CH_CONTROL(chan), value);
        tprRegWrite(pCard->devpvt, TPR_CH_CONTROL(chan), value);
        return 0;
    case TRIGEN:
        if (value)
            value = tprRegRead(pCard->devpvt, TPR_TR_CONTROL(chan)) | TPR_TC_ENABLE;
        else
            value = tprRegRead(pCard->devpvt, TPR_TR_CONTROL(chan)) & ~TPR_TC_ENABLE;
        value |= chan; /* The channel map doesn't readback! */
        WDEBUG(TPR_TR_CONTROL(chan), value);
        tprRegWrite(pCard->devpvt, TPR_TR_CONTROL(chan), value);
        return 1;
    case POL:
        if (value)
            value = tprRegRead(pCard->devpvt, TPR_TR_CONTROL(chan)) | TPR_TC_POLARITY;
        else
            value = tprRegRead(pCard->devpvt, TPR_TR_CONTROL(chan)) & ~TPR_TC_POLARITY;
        value |= chan; /* The channel map doesn't readback! */
        WDEBUG(TPR_TR_CONTROL(chan), value);
        tprRegWrite(pCard->devpvt, TPR_TR_CONTROL(chan), value);
        return 1;
    case RMODE: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        switch (value) {
        case TPR_ES_TYPE_FIXED:
            eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_FIXED(tprGetConfig(pCard, chan, FRATE));
            break;
        case TPR_ES_TYPE_AC:
            eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_AC(tprGetConfig(pCard, chan, ACRATE),
                                                                     tprGetConfig(pCard, chan, TSMASK));
            break;
        case TPR_ES_TYPE_SEQ:
            eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_SEQ(tprGetConfig(pCard, chan, SEQ));
            break;
        }
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case FRATE: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        if (TPR_ES_TYPESELECT(eventSelect) != TPR_ES_TYPE(TPR_ES_TYPE_FIXED))
            return 0;
        eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_FIXED(value);
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case ACRATE: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        if (TPR_ES_TYPESELECT(eventSelect) != TPR_ES_TYPE(TPR_ES_TYPE_AC))
            return 0;
        eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_TSSELECT(eventSelect) | TPR_ES_AC(value, 0);
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case TSMASK: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        if (TPR_ES_TYPESELECT(eventSelect) != TPR_ES_TYPE(TPR_ES_TYPE_AC))
            return 0;
        eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_ACSELECT(eventSelect) | TPR_ES_TS(value);
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case SEQ: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        // Write if LCLS-I or we are really in sequencer mode.
        if (pCard->config.mode != 0 && TPR_ES_TYPESELECT(eventSelect) != TPR_ES_TYPE(TPR_ES_TYPE_SEQ))
            return 0;
        eventSelect = TPR_ES_DESTSELECT(eventSelect) | TPR_ES_SEQ(value);
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case DMASK: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        eventSelect = TPR_ES_RATESELECT(eventSelect) | TPR_ES_DMODESELECT(eventSelect) | TPR_ES_DMASK(value);
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case DMODE: {
        u32 eventSelect = tprRegRead(pCard->devpvt, TPR_CH_EVTSEL(chan));
        eventSelect = TPR_ES_RATESELECT(eventSelect) | TPR_ES_DMODE(value) | TPR_ES_DMASKSELECT(eventSelect);
        WDEBUG(TPR_CH_EVTSEL(chan), eventSelect);
        tprRegWrite(pCard->devpvt, TPR_CH_EVTSEL(chan), eventSelect);
        return 1;
    }
    case BSADEL:
        if (value < 0) {
            WDEBUG(TPR_CH_BSADELAY(chan), TPR_BSADEL_NEG(value));
            tprRegWrite(pCard->devpvt, TPR_CH_BSADELAY(chan), TPR_BSADEL_NEG(value));
        } else {
            WDEBUG(TPR_CH_BSADELAY(chan), TPR_BSADEL_POS(value));
            tprRegWrite(pCard->devpvt, TPR_CH_BSADELAY(chan), TPR_BSADEL_POS(value));
        }
        return 0;
    case BSAWID:
        WDEBUG(TPR_CH_BSAWIDTH(chan), value);
        tprRegWrite(pCard->devpvt, TPR_CH_BSAWIDTH(chan), value);
        return 0;
    case TRGDEL:
        if (value < 0)
            value = 0;
        WDEBUG(TPR_TR_DELAY(chan), value);
        tprRegWrite(pCard->devpvt, TPR_TR_DELAY(chan), value);
        return 1;
    case TRGWID:
        WDEBUG(TPR_TR_WIDTH(chan), value);
        tprRegWrite(pCard->devpvt, TPR_TR_WIDTH(chan), value);
        return 1;
    case TRGFDEL:
        if (pCard->config.mode == 0) /* Not for LCLS-I! */
            return 0;
        WDEBUG(TPR_TR_FINEDEL(chan), value);
        tprRegWrite(pCard->devpvt, TPR_TR_FINEDEL(chan), value);
        return 1;
    case MSGDLY:
        WDEBUG(TPR_MSGDLY, value);
        tprRegWrite(pCard->devpvt, TPR_MSGDLY, value);
        return 0;  /* MCB - Not really, but probably not worth the effort to make this work. */
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
 *    dpvt->idx == MSGDLY              --> always write, as this isn't touched in LCLS-I.
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
        if (dpvt->lcls != -2 && dpvt->idx != MSGDLY &&                        \
            (mode < 0 || (dpvt->lcls >= 0 && dpvt->lcls != mode)))            \
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
            csr = tprRegRead(pCard->devpvt, TPR_CSR);
            pRec->val = (csr & TPR_CSR_LINK) ? 1 : 0;
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
            pRec->val = tprRegRead(pCard->devpvt, TPR_CH_EVTCNT(dpvt->chan)) & 0x7fffffff;/* No negative counts! */
            pRec->udf = 0;
            break;
        }
    } else {
        switch (dpvt->idx) {
        case FRAME:
            pRec->val = tprRegRead(pCard->devpvt, TPR_FRAMECNT) & 0x7fffffff; /* No negative counts! */
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
