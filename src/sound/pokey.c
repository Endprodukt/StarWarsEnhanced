#include "sound/pokey.h"

#include <string.h>

/* The polynomial counters, one bit per clock tick, made once. */
static uint8_t poly4[15], poly5[31], poly9[511], poly17[131071];
static int polys_made;

static void make_poly(uint8_t *out, int len, int bits, int tap_a, int tap_b)
{
    uint32_t r = (1u << bits) - 1;
    for (int i = 0; i < len; i++) {
        out[i] = (uint8_t)(r & 1);
        uint32_t fb = ((r >> tap_a) ^ (r >> tap_b)) & 1;
        r = (r >> 1) | (fb << (bits - 1));
    }
}

static void make_polys(void)
{
    make_poly(poly4, 15, 4, 0, 1);
    make_poly(poly5, 31, 5, 0, 2);
    make_poly(poly9, 511, 9, 0, 4);
    make_poly(poly17, 131071, 17, 0, 5);
    polys_made = 1;
}

void pokey_reset(pokey *p)
{
    if (!polys_made) make_polys();
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 4; i++) p->counter[i] = 1;
}

/* Clock ticks from one run-out of channel ch's divider to the next. */
static uint32_t period(const pokey *p, int ch)
{
    uint32_t base = (p->audctl & 0x01) ? 114 : 28;     /* 15 kHz or 64 kHz from 1.79 MHz */
    switch (ch) {
    case 0:
        return (p->audctl & 0x40) ? (uint32_t)p->audf[0] + 4 : ((uint32_t)p->audf[0] + 1) * base;
    case 1:
        if (p->audctl & 0x10) {                         /* 1 and 2 joined: 16 bits */
            uint32_t n = ((uint32_t)p->audf[1] << 8) | p->audf[0];
            return (p->audctl & 0x40) ? n + 7 : (n + 1) * base;
        }
        return ((uint32_t)p->audf[1] + 1) * base;
    case 2:
        return (p->audctl & 0x20) ? (uint32_t)p->audf[2] + 4 : ((uint32_t)p->audf[2] + 1) * base;
    default:
        if (p->audctl & 0x08) {                         /* 3 and 4 joined */
            uint32_t n = ((uint32_t)p->audf[3] << 8) | p->audf[2];
            return (p->audctl & 0x20) ? n + 7 : (n + 1) * base;
        }
        return ((uint32_t)p->audf[3] + 1) * base;
    }
}

void pokey_write(pokey *p, unsigned reg, uint8_t value)
{
    reg &= 15;
    if (reg < 8) {
        if (reg & 1) p->audc[reg >> 1] = value;
        else p->audf[reg >> 1] = value;
    } else if (reg == 8) {
        p->audctl = value;
    } else if (reg == 9) {                              /* STIMER: the dividers start over */
        for (int i = 0; i < 4; i++) p->counter[i] = period(p, i);
    }
}

/* The channel's divider has run out at clock tick t. */
static void clock_channel(pokey *p, int ch, uint64_t t)
{
    uint8_t c = p->audc[ch];
    if (!(c & 0x80) && !poly5[t % 31]) return;          /* gated by the 5 bit counter */
    if (c & 0x20) p->out[ch] ^= 1;                      /* pure tone */
    else if (c & 0x40) p->out[ch] = poly4[t % 15];
    else p->out[ch] = (p->audctl & 0x80) ? poly9[t % 511] : poly17[t % 131071];
}

float pokey_run(pokey *p, unsigned ticks)
{
    if (ticks == 0) return 0.0f;
    uint32_t sum = 0;
    for (int ch = 0; ch < 4; ch++) {
        uint8_t c = p->audc[ch];
        uint32_t vol = c & 0x0f;
        if (c & 0x10) {                                 /* volume only: the level itself */
            sum += vol * ticks;
            /* the divider keeps running, unheard */
            uint32_t per = period(p, ch), left = ticks;
            while (p->counter[ch] <= left) { left -= p->counter[ch]; p->counter[ch] = per; }
            p->counter[ch] -= left;
            continue;
        }
        if (vol == 0) {
            uint32_t per = period(p, ch), left = ticks;
            uint64_t t = p->ticks;
            while (p->counter[ch] <= left) {
                left -= p->counter[ch];
                t += p->counter[ch];
                p->counter[ch] = per;
                clock_channel(p, ch, t);
            }
            p->counter[ch] -= left;
            continue;
        }
        uint32_t per = period(p, ch), left = ticks, high = 0;
        uint64_t t = p->ticks;
        while (p->counter[ch] <= left) {
            if (p->out[ch]) high += p->counter[ch];
            left -= p->counter[ch];
            t += p->counter[ch];
            p->counter[ch] = per;
            clock_channel(p, ch, t);
        }
        if (p->out[ch]) high += left;
        p->counter[ch] -= left;
        sum += vol * high;
    }
    p->ticks += ticks;
    return (float)sum / (float)ticks;
}
