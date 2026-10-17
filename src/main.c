/* swenhanced - Star Wars (Atari, 1983), native, for VectorPie.
 *
 * At this stage: finds and checks the ROM set, runs the original's clocks
 * with the native interrupt (coins, switches, yoke and sight) and the
 * native main line as far as it is ported: the attract mode and the start
 * of a game up to the choice of a Death Star (docs/PLAN.md). --controls
 * shows the controls test, --calibrate the calibration screen for the
 * monitor.
 *
 * The self-test button opens the port's own test menu (platform/menu.h).
 *
 *   swenhanced [--calibrate | --controls] [--roms PATH] [--config INI] [--hz N]
 *              [--dvg [PORT] | --window | --windowed]
 */
#include "avg/avg.h"
#include "mbox/mbox.h"
#include "core/emu.h"
#include "core/machine.h"
#include "core/sched.h"
#include "game/game.h"
#include "platform/controls.h"
#include "platform/bind.h"
#include "platform/app.h"
#include "platform/audio.h"
#include "dogfight/dogfight.h"

/* The dogfight (the fourth Death Star to choose) is not offered for now: it
 * is not finished (the owner, 2026-10-17). Its code stays; 1 offers it again. */
#define SW_DOGFIGHT_OFFERED 0
#include "platform/demo.h"
#include "platform/menu.h"
#include "platform/yoke.h"
#include "render/config.h"
#include "render/dvgmap.h"
#include "render/picture.h"
#include "rom/romset.h"

#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>     /* (fsync) */
#endif

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

    /* First (the menu's SCREEN page draws these ten): the white edge through
     * the four blue points the game draws in every picture (VJINIT, at
     * +-119.5, +-137.5 of the lines' units): when the edge is on the tube,
     * so are they; the center cross; the four points themselves, blue. */
    const float px = 119.5f / GAME_SCALE, py = 137.5f / GAME_SCALE;
    add(c, -px, -py, px, -py, 7, 7, 0xff); add(c, px, -py, px, py, 7, 7, 0xff);
    add(c, px, py, -px, py, 7, 7, 0xff); add(c, -px, py, -px, -py, 7, 7, 0xff);
    add(c, -40, 0, 40, 0, 7, 7, 0xff); add(c, 0, -40, 0, 40, 7, 7, 0xff);
    add(c, -px, -py, -px, -py, 1, 7, 0xff); add(c, px, -py, px, -py, 1, 7, 0xff);
    add(c, px, py, px, py, 1, 7, 0xff); add(c, -px, py, -px, py, 1, 7, 0xff);
    /* the AVG window's edge (dim), the clip limits (VGLIM*) */
    add(c, l, b, r, b, 7, 3, 0x80); add(c, r, b, r, t, 7, 3, 0x80);
    add(c, r, t, l, t, 7, 3, 0x80); add(c, l, t, l, b, 7, 3, 0x80);
    float cl = -480, cr = 480, cb = -552, ct = 408;
    add(c, cl, cb, cr, cb, 3, 4, 0x80); add(c, cr, cb, cr, ct, 3, 4, 0x80);
    add(c, cr, ct, cl, ct, 3, 4, 0x80); add(c, cl, ct, cl, cb, 3, 4, 0x80);

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
    if (sw_act_pressed(ACT_FIRE_L)) c->mode = (c->mode + 1) % 4;
    int dx = sw_act_pressed(ACT_RIGHT) - sw_act_pressed(ACT_LEFT);
    int dy = sw_act_pressed(ACT_UP) - sw_act_pressed(ACT_DOWN);
    /* the cabinet has no such switches: a push of its yoke is a step too */
    static int held_x, held_y;
    float ax = vp_input_axis(in, "p1_stickx"), ay = vp_input_axis(in, "p1_sticky");
    int px = ax < -0.5f ? -1 : ax > 0.5f ? 1 : 0, py = ay < -0.5f ? 1 : ay > 0.5f ? -1 : 0;
    if (px != held_x) dx += px;
    if (py != held_y) dy += py;
    held_x = px;
    held_y = py;
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
        if (cfg->brightness > 2.0f) cfg->brightness = 2.0f;
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
    uint64_t    irqs;                          /* interrupts run: the game's clock */
    sw_picture  pic;                           /* the game's picture at any time (render/picture.h) */
    sw_emu      emu;                           /* the original program on the machine */
    int         native;                        /* the C routines only: no 6809 at all */
    sw_sndboard *snd;                          /* the sound board (its chips; its program in C too), NULL: none */
    int         original;                      /* --original: the whole original program on the 6809 */
    sw_demo     demo;                          /* the demo flight of the attract mode */
    sw_dogfight df;                            /* the dogfight (a fourth Death Star to choose) */
} board;

