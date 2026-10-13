/* subframe.h - the picture between two frames of the game, computed.
 *
 * The game moves everything once a frame (12 to 20 times a second) and
 * draws it the frame after. Here the state the game is in before a frame
 * and after it are kept, and for any time in between the things that move
 * (the player's ship, the others, shots, pieces, stars) are put where they
 * are on their way, and the frame's own view routines draw the picture of
 * that moment: with the right perspective, and with what comes into view
 * coming into view. The game itself does not notice: its state is not
 * touched, the drawing is done on a copy.
 *
 * At the frame's own time (t = 0) the picture is the frame's display list,
 * word for word; at t = 1 it is the one the game draws next.
 */
#ifndef SWVP_SUBFRAME_H
#define SWVP_SUBFRAME_H

#include "core/machine.h"

typedef struct sw_subframe {
    sw_machine a;       /* the machine as the frame's phase found it */
    sw_machine b;       /* and as it left it */
    int        valid;   /* the phase is one with a view of its own (sw_main_view) */
} sw_subframe;

/* For sw_machine.frame_hook (user: the sw_subframe to fill, m: its machine). */
void sw_subframe_before(sw_subframe *s, const sw_machine *m);
void sw_subframe_after(sw_subframe *s, const sw_machine *m);

/* The picture at t (0: the frame's own, 1: the next one's) into `scratch`:
 * its VG RAM then holds the display list, started from address 0 as on the
 * board. `live` is the machine now: the sight and what else the interrupt
 * animates is taken from there. Returns 0 if the frame has no view to draw
 * with (nothing is done then). */
int sw_subframe_render(const sw_subframe *s, const sw_machine *live, float t, sw_machine *scratch);

/* The picture the game will draw next, a frame ahead of it: its next frame
 * is run on `scratch` from where this one left the machine. For the
 * phases without a view of their own (the pages of the attract mode count
 * and draw in one): their lines are then moved from this frame's picture
 * to the next one's (render/smooth.h), which the game has not shown yet.
 * The display list is in scratch's VG RAM as above. */
void sw_subframe_next(const sw_subframe *s, sw_machine *scratch);

#endif
