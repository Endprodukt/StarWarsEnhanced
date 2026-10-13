/* machine.h - the Star Wars main board, as the native game code sees it.
 *
 * The port keeps the original memory layout: program RAM at 4800-4FFF,
 * math box RAM at 5000-5FFF, VG RAM at 0000-2FFF, every variable at the
 * address WSROOT.MAP gives it (core/sw_symbols.h). Game state can then be
 * compared byte for byte with MAME recordings, and the 1983 source reads
 * one to one against the C.
 *
 * The I/O registers keep their meaning (SW5.DOC / SWMAP.DOC), but nothing
 * here waits: the math box and the vector generator finish at once, which
 * the original code cannot tell apart from a fast board.
 */
#ifndef SWVP_MACHINE_H
#define SWVP_MACHINE_H

#include <stdint.h>

#include "core/sw_symbols.h"
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

typedef struct sw_machine {
    const sw_romset *rom;
    uint8_t   ram[0x800];        /* 4800-4FFF */
    uint8_t   vgram[0x3000];     /* 0000-2FFF */
    uint8_t   nvram[0x100];      /* 4500-45FF, 4 bits each (X2212) */
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

    sw_prng_fn prng;
    void      *prng_user;
    uint32_t   lfsr;
} sw_machine;

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
