/* samples.h - sounds of the game replaced by recordings.
 *
 * Every sound the game asks the sound board for (docs/SOUNDS.md: 59
 * requests, speech, music and effects) can be replaced by a WAV file named
 * after it: sounds/AUDXL.wav is played whenever the game asks for the
 * X-wing's lasers, and the board's own sound for that request is then not
 * made. Requests without a file sound as they always did, so sounds can be
 * replaced one at a time.
 *
 * Several recordings for one request (multisamples): NAME.wav, NAME_2.wav,
 * ... NAME_12.wav. Each time it is asked for one of them plays, chosen at
 * random, never the same twice running: a TIE fighter's pass-by or its
 * cannon do not sound the same every time.
 *
 * The files: WAV, PCM 8 or 16 bit, or IMA ADPCM (4 bit: a quarter of the
 * size); mono or stereo, any rate (they are brought to the board's rate and
 * to mono when loaded). package/sounds has them at 16 kHz, mono, ADPCM
 * (tools/swsamples).
 *
 * How they play, after what the board's program does with its own:
 *   effects  several at once, the same one too (a new shot does not cut the
 *            last); AUDPBZ stops AUDPB, AUDTCZ stops AUDTC.
 *   music    one piece at a time: a new one (replaced or not) ends the one
 *            playing.
 *   speech   one sentence after the other; SPKSKP drops those waiting.
 * Request 00 (the board's reset) silences everything.
 *
 * Besides the requests, the X-wing's own engine (enhanced: the arcade game
 * has none): a loop, one heard from inside the cockpit (XWENG.wav), one
 * from outside (XWEXT.wav), in effects/. The program says which, how loud
 * and how fast (sw_samples_engine); a change of view crossfades.
 */
#ifndef SWE_SAMPLES_H
#define SWE_SAMPLES_H

#include <stdint.h>

#define SW_SOUND_REQUESTS  60       /* 00 to 3B */
#define SW_SAMPLE_VOICES   12
#define SW_SAMPLE_QUEUE    8
#define SW_SAMPLE_VARIANTS 12

/* The engine's loops. */
enum { SW_ENGINE_COCKPIT, SW_ENGINE_OUTSIDE, SW_ENGINE_LOOPS };

typedef struct sw_sample { int16_t *pcm; uint32_t n; } sw_sample;     /* mono, at the board's rate */

typedef struct sw_samples {
    int      enabled;
    struct {
        sw_sample var[SW_SAMPLE_VARIANTS];      /* its recordings; none: not replaced */
        int       count, last;                  /* how many, which played last */
    } sound[SW_SOUND_REQUESTS];
    struct {
        int      id;                /* the request playing, 0: free */
        const sw_sample *rec;       /* which of its recordings */
        uint32_t at;
        uint32_t started;           /* (the oldest gives way when all are busy) */
    } effect[SW_SAMPLE_VOICES], music, speech;
    uint8_t  queue[SW_SAMPLE_QUEUE];    /* sentences waiting */
    int      queued;
    uint32_t clock;
    uint32_t rng;
    /* the engine */
    struct {
        sw_sample rec;
        double    at;               /* where in the loop (samples, between them) */
        float     gain;             /* as it is now: it eases to `want` */
        float     want;
    } engine[SW_ENGINE_LOOPS];
    float    engine_rate;           /* how fast it is played (1: as recorded) */
} sw_samples;

/* What request n asks for ("AUDXL"), "" if nothing. */
const char *sw_sound_name(int n);

/* The folder for that kind of sound: "voice", "music", "effects", or
 * "test" (the sound board's own test). */
const char *sw_sound_folder(int n);

/* Zero the struct first. Loads DIR/NAME.wav (and NAME_2.wav ...), or the
 * same in DIR/<its folder>/, for every request that has one, and the
 * engine's loops, at `rate` samples a second. A request that already has
 * recordings (from a folder asked before) keeps them. Returns how many
 * requests (and loops) were found. */
int  sw_samples_load(sw_samples *s, const char *dir, int rate);
void sw_samples_free(sw_samples *s);

/* The game asks for request n. Returns 0 if the board is to make the sound
 * itself, 1 if a recording plays instead, 2 the same and the board's own
 * music is to stop. */
int  sw_samples_request(sw_samples *s, int n);

/* The engine as it is to be heard: `which` SW_ENGINE_COCKPIT or
 * SW_ENGINE_OUTSIDE (-1: none, it fades out), `level` 0..1 (of its full
 * loudness), `rate` how fast it runs (1 as recorded; higher as the ship
 * speeds up). Called as often as it changes. */
void sw_samples_engine(sw_samples *s, int which, float level, float rate);

/* The next sample of everything playing (16 bit range, not clipped). */
int  sw_samples_next(sw_samples *s);

#endif
