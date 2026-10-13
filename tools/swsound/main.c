/* swsound - the sounds of the board, each as a WAV file.
 *
 *   swsound [--roms DIR] [--out DIR] [--max SECONDS] [NAME ...]
 *
 * Runs the sound board (its program in C) on its own, asks it for a
 * sound and records what it plays until it is over: a piece of music
 * until the board's music driver is idle again, speech until the speech
 * chip has nothing left, an effect until the output has been quiet for a
 * while. Written as DIR/NAME.wav (44.1 kHz, 16 bit, mono), the files that
 * sounds/ takes in place of the board's own (docs/SOUNDS.md). Without
 * names: all eleven pieces of music.
 */
#include "core/machine.h"
#include "rom/romset.h"
#include "sound/samples.h"
#include "sound/sndboard.h"
#include "sound/sn_symbols.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sw_romset rom;
static sw_machine m;
static sw_sndboard snd;

static const char *arg(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

static void wav_header(FILE *f, uint32_t samples)
{
    uint32_t bytes = samples * 2, rate = SW_SOUND_RATE, u;
    uint16_t v;
    fwrite("RIFF", 1, 4, f); u = 36 + bytes; fwrite(&u, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u = 16; fwrite(&u, 4, 1, f);
    v = 1; fwrite(&v, 2, 1, f); v = 1; fwrite(&v, 2, 1, f);
    fwrite(&rate, 4, 1, f); u = rate * 2; fwrite(&u, 4, 1, f);
    v = 2; fwrite(&v, 2, 1, f); v = 16; fwrite(&v, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
}

/* Is the board still at it? music: its driver has voices in use; speech:
 * a sentence on or waiting; else: nothing to tell, quiet decides. */
static int busy(int n)
{
    if (n >= 0x1b && n <= 0x25) return sw_snd_rd(&snd, SN_VIU) != 0;
    if (n >= 0x03 && n <= 0x1a) return (sw_snd_rd(&snd, SN_SP_DLY) | sw_snd_rd(&snd, SN_SP_STA)) != 0;
    return 0;
}

static int record(int n, const char *path, double max_seconds)
{
    sw_sndboard_init(&snd, &m);
    snd.native = 1;
    sw_sndboard_reset(&snd);
    m.sound_board = 1;
    static int16_t pcm[4096];
    int16_t *out = NULL;
    uint32_t count = 0, cap = 0, quiet = 0, started = 0;
    const uint32_t QUIET = SW_SOUND_RATE / 2, TAIL = SW_SOUND_RATE / 4;
    /* let the board settle, then the request */
    for (int i = 0; i < 60; i++) {
        sw_sndboard_run(&snd, 6144);
        while (sw_sndboard_read(&snd, pcm, 4096) > 0) { }
    }
    m.sound_out = (uint8_t)n;
    m.sound_out_full = 1;
    uint32_t limit = (uint32_t)(max_seconds * SW_SOUND_RATE);
    int was_busy = 0;
    while (count < limit) {
        sw_sndboard_run(&snd, 6144);
        int now_busy = busy(n);
        if (now_busy) was_busy = 1;
        for (int got; (got = sw_sndboard_read(&snd, pcm, 4096)) > 0; ) {
            if (count + (uint32_t)got > cap) {
                cap = cap ? cap * 2 : 1 << 20;
                out = (int16_t *)realloc(out, cap * sizeof *out);
                if (!out) return -1;
            }
            for (int i = 0; i < got; i++) {
                out[count++] = pcm[i];
                if (abs(pcm[i]) > 40) { quiet = 0; started = 1; }
                else quiet++;
            }
        }
        /* over: the board says so and the sound has faded, or nothing has
         * sounded for a while */
        if (was_busy && !now_busy && quiet > TAIL) break;
        if (!was_busy && started && quiet > QUIET && count > SW_SOUND_RATE / 4) break;
        if (!was_busy && !started && count > 2 * SW_SOUND_RATE) break;  /* nothing at all */
    }
    /* cut the quiet tail to a quarter second */
    if (quiet > TAIL) count -= quiet - TAIL;
    FILE *f = fopen(path, "wb");
    if (!f) { free(out); return -1; }
    wav_header(f, count);
    fwrite(out, 2, count, f);
    fclose(f);
    printf("swsound: %s  %5.1f s%s\n", path, (double)count / SW_SOUND_RATE, count >= limit ? "  (cut at the limit)" : "");
    free(out);
    return 0;
}

int main(int argc, char **argv)
{
    const char *paths[32];
    int n = 0;
    if (arg(argc, argv, "--roms")) paths[n++] = arg(argc, argv, "--roms");
    n += sw_romset_default_paths(paths + n, 32 - n);
    if (sw_romset_load(&rom, paths, n) != 0) {
        fprintf(stderr, "swsound: no ROM set found\n");
        return 1;
    }
    const char *dir = arg(argc, argv, "--out") ? arg(argc, argv, "--out") : "sounds";
    double max_seconds = arg(argc, argv, "--max") ? atof(arg(argc, argv, "--max")) : 180.0;
    sw_machine_init(&m, &rom);

    int done = 0, fail = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { i++; continue; }
        int req = -1;
        for (int k = 1; k < SW_SOUND_REQUESTS; k++)
            if (!strcmp(sw_sound_name(k), argv[i])) req = k;
        if (req < 0) { fprintf(stderr, "swsound: no sound named %s\n", argv[i]); fail = 1; continue; }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.wav", dir, argv[i]);
        if (record(req, path, max_seconds) != 0) { fprintf(stderr, "swsound: cannot write %s\n", path); fail = 1; }
        done++;
    }
    if (!done && !fail) {
        for (int req = 0x1b; req <= 0x25; req++) {       /* the music */
            char path[1024];
            snprintf(path, sizeof path, "%s/%s.wav", dir, sw_sound_name(req));
            if (record(req, path, max_seconds) != 0) { fprintf(stderr, "swsound: cannot write %s\n", path); fail = 1; }
        }
    }
    return fail;
}
