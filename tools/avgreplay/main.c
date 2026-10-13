/* avgreplay - draw display lists recorded from MAME on the vector board.
 *
 *   avgreplay attract.swcap [--lerp] [--loop] [--hz 60] [--config swenhanced.ini]
 *            [--dvg [PORT] | --window | --windowed]
 *
 * Ticks play at the original 20.51 Hz. Each recorded list runs through the
 * AVG interpreter and the display settings, so this shows the real game's
 * pictures exactly as the native renderer will map them: the place to tune
 * swenhanced.ini on the WG6100 before any game code exists.
 *
 * --lerp blends consecutive lists whose lines match one for one (same count
 * and colors): a first look at what interpolation does on the tube. Lists
 * that do not match are shown as recorded.
 *
 * Controls: button 1 pauses, right steps one tick while paused, ui_cancel
 * quits.
 */
#include "avg/avg.h"
#include "capture/capture.h"
#include "core/sched.h"
#include "platform/app.h"
#include "render/config.h"
#include "render/dvgmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct replay {
    swcap_reader rd;
    swcap_tick   tick;
    uint8_t      space[SW_AVG_SPACE_SIZE];
    sw_avg_list  prev, cur;
    int          have_prev, have_cur;
    int          loop;
    int          ended;
    int          paused;
    int          step;
    uint32_t     shown;
} replay;

static int load_next(replay *r)
{
    int got = swcap_next(&r->rd, &r->tick);
    if (got == 0 && r->loop && swcap_rewind(&r->rd) == 0) got = swcap_next(&r->rd, &r->tick);
    if (got != 1) {
        if (got < 0) fprintf(stderr, "avgreplay: damaged capture file\n");
        r->ended = 1;
        return 0;
    }
    swcap_space(&r->rd, &r->tick, r->space);

    sw_avg_list t = r->prev;
    r->prev = r->cur;
    r->cur = t;
    r->have_prev = r->have_cur;
    int st = sw_avg_run(r->space, r->tick.buffer, &r->cur, 0);
    if (st != SW_AVG_OK)
        fprintf(stderr, "avgreplay: tick %u: display list %s\n", r->tick.frame,
                st == SW_AVG_LIMIT ? "did not halt" : "left the VG space");
    r->have_cur = 1;
    r->shown++;
    return 1;
}

static void on_game_tick(void *user)
{
    replay *r = (replay *)user;
    if (r->ended) return;
    if (r->paused && !r->step) return;
    r->step = 0;
    load_next(r);
}

static int lists_match(const sw_avg_list *a, const sw_avg_list *b)
{
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++)
        if (a->lines[i].color != b->lines[i].color || a->lines[i].zz != b->lines[i].zz) return 0;
    return 1;
}

static void draw(sw_app *app, const sw_display_cfg *cfg, replay *r, int lerp, double alpha)
{
    if (!r->have_cur) return;
    if (lerp && r->have_prev && lists_match(&r->prev, &r->cur)) {
        /* Show the time between the two ticks: one tick behind, smooth. */
        static sw_line *mix;
        static int mix_cap;
        if (mix_cap < r->cur.count) {
            mix_cap = r->cur.count;
            mix = (sw_line *)realloc(mix, (size_t)mix_cap * sizeof *mix);
        }
        float a = (float)alpha;
        for (int i = 0; i < r->cur.count; i++) {
            const sw_line *p = &r->prev.lines[i], *c = &r->cur.lines[i];
            mix[i] = *c;
            mix[i].x0 = p->x0 + (c->x0 - p->x0) * a;
            mix[i].y0 = p->y0 + (c->y0 - p->y0) * a;
            mix[i].x1 = p->x1 + (c->x1 - p->x1) * a;
            mix[i].y1 = p->y1 + (c->y1 - p->y1) * a;
            mix[i].intensity = (uint8_t)(p->intensity + (c->intensity - p->intensity) * a);
        }
        sw_draw_lines(app->dvg, cfg, mix, r->cur.count);
    } else {
        sw_draw_lines(app->dvg, cfg, r->cur.lines, r->cur.count);
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            /* skip values of options that take one */
            if (i > 1 && (!strcmp(argv[i - 1], "--hz") || !strcmp(argv[i - 1], "--config") ||
                          !strcmp(argv[i - 1], "--dvg")))
                continue;
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr, "usage: avgreplay CAPTURE.swcap [--lerp] [--loop] [--hz N] [--config INI]\n"
                        "                 [--dvg [PORT] | --window | --windowed]\n");
        return 1;
    }

    static replay r;
    memset(&r, 0, sizeof r);
    if (swcap_open(&r.rd, path) != 0) {
        fprintf(stderr, "avgreplay: %s is not a capture file\n", path);
        return 1;
    }
    r.loop = sw_flag(argc, argv, "--loop");
    int lerp = sw_flag(argc, argv, "--lerp");

    sw_display_cfg cfg;
    sw_display_cfg_defaults(&cfg);
    const char *cfg_path = sw_arg(argc, argv, "--config");
    char buf[1024];
    if (!cfg_path) cfg_path = vp_asset_path("swenhanced.ini", buf, sizeof buf);
    if (sw_display_cfg_load(&cfg, cfg_path) == 0) printf("avgreplay: settings from %s\n", cfg_path);

    sw_app app;
    if (sw_app_open(&app, "avgreplay", argc, argv, 41.0) != 0) return 1;

    if (!load_next(&r)) {
        fprintf(stderr, "avgreplay: no ticks in %s\n", path);
        sw_app_close(&app);
        return 1;
    }
    if (!r.rd.have_vrom) fprintf(stderr, "avgreplay: no vector ROM in the capture\n");

    sw_sched sched;
    sw_sched_init(&sched);
    sw_sched_hooks hooks = { NULL, NULL, on_game_tick, &r };

    int status = 0;
    double dt;
    while (sw_app_poll(&app, &dt) && !r.ended) {
        if (vp_input_pressed(app.in, "p1_button1")) r.paused = !r.paused;
        if (r.paused && vp_input_pressed(app.in, "p1_right")) r.step = 1;
        sw_sched_advance(&sched, dt, &hooks);

        sw_app_begin_frame(&app);
        draw(&app, &cfg, &r, lerp, r.paused ? 1.0 : sw_sched_game_alpha(&sched));
        if (sw_app_end_frame(&app) != 0) {
            status = 1;
            break;
        }
    }
    printf("avgreplay: %u ticks shown\n", r.shown);
    sw_avg_list_free(&r.prev);
    sw_avg_list_free(&r.cur);
    swcap_close(&r.rd);
    sw_app_close(&app);
    return status;
}
