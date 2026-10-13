/* starwarsvp - Star Wars (Atari, 1983), native, for VectorPie.
 *
 * At this stage: finds and checks the ROM set, runs the original's clocks
 * with the native interrupt (coins, switches, yoke and sight) and the
 * native main line as far as it is ported: the attract mode and the start
 * of a game up to the choice of a Death Star (docs/PLAN.md). --controls
 * shows the controls test, --calibrate the calibration screen for the
 * monitor.
 *
 *   starwarsvp [--calibrate | --controls] [--roms PATH] [--config INI] [--hz N]
 *              [--dvg [PORT] | --window | --windowed]
 */
#include "avg/avg.h"
#include "mbox/mbox.h"
#include "core/emu.h"
#include "core/machine.h"
#include "core/sched.h"
#include "game/game.h"
#include "platform/controls.h"
#include "platform/app.h"
#include "platform/audio.h"
#include "render/config.h"
#include "render/dvgmap.h"
#include "render/smooth.h"
#include "rom/romset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATHS 48

/* --- the ROM search list ------------------------------------------------- */

static int add_rom_dir(const char **paths, int n, char store[][1024], int *ns, const char *dir)
{
    static const char *const forms[] = { "%s/starwars1.zip", "%s/starwar1.zip", "%s/starwars.zip", "%s/starwars1", "%s" };
    for (int f = 0; f < 5 && n < MAX_PATHS && *ns < MAX_PATHS; f++) {
        snprintf(store[*ns], 1024, forms[f], dir);
        paths[n++] = store[(*ns)++];
    }
    return n;
}

static int rom_paths(int argc, char **argv, const char **paths)
{
    static char store[MAX_PATHS][1024];
    int n = 0, ns = 0;

    const char *arg = sw_arg(argc, argv, "--roms");
    if (arg) n = add_rom_dir(paths, n, store, &ns, arg);

    /* The cabinet's own ROM folders: AdvanceMAME's dir_rom. */
    char rcpath[1024];
    vp_rc *rc = NULL;
    if (vp_rc_default_path(rcpath, sizeof rcpath) == VP_OK && vp_rc_load(rcpath, &rc) == VP_OK) {
        const char *dirs = vp_rc_get(rc, "dir_rom");
        if (dirs) {
            char copy[2048];
            snprintf(copy, sizeof copy, "%s", dirs);
            for (char *p = strtok(copy, ":;"); p; p = strtok(NULL, ":;"))
                n = add_rom_dir(paths, n, store, &ns, p);
        }
        vp_rc_free(rc);
    }

    n += sw_romset_default_paths(paths + n, MAX_PATHS - n);
    return n;
}

/* --- the calibration screen ---------------------------------------------- */

typedef struct calib {
    sw_line lines[1024];
    int     count;
    int     mode;       /* 0 size, 1 position, 2 gamma/floor, 3 brightness */
} calib;

/* The test pictures are laid out in the units of the game's vector words
 * and drawn at the scale the game draws at (VGSCAL: a quarter). */
#define GAME_SCALE 0.25f

static void add(calib *c, float x0, float y0, float x1, float y1, unsigned color, unsigned zz, unsigned lum)
{
    if (c->count >= (int)(sizeof c->lines / sizeof c->lines[0])) return;
    sw_line *l = &c->lines[c->count++];
    l->x0 = x0 * GAME_SCALE; l->y0 = y0 * GAME_SCALE; l->x1 = x1 * GAME_SCALE; l->y1 = y1 * GAME_SCALE;
    l->color = (uint8_t)color;
    l->zz = (uint8_t)zz;
    l->intensity = sw_avg_intensity(zz, lum);
    l->defocus = 0;
}

