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
#ifndef SWE_SNDBOARD_H
#define SWE_SNDBOARD_H

#include "core/machine.h"
#include "cpu/m6809.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"
#include "sound/samples.h"

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
    float    dc;                /* the standing part of the POKEYs' output, taken out */
    float    dc_speech;         /* and of the speech chip's */
    int32_t  budget;            /* CPU cycles to run */
    int16_t  ring[SW_SOUND_RING][2];        /* frames: left, right */
    volatile uint32_t ring_w, ring_r;
    float    volume;

    /* 1: the board's program is the C one (sndprog.c) and the 6809 is not
     * run. Set before sw_sndboard_reset. */
    int      native;
    int32_t  late_cycles;       /* how long after its first call of the speech driver a pass makes the second */
    int32_t  late;              /* CPU cycles until the pass of the main line ends (sw_sndprog_late), 0: it has */
    /* Recordings that replace sounds of the board (sound/samples.h), NULL:
     * none. Only the program in C asks them (native). */
    sw_samples *samples;
    /* For checking one against the other: called for every byte written to
     * a POKEY (kind 0, reg: 00-3F as on the bus), to the speech chip (kind
     * 1) and to the main board's latch (kind 2). */
    void   (*trace)(void *user, int kind, unsigned reg, uint8_t v);
    void    *trace_user;
} sw_sndboard;

void sw_sndboard_init(sw_sndboard *s, sw_machine *m);
void sw_sndboard_reset(sw_sndboard *s);

/* Silence at once: the board reset, and nothing it was playing let die
 * away (the recordings stopped dead, the music without its release, the
 * engine with them). For a quiet demo flight's end (main.c): what it played
 * at volume 0 is not to be heard in the attract mode after it. */
void sw_sndboard_hush(sw_sndboard *s);

/* The board's bus, for the program in C. */
uint8_t sw_snd_rd(sw_sndboard *s, uint16_t a);
void    sw_snd_wr(sw_sndboard *s, uint16_t a, uint8_t v);

/* sndprog.c: the reset, a request waiting in the latch of the main board,
 * and the main line's pass every 4 ms. */
void sw_sndprog_init(sw_sndboard *s);
void sw_sndprog_command(sw_sndboard *s);
void sw_sndprog_tick(sw_sndboard *s);
void sw_sndprog_late(sw_sndboard *s);

/* Run the sound CPU for `cycles` of the 1.512 MHz clock. */
void sw_sndboard_run(sw_sndboard *s, int cycles);

/* Take up to n samples out of the ring, mono (its two sides together), 16
 * bit. Returns how many. */
int sw_sndboard_read(sw_sndboard *s, int16_t *out, int n);

/* The same in stereo: up to n frames, left and right interleaved. The
 * board's own sound is in the middle, the recordings as they are. */
int sw_sndboard_read2(sw_sndboard *s, int16_t *out, int n);

#endif
