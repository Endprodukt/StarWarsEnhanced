/* swvideo - a film of the port, without the cabinet.
 *
 *   swvideo [--roms DIR] [--seconds N] [--attract N] [--fps N] [--size WxH] [--aim XX,YY]
 *           --wav FILE > frames.rgb
 *
 * Runs the game as starwarsvp does (the programs in C, steady frames, the
 * pictures in between computed), played by a script: N seconds of the
 * attract mode, then two coins, start, and the yoke weaving with the
 * trigger pulled. Every picture is drawn as a vector monitor shows it
 * (bright lines with a glow, a little afterglow) and written to stdout as
 * raw RGB24; what the sound board plays goes to the WAV file (44.1 kHz
 * mono). tools/swvideo/mux.py makes an MP4 of the two.
 */
#include "core/machine.h"
#include "core/sched.h"
#include "core/sw_symbols.h"
#include "game/game.h"
#include "render/picture.h"
#include "rom/romset.h"
#include "sound/sndboard.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define SND_CYCLES_PER_IRQ 6144     /* the sound board's 1.512 MHz clock, 246.09 interrupts a second */

/* The AVG window shown, as on the cabinet (render/config.c). */
#define AVG_LEFT   -127.5f
#define AVG_RIGHT   127.5f
#define AVG_BOTTOM -153.0f
#define AVG_TOP     150.0f

static sw_romset rom;
static sw_machine m;
static sw_sndboard snd;
static sw_picture pic;

static int W = 960, H = 720;
static float *light;        /* this picture's light, RGB */
static float *glow, *tmp;   /* a quarter size copy, blurred */
static float *after;        /* what still glows from the pictures before */
static uint8_t *rgb;

static const char *arg(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

/* A dot of light, spread over the four pixels around it. */
static void dot(float x, float y, const float c[3])
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    if (ix < 0 || iy < 0 || ix >= W - 1 || iy >= H - 1) return;
    float w[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    float *p = &light[3 * (iy * W + ix)];
    for (int k = 0; k < 3; k++) {
        p[k] += c[k] * w[0];
        p[3 + k] += c[k] * w[1];
        p[3 * W + k] += c[k] * w[2];
        p[3 * W + 3 + k] += c[k] * w[3];
    }
}

static void draw_line(const sw_line *l)
{
    if (!l->intensity) return;
    float k = powf((float)l->intensity / 255.0f, 0.8f);
    float c[3] = { (l->color & 4) ? k : 0, (l->color & 2) ? k : 0, (l->color & 1) ? k : 0 };
    float x0 = (l->x0 - AVG_LEFT) / (AVG_RIGHT - AVG_LEFT) * (float)W;
    float x1 = (l->x1 - AVG_LEFT) / (AVG_RIGHT - AVG_LEFT) * (float)W;
    float y0 = (AVG_TOP - l->y0) / (AVG_TOP - AVG_BOTTOM) * (float)H;
    float y1 = (AVG_TOP - l->y1) / (AVG_TOP - AVG_BOTTOM) * (float)H;
    float len = hypotf(x1 - x0, y1 - y0);
    if (len < 0.5f) {
        /* a dot: the beam rests there, so it is brighter */
        for (int k = 0; k < 3; k++) c[k] *= 2.5f;
        dot(x0, y0, c);
        dot(x0 + 0.5f, y0, c);
        dot(x0, y0 + 0.5f, c);
        return;
    }
    int n = (int)(len * 2.0f) + 1;
    for (int k = 0; k < 3; k++) c[k] *= 0.55f;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        dot(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, c);
    }
}