/* Everything through the same mapping as the game. */
static void build_pattern(calib *c, const sw_display_cfg *cfg)
{
    c->count = 0;
    float l = cfg->avg_left / GAME_SCALE, r = cfg->avg_right / GAME_SCALE;
    float b = cfg->avg_bottom / GAME_SCALE, t = cfg->avg_top / GAME_SCALE;

    /* The game's screen edge, the clip limits (VGLIM*), the center cross. */
    add(c, l, b, r, b, 7, 7, 0xff); add(c, r, b, r, t, 7, 7, 0xff);
    add(c, r, t, l, t, 7, 7, 0xff); add(c, l, t, l, b, 7, 7, 0xff);
    float cl = -480, cr = 480, cb = -552, ct = 408;
    add(c, cl, cb, cr, cb, 3, 4, 0x80); add(c, cr, cb, cr, ct, 3, 4, 0x80);
    add(c, cr, ct, cl, ct, 3, 4, 0x80); add(c, cl, ct, cl, cb, 3, 4, 0x80);
    add(c, -40, 0, 40, 0, 7, 7, 0xff); add(c, 0, -40, 0, 40, 7, 7, 0xff);

    /* A circle: round on the tube when the aspect is right. The tube is 4:3
     * and the AVG window is not square, so the circle is drawn in tube
     * proportions. */
    float sx = (r - l) / 4.0f, sy = (t - b) / 3.0f;
    float rad = 0.8f;
    for (int i = 0; i < 48; i++) {
        float a0 = (float)i / 48.0f * 6.2831853f, a1 = (float)(i + 1) / 48.0f * 6.2831853f;
        add(c, cosf(a0) * rad * sx, sinf(a0) * rad * sy, cosf(a1) * rad * sx, sinf(a1) * rad * sy, 2, 6, 0x80);
    }

    /* Seven colors, each at ZZ 1..7 (normal COLOR intensity 80), left to
     * right: the gamma and floor settings show here. */
    float y = t - 60;
    for (unsigned col = 1; col <= 7; col++, y -= 50) {
        for (unsigned zz = 1; zz <= 7; zz++) {
            float x = l + 40 + (float)(zz - 1) * 60;
            add(c, x, y, x + 45, y, col, zz, 0x80);
            add(c, x, y - 12, x + 45, y - 12, col, zz, 0x80);
        }
    }
}

static void calib_adjust(calib *c, sw_display_cfg *cfg, vp_input *in)
{
    if (vp_input_pressed(in, "p1_button1")) c->mode = (c->mode + 1) % 4;
    int dx = vp_input_pressed(in, "p1_right") - vp_input_pressed(in, "p1_left");
    int dy = vp_input_pressed(in, "p1_up") - vp_input_pressed(in, "p1_down");
    if (!dx && !dy) return;
    switch (c->mode) {
    case 0:     /* size: the margins shrink or grow the picture */
        cfg->margin_left -= dx * 16; cfg->margin_right -= dx * 16;
        cfg->margin_bottom -= dy * 16; cfg->margin_top -= dy * 16;
        break;
    case 1:     /* position */
        cfg->margin_left += dx * 16; cfg->margin_right -= dx * 16;
        cfg->margin_bottom += dy * 16; cfg->margin_top -= dy * 16;
        break;
    case 2:
        cfg->gamma += (float)dx * 0.05f;
        cfg->floor += (float)dy * 0.01f;
        if (cfg->gamma < 0.2f) cfg->gamma = 0.2f;
        if (cfg->floor < 0.0f) cfg->floor = 0.0f;
        if (cfg->floor > 0.6f) cfg->floor = 0.6f;
        break;
    case 3:
        cfg->brightness += (float)(dx + dy) * 0.05f;
        if (cfg->brightness < 0.1f) cfg->brightness = 0.1f;
        if (cfg->brightness > 1.0f) cfg->brightness = 1.0f;
        break;
    }
}

static void draw_text_lines(vp_dvg *d, const char *const *text, int n, int y)
{
    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 160, 160, 160);
    for (int i = 0; i < n; i++, y -= 130)
        vp_text(d, text[i], 2048, y, 90, VP_TEXT_CENTER);
}

/* --- the running board ------------------------------------------------------ */

typedef struct board {
    sw_machine  m;
    sw_controls ctl;
    float       sight_prev[2], sight_cur[2];   /* at the last two VG restarts */
    int         fire_frames;                   /* show FIRE for a moment */
    int         controls_test;                 /* no main line: the test consumes the lasers */
    uint8_t     space[SW_AVG_SPACE_SIZE];      /* what the vector generator reads */
    sw_avg_list list;                          /* the picture of the last VG restart */
    uint32_t    vg_seen;
    uint8_t     vg_buffer;                     /* which buffer that picture was in */
    uint64_t    irqs;                          /* interrupts run: the game's clock */
    sw_smooth   smooth;
    sw_emu      emu;                           /* the original program on the machine */
    int         native;                        /* --native: the C routines only */
    int         original;                      /* --original: the whole original program on the 6809 */
} board;

