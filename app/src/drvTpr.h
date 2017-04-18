#include"tpr.h"

struct tprGlobalConfig {
    struct boRecord *boRecord[3];
#define CRESET  0
#define XBAR    1
#define MODE    2
    // These are not used, but defining them is simpler than fixing the code!
    struct mbboRecord *mbboRecord[1];
    struct mbboDirectRecord *mbboDirectRecord[1];
    struct longoutRecord *longoutRecord[1];
    struct longinRecord *longinRecord[1];
};

struct tprChannelConfig {
    struct boRecord *boRecord[4];
#define ENABLE  ((1<<8)|0)
#define BSAEN   ((1<<8)|1)
#define TRIGEN  ((1<<8)|2)
#define POL     ((1<<8)|3)
    struct mbboRecord *mbboRecord[2];
#define EVENT   ((2<<8)|0)
#define DMASK   ((2<<8)|1)
    struct mbboDirectRecord *mbboDirectRecord[2];
#define DMODE   ((3<<8)|0)
#define TSMASK  ((3<<8)|1)
    struct longoutRecord *longoutRecord[6];
#define SEQ     ((4<<8)|0)
#define BSADEL  ((4<<8)|1)
#define BSAWID  ((4<<8)|2)
#define TRGDEL  ((4<<8)|3)
#define TRGWID  ((4<<8)|4)
#define TRGFDEL ((4<<8)|5)
    struct longinRecord *longinRecord[1];
#define CHANGE  ((5<<8)|0)
    IOSCANPVT ioscan;
};

struct tprConfig {
    struct tprGlobalConfig global;
    struct tprChannelConfig lcls[2][12];
    int mode;
};

typedef struct tprCardStruct {
    ELLNODE link;
    int card;
    int mmask;
    epicsMutexId cardLock;
    int fd[12];
    tprReg *r;
    tprQueues *q;
    struct tprConfig config;
} tprCardStruct;

extern tprCardStruct *tprGetCard(int card);
extern int tprWrite(tprCardStruct *pCard, int reg, int chan, int value);
