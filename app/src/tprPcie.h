//////////////////////////////////////////////////////////////////////////////
// This file is part of 'tpr'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'tpr', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//////////////////////////////////////////////////////////////////////////////
#include"tpr.h"

#define DEVNODE_NAME_BASE	"/dev/tpr"
#define MOD_SHARED 12

#define TPR_CONTROL_WINDOW      0xc0030
#define TPR_QUEUE_WINDOW        ((sizeof(struct TprQueues) + PAGE_SIZE) & PAGE_MASK)

/* Stuff from the kernel driver... */

/* These must be powers of two!!! */
#define MAX_TPR_ALLQ (32*1024)
#define MAX_TPR_BSAQ  1024
#define MSG_SIZE      32

/* DMA Buffer Size, Bytes (could be as small as 512B) */
#define BUF_SIZE 4096
#define NUMBER_OF_RX_BUFFERS 1023

struct TprEntry {
  u32 word[MSG_SIZE];
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
  struct TprEntry  bsaq  [MAX_TPR_BSAQ]; // queue of BSA messages
  struct TprQIndex allrp [MOD_SHARED];   /* indices into allq */
  long long        allwp [MOD_SHARED];   /* write pointer into allrp */
  long long        bsawp;
  long long        gwp;
  int              fifofull;
} tprQueues;