static float vg13(uint16_t v) { return (float)((int16_t)(uint16_t)(v << 3) >> 3); }

/* sw_machine.sound_wait: the game waits for the sound board to take a byte. */
static void on_sound_wait(void *u, int cycles)
{
    board *b = (board *)u;
    if (b->snd) sw_sndboard_run(b->snd, cycles);
}

static void on_irq(void *u)
{
    board *b = (board *)u;
    sw_controls_tick(&b->ctl, &b->m);
    if (b->native) sw_demo_tick(&b->demo, &b->m);  /* (a demo flight: its own player) */
    b->irqs++;
    sw_picture_irq_begin(&b->pic, (double)b->irqs / SW_IRQ_HZ);
    if (b->controls_test) {
        sw_irq(&b->m);
        return;
    }
    if (b->native) {
        sw_irq(&b->m);
        sw_main_poll(&b->m);
        if (b->snd) sw_sndboard_run(b->snd, SW_CPU_CYCLES_PER_IRQ);
    } else {
        sw_emu_irq(&b->emu);
    }
    sw_picture_irq_end(&b->pic);
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
    const char *text[] = { "STAR WARS ENHANCED  CONTROLS TEST",
                           mbox_ok ? "ROMS OK  MATH BOX OK" : "ROMS OK  MATH BOX DIFFERS", l1, l2 };
    draw_text_lines(app->dvg, text, 4, 3700);
}

/* The game's picture at the time now (seconds on the game's clock): the
 * last frames of the game, moved to where they are now (render/smooth.h). */
static void draw_game(sw_app *app, const sw_display_cfg *cfg, board *b, double now)
{
    const sw_line *lines;
    if (b->df.active) {
        /* the dogfight's picture, in the view chosen */
        int n = sw_dogfight_lines(&b->df, &b->m, &lines);
        b->pic.enhance.dogfight = 1;
        sw_dogfight_attitude(&b->df, &b->pic.enhance.df_bank, &b->pic.enhance.df_pitch);
        b->pic.enhance.df_throttle = sw_dogfight_thrust(&b->df);
        b->pic.enhance.df_over = b->df.over;
        /* (on the dogfight's own clock: the game's stands still meanwhile) */
        n = sw_enhance_run(&b->pic.enhance, &b->m, b->df.t, lines, n, &lines);
        if (b->pic.occlusion) {
            /* solid: the ROM's ships by their own faces, the interceptor by its */
            b->pic.solid.extra = b->df.faces;
            b->pic.solid.n_extra = b->df.n_faces;
            n = sw_solid_run(&b->pic.solid, lines, n, &lines);
        }
        sw_draw_lines(app->dvg, cfg, lines, n);
        return;
    }
    b->pic.enhance.dogfight = 0;
    int n = sw_picture_lines(&b->pic, now, cfg->smooth, &lines);
    if (SW_DOGFIGHT_OFFERED && b->native && b->m.ram[SW_PHASE - 0x4800] == SW_PHSSDS + 1) {
        /* the dogfight as a fourth Death Star to choose */
        static sw_line more[8192];
        int k = n < 6000 ? n : 6000;
        memcpy(more, lines, (size_t)k * sizeof *more);
        sw_avg_list l = { more, k, 8192, 0, 0 };
        sw_dogfight_choice_lines(&b->df, &b->m, &l);
        lines = more;
        n = l.count;
    }
    sw_draw_lines(app->dvg, cfg, lines, n);
}

