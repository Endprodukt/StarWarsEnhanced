/* samples.c - see samples.h. */
#include "sound/samples.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The requests of the sound board in their order (SNDPBX.MAC, docs/SOUNDS.md). */
static const char *const NAMES[SW_SOUND_REQUESTS] = {
    "", "AUDTST", "PMTST",
    "SPKALW", "SPKELE", "SPKFOA", "SPKFOR", "SPKGRE", "SPKHAV", "SPKHIT", "SPKI", "SPKIMO", "SPKLET",
    "SPKLOS", "SPKR2N", "SPKR2T", "SPKRED", "SPKREM", "SPKSHK", "SPKSIZ", "SPKSKP", "SPKSTA", "SPKSTR",
    "SPKTHI", "SPKTRU", "SPKUSE", "SPKYAU",
    "PMBEN", "PMCNT", "PMDAR", "PMDES", "PMEND", "PM4TH", "PMREB", "PMRRP", "PMSF2", "PMTH5", "PMTHB",
    "AUDCR", "AUDDF", "AUDDR", "AUDNAY", "AUDPB", "AUDPBZ", "AUDPF", "AUDPH", "AUDPS", "AUDRC", "AUDRD",
    "AUDRS", "AUDRY", "AUDSH", "AUDSS", "AUDSX", "AUDTC", "AUDTCZ", "AUDTH", "AUDTL", "AUDXL", "AUDYEA",
};

/* the Falcon's fly-by, in effects/ */
static const char *const FALCON = "FALCON";
static const char *const GROUND_BLAST = "AUDSXG";    /* AUDSX over the ground: towers and bunkers, no TIE engine */
/* the engine's loops, in effects/ */
static const char *const ENGINE[SW_ENGINE_LOOPS] = { "XWENG", "XWEXT" };

enum {
    FIRST_SPEECH = 3, SPKSKP = 20, SPKYAU = 26, LAST_SPEECH = 26,
    FIRST_MUSIC = 27, LAST_MUSIC = 37,
    AUDCR = 38, AUDDF = 39, AUDNAY = 41, AUDPB = 42, AUDPBZ = 43, AUDSX = 53, AUDTC = 54, AUDTCZ = 55, AUDYEA = 59,
};

/* the ducking (samples.h): how much, how fast */
#define DUCK_SPEECH  0.7079f        /* -3 dB under speech */
#define DUCK_BIG     0.8414f        /* -1.5 dB under a big explosion */
#define DUCK_ATTACK  0.020f         /* seconds */
#define DUCK_RELEASE 0.300f
#define BOARD_HOLD   0.15f          /* the speech chip's pauses between words still count */
#define MUSIC_TAIL   0.7f           /* seconds a replaced or stopped piece takes to die away (to -40 dB) */

static int is_big(int id) { return id == AUDSX || id == AUDDF || id == AUDCR; }

const char *sw_sound_name(int n)
{
    return n > 0 && n < SW_SOUND_REQUESTS ? NAMES[n] : "";
}

const char *sw_sound_folder(int n)
{
    if (n >= FIRST_SPEECH && n <= LAST_SPEECH) return "voice";
    if (n >= FIRST_MUSIC && n <= LAST_MUSIC) return "music";
    if (n == 1 || n == 2 || n == AUDNAY || n == AUDYEA) return "test";
    return n > LAST_MUSIC && n < SW_SOUND_REQUESTS ? "effects" : "";
}

/* --- WAV files ---------------------------------------------------------------------- */

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* IMA ADPCM (WAVE_FORMAT_IMA_ADPCM, 0x11): blocks of `align` bytes; each
 * begins, for each channel, with its first sample and the step's index (4
 * bytes), then 4-bit codes, the low nibble first, 8 of a channel at a time
 * (4 bytes) in turn. */
static const int IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767,
};
static const int IMA_INDEX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static int16_t ima_step(int *pred, int *index, unsigned code)
{
    int step = IMA_STEP[*index], diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    *pred += code & 8 ? -diff : diff;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *index += IMA_INDEX[code & 15];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return (int16_t)*pred;
}

/* Decodes `bytes` of IMA ADPCM into interleaved 16-bit frames (at most
 * `frames` of them, the file's own count). Returns the frames made. */
