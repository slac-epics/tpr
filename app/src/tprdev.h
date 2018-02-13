#ifndef __TPRDEV_H__
#define __TPRDEV_H__
#include"drvTpr.h"
/*
 * Abstract TPR device.  OK, maybe not *that* abstract!
 */

/*
 * Open a TPR card and subscribe to notifications for the specified minor device.
 * Minor devices 0-11 are timing channels, 12 is the control channel, and 13 is the BSA channel.
 *
 * This returns an opaque pointer for the TPR.  (Each channel gets the same pointer.)
 */
extern void *tprOpen(int card, int minor);

/*
 * Reclaim any resources associated with the specified minor channel.
 */
extern void tprClose(int card, int minor);

/* How we receive messages. */
typedef void (*tprMessageCallback)(void *token, int channel, tprHeader *msg);

/* Prepare any infrastructure needed to receive messages from this TPR and send them to a callback function. */
extern void tprRegister(void *tpr, tprMessageCallback cb, void *token);

/* Return 1 if the control channel for this card is open, 0 otherwise. */
extern int tprMaster(void *);

/*
 * Read/write registers.  Defines for reg are given below.
 */
extern void tprRegWrite(void *dev, int reg, uint32_t value);
extern uint32_t tprRegRead(void *dev, int reg);
#define TPR_FPGAVERSION    0
#define TPR_XBAROUT(n)     (1+(n))
#define TPR_IRQCONTROL     5
#define TPR_IRQSTATUS      6
#define TPR_COUNTRESET     7
#define TPR_CH_CONTROL(n)  (8+(n))
#define      TPR_CC_ENABLE 5
#define      TPR_CC_BSAEN  2
#define TPR_CH_EVTSEL(n)   (20+(n))
#define      TPR_ES_RATESELECT(es)  ((es) & 0x00001fff)
#define      TPR_ES_TYPESELECT(es)  ((es) & 0x00001800)
#define      TPR_ES_ACSELECT(es)    ((es) & 0x00001807)
#define      TPR_ES_TSSELECT(es)    ((es) & 0x000001f8)
#define      TPR_ES_DESTSELECT(es)  ((es) & 0xffffe000)
#define      TPR_ES_DMODESELECT(es) ((es) & 0x60000000)
#define      TPR_ES_DMASKSELECT(es) ((es) & 0x1fffe000)
#define            TPR_ES_TYPE_FIXED 0
#define            TPR_ES_TYPE_AC    1
#define            TPR_ES_TYPE_SEQ   2
#define      TPR_ES_TYPE(v)    (((v) & 3) << 11)
#define      TPR_ES_FIXED(r)   (TPR_ES_TYPE(TPR_ES_TYPE_FIXED) | ((r) & 0xf))
#define      TPR_ES_TS(ts)     (((ts) & 0x3f) << 3)
#define      TPR_ES_AC(r, ts)  (TPR_ES_TYPE(TPR_ES_TYPE_AC) | ((r) & 0x7) | TPR_ES_TS(ts))
#define      TPR_ES_SEQ(s)     (TPR_ES_TYPE(TPR_ES_TYPE_SEQ) | ((s) & 0x7ff))
#define      TPR_ES_DMODE(m)   (((m) & 0x3) << 29)
#define            TPR_ES_DMODE_INC  0
#define            TPR_ES_DMODE_EXC  1
#define            TPR_ES_DMODE_DK   2
#define      TPR_ES_DMASK(m)   (((m) & 0xffff) << 13)
#define TPR_CH_EVTCNT(n)   (32+(n))
#define TPR_CH_BSADELAY(n) (44+(n))
#define      TPR_BSADEL_NEG(v)  (((-(v)) & 0x7f) << 20)
#define      TPR_BSADEL_POS(v)  ((v) & 0xfffff)
#define TPR_CH_BSAWIDTH(n) (56+(n))
#define TPR_TR_CONTROL(n)  (68+(n))
#define      TPR_TC_ENABLE      0x80000000
#define      TPR_TC_POLARITY    0x00010000
#define TPR_TR_DELAY(n)    (80+(n))
#define TPR_TR_WIDTH(n)    (92+(n))
#define TPR_TR_FINEDEL(n)  (104+(n))
#define TPR_CSR            116
#define      TPR_CSR_RESETCNT 0x01
#define      TPR_CSR_LINK     0x02
#define      TPR_CSR_CLKSEL   0x10
#define TPR_MSGDLY         117
#define TPR_FRAMECNT       118
#endif /* __TPRDEV_H__ */
