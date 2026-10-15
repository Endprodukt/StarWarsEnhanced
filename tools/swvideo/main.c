/* swvideo - a film of the port, without the cabinet.
 *
 *   swvideo [--roms DIR] [--seconds N] [--attract N] [--fps N] [--size WxH] [--aim XX,YY]
 *           [--wire] [--every N] --wav FILE > frames.rgb
 *
 * --sounds DIR: recordings in place of sounds (DIR/NAME.wav, docs/SOUNDS.md).
 * --immortal: the shields never run out.
 * --check: no pictures; instead every picture's lines are looked at (a
 * coordinate that is not a number or off the screen, a line of a thing
 * whose distance is unknown) and the time the pictures took is told per
 * phase of the game.
 * --wire: wire frames as the arcade machine drew them (no occlusion).
 * --plain: without what the port adds over the surface (render/enhance.h).
 * --view N: in flight seen as the arcade machine (0), from the cockpit (1)
 *   or from behind the X-wing (2); --views-every S: each in turn, S seconds.
 * --demo: the attract mode's demo flight (platform/demo.h); with a long
 *   --attract, so that no coin of the script ends it.
 * --dogfight: the dogfight (dogfight/dogfight.h) from the start, flown by
 *   itself (the yoke weaving, the throttle up and down, a roll now and
 *   then), with sound; --wave N begins it at the wave N.
 * --aim 80,4c: the first game starts on the Death Star in the middle (wave
 * 3, which has the surface).
 * --every N: only every Nth picture is written (stills to look at).
 *
 * Runs the game as swenhanced does (the programs in C, steady frames, the
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
#include "dogfight/dogfight.h"
#include "platform/demo.h"
#include "render/picture.h"
#include "rom/romset.h"
#include "sound/sndboard.h"

#include <math.h>
#include <stdio.h>
#include <time.h>
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

static int arg_flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], name)) return 1;
    return 0;
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
    int immortal = arg_flag(argc, argv, "--immortal");
    int check = arg_flag(argc, argv, "--check");
    struct { double secs; long pictures, lines, bad_num, off_screen, no_depth, cut_tiny; } stat[64];
    static long off_by[32], depth_by[32];       /* by the kind of line (core/tags.h) */
    memset(stat, 0, sizeof stat);
    double from = arg(argc, argv, "--from") ? atof(arg(argc, argv, "--from")) : 0.0;   /* pictures from this second on */
    long every = arg(argc, argv, "--every") ? atol(arg(argc, argv, "--every")) : 1;
    if (every < 1) every = 1;
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

    /* the machine, as swenhanced sets it up */
    sw_machine_init(&m, &rom);
    m.yoke_exact = 1;
    m.steady = 1;
    sw_sndboard_init(&snd, &m);
    snd.native = 1;
    static sw_samples replaced_sounds;
    if (arg(argc, argv, "--sounds")) {
        int replaced = sw_samples_load(&replaced_sounds, arg(argc, argv, "--sounds"), SW_SOUND_RATE);
        fprintf(stderr, "swvideo: %d sound(s) replaced by recordings\n", replaced);
        replaced_sounds.enabled = 1;
        snd.samples = &replaced_sounds;
    }
    sw_sndboard_reset(&snd);
    sw_reset(&m);
    m.sound_board = 1;
    m.sound_wait = on_sound_wait;
    sw_picture_init(&pic, &m);
    pic.occlusion = !arg_flag(argc, argv, "--wire");
    pic.enhance.view = arg(argc, argv, "--view") ? atoi(arg(argc, argv, "--view")) % SW_VIEWS : SW_VIEW_ARCADE;
    double views_every = arg(argc, argv, "--views-every") ? atof(arg(argc, argv, "--views-every")) : 0.0;
    static sw_demo demo;
    demo.enabled = arg_flag(argc, argv, "--demo");
    int base_view = pic.enhance.view;
    static sw_dogfight df;
    int dogfight = arg_flag(argc, argv, "--dogfight");
    if (dogfight) sw_dogfight_start(&df, &m, arg(argc, argv, "--wave") ? atoi(arg(argc, argv, "--wave")) : 1);
    if (dogfight && arg_flag(argc, argv, "--howto")) sw_dogfight_howto(&df);     /* (the page of how it is flown first) */
    if (!arg_flag(argc, argv, "--plain")) {
        pic.enhance.surface = pic.enhance.stars = pic.enhance.towers = pic.enhance.dogfights = 1;
        pic.enhance.logo = 1;
        pic.enhance.clip = 1;
        pic.enhance.falcon = 1;
        pic.enhance.blast = 1;
        m.camera_ease = 1;
    }

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
        if (dogfight && df.preview) {
            sw_dogfight_input in0 = { 0 };
            sw_dogfight_step(&df, &in0, 1.0 / fps);
            const sw_line *dl;
            int dn = sw_dogfight_lines(&df, &m, &dl);
            uint32_t want = (uint32_t)((f + 1) * (double)SW_SOUND_RATE / fps);
            static const int16_t quiet[2048];
            while (samples < want) {
                uint32_t k = want - samples > 2048 ? 2048 : want - samples;
                fwrite(quiet, 2, k, wav);
                samples += k;
            }
            if (pic.occlusion) {
                pic.solid.extra = df.faces;
                pic.solid.n_extra = df.n_faces;
                dn = sw_solid_run(&pic.solid, dl, dn, &dl);
            }
            if (f % every != 0 || now < from) continue;
            if (every > 1) memset(after, 0, (size_t)W * H * 3 * sizeof *after);
            picture(dl, dn);
            if (fwrite(rgb, 3, (size_t)W * H, stdout) != (size_t)W * H) return 1;
            continue;
        }
        if (dogfight) {
            /* the dogfight's own pilot */
            double t = now;
            sw_dogfight_input in = { 0 };
            in.yaw = (float)(0.55 * sin(t * 0.37) + 0.25 * sin(t * 1.3));
            in.pitch = (float)(0.35 * sin(t * 0.23 + 1.0));
            in.faster = fmod(t, 14.0) > 4.0 && fmod(t, 14.0) < 8.0;
            in.slower = fmod(t, 14.0) > 10.0 && fmod(t, 14.0) < 12.0;
            if (fmod(t, 23.0) > 19.0 && fmod(t, 23.0) < 21.0) { in.faster = in.slower = 1; in.yaw = 0.8f; }
            /* after the nearest enemy, firing when it is before the nose */
            {
                double bd = 1e18, v[3] = { 0, 0, 0 };
                for (int k = 0; k < SW_DF_ENEMIES; k++) {
                    if (!df.enemy[k].kind) continue;
                    double a[3] = { df.enemy[k].b.pos[0] - df.pos[0], df.enemy[k].b.pos[1] - df.pos[1], df.enemy[k].b.pos[2] - df.pos[2] };
                    double dd = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
                    if (dd < bd) {
                        bd = dd;
                        v[0] = a[0] * df.f[0] + a[1] * df.f[1] + a[2] * df.f[2];
                        v[1] = a[0] * df.r[0] + a[1] * df.r[1] + a[2] * df.r[2];
                        v[2] = a[0] * df.u[0] + a[1] * df.u[1] + a[2] * df.u[2];
                    }
                }
                if (bd < 1e17) {
                    double ang_y = atan2(v[1], v[0]), ang_z = atan2(v[2], v[0]);
                    in.yaw = (float)(ang_y * 2.5 > 1.0 ? 1.0 : ang_y * 2.5 < -1.0 ? -1.0 : ang_y * 2.5);
                    in.pitch = (float)(ang_z * 2.5 > 1.0 ? 1.0 : ang_z * 2.5 < -1.0 ? -1.0 : ang_z * 2.5);
                    in.fire = v[0] > 0.0 && fabs(ang_y) < 0.06 && fabs(ang_z) < 0.06 && fmod(t, 0.5) < 0.25;
                    /* the torpedoes, charged, at what is ahead */
                    in.torpedo = df.torp_charge >= 1.0 && v[0] > 0.0 && fabs(ang_y) < 0.5 && fabs(ang_z) < 0.4 && fmod(t, 0.4) < 0.2;
                }
            }
            sw_dogfight_step(&df, &in, 1.0 / fps);
            /* --lose-at S: the ship is lost at S seconds (to film its end) */
            if (arg(argc, argv, "--lose-at") && !df.over && df.t >= atof(arg(argc, argv, "--lose-at"))) {
                df.shields = 0;
                df.over = 1;
                df.over_t = df.t;
            }
            /* its sounds on the sound board, as the program asks them */
            while ((double)irqs / SW_IRQ_HZ < now + 1.0 / fps) {
                if (!m.sound_out_full) {
                    int r = sw_dogfight_sound(&df);
                    if (r >= 0) { m.sound_out = (uint8_t)r; m.sound_out_full = 1; }
                }
                sw_sndboard_run(&snd, SND_CYCLES_PER_IRQ);
                irqs++;
                for (int got; (got = sw_sndboard_read(&snd, pcm, 4096)) > 0; ) {
                    fwrite(pcm, 2, (size_t)got, wav);
                    samples += (uint32_t)got;
                }
            }
            const sw_line *dl;
            int dn = sw_dogfight_lines(&df, &m, &dl);
            if (views_every > 0.0) pic.enhance.view = (int)(now / views_every) % SW_VIEWS;
            pic.enhance.dogfight = 1;
            sw_dogfight_attitude(&df, &pic.enhance.df_bank, &pic.enhance.df_pitch);
            pic.enhance.df_throttle = sw_dogfight_thrust(&df);
            pic.enhance.df_over = df.over;
            int count = sw_enhance_run(&pic.enhance, &m, now, dl, dn, &dl);
            /* the X-wing's engine, as swenhanced plays it */
            sw_samples_engine(&replaced_sounds, df.stage != SW_DF_PLAY || df.over ? -1 :
                              pic.enhance.view == SW_VIEW_OUTSIDE ? SW_ENGINE_OUTSIDE : SW_ENGINE_COCKPIT,
                              (pic.enhance.view == SW_VIEW_OUTSIDE ? 0.75f : 0.6f) * (0.8f + 0.2f * sw_dogfight_thrust(&df)),
                              1.0f + 0.15f * sw_dogfight_thrust(&df));
            if (pic.occlusion) {
                pic.solid.extra = df.faces;
                pic.solid.n_extra = df.n_faces;
                count = sw_solid_run(&pic.solid, dl, count, &dl);
            }
            if (f % every != 0 || now < from) continue;
            if (every > 1) memset(after, 0, (size_t)W * H * 3 * sizeof *after);
            picture(dl, count);
            if (fwrite(rgb, 3, (size_t)W * H, stdout) != (size_t)W * H) return 1;
            continue;
        }
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
            if (demo.enabled) {
                sw_demo_tick(&demo, &m);            /* (--demo: the attract mode's demo flight) */
                pic.enhance.view = demo.active ? demo.view : base_view;
            }
            irqs++;
            sw_picture_irq_begin(&pic, (double)irqs / SW_IRQ_HZ);
            /* --immortal: the shields never run out (to film what comes late in a game) */
            if (immortal && m.ram[SW_S_GAS - 0x4800] < 6) m.ram[SW_S_GAS - 0x4800] = 6;
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
        clock_t t0 = clock();
        if (views_every > 0.0) pic.enhance.view = (int)(now / views_every) % SW_VIEWS;   /* (--views-every: each view in turn) */
        int count = sw_picture_lines(&pic, now, 3, &lines);
        replaced_sounds.hold_yahoo = pic.enhance.falcon;
        if (pic.enhance.falcon_cue) {
            pic.enhance.falcon_cue = 0;
            sw_samples_cue(&replaced_sounds, SW_CUE_FALCON, pic.enhance.falcon_pass_in - SW_FALCON_PEAK);
            sw_samples_cue(&replaced_sounds, 0x1a, pic.enhance.falcon_pass_in);
        }
        sw_samples_engine(&replaced_sounds, !pic.enhance.plane_seen ? -1 :
                          pic.enhance.view == SW_VIEW_OUTSIDE ? SW_ENGINE_OUTSIDE : SW_ENGINE_COCKPIT,
                          pic.enhance.view == SW_VIEW_OUTSIDE ? 0.6f : 0.48f, 1.0f);
        if (m.ram[SW_PHASE - 0x4800] == SW_PHSSDS + 1) {
            /* the dogfight as a fourth choice of Death Star */
            static sw_line more[8192];
            static sw_dogfight choice;
            int k = count < 6000 ? count : 6000;
            memcpy(more, lines, (size_t)k * sizeof *more);
            sw_avg_list l = { more, k, 8192, 0, 0 };
            sw_dogfight_choice_lines(&choice, &m, &l);
            lines = more;
            count = l.count;
        }
        if (check) {
            unsigned ph = m.ram[SW_PHASE - 0x4800] >> 1;
            if (ph > 63) ph = 63;
            stat[ph].secs += (double)(clock() - t0) / CLOCKS_PER_SEC;
            stat[ph].pictures++;
            stat[ph].lines += count;
            if (getenv("SWDBG")) {
                int nb = 0;
                double lb = 0;
                for (int i = 0; i < count; i++)
                    if ((unsigned)(lines[i].id >> 58) == SW_TAG_BANNER) {
                        nb++;
                        lb += hypot(lines[i].x1 - lines[i].x0, lines[i].y1 - lines[i].y0);
                    }
                /* the topmost line of the logo */
                const sw_line *top = NULL;
                for (int i = 0; i < count; i++)
                    if ((unsigned)(lines[i].id >> 58) == SW_TAG_BANNER && (!top || lines[i].y0 + lines[i].y1 > top->y0 + top->y1))
                        top = &lines[i];
                /* and its short pieces (cut by the occlusion), and the lines right before the topmost */
                int tiny = 0, before = -1;
                for (int i = 0; i < count; i++) {
                    if ((unsigned)(lines[i].id >> 58) != SW_TAG_BANNER) continue;
                    if (hypotf(lines[i].x1 - lines[i].x0, lines[i].y1 - lines[i].y0) < 1.0f) tiny++;
                    if (&lines[i] == top) before = i;
                }
                fprintf(stderr, "PIC %.4f %02X %d banner %d %.2f top %.3f %.3f %.3f %.3f zz %u lum %u tiny %d prev %.2f %.2f\n",
                        now, m.ram[SW_PHASE - 0x4800], count, nb, lb, top ? top->x0 : 0.0f, top ? top->y0 : 0.0f,
                        top ? top->x1 : 0.0f, top ? top->y1 : 0.0f, top ? top->zz : 0u, top ? top->intensity : 0u, tiny,
                        before > 0 ? lines[before - 1].x1 : 0.0f, before > 0 ? lines[before - 1].y1 : 0.0f);
            }
            for (int i = 0; i < count; i++) {
                const sw_line *l = &lines[i];
                unsigned sec = (unsigned)(l->id >> 58);
                if (!(l->x0 == l->x0) || !(l->y0 == l->y0) || !(l->x1 == l->x1) || !(l->y1 == l->y1) ||
                    fabsf(l->x0) > 1e5f || fabsf(l->x1) > 1e5f || fabsf(l->y0) > 1e5f || fabsf(l->y1) > 1e5f) {
                    stat[ph].bad_num++;
                    continue;
                }
                if ((l->x0 < -129.0f && l->x1 < -129.0f) || (l->x0 > 129.0f && l->x1 > 129.0f) ||
                    (l->y0 < -155.0f && l->y1 < -155.0f) || (l->y0 > 152.0f && l->y1 > 152.0f)) {
                    stat[ph].off_screen++;
                    off_by[sec & 31]++;
                }
                if ((sec == SW_TAG_OBJECT || sec == SW_TAG_PANEL || sec == SW_TAG_TRENCH || sec == SW_TAG_SHOT) &&
                    (l->w0 == 0 || l->w1 == 0)) {
                    stat[ph].no_depth++;
                    depth_by[sec & 31]++;
                }
                if (fabsf(l->x1 - l->x0) + fabsf(l->y1 - l->y0) < 0.05f && l->zz == 7 && sec != SW_TAG_STAR && sec != SW_TAG_STARSEC)
                    stat[ph].cut_tiny++;
            }
            continue;
        }
        if (f % every != 0 || now < from) continue;
        if (every > 1) memset(after, 0, (size_t)W * H * 3 * sizeof *after);    /* (stills: no afterglow of the last one) */
        picture(lines, count);
        if (fwrite(rgb, 3, (size_t)W * H, stdout) != (size_t)W * H) return 1;
        if (f % 600 == 0)
            fprintf(stderr, "swvideo: %.0f s of %.0f, phase %02X, wave %d, shields %d, %d lines\n", now, seconds,
                    m.ram[SW_PHASE - 0x4800], m.ram[SW_GM_WAV - 0x4800] + 1, (int8_t)m.ram[SW_S_GAS - 0x4800], count);
    }
    fprintf(stderr, "swvideo: %u requests to the sound board, %u of them resets\n", (unsigned)m.snd_w, resets);
    if (check) {
        fprintf(stderr, "phase  pictures  ms/picture  lines  bad-number  off-screen  no-depth  tiny\n");
        for (int i = 0; i < 64; i++)
            if (stat[i].pictures)
                fprintf(stderr, "  %02X  %8ld  %10.2f  %5ld  %10ld  %10ld  %8ld  %4ld\n", i << 1, stat[i].pictures,
                        stat[i].secs * 1000.0 / stat[i].pictures, stat[i].lines / stat[i].pictures, stat[i].bad_num,
                        stat[i].off_screen, stat[i].no_depth, stat[i].cut_tiny);
        fprintf(stderr, "off the screen by kind:");
        for (int i = 0; i < 32; i++) if (off_by[i]) fprintf(stderr, " %d:%ld", i, off_by[i]);
        fprintf(stderr, "\nwithout depth by kind:");
        for (int i = 0; i < 32; i++) if (depth_by[i]) fprintf(stderr, " %d:%ld", i, depth_by[i]);
        fprintf(stderr, "\n");
    }
    fseek(wav, 0, SEEK_SET);
    wav_header(wav, samples);
    fclose(wav);
    fprintf(stderr, "swvideo: %ld pictures of %dx%d at %.0f a second, %.1f s of sound; at the end phase %02X, wave %d\n",
            frames, W, H, fps, (double)samples / SW_SOUND_RATE, m.ram[SW_PHASE - 0x4800],
            m.ram[SW_GM_WAV - 0x4800] + 1);
    return 0;
}