static uint32_t ima_decode(const uint8_t *d, uint32_t bytes, unsigned channels, unsigned align, uint32_t frames, int16_t *out)
{
    uint32_t made = 0;
    unsigned per_block = align > 4 * channels ? (align - 4 * channels) * 2 / channels + 1 : 0;
    for (uint32_t b = 0; b + 4 * channels <= bytes && made < frames && per_block; b += align) {
        const uint8_t *p = d + b;
        uint32_t len = bytes - b < align ? bytes - b : align;
        int pred[2], index[2];
        for (unsigned c = 0; c < channels; c++) {
            pred[c] = (int16_t)le16(p + 4 * c);
            index[c] = p[4 * c + 2] > 88 ? 88 : p[4 * c + 2];
            out[made * channels + c] = (int16_t)pred[c];
        }
        uint32_t base = made;
        made++;
        /* the codes after the headers: per channel 4 bytes (8 samples) in turn */
        uint32_t groups = (len - 4 * channels) / (4 * channels);
        for (uint32_t g = 0; g < groups; g++) {
            for (unsigned c = 0; c < channels; c++) {
                const uint8_t *q = p + 4 * channels + (g * channels + c) * 4;
                for (unsigned k = 0; k < 8; k++) {
                    uint32_t f = base + 1 + g * 8 + k;
                    if (f >= frames) continue;
                    unsigned code = k & 1 ? q[k >> 1] >> 4 : q[k >> 1] & 15;
                    out[f * channels + c] = ima_step(&pred[c], &index[c], code);
                }
            }
        }
        uint32_t end = base + 1 + groups * 8;
        made = end < frames ? end : frames;
    }
    return made;
}

/* The file at `rate`: stereo if it is (and `mono` is not asked for), else
 * mono. Returns 0, or -1 if it is not a WAV file of PCM (8 or 16 bit) or IMA
 * ADPCM. */
static int load_wav(const char *path, int rate, int16_t **pcm, uint32_t *n, uint8_t *ch_out, int mono)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = size > 44 && size < (64L << 20) ? (uint8_t *)malloc((size_t)size) : NULL;
    if (!d || fread(d, 1, (size_t)size, f) != (size_t)size) { free(d); fclose(f); return -1; }
    fclose(f);

    int ok = -1;
    unsigned channels = 0, bits = 0, format = 0, align = 0;
    uint32_t hz = 0, fact = 0;
    const uint8_t *data = NULL;
    uint32_t bytes = 0;
    if (memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WAVE", 4) == 0) {
        for (long at = 12; at + 8 <= size; ) {
            uint32_t len = le32(d + at + 4);
            if ((long)len > size - at - 8) len = (uint32_t)(size - at - 8);
            if (memcmp(d + at, "fmt ", 4) == 0 && len >= 16) {
                format = le16(d + at + 8);
                channels = le16(d + at + 10);
                hz = le32(d + at + 12);
                align = le16(d + at + 20);
                bits = le16(d + at + 22);
            } else if (memcmp(d + at, "fact", 4) == 0 && len >= 4) {
                fact = le32(d + at + 8);
            } else if (memcmp(d + at, "data", 4) == 0) {
                data = d + at + 8;
                bytes = len;
            }
            at += 8 + (long)len + (len & 1);
        }
    }
    /* the file's frames, 16 bit, interleaved */
    int16_t *src = NULL;
    uint32_t frames = 0;
    int pcm_ok = (format == 1 || format == 0xfffe) && (bits == 8 || bits == 16);
    int ima_ok = format == 0x11 && bits == 4 && align > 4 * channels;
    if (data && (channels == 1 || channels == 2) && hz >= 4000 && hz <= 192000 && (pcm_ok || ima_ok)) {
        if (pcm_ok) {
            unsigned step = channels * bits / 8;
            frames = bytes / step;
            src = frames ? (int16_t *)malloc((size_t)frames * channels * sizeof *src) : NULL;
            for (uint32_t i = 0; src && i < frames * channels; i++)
                src[i] = bits == 8 ? (int16_t)(((int)data[i] - 128) * 256) : (int16_t)le16(data + 2 * i);
        } else {
            unsigned per_block = (align - 4 * channels) * 2 / channels + 1;
            uint32_t most = (bytes + align - 1) / align * per_block;     /* (whole blocks) */
            /* (the frames in the last block, by "fact" where it fits: libsndfile
             * writes half of them there for a stereo file) */
            frames = fact && fact <= most && fact + per_block > most ? fact : most;
            src = frames ? (int16_t *)malloc((size_t)frames * channels * sizeof *src) : NULL;
            if (src) frames = ima_decode(data, bytes, channels, align, frames, src);
        }
    }
    if (src && frames) {
        unsigned och = channels == 2 && !mono ? 2 : 1;
        uint32_t out = (uint32_t)((uint64_t)frames * (unsigned)rate / hz);
        int16_t *p = out ? (int16_t *)malloc((size_t)out * och * sizeof *p) : NULL;
        if (p) {
            for (uint32_t i = 0; i < out; i++) {
                /* between the two samples it falls between */
                double at = (double)i * hz / rate;
                uint32_t i0 = (uint32_t)at, i1 = i0 + 1 < frames ? i0 + 1 : i0;
                double k = at - (double)i0;
                for (unsigned oc = 0; oc < och; oc++) {
                    double v[2];
                    uint32_t idx[2] = { i0, i1 };
                    for (int q = 0; q < 2; q++) {
                        if (och == 2) {
                            v[q] = src[(size_t)idx[q] * channels + oc];
                        } else {
                            double sum = 0;
                            for (unsigned c = 0; c < channels; c++) sum += src[(size_t)idx[q] * channels + c];
                            v[q] = sum / channels;
                        }
                    }
                    p[(size_t)i * och + oc] = (int16_t)(v[0] + (v[1] - v[0]) * k);
                }
            }
            *pcm = p;
            *n = out;
            *ch_out = (uint8_t)och;
            ok = 0;
        }
    }
    free(src);
    free(d);
    return ok;
}