static float vg13(uint16_t v) { return (float)((int16_t)(uint16_t)(v << 3) >> 3); }

static void on_irq(void *u)
{
    board *b = (board *)u;
    sw_controls_tick(&b->ctl, &b->m);
    b->irqs++;
    if (b->controls_test) {
        sw_irq(&b->m);
        return;
    }
    if (b->native) {
        sw_irq(&b->m);
        sw_main_poll(&b->m);
    } else if (b->original) {
        sw_emu_irq(&b->emu);
    } else {
        sw_emu_native_irq(&b->emu);
    }
    if (b->m.vg_starts != b->vg_seen) {
        /* The vector generator was started on a picture: the game's new
         * frame if the interrupt switched buffers, else the same one with
         * the sight and the animations moved on. */
        b->vg_seen = b->m.vg_starts;
        memcpy(b->space, b->m.vgram, sizeof b->m.vgram);
        memcpy(b->space + 0x3000, b->m.rom->vrom, sizeof b->m.rom->vrom);
        sw_avg_run_tagged(b->space, 0, b->m.vgtag, b->m.vgstart, &b->list, 0);
        int new_frame = b->m.vgram[0] != b->vg_buffer;
        b->vg_buffer = b->m.vgram[0];
        sw_smooth_key(&b->smooth, &b->list, (double)b->irqs / SW_IRQ_HZ, new_frame,
                      vg13(RAM16(&b->m, SW_VG_CX)) * 0.25f, vg13(RAM16(&b->m, SW_VG_CY)) * 0.25f);
    }
}

static void on_vg(void *u)
{
    /* The sight moves at VG restarts (RHPOS in DOVG): keep the last two
     * positions to draw between them. */
    board *b = (board *)u;
    b->sight_prev[0] = b->sight_cur[0];
    b->sight_prev[1] = b->sight_cur[1];
    b->sight_cur[0] = vg13(RAM16(&b->m, SW_VG_CX));
    b->sight_cur[1] = vg13(RAM16(&b->m, SW_VG_CY)) + (float)(int16_t)SW_VGOFFY;
}

static void on_game(void *u)
{
    /* Without the main line, consume the lasers as it would. */
    board *b = (board *)u;
    if (b->controls_test && RAM8(&b->m, SW_VG_LON)) {
        RAM8(&b->m, SW_VG_LON) = 0;
        b->fire_frames = 10;
    }
}

static void draw_controls_test(sw_app *app, const sw_display_cfg *cfg, board *b, double vg_alpha, int mbox_ok)
{
    /* The sight, between its last two positions. */
    float a = (float)vg_alpha;
    float x = b->sight_prev[0] + (b->sight_cur[0] - b->sight_prev[0]) * a;
    float y = b->sight_prev[1] + (b->sight_cur[1] - b->sight_prev[1]) * a;
    static calib s;
    s.count = 0;
    unsigned col = b->fire_frames > 0 ? 4 : 6;
    add(&s, x - 40, y, x - 12, y, col, 7, 0xff); add(&s, x + 12, y, x + 40, y, col, 7, 0xff);
    add(&s, x, y - 40, x, y - 12, col, 7, 0xff); add(&s, x, y + 12, x, y + 40, col, 7, 0xff);
    /* the yoke's reach: VGCUR limits, +-128 scaled to +-512 */
    float l = 4.0f * (int8_t)(uint8_t)SW_VGCURL, r = 4.0f * (int8_t)SW_VGCURR;
    float bt = 4.0f * (int8_t)(uint8_t)SW_VGCURB + (int16_t)SW_VGOFFY, t = 4.0f * (int8_t)SW_VGCURT + (int16_t)SW_VGOFFY;
    add(&s, l, bt, r, bt, 1, 3, 0x80); add(&s, r, bt, r, t, 1, 3, 0x80);
    add(&s, r, t, l, t, 1, 3, 0x80); add(&s, l, t, l, bt, 1, 3, 0x80);
    sw_draw_lines(app->dvg, cfg, s.lines, s.count);
    if (b->fire_frames > 0) b->fire_frames--;

    char l1[64], l2[64];
    snprintf(l1, sizeof l1, "CREDITS %u", RAM8(&b->m, SW_SSCRDT));
    snprintf(l2, sizeof l2, "PITCH %02X  YAW %02X", RAM8(&b->m, SW_PT_VL2), RAM8(&b->m, SW_PT_VL1));
    const char *text[] = { "STAR WARS VP  CONTROLS TEST",
                           mbox_ok ? "ROMS OK  MATH BOX OK" : "ROMS OK  MATH BOX DIFFERS", l1, l2 };
    draw_text_lines(app->dvg, text, 4, 3700);
}

