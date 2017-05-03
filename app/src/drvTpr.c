#include<sys/types.h>
#include<sys/stat.h>
#include<sys/select.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include<math.h>
#include<sys/mman.h>
#include<sys/errno.h>
#include<sys/user.h>
#include<epicsStdlib.h>        /* EPICS Standard C library support routines                      */
#include<epicsStdio.h>         /* EPICS Standard C I/O support routines                          */
#include<epicsTypes.h>         /* EPICS Architecture-independent type definitions                */
#include<epicsInterrupt.h>     /* EPICS Interrupt context support routines                       */
#include<epicsGeneralTime.h>   /* generalTimeTpRegister     */
#include<generalTimeSup.h>
#include<alarm.h>              /* EPICS Alarm status and severity definitions                    */
#include<dbAccess.h>           /* EPICS Database access definitions                              */
#include<dbEvent.h>            /* EPICS Event monitoring routines and definitions                */
#include<devLib.h>             /* EPICS Device hardware addressing support library               */
#include<devSup.h>             /* EPICS Device support layer structures and symbols              */
#include<drvSup.h>             /* EPICS Driver support layer structures and symbols              */
#include<errlog.h>             /* EPICS Error logging support library                            */
#include<recGbl.h>             /* EPICS Record Support global routine definitions                */
#include<registryFunction.h>   /* EPICS Registry support library                                 */
#include<epicsExport.h>        /* EPICS Symbol exporting macro definitions                       */
#include<iocsh.h>
#include<aSubRecord.h>
#include"drvTpr.h"

static epicsMutexId cardListLock;
static ELLLIST cardList;
int tprDebug = 0;

extern int tprTimeGet(epicsTimeStamp *epicsTime_ps, int eventCode);
extern void tprMessageProcess(tprCardStruct *pCard, int chan, tprHeader *message);

static int tprIrqHandlerThread(void *p)
{
    tprCardStruct *pCard = (tprCardStruct *)p;
    tprQueues *q = pCard->q;
    fd_set all;
    int i, mfd = -1;
    int64_t allrp[MOD_SHARED];
    int64_t chnrp[MOD_SHARED];

    FD_ZERO(&all);
    for (i = 0; i < DEVNODE_MINOR_CONTROL; i++) {
        if (pCard->mmask & (1 << i)) {
            if (pCard->fd[i] > mfd)
                mfd = pCard->fd[i];
            FD_SET(pCard->fd[i], &all);
            allrp[i] = q->allwp[i];
            chnrp[i] = q->chnwp[i];
        }
    }
    mfd++;

    while (1) {
        int cnt, nb;
        char buf[32];
        fd_set rds = all;

        cnt = select(mfd, &rds, NULL, NULL, NULL);
        if (cnt < 0)
            continue;  /* Assume we were just interrupted? */
        for (i = 0; cnt && i < DEVNODE_MINOR_CONTROL; i++) {
            if (!FD_ISSET(pCard->fd[i], &rds))
                continue;
            nb = read(pCard->fd[i], buf, 32); /* Why 32? */
            if (nb <= 0)
                continue;   /* This can't be good... */

            while(chnrp[i] < q->chnwp[i]) {
                tprMessageProcess(pCard, i,
                                  (tprHeader *)&q->chnq[i].entry[chnrp[i]++
                                                                 & (MAX_TPR_CHNQ-1)].word[0]);
            }
            while(allrp[i] < q->allwp[i]) {
                tprMessageProcess(pCard, i,
                                  (tprHeader *)&q->allq[q->allrp[i].idx[allrp[i]++ & (MAX_TPR_ALLQ-1)]
                                                        & (MAX_TPR_ALLQ-1)].word[0]);
            }
            cnt--;
        }
    }
    return 0; /* Never get here anyway. */
}

tprCardStruct *tprGetCard(int card)
{
    tprCardStruct *pCard;
    epicsMutexLock(cardListLock);
    for (pCard = (tprCardStruct *)ellFirst(&cardList);
         pCard != NULL;
         pCard = (tprCardStruct *)ellNext(&pCard->link)) {
        if (pCard->card == card) {
            epicsMutexUnlock(cardListLock);
            return pCard;
        }
    }
    epicsMutexUnlock(cardListLock);
    return NULL;
}

