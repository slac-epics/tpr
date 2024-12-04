//////////////////////////////////////////////////////////////////////////////
// This file is part of 'tpr'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'tpr', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<fcntl.h>
#include<sys/select.h>
#include<sys/mman.h>
#include<sys/errno.h>
#include<sys/user.h>
#include<errlog.h>             /* EPICS Error logging support library                            */
#include<epicsThread.h>        /* EPICS Thread library                                           */
#include"tprdev.h"
#include"tprPcie.h"

static uint32_t addressmap[] = {
    0x10000,                 /* TPR_FPGAVERSION    */

    0x40000,                 /* TPR_XBAROUT(0)     */
    0x40004,                 /* TPR_XBAROUT(1)     */
    0x40008,                 /* TPR_XBAROUT(2)     */
    0x4000c,                 /* TPR_XBAROUT(3)     */

    0x80000,                 /* TPR_IRQCONTROL     */
    0x80004,                 /* TPR_IRQSTATUS      */
    0x80010,                 /* TPR_COUNTRESET     */

    0x80020,                 /* TPR_CH_CONTROL(0)  */
    0x80040,                 /* TPR_CH_CONTROL(1)  */
    0x80060,                 /* TPR_CH_CONTROL(2)  */
    0x80080,                 /* TPR_CH_CONTROL(3)  */
    0x800a0,                 /* TPR_CH_CONTROL(4)  */
    0x800c0,                 /* TPR_CH_CONTROL(5)  */
    0x800e0,                 /* TPR_CH_CONTROL(6)  */
    0x80100,                 /* TPR_CH_CONTROL(7)  */
    0x80120,                 /* TPR_CH_CONTROL(8)  */
    0x80140,                 /* TPR_CH_CONTROL(9)  */
    0x80160,                 /* TPR_CH_CONTROL(10) */
    0x80180,                 /* TPR_CH_CONTROL(11) */

    0x80024,                 /* TPR_CH_EVTSEL(0)   */
    0x80044,                 /* TPR_CH_EVTSEL(1)   */
    0x80064,                 /* TPR_CH_EVTSEL(2)   */
    0x80084,                 /* TPR_CH_EVTSEL(3)   */
    0x800a4,                 /* TPR_CH_EVTSEL(4)   */
    0x800c4,                 /* TPR_CH_EVTSEL(5)   */
    0x800e4,                 /* TPR_CH_EVTSEL(6)   */
    0x80104,                 /* TPR_CH_EVTSEL(7)   */
    0x80124,                 /* TPR_CH_EVTSEL(8)   */
    0x80144,                 /* TPR_CH_EVTSEL(9)   */
    0x80164,                 /* TPR_CH_EVTSEL(10)  */
    0x80184,                 /* TPR_CH_EVTSEL(11)  */

    0x80028,                 /* TPR_CH_EVTCNT(0)   */
    0x80048,                 /* TPR_CH_EVTCNT(1)   */
    0x80068,                 /* TPR_CH_EVTCNT(2)   */
    0x80088,                 /* TPR_CH_EVTCNT(3)   */
    0x800a8,                 /* TPR_CH_EVTCNT(4)   */
    0x800c8,                 /* TPR_CH_EVTCNT(5)   */
    0x800e8,                 /* TPR_CH_EVTCNT(6)   */
    0x80108,                 /* TPR_CH_EVTCNT(7)   */
    0x80128,                 /* TPR_CH_EVTCNT(8)   */
    0x80148,                 /* TPR_CH_EVTCNT(9)   */
    0x80168,                 /* TPR_CH_EVTCNT(10)  */
    0x80188,                 /* TPR_CH_EVTCNT(11)  */

    0x8002c,                 /* TPR_CH_BSADELAY(0) */
    0x8004c,                 /* TPR_CH_BSADELAY(1) */
    0x8006c,                 /* TPR_CH_BSADELAY(2) */
    0x8008c,                 /* TPR_CH_BSADELAY(3) */
    0x800ac,                 /* TPR_CH_BSADELAY(4) */
    0x800cc,                 /* TPR_CH_BSADELAY(5) */
    0x800ec,                 /* TPR_CH_BSADELAY(6) */
    0x8010c,                 /* TPR_CH_BSADELAY(7) */
    0x8012c,                 /* TPR_CH_BSADELAY(8) */
    0x8014c,                 /* TPR_CH_BSADELAY(9) */
    0x8016c,                 /* TPR_CH_BSADELAY(10)*/
    0x8018c,                 /* TPR_CH_BSADELAY(11)*/

    0x80030,                 /* TPR_CH_BSAWIDTH(0) */
    0x80040,                 /* TPR_CH_BSAWIDTH(1) */
    0x80060,                 /* TPR_CH_BSAWIDTH(2) */
    0x80080,                 /* TPR_CH_BSAWIDTH(3) */
    0x800a0,                 /* TPR_CH_BSAWIDTH(4) */
    0x800c0,                 /* TPR_CH_BSAWIDTH(5) */
    0x800e0,                 /* TPR_CH_BSAWIDTH(6) */
    0x80100,                 /* TPR_CH_BSAWIDTH(7) */
    0x80120,                 /* TPR_CH_BSAWIDTH(8) */
    0x80140,                 /* TPR_CH_BSAWIDTH(9) */
    0x80160,                 /* TPR_CH_BSAWIDTH(10)*/
    0x80180,                 /* TPR_CH_BSAWIDTH(11)*/

    0x80200,                 /* TPR_TR_CONTROL(0)  */
    0x80210,                 /* TPR_TR_CONTROL(1)  */
    0x80220,                 /* TPR_TR_CONTROL(2)  */
    0x80230,                 /* TPR_TR_CONTROL(3)  */
    0x80240,                 /* TPR_TR_CONTROL(4)  */
    0x80250,                 /* TPR_TR_CONTROL(5)  */
    0x80260,                 /* TPR_TR_CONTROL(6)  */
    0x80270,                 /* TPR_TR_CONTROL(7)  */
    0x80280,                 /* TPR_TR_CONTROL(8)  */
    0x80290,                 /* TPR_TR_CONTROL(9)  */
    0x802a0,                 /* TPR_TR_CONTROL(10) */
    0x802b0,                 /* TPR_TR_CONTROL(11) */

    0x80204,                 /* TPR_TR_DELAY(0)    */
    0x80214,                 /* TPR_TR_DELAY(1)    */
    0x80224,                 /* TPR_TR_DELAY(2)    */
    0x80234,                 /* TPR_TR_DELAY(3)    */
    0x80244,                 /* TPR_TR_DELAY(4)    */
    0x80254,                 /* TPR_TR_DELAY(5)    */
    0x80264,                 /* TPR_TR_DELAY(6)    */
    0x80274,                 /* TPR_TR_DELAY(7)    */
    0x80284,                 /* TPR_TR_DELAY(8)    */
    0x80294,                 /* TPR_TR_DELAY(9)    */
    0x802a4,                 /* TPR_TR_DELAY(10)   */
    0x802b4,                 /* TPR_TR_DELAY(11)   */

    0x80208,                 /* TPR_TR_WIDTH(0)    */
    0x80218,                 /* TPR_TR_WIDTH(1)    */
    0x80228,                 /* TPR_TR_WIDTH(2)    */
    0x80238,                 /* TPR_TR_WIDTH(3)    */
    0x80248,                 /* TPR_TR_WIDTH(4)    */
    0x80258,                 /* TPR_TR_WIDTH(5)    */
    0x80268,                 /* TPR_TR_WIDTH(6)    */
    0x80278,                 /* TPR_TR_WIDTH(7)    */
    0x80288,                 /* TPR_TR_WIDTH(8)    */
    0x80298,                 /* TPR_TR_WIDTH(9)    */
    0x802a8,                 /* TPR_TR_WIDTH(10)   */
    0x802b8,                 /* TPR_TR_WIDTH(11)   */

    0x8020c,                 /* TPR_TR_FINEDEL(0)  */
    0x8021c,                 /* TPR_TR_FINEDEL(1)  */
    0x8022c,                 /* TPR_TR_FINEDEL(2)  */
    0x8023c,                 /* TPR_TR_FINEDEL(3)  */
    0x8024c,                 /* TPR_TR_FINEDEL(4)  */
    0x8025c,                 /* TPR_TR_FINEDEL(5)  */
    0x8026c,                 /* TPR_TR_FINEDEL(6)  */
    0x8027c,                 /* TPR_TR_FINEDEL(7)  */
    0x8028c,                 /* TPR_TR_FINEDEL(8)  */
    0x8029c,                 /* TPR_TR_FINEDEL(9)  */
    0x802ac,                 /* TPR_TR_FINEDEL(10) */
    0x802bc,                 /* TPR_TR_FINEDEL(11) */

    0xc0020,                 /* TPR_CSR            */
    0xc0024,                 /* TPR_MSGDLY         */

    0x801a8,                 /* TPR_FRAMECNT       */
};

