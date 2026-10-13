/* machine.h - the Star Wars main board, as the native game code sees it.
 *
 * The port keeps the original memory layout: program RAM at 4800-4FFF,
 * math box RAM at 5000-5FFF, VG RAM at 0000-2FFF, every variable at the
 * address WSROOT.MAP gives it (core/sw_symbols.h). Game state can then be
 * compared byte for byte with MAME recordings, and the 1983 source reads
 * one to one against the C.
 *
 * The I/O registers keep their meaning (SW5.DOC / SWMAP.DOC). The math box
 * finishes at once, which the original code cannot tell apart from a fast
 * board. The vector generator takes its time over a picture, as on the
 * board (avg/avg.h, sw_avg_cycles): the game waits for it, and that sets
 * the speed of the game when the screen is full.
 */
#ifndef SWVP_MACHINE_H
#define SWVP_MACHINE_H

#include <stdint.h>

#include "avg/avg.h"
#include "core/sw_symbols.h"
#include "core/tags.h"
#include "mbox/mbox.h"
#include "rom/romset.h"

/* Inputs as the board's switches present them: active LOW where SW5.DOC
 * says (not). */
typedef struct sw_inputs {
    uint8_t in0;      /* 4300: D7 left fire/start, D6 right fire/start, D5 spare,
                       * D4 self-test, D3 slam, D2 coin aux, D1 coin L, D0 coin R */
    uint8_t in1;      /* 4320: D5 left thumb, D4 right thumb, D3 spare, D2 diag
                       * (D7 math run, D6 VG halted are supplied by the board) */
    uint8_t opt0;     /* 4340: option switches 10D */
    uint8_t opt1;     /* 4360: option switches 10E/F */
    uint8_t adc[4];   /* A/D channels: 0 pitch (yoke up/down), 1 yaw (left/right) */
} sw_inputs;

#define SW_IN0_LFIRE  0x80
#define SW_IN0_RFIRE  0x40
#define SW_IN0_TEST   0x10
#define SW_IN0_SLAM   0x08
#define SW_IN0_COINA  0x04
#define SW_IN0_COINL  0x02
#define SW_IN0_COINR  0x01
#define SW_IN1_LTHUMB 0x20
#define SW_IN1_RTHUMB 0x10
#define SW_IN1_DIAG   0x04

/* Where random numbers come from (the board's 23-bit LFSR runs at 3 MHz, so
 * what a read sees depends on the CPU's timing). NULL: an LFSR stepped a
 * varying amount per read. Recordings can inject MAME's values. */
typedef uint8_t (*sw_prng_fn)(void *user);

/* A note next to a word of VG RAM (see sw_machine.vgnote). */
typedef sw_avg_note sw_vgnote;

