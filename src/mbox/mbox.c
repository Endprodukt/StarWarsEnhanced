#include "mbox/mbox.h"

#include <stddef.h>
#include <string.h>

int sw_mbox_go(sw_mbox *m, uint8_t pc)
{
    for (const sw_mbox_entry *e = sw_mbox_entries; e->name; e++) {
        if (e->pc == pc) {
            e->run(m);
            return 0;
        }
    }
    return -1;
}

void sw_mbox_join_proms(const uint8_t proms[0x1000], uint16_t out[1024])
{
    /* 136021-110: bits 15-12, -111: 11-8, -112: 7-4, -113: 3-0 */
    for (int i = 0; i < 1024; i++)
        out[i] = (uint16_t)(((proms[0x000 + i] & 0xf) << 12) | ((proms[0x400 + i] & 0xf) << 8) |
                            ((proms[0x800 + i] & 0xf) << 4) | (proms[0xc00 + i] & 0xf));
}

/* Micro-word bits (TCODE2.MAC). */
#define MW_LDA  0x8000
#define MW_LDB  0x4000
#define MW_LDC  0x2000
#define MW_CLA  0x1000
#define MW_IBC  0x0800
#define MW_HALT 0x0400
#define MW_SAC  0x0200
#define MW_LAC  0x0100
#define MW_DIR  0x0080

int sw_mbox_run_prom(sw_mbox *m, const uint16_t prom[1024], uint8_t pc)
{
    unsigned mpa = (unsigned)pc << 2;
    int steps = 0;

    for (;;) {
        uint16_t iw = prom[mpa & 0x3ff];
        uint16_t ma = (iw & MW_DIR) ? (uint16_t)(iw & 0x7f)
                                    : (uint16_t)((iw & 3) | ((m->bic & 0x1ff) << 2));
        uint16_t w = mb_rd(m, ma);
        steps++;

        if (iw & MW_CLA) m->acc = 0;
        if (iw & MW_LAC) m->acc = (int32_t)((uint32_t)w << 16);
        if (iw & MW_SAC) mb_wr(m, ma, (uint16_t)((uint32_t)m->acc >> 16));
        if (iw & MW_IBC) m->bic = (m->bic + 1) & 0x1ff;
        if (iw & MW_LDC) mb_mac(m, (int16_t)w);
        if (iw & MW_LDB) m->b = (int16_t)w;
        if (iw & MW_LDA) m->a = (int16_t)w;
        if (iw & MW_HALT) break;

        /* The top two address bits are not part of the counter: each page
         * of 256 wraps on itself. */
        mpa = (mpa & 0x300) | ((mpa + 1) & 0xff);
        if (steps > 100000) break;   /* no HALT: as MAME, give up */
    }
    return steps;
}

int sw_mbox_selftest(const uint16_t prom[1024], int trials)
{
    static sw_mbox m1, m2;
    uint32_t r = 0x9e3779b9u;
    int bad = 0;
    for (const sw_mbox_entry *e = sw_mbox_entries; e->name; e++) {
        for (int t = 0; t < trials; t++) {
            for (int i = 0; i < SW_MBRAM_SIZE; i++) {
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                m1.ram[i] = (uint8_t)r;
            }
            m1.a = (int16_t)r; m1.b = (int16_t)(r >> 16); m1.c = 0;
            m1.acc = (int32_t)(r * 2654435761u);
            m1.bic = (uint16_t)(r & 0x1ff);
            m2 = m1;
            e->run(&m1);
            sw_mbox_run_prom(&m2, prom, e->pc);
            if (memcmp(&m1.ram, &m2.ram, sizeof m1.ram) != 0 || m1.a != m2.a || m1.b != m2.b ||
                m1.acc != m2.acc || m1.bic != m2.bic) {
                bad++;
                break;
            }
        }
    }
    return bad;
}

/* --- divider ---------------------------------------------------------------- */

void sw_divider_write(sw_divider *d, int offset, uint8_t data)
{
    switch (offset) {
    case 4:     /* DVSRH */
        d->divisor = (uint16_t)((d->divisor & 0x00ff) | (data << 8));
        d->dvd_shift = d->dividend;
        d->quotient = 0;
        break;
    case 5:     /* DVSRL: restoring division as on the schematics (MAME); it
                 * gives the hardware's own "wrong" answers when divisor <
                 * 2 * dividend or divisor > 8000h */
        d->divisor = (uint16_t)((d->divisor & 0xff00) | data);
        for (int i = 1; i < 16; i++) {
            d->quotient = (uint16_t)(d->quotient << 1);
            uint32_t t = (uint32_t)d->dvd_shift + (uint32_t)(d->divisor ^ 0xffff) + 1u;
            if (t & 0x10000) {
                d->quotient |= 1;
                d->dvd_shift = (uint16_t)(t << 1);
            } else {
                d->dvd_shift = (uint16_t)(d->dvd_shift << 1);
            }
        }
        break;
    case 6:
        d->dividend = (uint16_t)((d->dividend & 0x00ff) | (data << 8));
        break;
    case 7:
        d->dividend = (uint16_t)((d->dividend & 0xff00) | data);
        break;
    default:
        break;
    }
}

uint16_t sw_divider_quotient(const sw_divider *d)
{
    return d->quotient;
}

uint16_t sw_divide(sw_divider *d, uint16_t dividend, uint16_t divisor)
{
    sw_divider_write(d, 6, (uint8_t)(dividend >> 8));
    sw_divider_write(d, 7, (uint8_t)dividend);
    sw_divider_write(d, 4, (uint8_t)(divisor >> 8));
    sw_divider_write(d, 5, (uint8_t)divisor);
    return d->quotient;
}
