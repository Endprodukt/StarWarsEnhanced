#include "platform/app.h"

#include <SDL.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t g_signal = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_signal = 1;
}

const char *sw_arg(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return NULL;
}

int sw_flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], name) == 0) return 1;
    return 0;
}

static double now_s(sw_app *a)
{
    uint64_t c = SDL_GetPerformanceCounter();
    a->clock += (double)(c - a->last_clock) / (double)a->perf_freq;
    a->last_clock = c;
    return a->clock;
}

int sw_app_open(sw_app *a, const char *title, int argc, char **argv, double board_hz)
{
    memset(a, 0, sizeof *a);
    a->title = title;

    vp_dvg_options o;
    const char *reason = "";
    int forced = 0, fullscreen = 0;
    const char *port = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dvg") == 0) {
            forced = VP_DVG_OUTPUT_BOARD;
            if (i + 1 < argc && argv[i + 1][0] != '-') port = argv[++i];
        } else if (strcmp(argv[i], "--window") == 0) {
            forced = VP_DVG_OUTPUT_WINDOW;
            fullscreen = 1;
        } else if (strcmp(argv[i], "--windowed") == 0) {
            forced = VP_DVG_OUTPUT_WINDOW;
            fullscreen = 0;
        }
    }
    if (forced) {
        memset(&o, 0, sizeof o);
        o.output = (vp_dvg_output)forced;
        o.port = port;
        o.fullscreen = fullscreen;
        o.window_title = title;
        reason = "command line";
    } else {
        vp_dvg_auto_options(&o, title, &reason);
    }
    if (sw_flag(argc, argv, "--vertical")) o.vertical = 1;

    if (vp_dvg_open_ex(&o, &a->dvg) != VP_OK) {
        fprintf(stderr, "%s: cannot open the output: %s\n", title, vp_last_error());
        return -1;
    }
    if (vp_input_open(&a->in) != VP_OK) {
        fprintf(stderr, "%s: cannot open the controls: %s\n", title, vp_last_error());
        vp_dvg_close(a->dvg);
        a->dvg = NULL;
        return -1;
    }
    a->on_board = vp_dvg_get_output(a->dvg) == VP_DVG_OUTPUT_BOARD;

    const char *hz = sw_arg(argc, argv, "--hz");
    a->frame_hz = hz ? atof(hz) : (a->on_board ? board_hz : 60.0);
    if (a->frame_hz < 10.0) a->frame_hz = 10.0;
    if (a->frame_hz > 240.0) a->frame_hz = 240.0;

    if (a->on_board) {
        char path[1024];
        vp_marquee_show(vp_asset_path("marquee.png", path, sizeof path));
    }

    const char *st = getenv("VP_STATS");
    a->stats = st && *st && strcmp(st, "0") != 0;

    printf("%s: VectorPie SDK %s, %s%s%s (%s), %.1f Hz frames, %d joystick(s)\n", title,
           vp_version(), vp_hardware_name(vp_dvg_hardware(a->dvg)), a->on_board ? " on " : "",
           vp_dvg_port(a->dvg), reason, a->frame_hz, vp_input_joystick_count(a->in));
    fflush(stdout);

    /* After SDL is up, so these replace SDL's own handlers. */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    a->perf_freq = SDL_GetPerformanceFrequency();
    a->last_clock = SDL_GetPerformanceCounter();
    a->next_frame = 0.0;
    return 0;
}

void sw_app_close(sw_app *a)
{
    vp_lamps_close();
    if (a->on_board) vp_marquee_clear();
    vp_input_close(a->in);
    vp_dvg_close(a->dvg);
    a->in = NULL;
    a->dvg = NULL;
}

int sw_app_poll(sw_app *a, double *dt)
{
    double before = a->clock;
    double t = now_s(a);
    *dt = t - before;

    vp_input_update(a->in);     /* pumps SDL's events */
    if (vp_dvg_window_closed(a->dvg) || g_signal) a->quit = 1;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    if (vp_input_pressed(a->in, "ui_cancel")) a->quit = 1;
    return !a->quit;
}

void sw_app_begin_frame(sw_app *a)
{
    a->frame_t0 = SDL_GetPerformanceCounter();
    vp_dvg_begin_frame(a->dvg);
}

static void report_stats(sw_app *a)
{
    vp_dvg_stats s;
    vp_dvg_get_stats(a->dvg, &s);
    double draw_ms = (double)(SDL_GetPerformanceCounter() - a->frame_t0) * 1000.0 / (double)a->perf_freq;
    const double v[5] = { (double)s.vectors, (double)s.blank_moves, (double)s.lit_length,
                          (double)s.blank_length, draw_ms };
    for (int i = 0; i < 5; i++) {
        a->stat_sum[i] += v[i];
        if (v[i] > a->stat_peak[i]) a->stat_peak[i] = v[i];
    }
    a->stat_frames++;
    double t = a->clock;
    if (t - a->stat_since >= 1.0) {
        double n = a->stat_frames;
        printf("%s stats: %.1f fps | avg/peak: vectors %.0f/%.0f, moves %.0f/%.0f, "
               "lit %.0f/%.0f, blank %.0f/%.0f, frame %.1f/%.1f ms\n",
               a->title, n / (t - a->stat_since),
               a->stat_sum[0] / n, a->stat_peak[0], a->stat_sum[1] / n, a->stat_peak[1],
               a->stat_sum[2] / n, a->stat_peak[2], a->stat_sum[3] / n, a->stat_peak[3],
               a->stat_sum[4] / n, a->stat_peak[4]);
        fflush(stdout);
        memset(a->stat_sum, 0, sizeof a->stat_sum);
        memset(a->stat_peak, 0, sizeof a->stat_peak);
        a->stat_frames = 0;
        a->stat_since = t;
    }
}

int sw_app_end_frame(sw_app *a)
{
    if (vp_dvg_end_frame(a->dvg, 0) != VP_OK) {
        fprintf(stderr, "%s: frame not sent: %s\n", a->title, vp_last_error());
        return -1;
    }
    double t = now_s(a);
    if (a->stats) report_stats(a);

    a->next_frame += 1.0 / a->frame_hz;
    if (a->next_frame > t) {
        double wait = a->next_frame - t;
        if (wait > 0.0005) SDL_Delay((Uint32)(wait * 1000.0));
    } else {
        a->next_frame = t;      /* running late: do not try to catch up frames */
    }
    return 0;
}