/* The enhanced settings, taken over: they are for the game in C only. */
static void apply_enhanced(board *b, const sw_display_cfg *cfg)
{
    int on = b->native;
    b->pic.occlusion = on && cfg->occlusion;
    b->pic.enhance.surface = on && cfg->enh_surface;
    b->pic.enhance.stars = on && cfg->enh_stars;
    b->pic.enhance.towers = on && cfg->enh_towers;
    b->pic.enhance.dogfights = on && cfg->enh_dogfights;
    b->pic.enhance.logo = on && cfg->logo_yellow;
    b->pic.enhance.clip = on && cfg->enh_clip;
    b->pic.enhance.falcon = on && cfg->enh_falcon;
    b->pic.enhance.blast = on && cfg->enh_blast;
    b->pic.enhance.view = on ? cfg->enh_view % SW_VIEWS : SW_VIEW_ARCADE;
    b->demo.enabled = on && cfg->enh_demo;
    b->m.camera_ease = (uint8_t)(on && cfg->enh_camera);
    if (b->snd && b->snd->samples) {
        b->snd->samples->enabled = on && cfg->sound_replace;
        sw_samples_volumes(b->snd->samples, cfg->vol_music / 100.0f, cfg->vol_effects / 100.0f,
                           cfg->vol_speech / 100.0f, cfg->vol_engine / 100.0f);
    }
    /* the yoke: its rest as its middle, and passed through exactly (the
     * arcade machine's own learning comes back with the next start) */
    b->ctl.autocenter = cfg->yoke_autocenter != 0;
    if (cfg->yoke_autocenter && on && !b->m.yoke_exact) sw_yoke_exact(&b->m);
}

/* --- the dogfight's best pilots --------------------------------------------- */

/* Its own table (not the arcade game's three in the NOVRAM): save/dogfight.hi,
 * a line each: initials, score, wave. */
static void df_scores_path(char *buf, size_t size)
{
    snprintf(buf, size, "%s/dogfight.hi", vp_save_dir());
}

static void df_scores_load(sw_df_scores *s)
{
    char path[1024], line[128];
    sw_dogfight_scores_default(s);
    df_scores_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    for (int i = 0; i < SW_DF_TABLE && fgets(line, sizeof line, f); i++) {
        char name[8] = "";
        long score = 0;
        int wave = 0;
        if (sscanf(line, "%3[^,],%ld,%d", name, &score, &wave) != 3) break;
        memset(s->name[i], 0, sizeof s->name[i]);
        memcpy(s->name[i], name, strlen(name) > 3 ? 3 : strlen(name));
        s->score[i] = score;
        s->wave[i] = wave;
    }
    fclose(f);
}

static void df_scores_save(const sw_df_scores *s)
{
    char path[1024];
    df_scores_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "swenhanced: cannot write %s\n", path);
        return;
    }
    for (int i = 0; i < SW_DF_TABLE; i++) fprintf(f, "%.3s,%ld,%d\n", s->name[i], s->score[i], s->wave[i]);
    fclose(f);
}

/* --- the high scores for the menu's marquee ------------------------------------ */

/* The game's own table (TCHSCR.MAC: ten lines, initials 0 blank, 1..26 A..Z,
 * the scores eight digits of BCD) as save/highscores.txt, the file the
 * VectorPie menu shows on the marquee (sdk/docs/06-hiscores.md: "NAME
 * SCORE", best first). Written only when it has changed, by a new file
 * renamed over the old (a cabinet switched off mid-save keeps the old). */
