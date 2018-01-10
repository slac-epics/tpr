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

static tprCardStruct *tprCards[MAX_TPR] = { NULL };
int tprDebug = 0;

extern int tprCurrentTimeStamp(epicsTimeStamp *epicsTime_ps, int eventCode);
extern void tprMessageProcess(tprCardStruct *pCard, int chan, tprHeader *message);

static int tprIrqHandlerThread(void *p)
{
    tprCardStruct *pCard = (tprCardStruct *)p;
    tprQueues *q = pCard->q;
    fd_set all;
    int i, mfd = -1;
    int64_t allrp[MOD_SHARED];
    int64_t bsarp = 0;
    int have_bsa = pCard->mmask & (1 << DEVNODE_MINOR_BSA);

    FD_ZERO(&all);
    for (i = 0; i < DEVNODE_MINOR_CONTROL; i++) {
        if (pCard->mmask & (1 << i)) {
            if (pCard->fd[i] > mfd)
                mfd = pCard->fd[i];
            FD_SET(pCard->fd[i], &all);
            allrp[i] = q->allwp[i];
        }
    }
    if (have_bsa) {
        if (pCard->fd[DEVNODE_MINOR_BSA-1] > mfd)
            mfd = pCard->fd[DEVNODE_MINOR_BSA-1];
        FD_SET(pCard->fd[DEVNODE_MINOR_BSA-1], &all);
        bsarp = q->bsawp;
    }
    mfd++;

    while (1) {
        int cnt, nb;
        char buf[32];
        fd_set rds = all;

        cnt = select(mfd, &rds, NULL, NULL, NULL);
        if (cnt < 0)
            continue;  /* Assume we were just interrupted? */

        if (have_bsa && FD_ISSET(pCard->fd[DEVNODE_MINOR_BSA-1], &rds)) {
            nb = read(pCard->fd[DEVNODE_MINOR_BSA-1], buf, 32); /* Why 32? */
            if (nb <= 0)
                continue;   /* This can't be good... */

            while(bsarp < q->bsawp) {
                tprMessageProcess(pCard, DEVNODE_MINOR_BSA,
                                  (tprHeader *)&q->bsaq[bsarp++ & (MAX_TPR_BSAQ-1)].word[0]);
            }
            cnt--;
        }
            
        for (i = 0; cnt && i < DEVNODE_MINOR_CONTROL; i++) {
            if (!FD_ISSET(pCard->fd[i], &rds))
                continue;
            nb = read(pCard->fd[i], buf, 32); /* Why 32? */
            if (nb <= 0)
                continue;   /* This can't be good... */

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
    return (card >= 0 && card < MAX_TPR) ? tprCards[card] : NULL;
}

static void TprConfigure(int card, int minor)
{
    char strDevice[20];
    int fd, i, ret;
    tprCardStruct *pCard;
    tprReg *r = NULL;
    tprQueues *q = NULL;
    int isNew = 0;

    if (card < 0 || card >= MAX_TPR) {
        errlogPrintf ("%s: Card %d declared, limit is 0 to %d\n", __func__, card, MAX_TPR);
        return;
    }
    pCard = tprGetCard(card);
    if (pCard && pCard->mmask & (1 << minor)) {
        errlogPrintf ("%s: Card %d, minor %d has already been configured\n", __func__, card, minor);
        return;
    }
    if (minor == DEVNODE_MINOR_BSA)
        ret = snprintf(strDevice, sizeof(strDevice), "%s%cBSA", DEVNODE_NAME_BASE, card + 'a');
    else
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
    if (minor == DEVNODE_MINOR_BSA)
        pCard->fd[minor-1] = fd;
    else
        pCard->fd[minor] = fd;
    if (r)
        pCard->r = r;
    if (q)
        pCard->q = q;

    if (isNew)
        tprCards[card] = pCard;
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
    int i;
    for (i = 0; i < MAX_TPR; i++) {
        tprCardStruct *pCard = tprCards[i];
        if (pCard) {
            printf("TPR Card %d, Modes 0x%x\n", pCard->card, pCard->mmask);
        }
    }
    return 0;
}

static int TprDrvInitialize(void)
{
    int i;
    static int done = 0;
    if (done)
        return 0;
    done = 1;

    for (i = 0; i < MAX_TPR; i++) {
        tprCardStruct *pCard = tprCards[i];
        if (pCard && (pCard->mmask & ~(1 << DEVNODE_MINOR_CONTROL))) {
            epicsThreadMustCreate("tprIrqHandler", epicsThreadPriorityHigh+9,
                                  epicsThreadGetStackSize(epicsThreadStackMedium),
                                  (EPICSTHREADFUNC)tprIrqHandlerThread, pCard);
        }
    }

    if (generalTimeRegisterEventProvider("timingGetEventTimeStamp", 1000,
                                         (TIMEEVENTFUN)timingGetEventTimeStamp)) {
        printf("Cannot register TPR time provider?!?\n");
    }
    if (generalTimeRegisterEventProvider("tprCurrentTimeStamp", 2000,
                                         (TIMEEVENTFUN)tprCurrentTimeStamp)) {
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
    if (pCard->r)
        tprWrite(pCard, MODE, -1, tprGetConfig(pCard, -1, MODE));  // Set mode if master!
}

/* Registration APIs */
static void drvTprRegister()
{
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
        /* double oldv = *(double *)psub->vale; */
        *(double *)psub->vale = newv;
    }
    return 0;
}

epicsRegisterFunction(tprRateInit);
epicsRegisterFunction(tprRateProc);
