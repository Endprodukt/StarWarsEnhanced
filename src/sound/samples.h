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
 * size); mono or stereo, any rate (they are brought to the board's rate when
 * loaded). A stereo recording plays in stereo (the music), a mono one in the
 * middle; the board's own sound is mono, as the arcade machine's. The
 * engine's loops are made mono. package/sounds has the effects and speech at
 * 16 kHz mono, the music stereo, ADPCM (tools/swsamples).
 *
 * How they play, after what the board's program does with its own:
 *   effects  several at once, the same one too (a new shot does not cut the
 *            last); AUDPBZ stops AUDPB, AUDTCZ stops AUDTC.
 *   music    one piece at a time: a new one (replaced or not) ends the one
 *            playing.
 *   speech   one sentence after the other; SPKSKP drops those waiting.
 * Request 00 (the board's reset) silences everything.
 *
 * Ducking (so that it is not all too much at once, and speech is always
 * understood): while a sentence is spoken (a recording, or the board's own
 * speech chip) everything else is 3 dB lower; while a big explosion or a
 * crash sounds (AUDSX, AUDDF, AUDCR) the other effects, the engine and the
 * music are 1.5 dB lower. It comes in within 20 ms and goes in 300 ms. The
 * board's own music and effects (its POKEYs) duck too (sw_samples_board_gain).
 *
 * Cues: a recording the program itself starts, a while from now, in a
 * voice of its own (sw_samples_cue): the Millennium Falcon's fly-by
 * (effects/FALCON.wav, FALCON_2.wav ...; each has its loudest at
 * SW_FALCON_PEAK seconds), and "Yahoo, you're all clear, kid" just as it
 * passes over the player. While the Falcon flies (hold_yahoo) the game's own
 * request for that line is taken and not played: it comes with the Falcon.
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

/* A recording at the board's rate: n frames of `ch` samples (1 mono, 2 left
 * and right, interleaved). */
typedef struct sw_sample { int16_t *pcm; uint32_t n; uint8_t ch; } sw_sample;

/* the recordings of one sound, and which played last */
typedef struct sw_sample_set {
    sw_sample var[SW_SAMPLE_VARIANTS];
    int       count, last;
} sw_sample_set;

#define SW_CUES        4
#define SW_CUE_FALCON  (-1)         /* sw_samples_cue: the Falcon's fly-by */
#define SW_FALCON_PEAK 0.85         /* seconds into each of its recordings: its loudest (tools/swsamples) */

typedef struct sw_samples {
    int      enabled;
    sw_sample_set sound[SW_SOUND_REQUESTS];     /* the recordings of each; none: not replaced */
    sw_sample_set falcon;                       /* the Millennium Falcon's fly-by */
    struct {
        const sw_sample *rec;       /* NULL: free */
        uint32_t wait, at;          /* samples to wait yet, where it is */
        int      speech;            /* (it counts as speech for the ducking) */
    } cue[SW_CUES];
    int      hold_yahoo;            /* the game's SPKYAU is taken and not played (the Falcon brings it) */
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
    /* the volumes (test menu, SOUND): music, effects, speech, engine, 1 = as made */
    float    vol[4];
    /* ducking: the gains as they are now (1: none), and the board's speech */
    float    duck_speech, duck_big;
    int      board_speaks;          /* set by the board: its speech chip makes sound now */
    uint32_t board_hold;            /* (samples it still counts as speaking) */
    int      rate;
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

/* Request n's recording (or, n SW_CUE_FALCON, the Falcon's fly-by) to begin
 * `delay` seconds from now, in a voice of its own (a negative delay: that
 * much of its beginning left out). Speech among them counts as speech. */
void sw_samples_cue(sw_samples *s, int n, double delay);

/* The engine as it is to be heard: `which` SW_ENGINE_COCKPIT or
 * SW_ENGINE_OUTSIDE (-1: none, it fades out), `level` 0..1 (of its full
 * loudness), `rate` how fast it runs (1 as recorded; higher as the ship
 * speeds up). Called as often as it changes. */
void sw_samples_engine(sw_samples *s, int which, float level, float rate);

/* The next frame of everything playing, left and right (16 bit range, not
 * clipped). */
void sw_samples_next2(sw_samples *s, int *left, int *right);

/* The same as one sample, the two sides together. */
int  sw_samples_next(sw_samples *s);

/* The volumes of the four kinds of sound (1 = as the recordings were made;
 * the board's own POKEYs take the effects', its speech chip the speech's). */
void sw_samples_volumes(sw_samples *s, float music, float effects, float speech, float engine);

/* How loud the board's own POKEYs (its music and effects) are to be now,
 * after the ducking (1 when nothing ducks, or the recordings are off) and
 * the effects' volume; and its speech chip, by the speech's volume. */
float sw_samples_board_gain(const sw_samples *s);
float sw_samples_board_speech_gain(const sw_samples *s);

#endif
