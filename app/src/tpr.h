#include<stdint.h>
typedef uint32_t u32;
typedef uint32_t __u32;

#define DEVNODE_NAME_BASE	"/dev/tpr"
#define DEVNODE_MINOR_CONTROL   12
#define MOD_SHARED 12
#define TPR_CONTROL_WINDOW      sizeof(struct TprReg)
#define TPR_QUEUE_WINDOW        ((sizeof(struct TprQueues) + PAGE_SIZE) & PAGE_MASK)

/* Stuff from the kernel driver... */

/* These must be powers of two!!! */
#define MAX_TPR_ALLQ (32*1024)
#define MAX_TPR_CHNQ  1024
#define MSG_SIZE      32

/* DMA Buffer Size, Bytes (could be as small as 512B) */
#define BUF_SIZE 4096
#define NUMBER_OF_RX_BUFFERS 1023

struct TprEntry {
  u32 word[MSG_SIZE];
};

struct ChnQueue {
  struct TprEntry entry[MAX_TPR_CHNQ];
};

struct TprQIndex {
  long long idx[MAX_TPR_ALLQ];
};

/*
 *  Maintain an indexed list into the tprq for each channel
 *  That way, applications of varied rates can jump to the next relevant entry
 *  Consider copying master queue to individual channel queues to reduce RT reqt
 */
typedef struct TprQueues {
  struct TprEntry  allq  [MAX_TPR_ALLQ]; /* master queue of shared messages */
  struct ChnQueue  chnq  [MOD_SHARED];   /* queue of single channel messages */
  struct TprQIndex allrp [MOD_SHARED];   /* indices into allq */
  long long        allwp [MOD_SHARED];   /* write pointer into allrp */
  long long        chnwp [MOD_SHARED];   /* write pointer into chnq's */
  long long        gwp;
  int              fifofull;
} tprQueues;

typedef struct TprReg {
  __u32 reserved_0[0x10000>>2];
  __u32 FpgaVersion;
  __u32 reserved_04[(0x30000>>2)-1];
  __u32 xbarOut[4]; // 0x40000
  __u32 reserved_30010[(0x40000>>2)-4];
  __u32 irqControl; // 0x80000
  __u32 irqStatus;
  __u32 reserved_8[2];
  __u32 countReset;
  __u32 trigMaster;
  __u32 reserved_18[2];
  struct ChReg {
    __u32 control;
    __u32 eventSelect;
#define ES_RATESELECT(es)  ((es) & 0x00001fff)
#define ES_TYPESELECT(es)  ((es) & 0x00001800)
#define ES_ACSELECT(es)    ((es) & 0x00001807)
#define ES_DESTSELECT(es)  ((es) & 0xffffe000)
#define ES_DMODESELECT(es) ((es) & 0x60000000)
#define ES_DMASKSELECT(es) ((es) & 0x1fffe000)
#define ES_TYPE_FIXED (0 << 11)
#define ES_TYPE_AC    (1 << 11)
#define ES_TYPE_SEQ   (2 << 11)
#define ES_FIXED(r)   (ES_TYPE_FIXED | ((r) & 0xf))
#define ES_TS(ts)     (((ts) & 0x3f) << 3)
#define ES_AC(r, ts)  (ES_TYPE_AC | ((r) & 0x7) | ES_TS(ts))
#define ES_SEQ(s)     (ES_TYPE_SEQ | ((s) & 0x7ff))
#define ES_DMODE(m)   (((m) & 0x3) << 29)
#define ES_DMASK(m)   (((m) & 0xffff) << 13)
    __u32 eventCount;
    __u32 bsadelay;
#define ES_BSANEG(v)  (((-(v)) & 0x7f) << 20)
#define ES_BSAPOS(v)  ((v) & 0xfffff)
    __u32 bsawidth;
    __u32 reserved[3];
  } channel[12];
  __u32 reserved_1a0[2];
  __u32 frameCount;
  __u32 reserved_1ac[21];
  struct TrReg {    // 0x80200
    __u32 control;
    __u32 delay;
    __u32 width;
    __u32 delayTap;
  } trigger[12];
  __u32 reserved_2c0[80];
  //  PcieRxDesc   0x80400
  __u32 rxFree    [16];   // WO 0x400 Write Desc/Address
  __u32 rxFreeStat[16];   // RO 0x440 Free FIFO (31:31 full, 30:30 valid, 9:0 count)
  __u32 reserved  [32];
  __u32 rxMaxFrame;       // RW 0x500 (31:31 freeEnable, 23:0 maxFrameSize)
  __u32 rxFifoSize;       // RW 0x504 Buffers per FIFO
  __u32 rxCount ;         // RO 0x508 (rxCount)
  __u32 lastDesc;         // RO 0x50C (lastDesc)
  __u32 reserved_80510[65212];
  __u32 sofcnt;
  __u32 eofcnt;
  __u32 reserved_c0000[6];
  __u32 CSR;
  __u32 msgDelay;
} tprReg;

typedef struct tprHeader {
    uint16_t chmask;
    uint8_t  tag;
#define TAG_DROP      0x80
#define TAG_LCLS1     0x40
#define TAG_TYPE_MASK 0xf
#define TAG_EVENT     0
#define TAG_BSA_CTRL  1
#define TAG_BSA_EVENT 2
#define TAG_END       15
    uint8_t  dma_ownership;
#define DMAOWN_NEW    0x80
#define DMAOWN_DROP   0x40
} __attribute__((packed)) tprHeader;

typedef struct tprEvent {
    struct tprHeader header;
    uint32_t         length;
    uint64_t         pulseID;
    uint32_t         nanosecs;
    uint32_t         seconds;
    uint16_t         rates;
#define RATE_AC_60Hz       0x8000
#define RATE_AC_30Hz       0x4000
#define RATE_AC_10Hz       0x2000
#define RATE_AC_5Hz        0x1000
#define RATE_AC_1Hz        0x0800
#define RATE_AC_HALFHz     0x0400
#define RATE_FIXED_929kHz  0x0040
#define RATE_FIXED_71kHz   0x0020
#define RATE_FIXED_10kHz   0x0010
#define RATE_FIXED_1kHz    0x0008
#define RATE_FIXED_100Hz   0x0004
#define RATE_FIXED_10Hz    0x0002
#define RATE_FIXED_1Hz     0x0001
    uint16_t         timeslot;
#define TS_71kHZ_RESYNC    0x8000
#define TS_CLK_TS_CHG_MASK 0x07ff
#define TS_CLK_TS_CHG_OFF  3
#define TS_ACMASK          7
    uint32_t         beamreq;                 // 28
    union {
        struct {
            uint32_t         mod[6];                  // 32
        } l1;
        struct {
            uint16_t         beamenergy[4];           // 32
            uint32_t         photon_wavelen;          // 40
            uint16_t         status;                  // 44
            uint16_t         mps_limit;               // 46
            uint64_t         mps_class;               // 48
        } l2;
    } misc;
    uint32_t         seq[9];                  // 56, last reserved for lcls1!
} __attribute__((packed)) tprEvent;

typedef struct tprBSAControl {
    struct tprHeader header;
    uint32_t         nanosecs;
    uint32_t         seconds;
    uint64_t         init;
    uint64_t         major;
    uint64_t         minor;
} __attribute__((packed)) tprBSAControl;

typedef struct tprBSAEvent {
    struct tprHeader header;
    uint64_t         PID;
    uint64_t         active;
    uint64_t         avgdone;
    uint32_t         nanosecs;
    uint32_t         seconds;
    uint64_t         update;
} __attribute__((packed)) tprBSAEvent;
