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
#include<drvSup.h>             /* EPICS Driver support layer structures and symbols              */
#include<errlog.h>             /* EPICS Error logging support library                            */
#include<recGbl.h>             /* EPICS Record Support global routine definitions                */
#include<registryFunction.h>   /* EPICS Registry support library                                 */
#include<epicsExport.h>        /* EPICS Symbol exporting macro definitions                       */
#include<iocsh.h>
#include<aSubRecord.h>
#include"drvTpr.h"
#include"tprdev.h"

static tprCardStruct *tprCards[MAX_TPR] = { NULL };
int tprDebug = 0;

extern int tprCurrentTimeStamp(epicsTimeStamp *epicsTime_ps, int eventCode);

tprCardStruct *tprGetCard(int card)
{
    return (card >= 0 && card < MAX_TPR) ? tprCards[card] : NULL;
}

static void TprConfigure(int card, int minor)
{
    int i;
    tprCardStruct *pCard;
    void *devpvt;
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
    devpvt = tprOpen(card, minor); 
    if (!devpvt)
        return;
    if (!pCard) {
        isNew = 1;
        pCard = (struct tprCardStruct *)malloc(sizeof(struct tprCardStruct));
        if (pCard == NULL) {
            errlogPrintf("%s@%d(malloc): failed.\n", __func__, __LINE__);
            free(pCard);
            tprClose(card, minor);
            return;
        }
        memset(pCard, 0, sizeof(struct tprCardStruct));
        pCard->card = card;
        pCard->cardLock = epicsMutexCreate();
        if (pCard->cardLock == 0) {
            errlogPrintf("%s@%d(epicsMutexCreate): failed.\n", __func__, __LINE__);
            free(pCard);
            tprClose(card, minor);
            return;
        }
        pCard->mmask = 0;
        pCard->config.mode = -1;
        for (i = 0; i < 12; i++) {
            scanIoInit(&pCard->config.lcls[0][i].ioscan);
            scanIoInit(&pCard->config.lcls[1][i].ioscan);
            scanIoInit(&pCard->client[i].ioscan);
        }
    }
    pCard->mmask |= 1 << minor;
    pCard->devpvt = devpvt;

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

    for (i = 0; i < MAX_TPR; i++)
        if (tprCards[i])
            tprInitialize(tprCards[i]);

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
    if (tprMaster(pCard->devpvt))
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
    *(epicsInt32 *)psub->vala = tprRegRead(pCard->devpvt, TPR_CH_EVTCNT(chan));
    *(epicsInt32 *)psub->vald = *(epicsInt32 *)psub->valc;
    *(epicsInt32 *)psub->valc = tprRegRead(pCard->devpvt, TPR_FRAMECNT);
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
