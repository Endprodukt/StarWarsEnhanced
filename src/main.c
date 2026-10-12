/* starwarsvp - Star Wars (Atari, 1983), native, for VectorPie.
 *
 * At this stage: finds and checks the ROM set, runs the original's clocks
 * and shows the calibration screen for the monitor. The game core follows
 * phase by phase (docs/PLAN.md).
 *
 *   starwarsvp [--calibrate] [--roms PATH] [--config INI] [--hz N]
 *              [--dvg [PORT] | --window | --windowed]
 */
#include "avg/avg.h"
#include "core/sched.h"
#include "platform/app.h"
#include "render/config.h"
#include "render/dvgmap.h"
#include "rom/romset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATHS 48

/* --- the ROM search list ------------------------------------------------- */

static int add_rom_dir(const char **paths, int n, char store[][1024], int *ns, const char *dir)
{
    static const char *const forms[] = { "%s/starwars1.zip", "%s/starwars.zip", "%s/starwars1", "%s" };
    for (int f = 0; f < 4 && n < MAX_PATHS && *ns < MAX_PATHS; f++) {
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

static void add(calib *c, float x0, float y0, float x1, float y1, unsigned color, unsigned zz, unsigned lum)
{
    if (c->count >= (int)(sizeof c->lines / sizeof c->lines[0])) return;
    sw_line *l = &c->lines[c->count++];
    l->x0 = x0; l->y0 = y0; l->x1 = x1; l->y1 = y1;
    l->color = (uint8_t)color;
    l->zz = (uint8_t)zz;
    l->intensity = sw_avg_intensity(zz, lum);
    l->defocus = 0;
}

/* Everything in AVG units through the same mapping as the game. */
static void build_pattern(calib *c, const sw_display_cfg *cfg)
{
    c->count = 0;
    float l = cfg->avg_left, r = cfg->avg_right, b = cfg->avg_bottom, t = cfg->avg_top;

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
    if (roms_ok) printf("starwarsvp: ROM set starwars1 complete\n");
    else fprintf(stderr, "starwarsvp: ROMs missing: %s\n", sw_romset_missing(&roms, missing, sizeof missing));

    sw_app app;
    if (sw_app_open(&app, "Star Wars VP", argc, argv, 50.0) != 0) return 1;

    int calibrate = sw_flag(argc, argv, "--calibrate");
    static calib cal;
    sw_sched sched;
    sw_sched_init(&sched);

    int status = 0;
    double dt;
    while (sw_app_poll(&app, &dt)) {
        sw_sched_advance(&sched, dt, NULL);
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
        } else {
            char line[96];
            snprintf(line, sizeof line, "CLOCK %.1f S  GAME TICK %llu", sched.game_count / (SW_IRQ_HZ / SW_GAME_DIV),
                     (unsigned long long)sched.game_count);
            const char *text[] = { "STAR WARS VP", "ROMS OK", "GAME CORE IN PROGRESS", line,
                                   "--CALIBRATE FOR THE MONITOR" };
            draw_text_lines(app.dvg, text, 5, 2600);
        }

        if (sw_app_end_frame(&app) != 0) {
            status = 1;
            break;
        }
    }
    sw_app_close(&app);
    return status;
}
