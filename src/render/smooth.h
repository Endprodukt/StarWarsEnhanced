/* smooth.h - the picture between two frames of the game.
 *
 * The game computes a new picture 12 to 20 times a second and the original
 * showed each one two or three times: that is its judder. Here the monitor
 * gets a new picture every time (50 a second on the board), in which every
 * line is where it is on its way from one game frame to the next. The game
 * itself runs unchanged, at its own pace; only what is shown in between is
 * added.
 *
 * Lines are found again from frame to frame by their id (avg.h,
 * sw_avg_run_tagged); of several with the same id (the stars, the same
 * letter in a text, the same line of two ships) the nearest is taken. A line with no partner in the frame before (a new
 * ship, a new star, another digit) is shown where it is, at once.
 *
 * Two ways, the setting `smooth`:
 *   1  between the last two frames: exact, but the world is shown one game
 *      frame (50 to 90 ms) late
 *   2  ahead of the last frame, at the speed of its last step: no delay,
 *      but a line that turns overshoots until the next frame corrects it
 * The sight is not part of the frames: the interrupt moves it, and it is
 * shown where the yoke has it now.
 */
#ifndef SWVP_SMOOTH_H
#define SWVP_SMOOTH_H

#include "avg/avg.h"

typedef struct sw_smooth_ref { uint64_t id; int index; } sw_smooth_ref;

typedef struct sw_smooth {
    sw_avg_list prev, cur;          /* the last two game frames, as lines */
    double      t_prev, t_cur;      /* when each became the picture (seconds) */
    int         frames;             /* game frames seen */
    int        *match;              /* for each line of cur: its line in prev, or -1 */
    int         match_cap;
    uint8_t    *taken;              /* scratch: lines of prev already paired */
    int         taken_cap;
    void       *pairs;              /* scratch */
    int         pairs_cap;
    unsigned    sight_routine;      /* the vector ROM routine that draws the sight (word address), 0: none */
    sw_smooth_ref *refs[2];         /* scratch: both lists sorted by id */
    int         refs_cap[2];
    sw_line    *out;
    int         out_cap;
    float       sight_key[2];       /* where the sight is in cur */
    float       sight_prev[2], sight_cur[2];    /* at the last two VG starts */
    double      t_sight_prev, t_sight_cur;
} sw_smooth;

void sw_smooth_free(sw_smooth *s);

/* A picture the vector generator was started on at time t: `lines` with
 * ids, new_frame if the game finished a frame since the last one (else the
 * same frame again, with what the interrupt animates moved on), and the
 * sight's position in it (AVG units). */
void sw_smooth_key(sw_smooth *s, const sw_avg_list *lines, double t, int new_frame,
                   float sight_x, float sight_y);

/* The picture for time t. mode: 0 the last picture as it is, 1 and 2 as
 * above. Returns the number of lines; *lines stays valid until the next
 * call. */
int sw_smooth_frame(sw_smooth *s, double t, int mode, const sw_line **lines);

#endif