static void hiscores_export(const sw_machine *m)
{
    static char last[512];
    char text[512];
    size_t at = 0;
    for (int i = 0; i < SW_NHSCRS; i++) {
        char name[4];
        int k = 0;
        for (int c = 0; c < 3; c++) {
            uint8_t a = m->ram[SW_INITLS + 3 * i + c - 0x4800];
            if (a > 26) return;                         /* (not a table yet) */
            name[k++] = a ? (char)('A' + a - 1) : '.';
        }
        while (k > 0 && name[k - 1] == '.') k--;       /* (blanks at its end) */
        name[k] = 0;
        unsigned long score = 0;
        for (int b = 0; b < 4; b++) {
            uint8_t v = m->ram[SW_HSCORS + 4 * i + b - 0x4800];
            if ((v >> 4) > 9 || (v & 15) > 9) return;   /* (not BCD: not a table yet) */
            score = score * 100 + (v >> 4) * 10 + (v & 15);
        }
        if (!score) continue;
        at += (size_t)snprintf(text + at, sizeof text - at, "%s %lu\n", k ? name : "---", score);
    }
    if (strcmp(text, last) == 0 || at == 0) return;
    char path[1024], tmp[1100];
    snprintf(path, sizeof path, "%s/highscores.txt", vp_save_dir());
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fputs(text, f);
    fflush(f);
#ifndef _WIN32
    fsync(fileno(f));
#endif
    fclose(f);
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp, path) == 0) memcpy(last, text, sizeof last);
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
        fprintf(stderr, "swenhanced: cannot write %s\n", path);
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
        sw_display_cfg_load(&cfg, vp_asset_path("swenhanced.ini", buf, sizeof buf));
        snprintf(cfg_path, sizeof cfg_path, "%s/swenhanced.ini", vp_save_dir());
    }
    sw_display_cfg_load(&cfg, cfg_path);

    static sw_romset roms;
    const char *paths[MAX_PATHS];
    int npaths = rom_paths(argc, argv, paths);
    int roms_ok = sw_romset_load(&roms, paths, npaths) == 0;
    char missing[512];
    int mbox_ok = 0;
    if (roms_ok) {
        printf("swenhanced: ROM set starwars1 complete\n");
        /* The native math box against the cabinet's own micro-code PROMs. */
        static uint16_t prom[1024];
        sw_mbox_join_proms(roms.mbox_prom, prom);
        int bad = sw_mbox_selftest(prom, 50);
        mbox_ok = bad == 0;
        if (mbox_ok) printf("swenhanced: math box matches the PROMs\n");
        else fprintf(stderr, "swenhanced: math box: %d program(s) differ from the PROMs\n", bad);
    }
    else fprintf(stderr, "swenhanced: ROMs missing: %s\n", sw_romset_missing(&roms, missing, sizeof missing));

    sw_app app;
    if (sw_app_open(&app, "Star Wars Enhanced", argc, argv, 50.0) != 0) return 1;
    sw_bind_init();

    int calibrate = sw_flag(argc, argv, "--calibrate");
    static calib cal;
    static sw_menu menu;
    int menu_calibrate = 0;                 /* the calibration screen, from the menu */
    sw_sched sched;
    sw_sched_init(&sched);

    static board brd;
    sw_machine_init(&brd.m, &roms);
    brd.controls_test = sw_flag(argc, argv, "--controls");
    nvram_load(&brd.m);
    uint32_t nv_saved = brd.m.nv_stores;
    brd.m.yoke_exact = cfg.yoke_autocenter != 0;
    brd.m.steady = sw_flag(argc, argv, "--original") ? 0 : (uint8_t)cfg.steady_frames;
    if (roms_ok) {
        /* Two ways to run the game:
         *   default     the game's and the sound board's programs in C:
         *               no 6809 at all
         *   --original  the whole original programs on 6809 cores, to
         *               compare with */
        brd.original = sw_flag(argc, argv, "--original");
        brd.native = !brd.original;
        if (!brd.controls_test && !sw_flag(argc, argv, "--nosound")) {
            static sw_sndboard snd;
            sw_sndboard_init(&snd, &brd.m);
            snd.native = !brd.original;
            /* Recordings in place of sounds of the board: the cabinet's own
             * in save/sounds first, then those that come with the program. */
            static sw_samples samples;
            char dir[1024], abuf[1024];
            snprintf(dir, sizeof dir, "%s/sounds", vp_save_dir());
            int replaced = sw_samples_load(&samples, dir, SW_SOUND_RATE);
            replaced += sw_samples_load(&samples, vp_asset_path("sounds", abuf, sizeof abuf), SW_SOUND_RATE);
            if (replaced) printf("swenhanced: %d sound(s) replaced by recordings\n", replaced);
            snd.samples = &samples;
            sw_sndboard_reset(&snd);
            brd.snd = &snd;
            brd.emu.snd = &snd;
            sw_audio_open(&snd);
        }
        if (brd.native || brd.controls_test) {
            sw_reset(&brd.m);
            brd.m.sound_board = brd.snd != NULL;
            brd.m.sound_wait = on_sound_wait;
            brd.m.sound_user = &brd;
        } else {
            sw_emu_init(&brd.emu, &brd.m);
            brd.emu.snd = brd.snd;
            brd.emu.yoke_exact = cfg.yoke_autocenter != 0;
        }
        sw_picture_init(&brd.pic, &brd.m);
        apply_enhanced(&brd, &cfg);
    }
    sw_controls_init(&brd.ctl, app.in);
    brd.ctl.autocenter = cfg.yoke_autocenter != 0;
    if (cfg.yoke_center_known) {            /* where the yoke rests, as learned before */
        brd.ctl.center_x = cfg.yoke_center_x;
        brd.ctl.center_y = cfg.yoke_center_y;
        brd.ctl.centered = 1;
    }
    if (roms_ok && sw_splash_verify(&brd.m) != 0)
        fprintf(stderr, "swenhanced: the splash table differs from the ROM\n");
    sw_sched_hooks hooks = { on_irq, on_vg, on_game, &brd };

    int status = 0;
    double dt;
    while (sw_app_poll(&app, &dt)) {
        sw_bind_update(app.in);         /* the digital inputs, the cabinet's or the owner's own */
        if (brd.snd && brd.snd->samples) {
            /* where the game is, for the small explosion's recording (samples.h) */
            /* (the phases are odd; the even one above each is the same phase running) */
            uint8_t ph = RAM8(&brd.m, SW_PHASE) | 1;
            sw_samples_ground(brd.snd->samples,
                              ph == SW_PHSGD || ph == SW_PHSG0B || ph == SW_PHSG1B || ph == SW_PHSG0D ||
                              ph == SW_PHSBS || ph == SW_PHSB0B || ph == SW_PHSB0D);
        }
        /* (the yoke's rest is learned while nobody plays: in the attract mode) */
        brd.ctl.learn = RAM8(&brd.m, SW_PHASE) >= SW_PHSBNR && RAM8(&brd.m, SW_PHASE) <= SW_PHSHIS + 1;
        /* A game begins (the attract mode left, not by a demo flight): from
         * the arcade machine's own view; start steps on from there. */
        {
            static int was_attract = 1;
            int attract = brd.ctl.learn;
            if (was_attract && !attract && !brd.demo.active && cfg.enh_view != SW_VIEW_ARCADE) {
                cfg.enh_view = SW_VIEW_ARCADE;
                apply_enhanced(&brd, &cfg);
            }
            was_attract = attract;
        }
        /* In flight (the X-wing's nose and guns in the picture) the start
         * button steps through the views; else it fires, to start a game. */
        int in_flight = brd.native && (brd.pic.enhance.plane_seen || brd.df.active) && !menu.open && !brd.demo.active;
        brd.ctl.start_fires = !in_flight;
        sw_controls_read(&brd.ctl, app.in);
        sw_yoke_update();
        /* A coin or a button ends a demo flight: the attract mode is back
         * at once, and the coin or the button counts there. */
        if (brd.demo.active) {
            static const sw_action keys[] = { ACT_FIRE_L, ACT_THUMB_L, ACT_THUMB_R, ACT_FIRE_R, ACT_COIN_AUX,
                                              ACT_START, ACT_START2, ACT_COIN1, ACT_COIN2 };
            for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
                if (sw_act_pressed(keys[i])) brd.demo.interrupt = 1;
        }
        /* The dogfight: chosen as a fourth Death Star, by firing at it; flown
         * with the yoke, the thumb buttons faster and slower. */
        if (SW_DOGFIGHT_OFFERED && brd.native && !brd.df.active && !menu.open && sw_dogfight_chosen(&brd.m) &&
            (sw_act_pressed(ACT_FIRE_L) || sw_act_pressed(ACT_FIRE_R) || sw_act_pressed(ACT_START))) {
            static sw_df_scores df_scores;
            static int df_scores_loaded;
            if (!df_scores_loaded) { df_scores_load(&df_scores); df_scores_loaded = 1; }
            brd.df.scores = &df_scores;
            sw_dogfight_start(&brd.df, &brd.m, 1);
            sw_dogfight_howto(&brd.df);         /* (first how it is flown) */
            if (cfg.enh_view != SW_VIEW_ARCADE) {   /* (from the arcade's own view, as every game) */
                cfg.enh_view = SW_VIEW_ARCADE;
                apply_enhanced(&brd, &cfg);
            }
            brd.df.fire_held = 1;               /* (the pull that chose it does not end that page) */
        }
        if (brd.df.active) {
            /* the yoke itself when there is one (each of its buttons on its
             * own, its whole travel), else the controls as the game has them */
            sw_dogfight_input din;
            if (sw_yoke_present()) {
                sw_yoke_axes(&din.yaw, &din.pitch);             /* (pulled back: the nose up) */
                din.fire = sw_yoke_held(SW_YOKE_RIGHT_TRIGGER);
                din.torpedo = sw_yoke_held(SW_YOKE_LEFT_TRIGGER);
                din.faster = sw_yoke_held(SW_YOKE_RIGHT_THUMB);
                din.slower = sw_yoke_held(SW_YOKE_LEFT_THUMB);
            } else {
                din.yaw = ((float)brd.ctl.adc_x - 128.0f) / 127.0f;
                din.pitch = ((float)brd.ctl.adc_y - 128.0f) / 127.0f;
                din.fire = sw_act_held(ACT_FIRE_L);
                din.torpedo = sw_act_held(ACT_FIRE_R);
                din.faster = sw_act_held(ACT_THUMB_L);
                din.slower = sw_act_held(ACT_THUMB_R);
            }
            sw_dogfight_step(&brd.df, &din, dt);
            /* its sounds, one at a time, as the game asks the board */
            if (!brd.m.sound_out_full) {
                int r = sw_dogfight_sound(&brd.df);
                if (r >= 0) { brd.m.sound_out = (uint8_t)r; brd.m.sound_out_full = 1; }
            }
            if (brd.df.scores && brd.df.scores->dirty) {
                brd.df.scores->dirty = 0;
                df_scores_save(brd.df.scores);
            }
            if (brd.df.finished) {
                /* Over: the board's sound stopped, and the game ends as its
                 * own games end (PHIEGM: "Remember...", the accounting, Ben's
                 * theme, the attract mode). */
                brd.df.active = 0;
                brd.m.ram[SW_PHASE - 0x4800] = SW_PHSEGM;
                brd.m.sound_out = 0;
                brd.m.sound_out_full = 1;
            }
        }
        if (in_flight && sw_act_pressed(ACT_START)) {
            cfg.enh_view = (cfg.enh_view % SW_VIEWS + 1) % SW_VIEWS;
            apply_enhanced(&brd, &cfg);
            if (sw_display_cfg_save(&cfg, cfg_path) != 0) fprintf(stderr, "swenhanced: cannot write %s\n", cfg_path);
        }
        /* Where the yoke rests is kept for the next start as soon as it is
         * known, or has moved (a cabinet is switched off, not shut down). */
        if (brd.ctl.autocenter && brd.ctl.centered &&
            (!cfg.yoke_center_known || fabsf(brd.ctl.center_x - cfg.yoke_center_x) > 0.01f ||
             fabsf(brd.ctl.center_y - cfg.yoke_center_y) > 0.01f)) {
            cfg.yoke_center_known = 1;
            cfg.yoke_center_x = brd.ctl.center_x;
            cfg.yoke_center_y = brd.ctl.center_y;
            if (sw_display_cfg_save(&cfg, cfg_path) != 0) fprintf(stderr, "swenhanced: cannot write %s\n", cfg_path);
        }
        /* The self-test button: out of where it led (the arcade program's
         * operator screens, the calibration), else the menu open or shut. */
        int menu_left = 0;
        /* The F2 key does the same, as the service key of MAME (the
         * VectorPie cabinet has a keyboard, and no self-test button bound:
         * all the buttons of its yoke are the fire button); and whatever the
         * owner bound to TEST MENU (platform/bind.h). */
        int f2 = sw_bind_test_pressed();
        if (brd.df.active && f2) {
            /* (stage 1: the service key leaves the dogfight, back to the attract mode) */
            brd.df.active = 0;
            brd.m.ram[SW_PHASE - 0x4800] = SW_PHSBNR;
            f2 = 0;
        } else if (roms_ok && !calibrate && f2) {
            if (brd.ctl.self_test) brd.ctl.self_test = 0;
            else if (menu_calibrate) menu_calibrate = 0;
            else if (menu.open) menu_left = 1;
            else { sw_demo_stop(&brd.demo, &brd.m); menu.open = 1; menu.page = 0; }
        }
        if (menu.open && !menu_calibrate && !menu_left) {
            switch (sw_menu_update(&menu, &cfg, &brd.m, app.in, app.clock)) {
            case SW_MENU_CHANGED:
                apply_enhanced(&brd, &cfg);
                brd.m.steady = brd.original ? 0 : (uint8_t)cfg.steady_frames;
                break;
            case SW_MENU_OPTION:            /* into the NOVRAM's EEPROM at once */
                memcpy(brd.m.nvstore, brd.m.nvram, sizeof brd.m.nvstore);
                nvram_save(&brd.m);
                break;
            case SW_MENU_SOUND:             /* as the main board asks for it */
                brd.m.sound_out = (uint8_t)menu.sound;
                brd.m.sound_out_full = 1;
                break;
            case SW_MENU_CALIBRATE:
                menu_calibrate = 1;
                cal.mode = 3;               /* (the same push of the trigger steps on to the first) */
                break;
            case SW_MENU_OPERATOR:
                brd.ctl.self_test = 1;
                menu_left = 1;
                break;
            case SW_MENU_CLOSE:
                menu_left = 1;
                break;
            }
        }
        if (menu_left) {
            menu.open = 0;
            if (sw_display_cfg_save(&cfg, cfg_path) == 0) printf("swenhanced: saved %s\n", cfg_path);
            else fprintf(stderr, "swenhanced: cannot write %s\n", cfg_path);
        }
        if (menu.open || brd.df.active) {
            /* the game stands still (in the menu, in the dogfight); the sound
             * board goes on, for the sound test */
            if (brd.snd) sw_sndboard_run(brd.snd, (int)((dt > 0.1 ? 0.1 : dt) * SW_IRQ_HZ * SW_CPU_CYCLES_PER_IRQ));
        } else {
            sw_sched_advance(&sched, dt, roms_ok ? &hooks : NULL);
        }
        /* With the attract mode's sound off (the operator's option MUSIC IN
         * ATTRACT: NO, the game's "sick and tired of the music"), a demo
         * flight, a game the program plays itself in the attract mode, is
         * quiet as well: its shots, music and voices are not heard. */
        if (brd.snd) brd.snd->volume = roms_ok && brd.demo.active && sw_option_get(&brd.m, 7, NULL) == 1 ? 0.0f : 1.0f;
        /* The Falcon flies over (render/enhance.h): its fly-by loudest just as
         * it passes, and "Yahoo, you're all clear, kid" with it (the game's
         * own request for that line is then not played: samples.h). */
        if (brd.snd && brd.snd->samples) {
            brd.snd->samples->hold_yahoo = brd.pic.enhance.falcon;
            if (brd.pic.enhance.falcon_cue) {
                brd.pic.enhance.falcon_cue = 0;
                sw_samples_cue(brd.snd->samples, SW_CUE_FALCON, brd.pic.enhance.falcon_pass_in - SW_FALCON_PEAK);
                sw_samples_cue(brd.snd->samples, 0x1a, brd.pic.enhance.falcon_pass_in);     /* SPKYAU */
            }
        }
        /* The X-wing's own engine (enhanced): in flight, from inside the
         * cockpit (the arcade's view too: it is the pilot's) or from outside,
         * as the view is; in the dogfight it runs up as the ship speeds up. */
        if (brd.snd && brd.snd->samples) {
            int flying = !menu.open && cfg.enh_engine &&
                         (brd.df.active ? brd.df.stage == SW_DF_PLAY && !brd.df.over : brd.pic.enhance.plane_seen);
            float thrust = brd.df.active ? sw_dogfight_thrust(&brd.df) : 0.0f;
            int outside = brd.pic.enhance.view == SW_VIEW_OUTSIDE;
            sw_samples_engine(brd.snd->samples, !flying ? -1 : outside ? SW_ENGINE_OUTSIDE : SW_ENGINE_COCKPIT,
                              (outside ? 0.75f : 0.6f) * (0.8f + 0.2f * thrust), 1.0f + 0.15f * thrust);   /* (under the rest) */
        }
        if (brd.snd) sw_audio_balance(brd.snd);
        /* (a demo flight keeps nothing: the machine is put back after it) */
        if (brd.m.nv_stores != nv_saved && !brd.demo.active) {      /* the program stored its NOVRAM */
            nv_saved = brd.m.nv_stores;
            nvram_save(&brd.m);
        }
        /* A cabinet is switched off, not shut down: every few seconds what
         * has changed in the NOVRAM's RAM (a high score, the accounting) is
         * kept, as the board's power-fail circuit would at the last moment. */
        static double nv_checked;
        static uint8_t nv_kept[sizeof brd.m.nvram];
        if (roms_ok && !calibrate && !brd.controls_test && !brd.demo.active && app.clock - nv_checked > 5.0) {
            nv_checked = app.clock;
            if (brd.irqs > 2000 && memcmp(nv_kept, brd.m.nvram, sizeof nv_kept) != 0) {
                memcpy(nv_kept, brd.m.nvram, sizeof nv_kept);
                memcpy(brd.m.nvstore, brd.m.nvram, sizeof brd.m.nvstore);
                nvram_save(&brd.m);
            }
            if (brd.irqs > 2000) hiscores_export(&brd.m);     /* (the marquee's table, when it has changed) */
        }
        /* the demo's view while it flies, the player's own again after it */
        if (brd.demo.active) brd.pic.enhance.view = brd.demo.view;
        if (brd.demo.ended) { brd.demo.ended = 0; apply_enhanced(&brd, &cfg); }
        sw_app_begin_frame(&app);

        if (!roms_ok) {
            const char *text[] = { "STAR WARS ENHANCED", "ROMS NOT FOUND", "NEEDED: MAME SET STARWARS1",
                                   "STARWARS1.ZIP AND STARWARS.ZIP", "IN THE ROM FOLDER OR --ROMS" };
            draw_text_lines(app.dvg, text, 5, 2600);
        } else if (calibrate || menu_calibrate) {
            calib_adjust(&cal, &cfg, app.in);
            if (calibrate && sw_act_pressed(ACT_THUMB_L)) {
                if (sw_display_cfg_save(&cfg, cfg_path) == 0) printf("swenhanced: saved %s\n", cfg_path);
                else fprintf(stderr, "swenhanced: cannot write %s\n", cfg_path);
            }
            build_pattern(&cal, &cfg);
            sw_draw_lines(app.dvg, &cfg, cal.lines, cal.count);
            /* what is being set, and how it is now */
            char what[48];
            switch (cal.mode) {
            case 0:  snprintf(what, sizeof what, "SIZE %.1f%% x %.1f%%", (4096 - cfg.margin_left - cfg.margin_right) * 100.0 / 4096.0,
                              (4096 - cfg.margin_bottom - cfg.margin_top) * 100.0 / 4096.0); break;
            case 1:  snprintf(what, sizeof what, "POSITION %+d %+d", (cfg.margin_left - cfg.margin_right) / 2,
                              (cfg.margin_bottom - cfg.margin_top) / 2); break;
            case 2:  snprintf(what, sizeof what, "GAMMA %.2f  BLACK %d%%", cfg.gamma, (int)lrintf(cfg.floor * 100.0f)); break;
            default: snprintf(what, sizeof what, "BRIGHTNESS %d%%", (int)lrintf(cfg.brightness * 100.0f)); break;
            }
            char line[96];
            snprintf(line, sizeof line, calibrate ? "%s  B1 MODE  B2 SAVE" : "%s  FIRE MODE  F2 BACK", what);
            const char *text[] = { line };
            draw_text_lines(app.dvg, text, 1, 700);
        } else if (menu.open) {
            if (sw_menu_screen_page(&menu)) {
                /* the edge through the game's four blue points, its middle and
                 * the points, behind the page of the picture's settings */
                build_pattern(&cal, &cfg);
                sw_draw_lines(app.dvg, &cfg, cal.lines, 10);
            }
            sw_menu_draw(&menu, &cfg, &brd.m, app.dvg);
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
