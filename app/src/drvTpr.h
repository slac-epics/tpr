//////////////////////////////////////////////////////////////////////////////
// This file is part of 'tpr'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'tpr', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#ifndef __DRVTPR_H__
#define __DRVTPR_H__
#include<ellLib.h>             /* EPICS Linked list support library                              */
#include<dbScan.h>             /* EPICS Database scan routines and definitions                   */
#include<epicsMutex.h>         /* EPICS Mutex support library                                    */
#include<epicsTime.h>          /* EPICS Time support library                                     */
#include<devSup.h>             /* EPICS Device support layer structures and symbols              */
#include"tpr.h"
#include"timingFifoApi.h"

#define MAX_TPR     2
#define MAX_TPR_CHAN    12

struct tprGlobalConfig {
    struct boRecord *boRecord[3];
#define CRESET  0
#define XBAR    1
#define MODE    2
    // These are not used, but defining them is simpler than fixing the code!
    struct mbboRecord *mbboRecord[1];
    struct mbboDirectRecord *mbboDirectRecord[1];
    struct longoutRecord *longoutRecord[2];
#define MSGDLY  ((6<<8)|0)
    struct longinRecord *longinRecord[1];
#define FRAME   ((7<<8)|0)
    struct biRecord *biRecord[1];
#define RXLINK  ((8<<8)|0)
};

struct tprChannelConfig {
    struct boRecord *boRecord[4];
#define ENABLE  ((1<<8)|0)
#define BSAEN   ((1<<8)|1)
#define TRIGEN  ((1<<8)|2)
#define POL     ((1<<8)|3)
    struct mbboRecord *mbboRecord[4];
#define RMODE   ((2<<8)|0)
#define FRATE   ((2<<8)|1)
#define ACRATE  ((2<<8)|2)
#define DMODE   ((2<<8)|3)
    struct mbboDirectRecord *mbboDirectRecord[2];
#define TSMASK  ((3<<8)|0)
#define DMASK   ((3<<8)|1)
    struct longoutRecord *longoutRecord[6];
#define SEQ     ((4<<8)|0)
#define BSADEL  ((4<<8)|1)
#define BSAWID  ((4<<8)|2)
#define TRGDEL  ((4<<8)|3)
#define TRGWID  ((4<<8)|4)
#define TRGFDEL ((4<<8)|5)
    struct longinRecord *longinRecord[3];
#define CHANGE  ((5<<8)|0)
#define COUNT   ((5<<8)|1)
    struct biRecord *biRecord[1];
    IOSCANPVT ioscan;
};

typedef struct tprChannelState {
    struct longoutRecord   *longoutRecord[1];
#define ENUM    ((1<<8)|0)
    struct eventRecord     *eventRecord[1];
#define EVT     ((2<<8)|0)
    struct waveformRecord  *waveformRecord[1];
#define MESSAGE ((3<<8)|0)
    struct longinRecord    *longinRecord[2];
#define PIDL    ((4<<8)|0)
#define PIDH    ((4<<8)|1)
    struct biRecord        *biRecord[1];
#define TMODE   ((5<<8)|0)
    IOSCANPVT      ioscan;
    int            mode;
} tprChannelState;

typedef struct tprConfig {
    struct tprGlobalConfig  global;
    struct tprChannelConfig lcls[2][MAX_TPR_CHAN];
    int                     mode;
} tprConfig;

typedef struct tprCardStruct {
    ELLNODE          link;
    int              card;
    int              mmask;
    void            *devpvt;
    epicsMutexId     cardLock;
    tprConfig        config;
    tprChannelState  client[MAX_TPR_CHAN];
} tprCardStruct;

extern tprCardStruct *tprGetCard(int card);
extern int tprWrite(tprCardStruct *pCard, int reg, int chan, int value);
extern int tprGetConfig(tprCardStruct *pCard, int chan, int reg);
extern int tprDebug;

#define WDEBUG(lhs, rhs) if (tprDebug & TPR_DEBUG_WRITE) printf("WRITE %d (0x%x) --> "#lhs" (0x%x)\n", (rhs), (rhs), (lhs))
#define TPR_DEBUG_WRITE     1

typedef struct dsetStruct {
        long            number;
        DEVSUPFUN       report;
        DEVSUPFUN       init;
        DEVSUPFUN       initRec;
        DEVSUPFUN       get_ioint_info;
        DEVSUPFUN       proc;
} dsetStruct;
#endif /* __DRVTPR_H__ */