/* NAME.wav, NAME_2.wav ... in one folder: into rec[] (count of them at
 * most); returns how many. */
static int load_set(const char *folder, const char *name, int rate, sw_sample *rec, int most, int mono)
{
    int got = 0;
    for (int k = 1; k <= most && got < most; k++) {
        char path[1024];
        if (k == 1) snprintf(path, sizeof path, "%s/%s.wav", folder, name);
        else snprintf(path, sizeof path, "%s/%s_%d.wav", folder, name, k);
        if (load_wav(path, rate, &rec[got].pcm, &rec[got].n, &rec[got].ch, mono) == 0) got++;
    }
    return got;
}

int sw_samples_load(sw_samples *s, const char *dir, int rate)
{
    int found = 0;
    char sub[1024];
    for (int i = 1; i < SW_SOUND_REQUESTS; i++) {
        if (s->sound[i].count || !NAMES[i][0]) continue;    /* (already there, from a folder asked before) */
        /* in the folder itself, or in its folder for that kind of sound */
        int got = load_set(dir, NAMES[i], rate, s->sound[i].var, SW_SAMPLE_VARIANTS, 0);
        if (!got) {
            snprintf(sub, sizeof sub, "%s/%s", dir, sw_sound_folder(i));
            got = load_set(sub, NAMES[i], rate, s->sound[i].var, SW_SAMPLE_VARIANTS, 0);
        }
        s->sound[i].count = got;
        s->sound[i].last = -1;
        if (got) found++;
    }
    if (!s->falcon.count) {
        snprintf(sub, sizeof sub, "%s/effects", dir);
        int got = load_set(dir, FALCON, rate, s->falcon.var, SW_SAMPLE_VARIANTS, 0);
        if (!got) got = load_set(sub, FALCON, rate, s->falcon.var, SW_SAMPLE_VARIANTS, 0);
        s->falcon.count = got;
        s->falcon.last = -1;
        if (got) found++;
    }
    if (!s->ground_blast.count) {
        snprintf(sub, sizeof sub, "%s/effects", dir);
        int got = load_set(dir, GROUND_BLAST, rate, s->ground_blast.var, SW_SAMPLE_VARIANTS, 0);
        if (!got) got = load_set(sub, GROUND_BLAST, rate, s->ground_blast.var, SW_SAMPLE_VARIANTS, 0);
        s->ground_blast.count = got;
        s->ground_blast.last = -1;
        if (got) found++;
    }
    for (int i = 0; i < SW_ENGINE_LOOPS; i++) {
        if (s->engine[i].rec.pcm) continue;
        int got = load_set(dir, ENGINE[i], rate, &s->engine[i].rec, 1, 1);
        if (!got) {
            snprintf(sub, sizeof sub, "%s/effects", dir);
            got = load_set(sub, ENGINE[i], rate, &s->engine[i].rec, 1, 1);
        }
        if (got) found++;
    }
    if (!s->engine_rate) s->engine_rate = 1.0f;
    if (!s->duck_speech) s->duck_speech = s->duck_big = 1.0f;
    if (s->vol[0] == 0.0f && s->vol[1] == 0.0f) s->vol[0] = s->vol[1] = s->vol[2] = s->vol[3] = 1.0f;
    s->rate = rate;
    if (!s->rng) s->rng = 0x9e3779b9u;
    return found;
}

