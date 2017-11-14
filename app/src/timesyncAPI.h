#ifndef INCevrTimeH
#define INCevrTimeH 

/*
 * The minimal declarations needed to make the timesync module work!
 */
#define MAX_TS_QUEUE           512              /* # timestamps queued per event */
#define MAX_TS_QUEUE_MASK      511

#include<epicsTime.h>

extern  int timesyncGetFifo(epicsTimeStamp     *epicsTime_ps,
                            long long          *fid, 
                            unsigned int        eventCode,
                            unsigned long long *idx,
                            int                 incr);
extern long long timesyncGetLastFiducial( );         /* Returns lastfid, the last fiducial set by ISR */
#endif
