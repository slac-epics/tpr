#ifndef INCevrTimeH
#define INCevrTimeH 

/*
 * The minimal declarations needed to make the timesync module work!
 */
#define MAX_TS_QUEUE           512              /* # timestamps queued per event */
#define MAX_TS_QUEUE_MASK      511

int evrTimeGetFifo(epicsTimeStamp     *epicsTime_ps,
                   unsigned int        eventCode,
                   unsigned long long *idx,
                   int                 incr);

#endif