void sw_samples_free(sw_samples *s)
{
    for (int i = 0; i < SW_SOUND_REQUESTS; i++)
        for (int k = 0; k < s->sound[i].count; k++) free(s->sound[i].var[k].pcm);
    for (int k = 0; k < s->falcon.count; k++) free(s->falcon.var[k].pcm);
    for (int k = 0; k < s->ground_blast.count; k++) free(s->ground_blast.var[k].pcm);
    for (int i = 0; i < SW_ENGINE_LOOPS; i++) free(s->engine[i].rec.pcm);
    memset(s, 0, sizeof *s);
}

/* --- playing --------------------------------------------------------------------------- */

static void stop_effect(sw_samples *s, int id)
{
    for (int i = 0; i < SW_SAMPLE_VOICES; i++)
        if (s->effect[i].id == id) s->effect[i].id = 0;
}

/* One of a set's recordings, at random, not the one before. */
static const sw_sample *pick_of(sw_samples *s, sw_sample_set *set)
{
    int c = set->count, k = 0;
    if (c > 1) {
        s->rng = s->rng * 1664525u + 1013904223u;
        k = (int)((s->rng >> 8) % (uint32_t)(c - (set->last >= 0 ? 1 : 0)));
        if (set->last >= 0 && k >= set->last) k++;
    }
    set->last = k;
    return &set->var[k];
}

static const sw_sample *pick(sw_samples *s, int n) { return pick_of(s, &s->sound[n]); }

/* The music playing is let go: it dies away (sw_samples_next2) rather than
 * stopping dead. */
static void music_release(sw_samples *s)
{
    if (s->music.id) {
        s->tail.rec = s->music.rec;
        s->tail.at = s->music.at;
        s->tail.gain = 1.0f;
    }
    s->music.id = 0;
}

void sw_samples_cue(sw_samples *s, int n, double delay)
{
    sw_sample_set *set = n == SW_CUE_FALCON ? &s->falcon : n > 0 && n < SW_SOUND_REQUESTS ? &s->sound[n] : NULL;
    if (!s->enabled || !set || !set->count) return;
    int v = 0;
    for (int i = 0; i < SW_CUES; i++)
        if (!s->cue[i].rec) { v = i; break; }
    const sw_sample *r = pick_of(s, set);
    double k = (double)(s->rate > 0 ? s->rate : 44100);
    s->cue[v].rec = r;
    s->cue[v].wait = delay > 0.0 ? (uint32_t)(delay * k) : 0;
    s->cue[v].at = delay < 0.0 ? (uint32_t)(-delay * k) : 0;
    s->cue[v].speech = n >= FIRST_SPEECH && n <= LAST_SPEECH;
    if (s->cue[v].at >= r->n) s->cue[v].rec = NULL;
}

void sw_samples_hush(sw_samples *s)
{
    sw_samples_request(s, 0);
    s->tail.rec = NULL;
    for (int i = 0; i < SW_ENGINE_LOOPS; i++) s->engine[i].gain = s->engine[i].want = 0.0f;
}

