/* tms5220.h - the TMS5220 speech synthesizer, as the Star Wars sound board
 * uses it: Speak External (the CPU feeds LPC frames through the 16 byte
 * FIFO) and the status byte. The speech ROM interface is not here.
 *
 * LPC-10: per 25 ms frame an energy, a pitch (0: unvoiced, noise) and ten
 * reflection coefficients, interpolated in eight steps and run through a
 * lattice filter. The chip's own tables (energy, pitch, K, chirp) are those
 * of MAME's tms5110r.hxx (BSD-3-Clause; Frank Palazzolo, Couriersud,
 * Jonathan Gevaryahu), verified there against a decapped TMS5220.
 */
#ifndef SWVP_TMS5220_H
#define SWVP_TMS5220_H

#include <stdint.h>

typedef struct tms5220 {
    uint8_t  fifo[16];
    int      fifo_head, fifo_count, fifo_bits;  /* bits taken of the head byte */
    int      speak_external;    /* the mode: bytes written go to the FIFO */
    int      talking;           /* frames are being spoken */
    int      buffer_empty_stop; /* ran dry: stopped */

    /* the frame being approached and where the parameters are now */
    int      new_energy, new_pitch, new_k[10];
    int      cur_energy, cur_pitch, cur_k[10];
    int      old_zero, new_zero;        /* silent frames: no interpolation across */
    int      old_unvoiced, new_unvoiced;
    int      sample_in_period;  /* 0..24 */
    int      period;            /* 0..7: interpolation step within the frame */
    int      pitch_count;
    uint16_t rng;
    int32_t  u[11], x[10];
    int      stop_after_frame;
} tms5220;

void tms5220_reset(tms5220 *t);

/* A byte written by the CPU (write strobe): a command, or FIFO data while
 * speaking external. */
void tms5220_write(tms5220 *t, uint8_t data);

/* The status byte: D7 talk status, D6 buffer low, D5 buffer empty. */
uint8_t tms5220_status(const tms5220 *t);

/* 1 while the chip can take a byte. */
int tms5220_ready(const tms5220 *t);

/* The next sample (8400 a second with the board's 672 kHz clock), about
 * -8192..8191. */
int tms5220_sample(tms5220 *t);

#endif
