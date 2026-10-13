/* pokey.h - the sound of an Atari POKEY (C012294).
 *
 * The Star Wars sound board has four, write-only: only the tone generators
 * are here (AUDF1-4, AUDC1-4, AUDCTL with the 1.79 MHz clocks, the 15 kHz
 * base clock and the joined 16-bit channels, the 4, 5, 9 and 17 bit
 * polynomial counters). The high-pass filters, pots, keyboard, serial port
 * and timer interrupts are not.
 */
#ifndef SWVP_POKEY_H
#define SWVP_POKEY_H

#include <stdint.h>

typedef struct pokey {
    uint8_t  audf[4], audc[4], audctl;
    uint32_t counter[4];    /* clock ticks until the channel's divider runs out */
    uint8_t  out[4];        /* the channel's flip-flop */
    uint64_t ticks;         /* clock ticks since reset: where the polynomial counters are */
} pokey;

void pokey_reset(pokey *p);
void pokey_write(pokey *p, unsigned reg, uint8_t value);

/* Advance by `ticks` cycles of the chip's clock (1.512 MHz here) and return
 * the output averaged over them, 0..60 (four channels of volume 0..15). */
float pokey_run(pokey *p, unsigned ticks);

#endif