int sw_samples_request(sw_samples *s, int n)
{
    if (n == 0) {                           /* the board's reset */
        for (int i = 0; i < SW_SAMPLE_VOICES; i++) s->effect[i].id = 0;
        music_release(s);
        for (int i = 0; i < SW_CUES; i++) s->cue[i].rec = NULL;
        s->music.id = s->speech.id = 0;
        s->queued = 0;
        return 0;
    }
    if (n < 0 || n >= SW_SOUND_REQUESTS) return 0;
    /* what ends something does so whether it is replaced itself or not */
    if (n == AUDPBZ) stop_effect(s, AUDPB);
    if (n == AUDTCZ) stop_effect(s, AUDTC);
    if (n == SPKSKP) s->queued = 0;
    if (n == SPKYAU && s->hold_yahoo && s->enabled && s->sound[n].count) return 1;   /* (it comes with the Falcon) */
    if (n >= FIRST_MUSIC && n <= LAST_MUSIC) music_release(s);
    /* the small explosion over the ground: the plain blast when there is one */
    sw_sample_set *set = n == AUDSX && s->on_ground && s->ground_blast.count ? &s->ground_blast : &s->sound[n];
    if (!s->enabled || !set->count) return 0;

    s->clock++;
    if (n >= FIRST_MUSIC && n <= LAST_MUSIC) {
        s->music.id = n;
        s->music.rec = pick(s, n);
        s->music.at = 0;
        return 2;
    }
    if (n >= FIRST_SPEECH && n <= LAST_SPEECH) {
        if (!s->speech.id) {
            s->speech.id = n;
            s->speech.rec = pick(s, n);
            s->speech.at = 0;
        } else if (s->queued < SW_SAMPLE_QUEUE) {
            s->queue[s->queued++] = (uint8_t)n;
        }
        return 1;
    }
    /* an effect: a free voice, or the oldest */
    int v = 0;
    for (int i = 0; i < SW_SAMPLE_VOICES; i++) {
        if (s->effect[i].id == 0) { v = i; break; }
        if (s->effect[i].started < s->effect[v].started) v = i;
    }
    s->effect[v].id = n;
    s->effect[v].rec = pick_of(s, set);
    s->effect[v].at = 0;
    s->effect[v].started = s->clock;
    return 1;
}

void sw_samples_ground(sw_samples *s, int on) { s->on_ground = on != 0; }

void sw_samples_engine(sw_samples *s, int which, float level, float rate)
{
    for (int i = 0; i < SW_ENGINE_LOOPS; i++) s->engine[i].want = i == which ? level : 0.0f;
    s->engine_rate = rate > 0.25f ? rate : 0.25f;
}

/* One frame of a recording playing, its two sides (a mono one in both);
 * when it has ended the voice is freed. */
static void play(int *id, const sw_sample *rec, uint32_t *at, float *l, float *r)
{
    if (rec->ch == 2) {
        *l = rec->pcm[(size_t)*at * 2];
        *r = rec->pcm[(size_t)*at * 2 + 1];
    } else {
        *l = *r = rec->pcm[*at];
    }
    if (++*at >= rec->n) *id = 0;
}

/* eases a ducking gain to where it is to be */
static float ease_duck(float g, float to, int rate)
{
    float k = 1.0f / ((g > to ? DUCK_ATTACK : DUCK_RELEASE) * (float)(rate > 0 ? rate : 44100));
    return g + (to - g) * (k > 1.0f ? 1.0f : k);
}

void sw_samples_volumes(sw_samples *s, float music, float effects, float speech, float engine)
{
    s->vol[0] = music;
    s->vol[1] = effects;
    s->vol[2] = speech;
    s->vol[3] = engine;
}

static float vol_of(const sw_samples *s, int k) { return s->vol[0] == 0.0f && s->vol[1] == 0.0f ? 1.0f : s->vol[k]; }

float sw_samples_board_gain(const sw_samples *s)
{
    const float v = vol_of(s, 1);
    if (!s->enabled || !s->duck_speech) return v;
    return s->duck_speech * s->duck_big * v;
}

float sw_samples_board_speech_gain(const sw_samples *s) { return vol_of(s, 2); }

int sw_samples_next(sw_samples *s)
{
    int l, r;
    sw_samples_next2(s, &l, &r);
    return (l + r) / 2;
}

