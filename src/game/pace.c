/* pace.c - how long the 6809 took over a frame.
 *
 * The original is not a 20.5 Hz game that sometimes drops a frame: its
 * main line needs 25 to 50 ms for a frame of the attract mode, the vector
 * generator up to 40 ms for a picture, and a frame can only start when
 * both the 12th interrupt has come and the interrupt has handed over the
 * VG buffer of the picture before. The speed of everything on the screen
 * follows from that, so the native game has to wait as the 6809 did.
 *
 * The native code is done at once, so the time is modelled: the routines
 * count the work they do (sw_machine.n_xxx) and each kind of work has a
 * cost. The costs were fitted to MAME: tools/mame/swcapture.lua records
 * how long the main line took over each frame (TIME), tools/swcompare
 * --csv writes that next to the native counts of the same frame. Over the
 * 3000 frames of the attract mode the model is within 1 ms (rms) of the
 * 25..50 ms measured. Phases ported later add their own counts and must be
 * fitted again (docs/PLAN.md).
 */
#include "game/game.h"

/* Costs in cycles of the 12.096 MHz master clock (12.096 per microsecond). */
#define COST_CHAR    568u    /* a character of a message: 47 us */
#define COST_DIGIT   898u    /* a numeral: 74 us */
#define COST_MESS    913u    /* a message's color and position: 75 us */
#define COST_STAR   5517u    /* a star transformed and queued for display: 456 us */
#define COST_RENEW  5051u    /* a star replaced by a new one: 418 us */

uint32_t sw_pace_frame_cycles(sw_machine *m)
{
    uint32_t c = m->n_chars * COST_CHAR + m->n_digits * COST_DIGIT + m->n_mess * COST_MESS +
                 m->n_shown * COST_STAR + m->n_renew * COST_RENEW;
    m->n_chars = m->n_digits = m->n_mess = m->n_mbox = m->n_shown = m->n_renew = 0;
    return c;
}
