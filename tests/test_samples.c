/* test_samples - sound/samples.c: a WAV file takes the place of the sound
 * named after it, and plays as long as it is; what ends a sound ends its
 * recording too. Needs no ROM set. */
#include "sound/samples.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sw_samples s;

/* A WAV file: stereo, 16 bit, 22050 Hz, `frames` of a square wave. */
static int write_wav(const char *path, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t bytes = frames * 4, u;
    uint16_t v;
    fwrite("RIFF", 1, 4, f); u = 36 + bytes; fwrite(&u, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u = 16; fwrite(&u, 4, 1, f);
    v = 1; fwrite(&v, 2, 1, f); v = 2; fwrite(&v, 2, 1, f);
    u = 22050; fwrite(&u, 4, 1, f); u = 22050 * 4; fwrite(&u, 4, 1, f);
    v = 4; fwrite(&v, 2, 1, f); v = 16; fwrite(&v, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t x = (i / 20) & 1 ? 8000 : 4000;        /* (never 0: every sample of it counts as sound) */
        fwrite(&x, 2, 1, f);
        fwrite(&x, 2, 1, f);
    }
    fclose(f);
    return 0;
}

/* The same wave as IMA ADPCM, mono, 22050 Hz, blocks of 256 bytes
 * (505 frames): the encoder as the format has it. */
static int write_ima(const char *path, uint32_t frames)
{
    static const int STEP[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
        4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767,
    };
    static const int INDEX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
    const unsigned ALIGN = 256, PER = (ALIGN - 4) * 2 + 1;
    uint32_t blocks = (frames + PER - 1) / PER, bytes = blocks * ALIGN, u;
    uint16_t v;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f); u = 4 + 28 + 12 + 8 + bytes; fwrite(&u, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u = 20; fwrite(&u, 4, 1, f);
    v = 0x11; fwrite(&v, 2, 1, f); v = 1; fwrite(&v, 2, 1, f);
    u = 22050; fwrite(&u, 4, 1, f); u = 22050 * ALIGN / PER; fwrite(&u, 4, 1, f);
    v = (uint16_t)ALIGN; fwrite(&v, 2, 1, f); v = 4; fwrite(&v, 2, 1, f);
    v = 2; fwrite(&v, 2, 1, f); v = (uint16_t)PER; fwrite(&v, 2, 1, f);
    fwrite("fact", 1, 4, f); u = 4; fwrite(&u, 4, 1, f); fwrite(&frames, 4, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t b = 0; b < blocks; b++) {
        uint8_t blk[256];
        memset(blk, 0, sizeof blk);
        uint32_t f0 = b * PER;
        int pred = (f0 / 20) & 1 ? 8000 : 4000, index = 0;
        blk[0] = (uint8_t)pred; blk[1] = (uint8_t)(pred >> 8); blk[2] = 0;
        for (unsigned k = 1; k < PER; k++) {
            uint32_t i = f0 + k;
            int x = i < frames ? ((i / 20) & 1 ? 8000 : 4000) : pred;
            int step = STEP[index], diff = x - pred, code = 0;
            if (diff < 0) { code = 8; diff = -diff; }
            int dq = step >> 3;
            if (diff >= step) { code |= 4; diff -= step; dq += step; }
            if (diff >= step >> 1) { code |= 2; diff -= step >> 1; dq += step >> 1; }
            if (diff >= step >> 2) { code |= 1; dq += step >> 2; }
            pred += code & 8 ? -dq : dq;
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            index += INDEX[code];
            index = index < 0 ? 0 : index > 88 ? 88 : index;
            unsigned at = 4 + (k - 1) / 2;
            blk[at] |= (uint8_t)((k - 1) & 1 ? code << 4 : code);
        }
        fwrite(blk, 1, ALIGN, f);
    }
    fclose(f);
    return 0;
}

static int loud(int samples)
{
    int n = 0;
    for (int i = 0; i < samples; i++) n += sw_samples_next(&s) != 0;
    return n;
}

int main(void)
{
    const char *dir = getenv("TMPDIR") ? getenv("TMPDIR") : ".";
    static const char *const names[] = { "AUDXL", "AUDPB", "PMTH5", "SPKRED", "SPKTHI" };
    char path[5][1024];
    int fail = 0;
    for (int i = 0; i < 5; i++) {
        snprintf(path[i], sizeof path[i], "%s/%s.wav", dir, names[i]);
        if (write_wav(path[i], 2205) != 0) { printf("test_samples: cannot write %s, skipped\n", path[i]); return 0; }
    }
    int found = sw_samples_load(&s, dir, 44100);
    for (int i = 0; i < 5; i++) remove(path[i]);
    if (found != 5) { printf("%d files found, 5 expected\n", found); fail = 1; }
    /* a stereo file is kept in stereo: both its sides come out */
    if (s.sound[0x24].count && s.sound[0x24].var[0].ch != 2) { printf("PMTH5 not loaded in stereo\n"); fail = 1; }
    if (strcmp(sw_sound_folder(0x24), "music") != 0 || strcmp(sw_sound_folder(0x10), "voice") != 0 ||
        strcmp(sw_sound_folder(0x3a), "effects") != 0 || strcmp(sw_sound_folder(0x29), "test") != 0) { printf("folders\n"); fail = 1; }
    if (strcmp(sw_sound_name(0x3a), "AUDXL") != 0 || strcmp(sw_sound_name(0x10), "SPKRED") != 0) { printf("names\n"); fail = 1; }

    /* switched off, nothing is replaced */
    if (sw_samples_request(&s, 0x3a) != 0) { printf("replaces while switched off\n"); fail = 1; }
    s.enabled = 1;

    /* an effect: 0.1 s at 22050 Hz is 4410 samples at 44100 Hz */
    if (sw_samples_request(&s, 0x3a) != 1) { printf("AUDXL not taken\n"); fail = 1; }
    if (sw_samples_request(&s, 0x39) != 0) { printf("AUDTL taken, though it has no file\n"); fail = 1; }
    int n = loud(6000);
    if (n != 4410) { printf("AUDXL sounded for %d samples, 4410 expected\n", n); fail = 1; }

    /* AUDPBZ ends AUDPB */
    sw_samples_request(&s, 0x2a);
    loud(100);
    if (sw_samples_request(&s, 0x2b) != 0) { printf("AUDPBZ taken\n"); fail = 1; }
    if (loud(1000) != 0) { printf("AUDPB goes on after AUDPBZ\n"); fail = 1; }

    /* music: taken, and the board's own is to stop; another piece ends it */
    if (sw_samples_request(&s, 0x24) != 2) { printf("PMTH5 not taken as music\n"); fail = 1; }
    loud(100);
    if (sw_samples_request(&s, 0x1d) != 0) { printf("PMDAR taken\n"); fail = 1; }
    /* (it dies away over MUSIC_TAIL seconds, a release, in place of stopping dead) */
    if (loud(1000) == 0) { printf("PMTH5 cut dead under the next piece, no release\n"); fail = 1; }
    loud(22050);
    if (loud(1000) != 0) { printf("PMTH5 still sounds a second after the next piece\n"); fail = 1; }

    /* speech: one sentence after the other */
    sw_samples_request(&s, 0x10);
    sw_samples_request(&s, 0x17);
    n = loud(12000);
    if (n != 8820) { printf("two sentences sounded for %d samples, 8820 expected\n", n); fail = 1; }
    /* SPKSKP drops the one waiting; the reset silences all */
    sw_samples_request(&s, 0x10);
    sw_samples_request(&s, 0x17);
    sw_samples_request(&s, 0x14);
    n = loud(12000);
    if (n != 4410) { printf("after SPKSKP %d samples, 4410 expected\n", n); fail = 1; }
    sw_samples_request(&s, 0x3a);
    sw_samples_request(&s, 0);
    if (loud(1000) != 0) { printf("sound after the reset\n"); fail = 1; }

    sw_samples_free(&s);

    /* several recordings for one request (AUDTC, AUDTC_2, AUDTC_3: of
     * different lengths), IMA ADPCM among them; each is played in turn at
     * random, never the same twice running */
    {
        char p1[1024], p2[1024], p3[1024], pe[1024];
        snprintf(p1, sizeof p1, "%s/AUDTC.wav", dir);
        snprintf(p2, sizeof p2, "%s/AUDTC_2.wav", dir);
        snprintf(p3, sizeof p3, "%s/AUDTC_3.wav", dir);
        snprintf(pe, sizeof pe, "%s/XWENG.wav", dir);
        if (write_wav(p1, 1000) != 0 || write_wav(p2, 2000) != 0 || write_ima(p3, 3000) != 0 || write_wav(pe, 500) != 0) {
            printf("test_samples: cannot write the variants, skipped\n");
        } else {
            found = sw_samples_load(&s, dir, 44100);
            remove(p1); remove(p2); remove(p3); remove(pe);
            if (found != 2 || s.sound[0x36].count != 3) { printf("variants: %d found, %d of AUDTC\n", found, s.sound[0x36].count); fail = 1; }
            /* the ADPCM one, decoded: 3000 frames at 22050 are 6000 at 44100, and as the wave was */
            int ok_ima = 0;
            for (int k = 0; k < s.sound[0x36].count; k++) {
                const sw_sample *r = &s.sound[0x36].var[k];
                if (r->n != 6000) continue;
                int bad = 0;
                for (uint32_t i = 200; i < r->n; i += 7) {
                    int want = ((i / 2) / 20) & 1 ? 8000 : 4000, got = r->pcm[i];
                    if ((i / 2) % 20 > 11 && (i / 2) % 20 < 19 && abs(got - want) > 600) bad++;     /* (once settled after an edge) */
                }
                ok_ima = bad == 0;
            }
            if (!ok_ima) { printf("the ADPCM recording is not as it was\n"); fail = 1; }
            s.enabled = 1;
            int prev = -1, seen[3] = { 0, 0, 0 }, twice = 0;
            for (int t = 0; t < 30; t++) {
                sw_samples_request(&s, 0x36);
                int len = loud(7000);
                int k = len == 2000 ? 0 : len == 4000 ? 1 : len == 6000 ? 2 : -1;
                if (k < 0) { printf("a variant sounded for %d samples\n", len); fail = 1; break; }
                seen[k] = 1;
                if (k == prev) twice = 1;
                prev = k;
            }
            if (!seen[0] || !seen[1] || !seen[2] || twice) { printf("variants not each, or twice running\n"); fail = 1; }
            /* two shots at once: the second does not cut the first */
            sw_samples_request(&s, 0x36);
            loud(100);
            sw_samples_request(&s, 0x36);
            if (loud(8000) < 2000) { printf("a second shot cut the first\n"); fail = 1; }
            /* the engine: on, it sounds for as long as it is wanted, round and round; off, it fades */
            sw_samples_engine(&s, SW_ENGINE_COCKPIT, 1.0f, 1.0f);
            if (loud(30000) < 25000) { printf("the engine does not go on\n"); fail = 1; }
            sw_samples_engine(&s, -1, 0.0f, 1.0f);
            loud(20000);
            if (loud(1000) != 0) { printf("the engine does not stop\n"); fail = 1; }
            sw_samples_free(&s);
        }
    }
    /* ducking: under speech the rest -3 dB, under a big explosion -1.5 dB, then back */
    {
        char ps[1024], px[1024];
        snprintf(ps, sizeof ps, "%s/SPKRED.wav", dir);
        snprintf(px, sizeof px, "%s/AUDSX.wav", dir);
        if (write_wav(ps, 4410) == 0 && write_wav(px, 4410) == 0) {
            found = sw_samples_load(&s, dir, 44100);
            remove(ps); remove(px);
            s.enabled = 1;
            float g0 = sw_samples_board_gain(&s);
            sw_samples_request(&s, 0x10);
            loud(6000);                                 /* (SPKRED is 8820 samples long) */
            float g1 = sw_samples_board_gain(&s);
            loud(40000);
            float g2 = sw_samples_board_gain(&s);
            sw_samples_request(&s, 0x35);
            loud(6000);
            float g3 = sw_samples_board_gain(&s);
            if (found != 2 || g0 < 0.99f || g1 > 0.72f || g1 < 0.69f || g2 < 0.97f || g3 > 0.86f || g3 < 0.80f) {
                printf("ducking: %d found, gains %.3f %.3f %.3f %.3f\n", found, g0, g1, g2, g3);
                fail = 1;
            }
            sw_samples_free(&s);
        }
    }
    /* cues: the Falcon's fly-by and "Yahoo" begin exactly when asked; while
     * the Falcon flies the game's own SPKYAU is taken and not played */
    {
        char pf[1024], py[1024];
        snprintf(pf, sizeof pf, "%s/FALCON.wav", dir);
        snprintf(py, sizeof py, "%s/SPKYAU.wav", dir);
        if (write_wav(pf, 2205) == 0 && write_wav(py, 2205) == 0) {
            found = sw_samples_load(&s, dir, 44100);
            remove(pf); remove(py);
            s.enabled = 1;
            s.hold_yahoo = 1;
            int taken = sw_samples_request(&s, 0x1a);
            int silent = loud(5000) == 0;
            sw_samples_cue(&s, SW_CUE_FALCON, 0.1);         /* 4410 samples from now */
            int first = -1;
            for (int i = 0; i < 9000 && first < 0; i++) if (sw_samples_next(&s) != 0) first = i;
            loud(10000);
            sw_samples_cue(&s, 0x1a, 0.05);                 /* 2205 */
            int second = -1;
            for (int i = 0; i < 9000 && second < 0; i++) if (sw_samples_next(&s) != 0) second = i;
            if (found != 2 || taken != 1 || !silent || first != 4410 || second != 2205) {
                printf("cues: %d found, taken %d, silent %d, the fly-by at %d (4410), Yahoo at %d (2205)\n",
                       found, taken, silent, first, second);
                fail = 1;
            }
            sw_samples_free(&s);
        }
    }
    printf("test_samples: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