static void TprConfigure(int card, int minor)
{
    char strDevice[20];
    int fd, i, ret;
    tprCardStruct *pCard;
    tprReg *r = NULL;
    tprQueues *q = NULL;
    int isNew = 0;

    pCard = tprGetCard(card);
    if (pCard && pCard->mmask & (1 << minor)) {
        errlogPrintf ("%s: Card %d, minor %d has already been configured\n", __func__, card, minor);
        return;
    }
    ret = snprintf(strDevice, sizeof(strDevice), "%s%c%c", DEVNODE_NAME_BASE, card + 'a',
                   (minor == DEVNODE_MINOR_CONTROL) ? 0 : ((minor <= 9) ? ('0' + minor)
                                                                        : ('a' - 10 + minor)));
    if (ret < 0) {
        errlogPrintf("%s@%d(snprintf): %s.\n", __func__, __LINE__, strerror(-ret));
        return;
    }
    fd = open(strDevice, O_RDWR);
    if (fd < 0) {
        errlogPrintf("%s@%d(open) Error: %s opening %s\n", __func__, __LINE__, strerror(errno), strDevice );
        return;
    }
    if (minor == DEVNODE_MINOR_CONTROL)
        r = (tprReg *) mmap(0, TPR_CONTROL_WINDOW, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    else if (pCard && pCard->q)
        q = pCard->q;
    else
        q = (tprQueues *) mmap(0, TPR_QUEUE_WINDOW, PROT_READ, MAP_SHARED, fd, 0);
    if (!r && !q) { // We had to set *one* of these!
        close(fd);
        return;
    }
    if (!pCard) {
        isNew = 1;
        pCard = (struct tprCardStruct *)malloc(sizeof(struct tprCardStruct));
        if (pCard == NULL) {
            errlogPrintf("%s@%d(malloc): failed.\n", __func__, __LINE__);
            free(pCard);
            close(fd);
            return;
        }
        memset(pCard, 0, sizeof(struct tprCardStruct));
        pCard->card = card;
        pCard->cardLock = epicsMutexCreate();
        if (pCard->cardLock == 0) {
            errlogPrintf("%s@%d(epicsMutexCreate): failed.\n", __func__, __LINE__);
            free(pCard);
            close(fd);
            return;
        }
        pCard->mmask = 0;
        pCard->config.mode = -1;
        for (i = 0; i < 12; i++) {
            scanIoInit(&pCard->config.lcls[0][i].ioscan);
            scanIoInit(&pCard->config.lcls[1][i].ioscan);
            scanIoInit(&pCard->client[i].ioscan);
            pCard->fd[i] = -1;
        }
    }
    pCard->mmask |= 1 << minor;
    pCard->fd[minor] = fd;
    if (r)
        pCard->r = r;
    if (q)
        pCard->q = q;

    if (isNew) {
        epicsMutexLock(cardListLock);
        ellAdd(&cardList, &pCard->link); 
        epicsMutexUnlock(cardListLock);
    }

    /* Finish initialization! */
}

static void TprDebugLevel(int level)
{
    if (tprDebug < 0) {
        printf("TprDebugLevel = %d\n", tprDebug);
    } else {
        printf("TprDebugLevel = %d (was %d)\n", level, tprDebug);
        tprDebug = level;
    }
}

/* Guideline: 0=one-line per device, 1=more, 2=lots of info */
static int TprDrvReport(int level)
{
    tprCardStruct *pCard;
    epicsMutexLock(cardListLock);
    for (pCard = (tprCardStruct *)ellFirst(&cardList);
         pCard != NULL;
         pCard = (tprCardStruct *)ellNext(&pCard->link)) {
        printf("TPR Card %d, Modes 0x%x\n", pCard->card, pCard->mmask);
    }
    epicsMutexUnlock(cardListLock);
    return 0;
}

static int TprDrvInitialize(void)
{
    tprCardStruct *pCard;
    static int done = 0;
    if (done)
        return 0;
    done = 1;

    epicsMutexLock(cardListLock);
    for (pCard = (tprCardStruct *)ellFirst(&cardList);
         pCard != NULL;
         pCard = (tprCardStruct *)ellNext(&pCard->link)) {
        if (pCard->mmask & ~(1 << DEVNODE_MINOR_CONTROL)) {
            epicsThreadMustCreate("tprIrqHandler", epicsThreadPriorityHigh,
                                  epicsThreadGetStackSize(epicsThreadStackMedium),
                                  (EPICSTHREADFUNC)tprIrqHandlerThread, pCard);
        }
    }
    epicsMutexUnlock(cardListLock);

    if (generalTimeRegisterEventProvider("tprTimeGet", 2000, (TIMEEVENTFUN) tprTimeGet)) {
        printf("Cannot register TPR time provider?!?\n");
    }
    return 0;
}

struct drvet drvTpr = {
  2,
  (DRVSUPFUN) TprDrvReport,
  (DRVSUPFUN) TprDrvInitialize
};
epicsExportAddress(drvet, drvTpr);

/*****************************************************************************************/

/* iocsh command: TprConfigure */
static const iocshArg TprConfigureArg0 = {"Card", iocshArgInt};
static const iocshArg TprConfigureArg1 = {"Minor", iocshArgInt};
static const iocshArg *const TprConfigureArgs[2] = {
    &TprConfigureArg0,
    &TprConfigureArg1,
};
static const iocshFuncDef TprConfigureDef = {"TprConfigure", 2, TprConfigureArgs};

static void TprConfigureCall(const iocshArgBuf *args)
{
    TprConfigure(args[0].ival, args[1].ival);
}

/* iocsh command: TprDebugLevel */
static const iocshArg TprDebugLevelArg0 = {"Level", iocshArgInt};
static const iocshArg *const TprDebugLevelArgs[1] = {&TprDebugLevelArg0};
static const iocshFuncDef TprDebugLevelDef = {"TprDebugLevel", 1, TprDebugLevelArgs};

static void TprDebugLevelCall(const iocshArgBuf *args)
{
    TprDebugLevel(args[0].ival);
}

/* iocsh command: TprDrvReport */
static const iocshArg TprDrvReportArg0 = {"Level" , iocshArgInt};
static const iocshArg *const TprDrvReportArgs[1] = {&TprDrvReportArg0};
static const iocshFuncDef TprDrvReportDef = {"TprDrvReport", 1, TprDrvReportArgs};

static void TprDrvReportCall(const iocshArgBuf *args)
{
    TprDrvReport(args[0].ival);
}

/* iocsh command: TprStart */
static const iocshArg TprStartArg0 = {"Card" , iocshArgInt};
static const iocshArg *const TprStartArgs[1] = {&TprStartArg0};
static const iocshFuncDef TprStartDef = {"TprStart", 1, TprStartArgs};

static void TprStartCall(const iocshArgBuf *args)
{
    tprCardStruct *pCard = tprGetCard(args[0].ival);
    tprWrite(pCard, MODE, -1, tprGetConfig(pCard, -1, MODE));
}

/* Registration APIs */
static void drvTprRegister()
{
    cardListLock = epicsMutexCreate();
    ellInit(&cardList);
    iocshRegister(&TprConfigureDef, TprConfigureCall );
    iocshRegister(&TprDebugLevelDef, TprDebugLevelCall );
    iocshRegister(&TprDrvReportDef, TprDrvReportCall );
    iocshRegister(&TprStartDef, TprStartCall );
}
epicsExportRegistrar(drvTprRegister);

long tprRateInit(struct aSubRecord *psub)
{
    *(epicsInt32 *)psub->vala = 0;
    *(epicsInt32 *)psub->valb = 0;
    *(epicsInt32 *)psub->valc = 0;
    *(epicsInt32 *)psub->vald = 0;
    *(double *)psub->vale = 0.0;
    *(tprCardStruct **)psub->valf = tprGetCard(*(epicsInt32 *)psub->c);
    return 0;
}

long tprRateProc(struct aSubRecord *psub)
{
    double freq = (*(epicsInt32 *)psub->a==1) ? 360. : 929000.;
    int chan = *(epicsInt32 *)psub->b;
    tprCardStruct *pCard = *(tprCardStruct **)psub->valf;

    *(epicsInt32 *)psub->valb = *(epicsInt32 *)psub->vala;
    *(epicsInt32 *)psub->vala = pCard->r->channel[chan].eventCount;
    *(epicsInt32 *)psub->vald = *(epicsInt32 *)psub->valc;
    *(epicsInt32 *)psub->valc = pCard->r->frameCount;
    if (*(epicsInt32 *)psub->valb == 0 || *(epicsInt32 *)psub->a - 1 != pCard->config.mode)
        *(double *)psub->vale = 0.0;
    else {
        double newv = freq * (double)(*(epicsInt32 *)psub->vala - *(epicsInt32 *)psub->valb) /
                             (double)(*(epicsInt32 *)psub->valc - *(epicsInt32 *)psub->vald);
        double oldv = *(double *)psub->vale;
        /*
         * Sigh... it's possible we've caught an extra event, and this will throw our count way off (0.5 Hz).
         * So, if we're high by only 0.5 Hz or so, just deduct the extra.  (If it's a big diff, we're probably
         * really changing, so we're off anyway!
         */
        if (newv - oldv > 0.4 && newv - oldv < 1.0 && fabs(oldv) > 0.01)
            *(double *)psub->vale = freq * (double)(*(epicsInt32 *)psub->vala - *(epicsInt32 *)psub->valb - 1) /
                                           (double)(*(epicsInt32 *)psub->valc - *(epicsInt32 *)psub->vald);
        else
            *(double *)psub->vale = newv;
    }
    return 0;
}

epicsRegisterFunction(tprRateInit);
epicsRegisterFunction(tprRateProc);