typedef struct sw_machine {
    const sw_romset *rom;
    uint8_t   ram[0x800];        /* 4800-4FFF */
    uint8_t   vgram[0x3000];     /* 0000-2FFF */
    uint8_t   nvram[0x100];      /* 4500-45FF, 4 bits each (X2212): its RAM */
    uint8_t   nvstore[0x100];    /* the X2212's EEPROM: RECALL copies it to the RAM, STORE back */
    uint32_t  nv_stores;         /* STOREs done: when it changes the EEPROM wants saving */
    sw_mbox   mbox;              /* 5000-5FFF and the matrix processor */
    sw_divider div;
    sw_inputs in;

    uint8_t   adc_channel;
    uint8_t   latch[8];          /* 4680-4687, D7: coin counters, LEDs, bank, ... */
    uint8_t   sound_out;         /* MAINWR, the last byte for the sound board */
    uint8_t   sound_out_full;
    uint8_t   sound_in;          /* MAINREAD */
    uint8_t   sound_in_full;
    uint32_t  vg_starts;         /* VGGO writes: a new picture for the renderer */
    uint32_t  vg_left;           /* master clock cycles until the VG halts; 0: halted */

    sw_prng_fn prng;
    void      *prng_user;
    uint32_t   lfsr;

    /* The main line (game/mainline.c) */
    uint16_t   y;                /* the 6809's Y: where the display list is being written */
    uint8_t    main_state;       /* SW_MAIN_xxx */
    uint8_t    in_irq;           /* inside sw_irq (who is reading the PRNG) */
    uint8_t    hold_frames;      /* game frames the main line still waits inside a routine (the NOVRAM test) */
    /* Option steady_frames (game/pace.c). 0: a frame takes as long as the
     * 6809 took over it. 1: the frames of a phase come at one fixed pace,
     * the average of the arcade machine in that phase. 2: a frame every
     * 12th interrupt in every phase. */
    /* How much the laser's hit test takes off a building's 1/X for the
     * arcade machine's linear scale with the distance (GRLZCL: 100h, the
     * scale 1 - XP/4000h). The enhanced towers may be sized with a weaker
     * scale (render/enhance.h, tower_size): then less, so that the hit test
     * meets what is drawn. */
    uint16_t   tower_adjust;
    /* For the renderer: the phase whose view drew the list now in VG RAM
     * (set when a view begins). Between a phase's set-up and its first
     * view the phase byte is already the new one while the list, and the
     * viewer's unit vectors, are still the old view's. */
    uint8_t    view_phase;
    uint8_t    steady;
    uint32_t   irqs;             /* interrupts run */
    uint32_t   steady_due;       /* when the next frame is due, in sixteenths of an interrupt */
    uint8_t    frame_irqs;       /* interrupts from the frame just run to the next one */
    uint8_t    yoke_exact;       /* option yoke_autocenter: the yoke passed through exactly */

    /* The phases not ported yet: a way to run the original's code for one
     * frame (core/emu.h, sw_emu_attach). It returns the time the 6809 took,
     * in master clock cycles. original_suspended: the code stopped in a
     * WAITFRAME of its own and goes on from there next frame. */
    uint32_t (*original_frame)(void *user);
    void      *original_user;
    /* A routine of a phase that is ported except for it: the original's
     * subroutine at addr is run to its RTS, with the registers below going
     * in and coming back (Y is m->y). Its time is added to original_cycles.
     * NULL without the ROM's code at hand (sw_emu_attach sets it). */
    void     (*original_call)(void *user, uint16_t addr);
    uint8_t    ra, rb, rcc;      /* A, B and the condition codes */
    uint16_t   rx, ru;           /* X, U */
    /* Checking against a recording: reads of RAM pass through here. */
    uint8_t  (*ram_read_hook)(void *user, uint16_t addr, uint8_t value);
    void      *ram_read_user;
    uint32_t   original_cycles;
    uint8_t    original_suspended;
    uint8_t    sound_board;      /* a sound board takes the requests (else they are dropped) */
    void     (*sound_wait)(void *user, int cycles);    /* let the sound board run that long */
    void      *sound_user;
    /* Called by the main line around a frame's phase (after: 0 before it
     * runs, 1 when it has): for the pictures in between (game/subframe.h). */
    void     (*frame_hook)(void *user, int after);
    void      *frame_user;
    uint8_t    unported_phase;   /* the last phase asked for that is not ported yet */
    uint32_t   frames;           /* game frames run */

    /* For the renderer only (core/tags.h): what each word of VG RAM belongs
     * to, the tag being written now, and which star each entry of the
     * star queue is. */
    uint16_t   vgtag[0x1800];
    uint8_t    vgstart[0x1800];  /* 1: the word begins a new piece (a new call of the routine that writes it) */
    uint16_t   tag;
    /* Also for the renderer only (render/solid.h): how far away the point a
     * word draws to is (the divider's quotient, 1/X: larger is nearer; 0:
     * not known), and for the lines of a 3-D object which object it is
     * (model: its number + 1), which one drawn (inst) and which of its
     * points (pt: number + 1). */
    sw_vgnote  vgnote[0x1800];
    sw_vgnote  note;             /* what is noted for the words written now */
    uint16_t   pt_w[64];         /* 1/X of the points of the object projected last */
    int16_t    pt_v[64][3];      /* and, if pt_d3, where they are as the player sees them in space */
    uint8_t    pt_d3;
    uint16_t   inst_n;           /* objects drawn so far */
    uint8_t    star_gen[64];     /* how often star n was replaced */
    uint8_t    star_of[64];      /* the star of the nth queued entry */
    /* Work done, counted for the pace of the game (game/pace.c): characters,
     * numerals and messages put out, math box runs, stars shown. */
    uint32_t   n_chars, n_digits, n_mess, n_mbox, n_shown, n_renew;
    /* The same for all of the game: memory read and written, and the math
     * box programs run, by their PC. */
    uint32_t   n_rd, n_wr;
    uint32_t   n_rd_iframe;      /* n_rd when IFRAME was through (interrupts read too, until the phase runs) */
    uint16_t   n_mb[256];
    uint32_t   busy;             /* master clock cycles the 6809 still works on the frame */
    uint8_t    held_buffer;      /* VGSYNC while it does: the buffer it writes */
    uint8_t    snd_q[64];        /* requests sent to the sound board, a ring */
    uint32_t   snd_w;
} sw_machine;

enum { SW_MAIN_WAITFRAME, SW_MAIN_WAITVG, SW_MAIN_RUN, SW_MAIN_BUSY, SW_MAIN_BUSY_NOSYNC };

/* Master clock cycles from one interrupt to the next (12.096 MHz / 246.09 Hz). */
#define SW_IRQ_CYCLES 49152u

void sw_machine_init(sw_machine *m, const sw_romset *rom);

/* Bus access by 6809 address. */
uint8_t  sw_rd8(sw_machine *m, uint16_t addr);
void     sw_wr8(sw_machine *m, uint16_t addr, uint8_t v);
uint16_t sw_rd16(sw_machine *m, uint16_t addr);      /* big-endian */
void     sw_wr16(sw_machine *m, uint16_t addr, uint16_t v);

/* Program RAM directly (4800-4FFF), for the hot paths. */
static inline uint8_t *sw_ram(sw_machine *m, uint16_t addr) { return &m->ram[(addr - 0x4800) & 0x7ff]; }
#define RAM8(m, sym)        (*sw_ram((m), (sym)))
#define RAM16(m, sym)       ((uint16_t)((RAM8(m, sym) << 8) | RAM8(m, (sym) + 1)))
#define SETRAM16(m, sym, v) do { uint16_t v_ = (uint16_t)(v); RAM8(m, sym) = (uint8_t)(v_ >> 8); RAM8(m, (sym) + 1) = (uint8_t)v_; } while (0)

/* VG RAM words (big-endian, as the AVG reads them). */
static inline void sw_vg_wr16(sw_machine *m, uint16_t addr, uint16_t v)
{
    m->vgram[addr % 0x3000] = (uint8_t)(v >> 8);
    m->vgram[(addr + 1) % 0x3000] = (uint8_t)v;
}

/* Program ROM words by address (8000-FFFF, or 6000-7FFF in the current bank). */
uint16_t sw_rom16(const sw_machine *m, uint16_t addr);

#endif
