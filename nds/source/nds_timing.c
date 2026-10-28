#include "nds_timing.h"

#include <nds.h>

/* Timer 2 counts at 33.513982 MHz / 1024 = 32728.5 Hz; timer 3 counts its
 * overflows. One DS screen frame (59.8261 Hz) is 547.07 ticks. */
void NdsTimingStart(void) {
  TIMER_CR(2) = 0;
  TIMER_CR(3) = 0;
  TIMER_DATA(2) = 0;
  TIMER_DATA(3) = 0;
  TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE;
  TIMER_CR(2) = TIMER_ENABLE | TIMER_DIV_1024;
}

uint32_t NdsTimingTicks(void) {
  uint16_t hi, lo;
  do {
    hi = TIMER_DATA(3);
    lo = TIMER_DATA(2);
  } while (hi != TIMER_DATA(3));
  return ((uint32_t)hi << 16) | lo;
}

uint32_t NdsTimingFramesElapsed(void) {
  return (uint32_t)(((uint64_t)NdsTimingTicks() * 100u) / 54707u);
}