/* A box blur of the quarter size picture, a few times: the glow. */
static void blur(int w, int h)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int k = 0; k < 3; k++) {
                    float a = 0;
                    for (int d = -2; d <= 2; d++) {
                        int xx = x + d < 0 ? 0 : x + d >= w ? w - 1 : x + d;
                        a += glow[3 * (y * w + xx) + k];
                    }
                    tmp[3 * (y * w + x) + k] = a * 0.2f;
                }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int k = 0; k < 3; k++) {
                    float a = 0;
                    for (int d = -2; d <= 2; d++) {
                        int yy = y + d < 0 ? 0 : y + d >= h ? h - 1 : y + d;
                        a += tmp[3 * (yy * w + x) + k];
                    }
                    glow[3 * (y * w + x) + k] = a * 0.2f;
                }
    }
}

static void picture(const sw_line *lines, int n)
{
    memset(light, 0, (size_t)W * H * 3 * sizeof *light);
    for (int i = 0; i < n; i++) draw_line(&lines[i]);

    int w = W / 4, h = H / 4;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int k = 0; k < 3; k++) {
                float a = 0;
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 4; dx++) a += light[3 * ((y * 4 + dy) * W + x * 4 + dx) + k];
                glow[3 * (y * w + x) + k] = a / 16.0f;
            }
    blur(w, h);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = 0; k < 3; k++) {
                int i = 3 * (y * W + x) + k;
                float v = light[i] + 1.6f * glow[3 * ((y / 4) * w + x / 4) + k];
                after[i] = v + after[i] * 0.22f;        /* the phosphor's afterglow */
                v = 1.0f - expf(-1.8f * after[i]);
                rgb[i] = (uint8_t)(v * 255.0f + 0.5f);
            }
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

/* sw_machine.sound_wait: the game waits for the sound board to take a byte
 * (without it a second request in a frame resets the board). */
static void on_sound_wait(void *u, int cycles)
{
    (void)u;
    sw_sndboard_run(&snd, cycles);
}

