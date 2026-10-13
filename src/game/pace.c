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

/* The game itself (phases from SP1 on): the routines are too many to count
 * each kind of work, so what is counted is what a line-by-line port has in
 * common with the original: the memory it reads (sw_machine.n_rd) and the
 * math box programs it runs and waits for (n_mb, by their PC). To that
 * comes what a phase costs in any case. Fitted to MAME the same way
 * (12,700 frames of five recordings, tools/swcompare --csv): within 0.9 ms
 * (rms) of the 25 to 80 ms measured, and right on average in every phase. */
#define COST_G_CHAR  343u
#define COST_G_DIGIT 313u
#define COST_READ     58u    /* a byte read: 4.8 us */

static const struct { uint8_t pc; uint16_t cost; } MB_COST[] = {
    { 0x00, 1280 }, { 0x0e, 137 }, { 0x2a, 1303 }, { 0x40, 336 }, { 0x50, 1362 },
    { 0x60, 903 }, { 0x67, 1825 }, { 0x80, 336 }, { 0x86, 400 }, { 0xb0, 2321 },
};

/* What a frame of a phase costs before any of the work counted. Phases
 * that last one frame and were too few to fit share the value of the one
 * they lead into. */
static uint32_t phase_base(uint8_t phase)
{
    switch (phase) {
    case SW_PHSSP1:     return 68982;
    case SW_PHSSP1 + 1: return 7121;
    case SW_PHSSP2:     return 37427;
    case SW_PHSSP2 + 1: return 36402;
    case SW_PHSS0G:
    case SW_PHSS0G + 1: return 36631;
    case SW_PHSS1G:
    case SW_PHSS1G + 1: return 24503;
    case SW_PHSGD:
    case SW_PHSGD + 1:
    case SW_PHSG0D:
    case SW_PHSG0D + 1: return 45002;
    case SW_PHSG0B:
    case SW_PHSG0B + 1: return 7604;
    case SW_PHSG1B:     return 84104;
    case SW_PHSG1B + 1: return 73765;
    case SW_PHSBS:      return 82012;
    case SW_PHSBS + 1:  return 85817;
    case SW_PHSNXT:
    case SW_PHSNXT + 1: return 26366;
    case SW_PHSS0D:
    case SW_PHSS0D + 1: return 35865;
    case SW_PHSB0D:
    case SW_PHSB0D + 1: return 81268;
    default:            return 20000;
    }
}

/* PHASE is the phase the frame began in. */
uint32_t sw_pace_frame_cycles(sw_machine *m, uint8_t phase)
{
    uint32_t c;
    if (phase >= SW_PHSSP1) {
        c = phase_base(phase) + m->n_chars * COST_G_CHAR + m->n_digits * COST_G_DIGIT + m->n_rd * COST_READ;
        for (unsigned i = 0; i < sizeof MB_COST / sizeof MB_COST[0]; i++)
            c += (uint32_t)m->n_mb[MB_COST[i].pc] * MB_COST[i].cost;
    } else {
        c = m->n_chars * COST_CHAR + m->n_digits * COST_DIGIT + m->n_mess * COST_MESS +
            m->n_shown * COST_STAR + m->n_renew * COST_RENEW;
    }
    m->n_chars = m->n_digits = m->n_mess = m->n_mbox = m->n_shown = m->n_renew = 0;
    return c;
}