static struct tprPcie {
    int                fd[DEVNODE_MINOR_BSA+1];
    uint32_t          *r;
    tprQueues         *q;
    void              *token;
    tprMessageCallback cb;
} tprData[MAX_TPR] = {
    {
        { -1 },
        NULL,
        NULL
    }
};

void *tprOpen(int card, int minor)
{
    char strDevice[20];
    int ret;
    struct tprPcie *devpvt = &tprData[card];
    static int didinit = 0;

    if (!didinit) {
        int i;
        didinit = 1;
        for (i = 0; i < MAX_TPR; i++) {
            int j;
            for (j = 0; j <= DEVNODE_MINOR_BSA; j++)
                tprData[i].fd[j] = -1;
            tprData[i].r = NULL;
            tprData[i].q = NULL;
            tprData[i].token = NULL;
            tprData[i].cb = NULL;
        }
        didinit = 1;
    }

    if (minor == DEVNODE_MINOR_BSA)
        ret = snprintf(strDevice, sizeof(strDevice), "%s%cBSA", DEVNODE_NAME_BASE, card + 'a');
    else
        ret = snprintf(strDevice, sizeof(strDevice), "%s%c%c", DEVNODE_NAME_BASE, card + 'a',
                       (minor == DEVNODE_MINOR_CONTROL) ? 0 : ((minor <= 9) ? ('0' + minor)
                                                                        : ('a' - 10 + minor)));
    if (ret < 0) {
        errlogPrintf("%s@%d(snprintf): %s.\n", __func__, __LINE__, strerror(-ret));
        return NULL;
    }
    devpvt->fd[minor] = open(strDevice, O_RDWR);
    if (devpvt->fd[minor] < 0) {
        errlogPrintf("%s@%d(open) Error: %s opening %s\n", __func__, __LINE__, strerror(errno), strDevice);
        return NULL;
    }
    if (minor == DEVNODE_MINOR_CONTROL)
        devpvt->r = (uint32_t *) mmap(0, TPR_CONTROL_WINDOW, PROT_WRITE|PROT_READ, MAP_SHARED, devpvt->fd[minor], 0);
    else if (!devpvt->q)
        devpvt->q = (tprQueues *) mmap(0, TPR_QUEUE_WINDOW, PROT_READ, MAP_SHARED, devpvt->fd[minor], 0);
    if (!devpvt->r && !devpvt->q) { // We had to set *one* of these!
        close(devpvt->fd[minor]);
        return NULL;
    }
    return (void *)devpvt;
}