int main(int argc, char **argv)
{
    const char *paths[32];
    int n = 0;
    if (arg(argc, argv, "--roms")) paths[n++] = arg(argc, argv, "--roms");
    n += sw_romset_default_paths(paths + n, 32 - n);
    if (sw_romset_load(&rom, paths, n) != 0) {
        fprintf(stderr, "swvideo: no ROM set found\n");
        return 1;
    }
    double seconds = arg(argc, argv, "--seconds") ? atof(arg(argc, argv, "--seconds")) : 120.0;
    double attract = arg(argc, argv, "--attract") ? atof(arg(argc, argv, "--attract")) : 45.0;
    double fps = arg(argc, argv, "--fps") ? atof(arg(argc, argv, "--fps")) : 60.0;
    if (arg(argc, argv, "--size")) sscanf(arg(argc, argv, "--size"), "%dx%d", &W, &H);
    /* where the yoke is held for the start: on the easy Death Star (top left) */
    unsigned aim_x = 0x08, aim_y = 0xb6;
    if (arg(argc, argv, "--aim")) sscanf(arg(argc, argv, "--aim"), "%x,%x", &aim_x, &aim_y);
    W &= ~3; H &= ~3;
    FILE *wav = arg(argc, argv, "--wav") ? fopen(arg(argc, argv, "--wav"), "wb") : NULL;
    if (!wav) {
        fprintf(stderr, "swvideo: --wav FILE is needed\n");
        return 1;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    light = (float *)calloc((size_t)W * H * 3, sizeof *light);
    after = (float *)calloc((size_t)W * H * 3, sizeof *after);
    glow = (float *)calloc((size_t)(W / 4) * (H / 4) * 3, sizeof *glow);
    tmp = (float *)calloc((size_t)(W / 4) * (H / 4) * 3, sizeof *tmp);
    rgb = (uint8_t *)malloc((size_t)W * H * 3);

    /* the machine, as starwarsvp sets it up */
    sw_machine_init(&m, &rom);
    m.yoke_exact = 1;
    m.steady = 1;
    sw_sndboard_init(&snd, &m);
    snd.native = 1;
    sw_sndboard_reset(&snd);
    sw_reset(&m);
    m.sound_board = 1;
    m.sound_wait = on_sound_wait;
    sw_picture_init(&pic, &m);

    wav_header(wav, 0);
    uint32_t samples = 0;
    static int16_t pcm[4096];
    uint64_t irqs = 0;
    uint32_t seen = 0;
    unsigned resets = 0;
    long frames = (long)(seconds * fps);
    long start = (long)(attract * SW_IRQ_HZ);      /* the interrupt the first coin drops at */
    for (long f = 0; f < frames; f++) {
        double now = (double)f / fps;
        while ((double)irqs / SW_IRQ_HZ < now) {
            long i = (long)irqs - start;
            uint8_t in0 = 0xff;
            if ((i >= 0 && i < 25) || (i >= 250 && i < 275)) in0 &= (uint8_t)~SW_IN0_COINL;
            if (i >= 600 && i < 640) in0 &= (uint8_t)~SW_IN0_LFIRE;     /* start */
            if (i >= 1050 && i < 1090) in0 &= (uint8_t)~SW_IN0_LFIRE;   /* and fire at the Death Star chosen */
            if (i > 1200) {
                /* the yoke weaving, the trigger pulled again and again */
                double t = (double)i / SW_IRQ_HZ;
                m.in.adc[1] = (uint8_t)(128.0 + 62.0 * sin(t * 1.7) + 18.0 * sin(t * 4.1));
                m.in.adc[0] = (uint8_t)(128.0 + 38.0 * sin(t * 1.1 + 1.0) + 12.0 * sin(t * 3.3));
                if (i % 70 < 22) in0 &= (uint8_t)~SW_IN0_LFIRE;
            } else if (i > 700) {
                /* the sight on the Death Star to start with */
                m.in.adc[1] = aim_x;
                m.in.adc[0] = aim_y;
            } else {
                m.in.adc[0] = m.in.adc[1] = 0x80;
            }
            if (i > 3000 && m.ram[SW_PHASE - 0x4800] == SW_PHSSDS + 1) {
                /* a second game: the Death Star in the middle (wave 3, with towers) */
                m.in.adc[1] = 0x80;
                m.in.adc[0] = 0x4c;
                in0 = (i % 60 < 20) ? (uint8_t)~SW_IN0_LFIRE : 0xff;
            }
            m.in.in0 = in0;
            irqs++;
            sw_picture_irq_begin(&pic, (double)irqs / SW_IRQ_HZ);
            sw_irq(&m);
            sw_main_poll(&m);
            for (; seen < m.snd_w; seen++) resets += m.snd_q[seen % sizeof m.snd_q] == 0;
            sw_sndboard_run(&snd, SND_CYCLES_PER_IRQ);
            sw_picture_irq_end(&pic);
            for (int got; (got = sw_sndboard_read(&snd, pcm, 4096)) > 0; ) {
                fwrite(pcm, 2, (size_t)got, wav);
                samples += (uint32_t)got;
            }
        }
        const sw_line *lines;
        int count = sw_picture_lines(&pic, now, 3, &lines);
        picture(lines, count);
        if (fwrite(rgb, 3, (size_t)W * H, stdout) != (size_t)W * H) return 1;
        if (f % 600 == 0)
            fprintf(stderr, "swvideo: %.0f s of %.0f, phase %02X, wave %d, shields %d, %d lines\n", now, seconds,
                    m.ram[SW_PHASE - 0x4800], m.ram[SW_GM_WAV - 0x4800] + 1, (int8_t)m.ram[SW_S_GAS - 0x4800], count);
    }
    fprintf(stderr, "swvideo: %u requests to the sound board, %u of them resets\n", (unsigned)m.snd_w, resets);
    fseek(wav, 0, SEEK_SET);
    wav_header(wav, samples);
    fclose(wav);
    fprintf(stderr, "swvideo: %ld pictures of %dx%d at %.0f a second, %.1f s of sound; at the end phase %02X, wave %d\n",
            frames, W, H, fps, (double)samples / SW_SOUND_RATE, m.ram[SW_PHASE - 0x4800],
            m.ram[SW_GM_WAV - 0x4800] + 1);
    return 0;
}
