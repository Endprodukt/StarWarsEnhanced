/* demo.c - see demo.h. */
#include "platform/demo.h"

#include "core/sched.h"
#include "core/sw_symbols.h"
#include "render/enhance.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_SECONDS 112.0      /* space, the surface, the trench of the Death Star in the middle, its exhaust port */

static uint8_t ram(const sw_machine *m, uint16_t a) { return m->ram[(a - 0x4800) & 0x7ff]; }

static void start(sw_demo *d, sw_machine *m)
{
    d->saved = (sw_machine *)malloc(sizeof *d->saved);
    if (!d->saved) return;
    *d->saved = *m;
    d->active = 1;
    d->irq = 0;
    d->parts = 0;
    d->begun = 0;
    d->view = d->first_view;
    if (ram(m, SW_SSCRDT) == 0) m->ram[SW_SSCRDT - 0x4800] = 1;     /* a credit, for the demo only */
}

void sw_demo_stop(sw_demo *d, sw_machine *m)
{
    if (!d->active) return;
    *m = *d->saved;             /* as it was: the attract mode going on to the high scores */
    free(d->saved);
    d->saved = NULL;
    d->active = 0;
    d->ended = 1;
    d->first_view = (d->first_view + 1) % SW_VIEWS;
    /* the demo's sound stops: the board is reset, as by request 00 */
    m->sound_out = 0;
    m->sound_out_full = 1;
}

void sw_demo_tick(sw_demo *d, sw_machine *m)
{
    uint8_t phase = ram(m, SW_PHASE);
    if (!d->active) {
        /* from the page of scores to the high scores, nobody having paid
         * (in free play the attract mode always holds its credit) */
        int free_play = (m->nvram[(SW_OPTS0 + 1) & 0xff] & 0x03) == 0;
        int paid = ram(m, SW_SSCRDT) != 0 && !free_play;
        if (d->enabled && !paid && (phase == SW_PHSHIS || phase == SW_PHSHIS + 1) && d->last_phase == SW_PHSSCR + 1)
            start(d, m);
        d->last_phase = phase;
        if (!d->active) return;
    }
    /* (the attract mode's pages: before the game has begun, or after it is
     * over; choosing the Death Star comes in between, numbered lower) */
    int attract = phase >= SW_PHSBNR && phase <= SW_PHSHIS + 1;
    if (!attract) d->begun = 1;
    if (d->interrupt || d->irq > (long)(DEMO_SECONDS * SW_IRQ_HZ) || (d->begun && attract) ||
        (!d->begun && d->irq > (long)(15.0 * SW_IRQ_HZ))) {
        d->interrupt = 0;
        sw_demo_stop(d, m);
        d->last_phase = ram(m, SW_PHASE);
        return;
    }
    d->irq++;

    /* what the demo's player does */
    long i = d->irq;
    uint8_t in0 = 0xff;
    m->in.adc[0] = m->in.adc[1] = 0x80;
    /* fire, to start: again and again until the game has begun (a push in
     * the frame that sets a page up is not looked at) */
    if (!d->begun && i % 60 >= 20 && i % 60 < 50) in0 &= (uint8_t)~SW_IN0_LFIRE;
    if (phase == SW_PHSSDS + 1 && i > 120) {
        /* the Death Star in the middle (wave 3: towers on its surface) */
        m->in.adc[1] = 0x80;
        m->in.adc[0] = 0x4c;
        if (i % 60 < 20) in0 &= (uint8_t)~SW_IN0_LFIRE;
    } else if (phase >= SW_PHSSP1) {
        /* the yoke weaving, the trigger pulled again and again */
        double t = (double)i / SW_IRQ_HZ;
        m->in.adc[1] = (uint8_t)(128.0 + 62.0 * sin(t * 1.7) + 18.0 * sin(t * 4.1));
        m->in.adc[0] = (uint8_t)(128.0 + 38.0 * sin(t * 1.1 + 1.0) + 12.0 * sin(t * 3.3));
        if (i % 70 < 22) in0 &= (uint8_t)~SW_IN0_LFIRE;
    }
    m->in.in0 = in0;
    m->in.in1 = 0xff;
    if (ram(m, SW_S_GAS) < 6) m->ram[SW_S_GAS - 0x4800] = 6;          /* the shields never run out */

    /* a new part of the flight, a new view */
    if (phase != d->last_phase && (phase == SW_PHSSP1 + 1 || phase == SW_PHSGD + 1 || phase == SW_PHSBS + 1)) {
        d->view = (d->first_view + d->parts) % SW_VIEWS;
        d->parts++;
    }
    d->last_phase = phase;
}
