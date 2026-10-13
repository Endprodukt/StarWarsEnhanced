/* sndboard.h - the Star Wars sound board.
 *
 * Its own 6809 runs the sound program from the cabinet's ROMs (music,
 * effects, speech data); around it a 6532 RIOT (RAM, timer, the ports to
 * the speech chip), four POKEYs and the TMS5220, and the two latches to
 * the main board. Memory map and wiring as in MAME's starwars.cpp.
 *
 * The board is run in step with the main CPU and makes samples at a fixed
 * rate into a ring; the platform's audio device takes them from there.
 */
#ifndef SWVP_SNDBOARD_H
#define SWVP_SNDBOARD_H

#include "core/machine.h"
#include "cpu/m6809.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"

#define SW_SOUND_RATE 44100
#define SW_SOUND_RING 16384     /* samples: a third of a second */

typedef struct sw_sndboard {
    sw_machine *m;              /* the latches to the main board live there */
    m6809    cpu;
    uint8_t  ram[0x800];        /* 2000-27FF */

    /* the RIOT */
    uint8_t  riot_ram[0x80];
    uint8_t  dra, ddra, drb, ddrb;
    uint32_t timer;             /* CPU cycles until it runs out, then counts on */
    uint32_t timer_div;         /* 1, 8, 64, 1024 */
    uint8_t  timer_value;       /* what a read shows */
    uint8_t  timer_flag, timer_irq_enable;
    uint8_t  pa7_flag, pa7_irq_enable, pa7_edge_rising, pa7_last;
    uint32_t timer_sub;         /* cycles into the current count */
    int      timer_expired;     /* counting down from FF at one a cycle */

    pokey    pokeys[4];
    tms5220  tms;
    uint8_t  tms_bus;           /* what the CPU put on the speech chip's data bus */
    uint32_t tms_phase;         /* clock fraction to the next speech sample */
    int      tms_out;

    /* making samples */
    uint32_t sample_phase;      /* CPU cycles times the rate, to the next sample */
    uint32_t pending_ticks;     /* CPU cycles since the last sample */
    float    pokey_sum;         /* POKEY output times ticks since the last sample */
    uint32_t pokey_done;        /* of pending_ticks, how many the POKEYs have run */
    float    last_pokey;
    float    dc;                /* the standing part of the output, taken out */
    int32_t  budget;            /* CPU cycles to run */
    int16_t  ring[SW_SOUND_RING];
    volatile uint32_t ring_w, ring_r;
    float    volume;
} sw_sndboard;

void sw_sndboard_init(sw_sndboard *s, sw_machine *m);
void sw_sndboard_reset(sw_sndboard *s);

/* Run the sound CPU for `cycles` of the 1.512 MHz clock. */
void sw_sndboard_run(sw_sndboard *s, int cycles);

/* Take up to n samples out of the ring (mono, 16 bit). Returns how many. */
int sw_sndboard_read(sw_sndboard *s, int16_t *out, int n);

#endif
