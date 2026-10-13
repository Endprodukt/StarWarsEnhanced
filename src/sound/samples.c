/* samples.c - see samples.h. */
#include "sound/samples.h"

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

enum {
    FIRST_SPEECH = 3, SPKSKP = 20, LAST_SPEECH = 26,
    FIRST_MUSIC = 27, LAST_MUSIC = 37,
    AUDNAY = 41, AUDPB = 42, AUDPBZ = 43, AUDTC = 54, AUDTCZ = 55, AUDYEA = 59,
};

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

/* The file as mono samples at `rate`. Returns 0, or -1 if it is not a PCM
 * WAV file of 8 or 16 bit. */
static int load_wav(const char *path, int rate, int16_t **pcm, uint32_t *n)
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
    unsigned channels = 0, bits = 0;
    uint32_t hz = 0;
    const uint8_t *data = NULL;
    uint32_t bytes = 0;
    if (memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WAVE", 4) == 0) {
        for (long at = 12; at + 8 <= size; ) {
            uint32_t len = le32(d + at + 4);
            if ((long)len > size - at - 8) len = (uint32_t)(size - at - 8);
            if (memcmp(d + at, "fmt ", 4) == 0 && len >= 16) {
                unsigned format = le16(d + at + 8);
                channels = le16(d + at + 10);
                hz = le32(d + at + 12);
                bits = le16(d + at + 22);
                if (format != 1 && format != 0xfffe) channels = 0;     /* PCM only */
            } else if (memcmp(d + at, "data", 4) == 0) {
                data = d + at + 8;
                bytes = len;
            }
            at += 8 + (long)len + (len & 1);
        }
    }
    if (data && (channels == 1 || channels == 2) && (bits == 8 || bits == 16) && hz >= 4000 && hz <= 192000) {
        unsigned step = channels * bits / 8;
        uint32_t frames = bytes / step;
        uint32_t out = (uint32_t)((uint64_t)frames * (unsigned)rate / hz);
        int16_t *p = out ? (int16_t *)malloc(out * sizeof *p) : NULL;
        if (p) {
            for (uint32_t i = 0; i < out; i++) {
                /* between the two samples it falls between */
                double at = (double)i * hz / rate;
                uint32_t i0 = (uint32_t)at, i1 = i0 + 1 < frames ? i0 + 1 : i0;
                double k = at - (double)i0, v[2];
                uint32_t idx[2] = { i0, i1 };
                for (int q = 0; q < 2; q++) {
                    const uint8_t *s = data + (size_t)idx[q] * step;
                    double sum = 0;
                    for (unsigned c = 0; c < channels; c++)
                        sum += bits == 8 ? ((int)s[c] - 128) * 256 : (int16_t)le16(s + 2 * c);
                    v[q] = sum / channels;
                }
                p[i] = (int16_t)(v[0] + (v[1] - v[0]) * k);
            }
            *pcm = p;
            *n = out;
            ok = 0;
        }
    }
    free(d);
    return ok;
}

int sw_samples_load(sw_samples *s, const char *dir, int rate)
{
    int found = 0;
    for (int i = 1; i < SW_SOUND_REQUESTS; i++) {
        char path[1024];
        if (s->sound[i].pcm) continue;                  /* (already there, from a folder asked before) */
        /* in the folder itself, or in its folder for that kind of sound */
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAMES[i]);
        if (load_wav(path, rate, &s->sound[i].pcm, &s->sound[i].n) == 0) { found++; continue; }
        snprintf(path, sizeof path, "%s/%s/%s.wav", dir, sw_sound_folder(i), NAMES[i]);
        if (load_wav(path, rate, &s->sound[i].pcm, &s->sound[i].n) == 0) found++;
    }
    return found;
}

void sw_samples_free(sw_samples *s)
{
    for (int i = 0; i < SW_SOUND_REQUESTS; i++) free(s->sound[i].pcm);
    memset(s, 0, sizeof *s);
}

/* --- playing --------------------------------------------------------------------------- */

static void stop_effect(sw_samples *s, int id)
{
    for (int i = 0; i < SW_SAMPLE_VOICES; i++)
        if (s->effect[i].id == id) s->effect[i].id = 0;
}

int sw_samples_request(sw_samples *s, int n)
{
    if (n == 0) {                           /* the board's reset */
        for (int i = 0; i < SW_SAMPLE_VOICES; i++) s->effect[i].id = 0;
        s->music.id = s->speech.id = 0;
        s->queued = 0;
        return 0;
    }
    if (n < 0 || n >= SW_SOUND_REQUESTS) return 0;
    /* what ends something does so whether it is replaced itself or not */
    if (n == AUDPBZ) stop_effect(s, AUDPB);
    if (n == AUDTCZ) stop_effect(s, AUDTC);
    if (n == SPKSKP) s->queued = 0;
    if (n >= FIRST_MUSIC && n <= LAST_MUSIC) s->music.id = 0;
    if (!s->enabled || !s->sound[n].pcm) return 0;

    s->clock++;
    if (n >= FIRST_MUSIC && n <= LAST_MUSIC) {
        s->music.id = n;
        s->music.at = 0;
        return 2;
    }
    if (n >= FIRST_SPEECH && n <= LAST_SPEECH) {
        if (!s->speech.id) {
            s->speech.id = n;
            s->speech.at = 0;
        } else if (s->queued < SW_SAMPLE_QUEUE) {
            s->queue[s->queued++] = (uint8_t)n;
        }
        return 1;
    }
    /* an effect: the voice that has it already, a free one, or the oldest */
    int v = 0;
    for (int i = 0; i < SW_SAMPLE_VOICES; i++) {
        if (s->effect[i].id == n) { v = i; break; }
        if (s->effect[v].id != 0 && (s->effect[i].id == 0 || s->effect[i].started < s->effect[v].started)) v = i;
    }
    s->effect[v].id = n;
    s->effect[v].at = 0;
    s->effect[v].started = s->clock;
    return 1;
}

int sw_samples_next(sw_samples *s)
{
    int sum = 0;
    for (int i = 0; i < SW_SAMPLE_VOICES; i++) {
        if (!s->effect[i].id) continue;
        sum += s->sound[s->effect[i].id].pcm[s->effect[i].at];
        if (++s->effect[i].at >= s->sound[s->effect[i].id].n) s->effect[i].id = 0;
    }
    if (s->music.id) {
        sum += s->sound[s->music.id].pcm[s->music.at];
        if (++s->music.at >= s->sound[s->music.id].n) s->music.id = 0;
    }
    if (s->speech.id) {
        sum += s->sound[s->speech.id].pcm[s->speech.at];
        if (++s->speech.at >= s->sound[s->speech.id].n) {
            s->speech.id = 0;
            if (s->queued) {                /* the next sentence waiting */
                s->speech.id = s->queue[0];
                s->speech.at = 0;
                memmove(s->queue, s->queue + 1, (size_t)--s->queued);
            }
        }
    }
    return sum;
}
