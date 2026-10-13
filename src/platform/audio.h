/* audio.h - the sound board's samples to the sound device.
 *
 * The SDK's vp_audio plays files and ready samples; the sound board makes
 * its sound as it runs, so it has an SDL audio device of its own that
 * pulls from the board's ring (sound/sndboard.h). With no sound device the
 * game runs silent.
 */
#ifndef SWVP_AUDIO_H
#define SWVP_AUDIO_H

#include "sound/sndboard.h"

/* Returns 0, or -1 if no device could be opened (a message is printed). */
int  sw_audio_open(sw_sndboard *snd);
void sw_audio_close(void);

/* Keep the board's ring from running full when the device takes samples a
 * little slower than the board makes them: call once per frame. */
void sw_audio_balance(sw_sndboard *snd);

#endif
