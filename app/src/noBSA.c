#include<stdint.h>
#include"bsaCallbackApi.h"

void noBsaTimingCallback( void * pUserPvt, BsaTimingData * newPattern )
{
    return; /* NOP! */
}