void sw_samples_next2(sw_samples *s, int *left, int *right)
{
    /* the ducking: under speech everything else, under a big explosion the rest */
    if (s->board_speaks) s->board_hold = (uint32_t)(BOARD_HOLD * (float)(s->rate > 0 ? s->rate : 44100));
    else if (s->board_hold) s->board_hold--;
    int big = 0;
    for (int i = 0; i < SW_SAMPLE_VOICES; i++) big |= is_big(s->effect[i].id);
    int speaking = s->speech.id != 0 || s->board_hold > 0;
    for (int i = 0; i < SW_CUES; i++) speaking |= s->cue[i].rec && s->cue[i].speech && !s->cue[i].wait;
    if (!s->duck_speech) s->duck_speech = s->duck_big = 1.0f;
    s->duck_speech = ease_duck(s->duck_speech, s->enabled && speaking ? DUCK_SPEECH : 1.0f, s->rate);
    s->duck_big = ease_duck(s->duck_big, s->enabled && big ? DUCK_BIG : 1.0f, s->rate);
    float rest = s->duck_speech * s->duck_big, bigs = s->duck_speech;

    /* each side: as it is (speech), ducked by both (under), by speech only (over) */
    const float vm = vol_of(s, 0), ve = vol_of(s, 1), vs = vol_of(s, 2), vg = vol_of(s, 3);   /* the volumes */
    float sum[2] = { 0.0f, 0.0f }, under[2] = { 0.0f, 0.0f }, over[2] = { 0.0f, 0.0f }, l, r;
    for (int i = 0; i < SW_SAMPLE_VOICES; i++) {
        if (!s->effect[i].id) continue;
        int b = is_big(s->effect[i].id);
        play(&s->effect[i].id, s->effect[i].rec, &s->effect[i].at, &l, &r);
        float *to = b ? over : under;
        to[0] += l * ve;
        to[1] += r * ve;
    }
    if (s->music.id) {
        play(&s->music.id, s->music.rec, &s->music.at, &l, &r);
        under[0] += l * vm;
        under[1] += r * vm;
    }
    if (s->tail.rec) {                      /* the piece let go, dying away */
        if (s->tail_k <= 0.0f) s->tail_k = expf(-4.6f / (MUSIC_TAIL * (float)(s->rate > 0 ? s->rate : 44100)));
        int live = 1;
        play(&live, s->tail.rec, &s->tail.at, &l, &r);
        under[0] += l * vm * s->tail.gain;
        under[1] += r * vm * s->tail.gain;
        s->tail.gain *= s->tail_k;
        if (!live || s->tail.gain < 0.01f) s->tail.rec = NULL;
    }
    /* the cues: speech over everything, the Falcon under speech only */
    for (int i = 0; i < SW_CUES; i++) {
        if (!s->cue[i].rec) continue;
        if (s->cue[i].wait) { s->cue[i].wait--; continue; }
        int live = 1;
        play(&live, s->cue[i].rec, &s->cue[i].at, &l, &r);
        if (!live) s->cue[i].rec = NULL;
        float *to = s->cue[i].speech ? sum : over;
        const float v = s->cue[i].speech ? vs : ve;
        to[0] += l * v;
        to[1] += r * v;
    }
    if (s->speech.id) {
        play(&s->speech.id, s->speech.rec, &s->speech.at, &l, &r);
        sum[0] += l * vs;
        sum[1] += r * vs;
        if (!s->speech.id && s->queued) {   /* the next sentence waiting */
            s->speech.id = s->queue[0];
            s->speech.rec = pick(s, s->speech.id);
            s->speech.at = 0;
            memmove(s->queue, s->queue + 1, (size_t)--s->queued);
        }
    }
    /* the engine: each loop eases to how loud it is wanted (a quarter of a
     * second from silence to full), played at its rate, round and round */
    for (int i = 0; i < SW_ENGINE_LOOPS; i++) {
        float g = s->engine[i].gain, w = s->enabled ? s->engine[i].want : 0.0f;
        if (g != w) {
            const float STEP = 1.0f / 11025.0f;
            g = g < w ? (g + STEP > w ? w : g + STEP) : (g - STEP < w ? w : g - STEP);
            s->engine[i].gain = g;
        }
        const sw_sample *r = &s->engine[i].rec;
        if (g <= 0.0f || !r->pcm || r->n < 2) continue;
        uint32_t i0 = (uint32_t)s->engine[i].at, i1 = i0 + 1 < r->n ? i0 + 1 : 0;
        float k = (float)(s->engine[i].at - i0);
        float v = (r->pcm[i0] + (r->pcm[i1] - r->pcm[i0]) * k) * g * vg;   /* (vg: the engine's volume) */
        under[0] += v;
        under[1] += v;
        s->engine[i].at += s->engine_rate;
        while (s->engine[i].at >= r->n) s->engine[i].at -= r->n;
    }
    *left = (int)(sum[0] + under[0] * rest + over[0] * bigs);
    *right = (int)(sum[1] + under[1] * rest + over[1] * bigs);
}
