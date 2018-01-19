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

static struct tprPcie {
    int              fd[DEVNODE_MINOR_BSA+1];
    uint32_t        *r;
    tprQueues       *q;
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
    tprCardStruct *pCard = (tprCardStruct *)p;
    struct tprPcie *devpvt = (struct tprPcie *)pCard->devpvt;
    tprQueues *q = devpvt->q;
    fd_set all;
    int i, mfd = -1;
    int64_t allrp[MOD_SHARED];
    int64_t bsarp = 0;
    int have_bsa = pCard->mmask & (1 << DEVNODE_MINOR_BSA);

    FD_ZERO(&all);
    for (i = 0; i < DEVNODE_MINOR_CONTROL; i++) {
        if (pCard->mmask & (1 << i)) {
            if (devpvt->fd[i] > mfd)
                mfd = devpvt->fd[i];
            FD_SET(devpvt->fd[i], &all);
            allrp[i] = q->allwp[i];
        }
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
                tprMessageProcess(pCard, DEVNODE_MINOR_BSA,
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
                tprMessageProcess(pCard, i,
                                  (tprHeader *)&q->allq[q->allrp[i].idx[allrp[i]++ & (MAX_TPR_ALLQ-1)]
                                                        & (MAX_TPR_ALLQ-1)].word[0]);
            }
            cnt--;
        }
    }
    return 0; /* Never get here anyway. */
}

void tprInitialize(tprCardStruct *pCard)
{
    if (pCard->mmask & ~(1 << DEVNODE_MINOR_CONTROL))
        epicsThreadMustCreate("tprIrqHandler", epicsThreadPriorityHigh+9,
                              epicsThreadGetStackSize(epicsThreadStackMedium),
                              (EPICSTHREADFUNC)tprIrqHandlerThread, pCard);
}

int tprMaster(void *dev)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    return devpvt->r != NULL;
}

void tprRegWrite(void *dev, int reg, uint32_t value)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    *(uint32_t *)(devpvt->r + reg) = value;
}

uint32_t tprRegRead(void *dev, int reg)
{
    struct tprPcie *devpvt = (struct tprPcie *)dev;
    return *(uint32_t *)(devpvt->r + reg);
}