void tprClose(int card, int minor)
{
    if (tprData[card].fd[minor] >= 0) {
        close(tprData[card].fd[minor]);
        tprData[card].fd[minor] = -1;
    }
}

static int tprIrqHandlerThread(void *p)
{
    struct tprPcie *devpvt = (struct tprPcie *)p;
    tprQueues *q = devpvt->q;
    fd_set all;
    int i, mfd = -1;
    int64_t allrp[MOD_SHARED];
    int64_t bsarp = 0;
    int have_bsa = (devpvt->fd[DEVNODE_MINOR_BSA] >= 0);

    FD_ZERO(&all);
    for (i = 0; i < DEVNODE_MINOR_CONTROL; i++) {
        if (devpvt->fd[i] < 0)
            continue;
        if (devpvt->fd[i] > mfd)
            mfd = devpvt->fd[i];
        FD_SET(devpvt->fd[i], &all);
        allrp[i] = q->allwp[i];
    }
    if (have_bsa) {
        if (devpvt->fd[DEVNODE_MINOR_BSA] > mfd)
            mfd = devpvt->fd[DEVNODE_MINOR_BSA];
        FD_SET(devpvt->fd[DEVNODE_MINOR_BSA], &all);
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

        if (have_bsa && FD_ISSET(devpvt->fd[DEVNODE_MINOR_BSA], &rds)) {
            nb = read(devpvt->fd[DEVNODE_MINOR_BSA], buf, 32); /* Why 32? */
            if (nb <= 0)
                continue;   /* This can't be good... */

            while(bsarp < q->bsawp) {
                if (devpvt->cb)
                    devpvt->cb(devpvt->token, DEVNODE_MINOR_BSA,
                               (tprHeader *)&q->bsaq[bsarp++ & (MAX_TPR_BSAQ-1)].word[0]);
            }
            cnt--;
        }
            
        for (i = 0; cnt && i < DEVNODE_MINOR_CONTROL; i++) {
            if (!FD_ISSET(devpvt->fd[i], &rds))
                continue;
            nb = read(devpvt->fd[i], buf, 32); /* Why 32? */
            if (nb <= 0)
                continue;   /* This can't be good... */

            while(allrp[i] < q->allwp[i]) {
                if (devpvt->cb)
                    devpvt->cb(devpvt->token, i,
                               (tprHeader *)&q->allq[q->allrp[i].idx[allrp[i]++ & (MAX_TPR_ALLQ-1)]
                                                     & (MAX_TPR_ALLQ-1)].word[0]);
            }
            cnt--;
        }
    }
    return 0; /* Never get here anyway. */
}

void tprRegister(void *dev, tprMessageCallback cb, void *token)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    devpvt->cb = cb;
    devpvt->token = token;
    epicsThreadMustCreate("tprIrqHandler", epicsThreadPriorityHigh+9,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)tprIrqHandlerThread, devpvt);
}

int tprMaster(void *dev)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    return devpvt->r != NULL;
}

void tprRegWrite(void *dev, int reg, uint32_t value)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    *(uint32_t *)(addressmap[reg] + (char *)devpvt->r) = value;
}

uint32_t tprRegRead(void *dev, int reg)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    return *(uint32_t *)(addressmap[reg] + (char *)devpvt->r);
}
