#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include<sys/mman.h>
#include<sys/errno.h>
#include<sys/user.h>
#include<epicsStdlib.h>        /* EPICS Standard C library support routines                      */
#include<epicsStdio.h>         /* EPICS Standard C I/O support routines                          */
#include<epicsTypes.h>         /* EPICS Architecture-independent type definitions                */
#include<epicsInterrupt.h>     /* EPICS Interrupt context support routines                       */
#include<epicsMutex.h>         /* EPICS Mutex support library                                    */
#include<alarm.h>              /* EPICS Alarm status and severity definitions                    */
#include<dbAccess.h>           /* EPICS Database access definitions                              */
#include<dbEvent.h>            /* EPICS Event monitoring routines and definitions                */
#include<dbScan.h>             /* EPICS Database scan routines and definitions                   */
#include<devLib.h>             /* EPICS Device hardware addressing support library               */
#include<devSup.h>             /* EPICS Device support layer structures and symbols              */
#include<ellLib.h>             /* EPICS Linked list support library                              */
#include<errlog.h>             /* EPICS Error logging support library                            */
#include<recGbl.h>             /* EPICS Record Support global routine definitions                */
#include<registryFunction.h>   /* EPICS Registry support library                                 */
#include<epicsExport.h>        /* EPICS Symbol exporting macro definitions                       */
#include<iocsh.h>
#include"drvTpr.h"

static epicsMutexId cardListLock;
static ELLLIST cardList;
int tprDebug = 0;

struct tprIrqThreadArg {
    tprCardStruct *pCard;
    int minor;
};

static int tprIrqHandlerThread(void *p)
{
    struct tprIrqThreadArg *arg = (struct tprIrqThreadArg *)p;
    tprCardStruct *pCard = arg->pCard;
    int idx = arg->minor;
    tprQueues *q = pCard->q;
    int64_t allrp = q->allwp[idx];
    int64_t chnrp = q->chnwp[idx];
    int nb;
    char buf[32];

    free(arg);

    while (1) {
        nb = read(pCard->fd[idx], buf, 32);
        while(chnrp < q->chnwp[idx]) {
            /* q->chnq[idx].entry[chnrp & (MAX_TPR_CHNQ-1)] --> struct TprEntry */
            chnrp++;
        }
        while(allrp < q->allwp[idx]) {
            /* q->allq[q->allrp[idx].idx[allrp & (MAX_TPR_ALLQ-1)] & (MAX_TPR_ALLQ-1)] --> struct TprEntry */
            allrp++;
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
            if (pCard->config.mode != -1)
                printf("mode = %d?\n", pCard->config.mode);
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
        errlogPrintf("%s@%d(EvrOpen) Error: %s opening %s\n", __func__, __LINE__, strerror(errno), strDevice );
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
            pCard->fd[i] = -1;
        }
    }
    pCard->mmask |= 1 << minor;
    pCard->fd[minor] = fd;
    if (r)
        pCard->r = r;
    if (q)
        pCard->q = q;

    epicsMutexLock(cardListLock);
    ellAdd(&cardList, &pCard->link); 
    epicsMutexUnlock(cardListLock);

    /* Finish initialization! */
    epicsMutexLock(pCard->cardLock);
    if (minor != DEVNODE_MINOR_CONTROL) {
        struct tprIrqThreadArg *p = (struct tprIrqThreadArg *)malloc(sizeof(struct tprIrqThreadArg));
        p->pCard = pCard;
        p->minor = minor;
        epicsThreadMustCreate("tprIrqHandler", epicsThreadPriorityHigh,
                              epicsThreadGetStackSize(epicsThreadStackMedium),
                              (EPICSTHREADFUNC)tprIrqHandlerThread, p);
    }
    epicsMutexUnlock(pCard->cardLock);
}

static void TprDebugLevel(int level)
{
    printf("TprDebugLevel = %d (was %d)\n", level, tprDebug);
    tprDebug = level;
}

static void TprDrvReport(int level)
{
    printf("Nothing to report!\n");
}

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

