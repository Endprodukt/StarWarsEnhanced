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
    if (loud(1000) != 0) { printf("PMTH5 goes on under the next piece\n"); fail = 1; }

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
    printf("test_samples: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