/* The game's picture at the time now (seconds on the game's clock): the
 * last frames of the game, moved to where they are now (render/smooth.h). */
static void draw_game(sw_app *app, const sw_display_cfg *cfg, board *b, double now)
{
    const sw_line *lines;
    int n = sw_smooth_frame(&b->smooth, now, cfg->smooth, &lines);
    sw_draw_lines(app->dvg, cfg, lines, n);
}

/* --- the NOVRAM's file --------------------------------------------------------- */

/* Options, accounting, the three best scores and the yoke's learned range
 * live in the board's NOVRAM: kept in save/nvram.bin. */
static void nvram_path(char *buf, size_t size)
{
    snprintf(buf, size, "%s/nvram.bin", vp_save_dir());
}

static void nvram_load(sw_machine *m)
{
    char path[1024];
    nvram_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t d[sizeof m->nvstore];
    if (fread(d, 1, sizeof d, f) == sizeof d) memcpy(m->nvstore, d, sizeof d);
    fclose(f);
}

static void nvram_save(const sw_machine *m)
{
    char path[1024];
    nvram_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "starwarsvp: cannot write %s\n", path);
        return;
    }
    fwrite(m->nvstore, 1, sizeof m->nvstore, f);
    fclose(f);
}

/* --- main ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    sw_display_cfg cfg;
    sw_display_cfg_defaults(&cfg);
    char cfg_path[1024], buf[1024];
    const char *arg = sw_arg(argc, argv, "--config");
    if (arg) {
        snprintf(cfg_path, sizeof cfg_path, "%s", arg);
    } else {
        /* Package defaults, then the cabinet's own tuning in save/. */
        sw_display_cfg_load(&cfg, vp_asset_path("starwarsvp.ini", buf, sizeof buf));
        snprintf(cfg_path, sizeof cfg_path, "%s/starwarsvp.ini", vp_save_dir());
    }
    sw_display_cfg_load(&cfg, cfg_path);

    static sw_romset roms;
    const char *paths[MAX_PATHS];
    int npaths = rom_paths(argc, argv, paths);
    int roms_ok = sw_romset_load(&roms, paths, npaths) == 0;
    char missing[512];
    int mbox_ok = 0;
    if (roms_ok) {
        printf("starwarsvp: ROM set starwars1 complete\n");
        /* The native math box against the cabinet's own micro-code PROMs. */
        static uint16_t prom[1024];
        sw_mbox_join_proms(roms.mbox_prom, prom);
        int bad = sw_mbox_selftest(prom, 50);
        mbox_ok = bad == 0;
        if (mbox_ok) printf("starwarsvp: math box matches the PROMs\n");
        else fprintf(stderr, "starwarsvp: math box: %d program(s) differ from the PROMs\n", bad);
    }
    else fprintf(stderr, "starwarsvp: ROMs missing: %s\n", sw_romset_missing(&roms, missing, sizeof missing));

    sw_app app;
    if (sw_app_open(&app, "Star Wars VP", argc, argv, 50.0) != 0) return 1;

    int calibrate = sw_flag(argc, argv, "--calibrate");
    static calib cal;
    sw_sched sched;
    sw_sched_init(&sched);

    static board brd;
    sw_machine_init(&brd.m, &roms);
    brd.controls_test = sw_flag(argc, argv, "--controls");
    nvram_load(&brd.m);
    uint32_t nv_saved = brd.m.nv_stores;
    brd.native = sw_flag(argc, argv, "--native");
    brd.m.yoke_exact = cfg.yoke_autocenter != 0;
    if (roms_ok) {
        /* Three ways to run the game:
         *   default     the C routines; the 6809 stands in for the phases
         *               that are not ported yet
         *   --native    the C routines only (a phase not ported: back to
         *               the banner)
         *   --original  the whole original program on the 6809 */
        brd.original = sw_flag(argc, argv, "--original");
        if (brd.native || brd.controls_test) {
            sw_reset(&brd.m);
        } else {
            brd.emu.yoke_exact = cfg.yoke_autocenter != 0;
            if (!sw_flag(argc, argv, "--nosound")) {
                static sw_sndboard snd;
                sw_sndboard_init(&snd, &brd.m);
                brd.emu.snd = &snd;
                sw_audio_open(&snd);
            }
            if (brd.original) {
                struct sw_sndboard *snd = brd.emu.snd;
                sw_emu_init(&brd.emu, &brd.m);
                brd.emu.snd = snd;
                brd.emu.yoke_exact = cfg.yoke_autocenter != 0;
            } else {
                sw_reset(&brd.m);
                sw_emu_attach(&brd.emu, &brd.m);
            }
        }
        /* The sight is drawn by the routine the word at VGSITE calls. */
        brd.smooth.sight_routine = (unsigned)(((roms.vrom[SW_VGSITE - 0x3000] << 8) | roms.vrom[SW_VGSITE - 0x2fff]) & 0x1fff);
    }
    sw_controls_init(&brd.ctl, app.in);
    if (roms_ok && sw_splash_verify(&brd.m) != 0)
        fprintf(stderr, "starwarsvp: the splash table differs from the ROM\n");
    sw_sched_hooks hooks = { on_irq, on_vg, on_game, &brd };

    int status = 0;
    double dt;
    while (sw_app_poll(&app, &dt)) {
        sw_controls_read(&brd.ctl, app.in);
        sw_sched_advance(&sched, dt, roms_ok ? &hooks : NULL);
        if (brd.emu.snd) sw_audio_balance(brd.emu.snd);
        if (brd.m.nv_stores != nv_saved) {      /* the program stored its NOVRAM */
            nv_saved = brd.m.nv_stores;
            nvram_save(&brd.m);
        }
        /* A cabinet is switched off, not shut down: every few seconds what
         * has changed in the NOVRAM's RAM (a high score, the accounting) is
         * kept, as the board's power-fail circuit would at the last moment. */
        static double nv_checked;
        static uint8_t nv_kept[sizeof brd.m.nvram];
        if (roms_ok && !calibrate && !brd.controls_test && app.clock - nv_checked > 5.0) {
            nv_checked = app.clock;
            if (brd.irqs > 2000 && memcmp(nv_kept, brd.m.nvram, sizeof nv_kept) != 0) {
                memcpy(nv_kept, brd.m.nvram, sizeof nv_kept);
                memcpy(brd.m.nvstore, brd.m.nvram, sizeof brd.m.nvstore);
                nvram_save(&brd.m);
            }
        }
        sw_app_begin_frame(&app);

        if (!roms_ok) {
            const char *text[] = { "STAR WARS VP", "ROMS NOT FOUND", "NEEDED: MAME SET STARWARS1",
                                   "STARWARS1.ZIP AND STARWARS.ZIP", "IN THE ROM FOLDER OR --ROMS" };
            draw_text_lines(app.dvg, text, 5, 2600);
        } else if (calibrate) {
            calib_adjust(&cal, &cfg, app.in);
            if (vp_input_pressed(app.in, "p1_button2")) {
                if (sw_display_cfg_save(&cfg, cfg_path) == 0) printf("starwarsvp: saved %s\n", cfg_path);
                else fprintf(stderr, "starwarsvp: cannot write %s\n", cfg_path);
            }
            build_pattern(&cal, &cfg);
            sw_draw_lines(app.dvg, &cfg, cal.lines, cal.count);
            static const char *const modes[] = { "SIZE", "POSITION", "GAMMA FLOOR", "BRIGHTNESS" };
            char line[64];
            snprintf(line, sizeof line, "%s  B1 MODE  B2 SAVE", modes[cal.mode]);
            const char *text[] = { line };
            draw_text_lines(app.dvg, text, 1, 700);
        } else if (brd.controls_test) {
            draw_controls_test(&app, &cfg, &brd, sw_sched_vg_alpha(&sched), mbox_ok);
        } else {
            draw_game(&app, &cfg, &brd, (double)brd.irqs / SW_IRQ_HZ + sched.acc);
        }

        if (sw_app_end_frame(&app) != 0) {
            status = 1;
            break;
        }
    }
    if (roms_ok && !calibrate && !brd.controls_test) {
        /* Power off: what the NOVRAM's RAM holds goes into its EEPROM, as
         * the board's power-fail circuit stores it. */
        memcpy(brd.m.nvstore, brd.m.nvram, sizeof brd.m.nvstore);
        nvram_save(&brd.m);
    }
    sw_audio_close();
    sw_app_close(&app);
    return status;
}
