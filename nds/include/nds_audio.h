#ifndef SMW_NDS_AUDIO_H
#define SMW_NDS_AUDIO_H

/* Music and sound effects, streamed to the DS sound hardware. */

/* Start streaming. Call after NdsTimingStart(). */
void NdsAudioInit(void);

/* Call once per game frame, right after PortRunFrame(). */
void NdsAudioFrame(void);

#endif
