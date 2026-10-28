#ifndef SMW_NDS_TIMING_H
#define SMW_NDS_TIMING_H

#include <stdint.h>

/* Start counting real time with hardware timers 2 and 3. */
void NdsTimingStart(void);

/* Number of 59.8261 Hz screen frames since NdsTimingStart(). */
uint32_t NdsTimingFramesElapsed(void);

#endif
