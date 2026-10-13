/* samples.h - sounds of the game replaced by recordings.
 *
 * Every sound the game asks the sound board for (docs/SOUNDS.md: 59
 * requests, speech, music and effects) can be replaced by a WAV file named
 * after it: sounds/AUDXL.wav is played whenever the game asks for the
 * X-wing's lasers, and the board's own sound for that request is then not
 * made. Requests without a file sound as they always did, so sounds can be
 * replaced one at a time.
 *
 * The files: WAV, PCM, 8 or 16 bit, mono or stereo, any rate (they are
 * brought to the board's rate and to mono when loaded).
 *
 * How they play, after what the board's program does with its own:
 *   effects  several at once; one asked for again starts again. AUDPBZ
 *            stops AUDPB, AUDTCZ stops AUDTC.
 *   music    one piece at a time: a new one (replaced or not) ends the one
 *            playing.
 *   speech   one sentence after the other; SPKSKP drops those waiting.
 * Request 00 (the board's reset) silences everything.
 */
#ifndef SWE_SAMPLES_H
#define SWE_SAMPLES_H

#include <stdint.h>

#define SW_SOUND_REQUESTS 60        /* 00 to 3B */
#define SW_SAMPLE_VOICES  8
#define SW_SAMPLE_QUEUE   8

typedef struct sw_samples {
    int      enabled;
    struct {
        int16_t *pcm;               /* mono, at the board's rate; NULL: not replaced */
        uint32_t n;
    } sound[SW_SOUND_REQUESTS];
    struct {
        int      id;                /* the request playing, 0: free */
        uint32_t at;
        uint32_t started;           /* (the oldest gives way when all are busy) */
    } effect[SW_SAMPLE_VOICES], music, speech;
    uint8_t  queue[SW_SAMPLE_QUEUE];    /* sentences waiting */
    int      queued;
    uint32_t clock;
} sw_samples;

/* What request n asks for ("AUDXL"), "" if nothing. */
const char *sw_sound_name(int n);

/* The folder for that kind of sound: "voice", "music", "effects", or
 * "test" (the sound board's own test). */
const char *sw_sound_folder(int n);

/* Zero the struct first. Loads DIR/NAME.wav, or DIR/<its folder>/NAME.wav,
 * for every request that has one, at `rate` samples a second. Returns how
 * many were found. */
int  sw_samples_load(sw_samples *s, const char *dir, int rate);
void sw_samples_free(sw_samples *s);

/* The game asks for request n. Returns 0 if the board is to make the sound
 * itself, 1 if a recording plays instead, 2 the same and the board's own
 * music is to stop. */
int  sw_samples_request(sw_samples *s, int n);

/* The next sample of everything playing (16 bit range, not clipped). */
int  sw_samples_next(sw_samples *s);

#endif
