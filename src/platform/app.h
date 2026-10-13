/* app.h - the program's outer loop on the VectorPie SDK.
 *
 * vp_run is not used: it hands draw() no position between updates and
 * runs updates at a whole number of Hz, while this game needs 246.09 Hz
 * IRQs and interpolated frames. This loop keeps vp_run's conventions
 * (output chosen the same way, --dvg/--window/--windowed/--vertical,
 * ui_cancel and Ctrl+C stop it, the marquee on the HDMI screen while the
 * board draws, VP_STATS=1) and adds what the game needs: a real-time clock
 * for sw_sched and frame pacing at any rate.
 */
#ifndef SWVP_APP_H
#define SWVP_APP_H

#include <stdint.h>

#include <vectorpie/vp.h>

typedef struct sw_app {
    const char *title;
    vp_dvg     *dvg;
    vp_input   *in;
    int         on_board;     /* drawing on the vector board */
    double      frame_hz;     /* frames sent per second */
    int         quit;
    int         stats;        /* VP_STATS=1 */

    /* internal */
    uint64_t    perf_freq;
    uint64_t    last_clock;
    double      next_frame;   /* seconds, on the app clock */
    double      clock;        /* seconds since open */
    double      last_poll;    /* the clock at the last sw_app_poll */
    uint64_t    frame_t0;
    double      stat_since;
    int         stat_frames;
    double      stat_sum[5], stat_peak[5];
} sw_app;

/* Options understood (others are left to the program): --dvg [PORT],
 * --window, --windowed, --vertical, --hz N (frame rate; default 60 in a
 * window, board_hz on the board). Returns 0, or -1 with a message printed. */
int  sw_app_open(sw_app *a, const char *title, int argc, char **argv, double board_hz);
void sw_app_close(sw_app *a);

/* Read the controls; false when the program should stop. Returns the real
 * seconds elapsed since the previous call in *dt. */
int  sw_app_poll(sw_app *a, double *dt);

void sw_app_begin_frame(sw_app *a);
/* Send the frame, then sleep until the next one is due. -1 if the board
 * failed (the program should stop). */
int  sw_app_end_frame(sw_app *a);

/* The value of an option "--name VALUE" in argv, or NULL. */
const char *sw_arg(int argc, char **argv, const char *name);
int         sw_flag(int argc, char **argv, const char *name);

#endif
