/* picture.h - the game's picture at any time.
 *
 * The game draws a picture a frame; the monitor wants one whenever it is
 * done with the last. This keeps what is needed to give it the picture of
 * the moment (the setting `smooth`):
 *   3  computed from the state of the game where the phase has a view of
 *      its own (game/subframe.h); elsewhere the lines of this frame's
 *      picture on their way to the next one's, which is computed a frame
 *      ahead
 *   1, 2  the lines of the last pictures shown, moved (render/smooth.h)
 *   0  the last picture as it is
 * adds what the port adds to the game's pictures (render/enhance.h) and
 * cuts away what is behind something else, if asked to (render/solid.h).
 */
#ifndef SWVP_PICTURE_H
#define SWVP_PICTURE_H

#include "avg/avg.h"
#include "core/machine.h"
#include "game/subframe.h"
#include "render/smooth.h"
#include "render/solid.h"
#include "render/enhance.h"

typedef struct sw_picture {
    sw_machine *m;
    double      t;                          /* the game's clock at the interrupt being run (seconds) */
    uint32_t    frames_before;              /* game frames run before it */
    uint8_t     space[SW_AVG_SPACE_SIZE];   /* what the vector generator reads */
    sw_avg_list list;                       /* the picture of the last VG restart */
    uint32_t    vg_seen;
    uint8_t     vg_buffer;                  /* which buffer that picture was in */
    sw_smooth   smooth;                     /* the pictures shown */
    /* The last two frames the C routines ran, the one on display, when it
     * came up and how long a frame lasts. */
    sw_subframe pair[2];
    sw_subframe shown;
    double      shown_at, frame_time;
    sw_smooth   ahead;                      /* this frame's picture and the next one's, computed ahead */
    sw_avg_list nextlist, sublist;
    sw_machine  scratch;
    sw_machine  next;                       /* the game as it is in the next frame's picture (computed ahead) */
    int         occlusion;                  /* 1: what is behind something is not drawn (render/solid.h) */
    sw_solid    solid;
    sw_enhance  enhance;                    /* what the port adds to the picture (its settings are set by the caller) */
    uint8_t     subspace[SW_AVG_SPACE_SIZE];
} sw_picture;

/* Zero the struct first. Takes the machine's frame_hook. */
void sw_picture_init(sw_picture *p, sw_machine *m);

/* Around every interrupt (and what the main line does after it): t is the
 * game's clock then. */
void sw_picture_irq_begin(sw_picture *p, double t);
void sw_picture_irq_end(sw_picture *p);

/* The picture at the time now. Returns the number of lines; *lines stays
 * valid until the next call. */
int sw_picture_lines(sw_picture *p, double now, int smooth, const sw_line **lines);

#endif
