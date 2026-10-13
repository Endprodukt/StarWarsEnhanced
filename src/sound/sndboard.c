#include "sound/sndboard.h"

#include <string.h>

#define CPU_HZ   1512000u       /* 12.096 MHz / 8: the 6809, the RIOT and the POKEYs */
#define TMS_HZ   8400u          /* 672 kHz / 80 */

/* --- the POKEYs are run only when something changes or a sample is due -------- */

static void flush_pokeys(sw_sndboard *s, uint32_t ticks)
{
    if (!ticks) return;
    for (int i = 0; i < 4; i++) s->pokey_sum += pokey_run(&s->pokeys[i], ticks) * (float)ticks;
}

/* --- the RIOT --------------------------------------------------------------------- */

static void update_irq(sw_sndboard *s)
{
    s->cpu.irq = (uint8_t)((s->timer_flag && s->timer_irq_enable) || (s->pa7_flag && s->pa7_irq_enable));
}

static uint8_t port_a_in(const sw_sndboard *s)
{
    uint8_t in = 0xff;
    if (!s->m->sound_out_full) in &= 0x7f;          /* PA7: a byte from the main board waits */
    if (!s->m->sound_in_full) in &= 0xbf;           /* PA6: the main board has not taken ours yet */
    if (tms5220_ready(&s->tms)) in &= 0xfb;         /* PA2: READY of the speech chip, low = ready */
    return (uint8_t)((s->dra & s->ddra) | (in & ~s->ddra));
}

static void port_a_out(sw_sndboard *s, uint8_t old_out)
{
    uint8_t out = (uint8_t)((s->dra & s->ddra) | ~s->ddra);
    if ((old_out & 0x01) && !(out & 0x01)) {        /* WS falls: the chip takes the byte on its bus */
        uint8_t v = (uint8_t)((s->drb & s->ddrb) | ~s->ddrb);
        if (s->trace) s->trace(s->trace_user, 1, 0, v);
        tms5220_write(&s->tms, v);
    }
    if ((old_out & 0x02) && !(out & 0x02))          /* RS falls: it puts its status there */
        s->tms_bus = tms5220_status(&s->tms);
}

static uint8_t riot_read(sw_sndboard *s, unsigned reg)
{
    if (!(reg & 0x04)) {
        switch (reg & 3) {
        case 0: return port_a_in(s);
        case 1: return s->ddra;
        case 2: {
            uint8_t out = (uint8_t)((s->dra & s->ddra) | ~s->ddra);
            uint8_t in = !(out & 0x02) ? s->tms_bus : 0xff;     /* RS low: the chip drives the bus */
            return (uint8_t)((s->drb & s->ddrb) | (in & ~s->ddrb));
        }
        default: return s->ddrb;
        }
    }
    if (reg & 0x01) {                               /* interrupt flags */
        uint8_t v = (uint8_t)((s->timer_flag ? 0x80 : 0) | (s->pa7_flag ? 0x40 : 0));
        s->pa7_flag = 0;
        update_irq(s);
        return v;
    }
    /* the timer */
    s->timer_irq_enable = (reg & 0x08) != 0;
    uint8_t v = s->timer_expired ? (uint8_t)(0xff - (s->timer_sub & 0xff))
                                 : (uint8_t)(s->timer / s->timer_div);
    s->timer_flag = 0;
    update_irq(s);
    return v;
}

static void riot_write(sw_sndboard *s, unsigned reg, uint8_t v)
{
    if (!(reg & 0x04)) {
        uint8_t old_out = (uint8_t)((s->dra & s->ddra) | ~s->ddra);
        switch (reg & 3) {
        case 0: s->dra = v; port_a_out(s, old_out); break;
        case 1: s->ddra = v; port_a_out(s, old_out); break;
        case 2: s->drb = v; break;
        default: s->ddrb = v; break;
        }
        return;
    }
    if (reg & 0x10) {                               /* start the timer */
        static const uint32_t DIV[4] = { 1, 8, 64, 1024 };
        s->timer_div = DIV[reg & 3];
        s->timer = (uint32_t)v * s->timer_div + 1;
        s->timer_expired = 0;
        s->timer_sub = 0;
        s->timer_flag = 0;
        s->timer_irq_enable = (reg & 0x08) != 0;
    } else {                                        /* the edge detector on PA7 */
        s->pa7_irq_enable = (reg & 0x02) != 0;
        s->pa7_edge_rising = (reg & 0x01) != 0;
    }
    update_irq(s);
}

static void riot_run(sw_sndboard *s, uint32_t cycles)
{
    if (!s->timer_expired) {
        if (s->timer > cycles) {
            s->timer -= cycles;
        } else {
            s->timer_sub = cycles - s->timer;
            s->timer_expired = 1;
            s->timer_flag = 1;
        }
    } else {
        s->timer_sub += cycles;
    }
    uint8_t pa7 = s->m->sound_out_full != 0;
    if (pa7 != s->pa7_last) {
        if (pa7 == s->pa7_edge_rising) s->pa7_flag = 1;
        s->pa7_last = pa7;
    }
    update_irq(s);
}

/* --- the sound CPU's bus ------------------------------------------------------------ */

static uint8_t bus_read(void *user, uint16_t a)
{
    sw_sndboard *s = (sw_sndboard *)user;
    if (a >= 0x4000) return s->m->rom ? s->m->rom->sound[a] : 0xff;
    if (a >= 0x2000 && a < 0x2800) return s->ram[a - 0x2000];
    if (a >= 0x1000 && a < 0x1080) return s->riot_ram[a - 0x1000];
    if (a >= 0x1080 && a < 0x10a0) return riot_read(s, a & 0x1f);
    if (a >= 0x0800 && a < 0x1000) {                /* SIN: the byte from the main board */
        s->m->sound_out_full = 0;
        return s->m->sound_out;
    }
    return 0xff;
}

static void bus_write(void *user, uint16_t a, uint8_t v)
{
    sw_sndboard *s = (sw_sndboard *)user;
    if (a >= 0x2000 && a < 0x2800) { s->ram[a - 0x2000] = v; return; }
    if (a >= 0x1000 && a < 0x1080) { s->riot_ram[a - 0x1000] = v; return; }
    if (a >= 0x1080 && a < 0x10a0) { riot_write(s, a & 0x1f, v); return; }
    if (a >= 0x1800 && a < 0x1840) {                /* the four POKEYs */
        unsigned off = a - 0x1800;
        flush_pokeys(s, s->pending_ticks - s->pokey_done);
        s->pokey_done = s->pending_ticks;
        if (s->trace) s->trace(s->trace_user, 0, off, v);
        pokey_write(&s->pokeys[(off >> 3) & 3], (off & 7) | ((off & 0x20) >> 2), v);
        return;
    }
    if (a < 0x0800) {                               /* SOUT: a byte for the main board */
        if (s->trace) s->trace(s->trace_user, 2, 0, v);
        s->m->sound_in = v;
        s->m->sound_in_full = 1;
    }
}

uint8_t sw_snd_rd(sw_sndboard *s, uint16_t a) { return bus_read(s, a); }
void sw_snd_wr(sw_sndboard *s, uint16_t a, uint8_t v) { bus_write(s, a, v); }

/* --- set-up and running ---------------------------------------------------------------- */

void sw_sndboard_reset(sw_sndboard *s)
{
    for (int i = 0; i < 4; i++) pokey_reset(&s->pokeys[i]);
    tms5220_reset(&s->tms);
    s->dra = s->ddra = s->drb = s->ddrb = 0;
    s->timer = 0x100 * 1024;
    s->timer_div = 1024;
    s->timer_expired = 0;
    s->timer_flag = s->timer_irq_enable = 0;
    s->pa7_flag = s->pa7_irq_enable = 0;
    s->m->sound_out_full = 0;
    s->m->sound_in_full = 0;
    if (s->native) sw_sndprog_init(s);
    else m6809_reset(&s->cpu);
}

void sw_sndboard_init(sw_sndboard *s, sw_machine *m)
{
    memset(s, 0, sizeof *s);
    s->m = m;
    s->volume = 1.0f;
    s->late_cycles = 800;
    m6809_init(&s->cpu, bus_read, bus_write, s);
    sw_sndboard_reset(s);
}

static void emit_sample(sw_sndboard *s)
{
    flush_pokeys(s, s->pending_ticks - s->pokey_done);
    /* Four POKEYs of four channels of volume 15: 0..240. MAME's balance:
     * each POKEY 0.2, the speech 0.5. */
    if (s->pending_ticks) s->last_pokey = s->pokey_sum / (float)s->pending_ticks / 60.0f * 0.2f;
    float pk = s->last_pokey;
    float sp = (float)s->tms_out / 8192.0f * 0.5f;
    float v = pk + sp;
    /* The POKEYs' output is all positive: take the standing part out. */
    s->dc += (v - s->dc) * 0.002f;
    v = (v - s->dc) * s->volume * 28000.0f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    uint32_t w = s->ring_w;
    if (w - s->ring_r < SW_SOUND_RING) {
        s->ring[w % SW_SOUND_RING] = (int16_t)v;
        s->ring_w = w + 1;
    }
    s->pending_ticks = 0;
    s->pokey_done = 0;
    s->pokey_sum = 0.0f;
}

void sw_sndboard_run(sw_sndboard *s, int cycles)
{
    s->budget += cycles;
    while (s->budget > 0) {
        int cy;
        if (s->native) {
            /* The program in C takes no time: the board's clock goes on in
             * small steps, and what the interrupt and the main line did is
             * done when their cause comes up. */
            cy = 16;
            if (s->m->sound_out_full) sw_sndprog_command(s);
            if (s->late > 0 && (s->late -= cy) <= 0) sw_sndprog_late(s);
            /* (the 6809 has the timer going again 42 to 47 cycles after it
             * ran out: so long does its interrupt take to get there) */
            if (s->timer_flag && s->timer_sub >= 36) {
                if (s->late > 0) { s->late = 0; sw_sndprog_late(s); }
                sw_sndprog_tick(s);
            }
        } else {
            cy = m6809_step(&s->cpu);
        }
        s->budget -= cy;
        riot_run(s, (uint32_t)cy);
        s->pending_ticks += (uint32_t)cy;
        s->tms_phase += (uint32_t)cy * TMS_HZ;
        while (s->tms_phase >= CPU_HZ) {
            s->tms_phase -= CPU_HZ;
            s->tms_out = tms5220_sample(&s->tms);
        }
        s->sample_phase += (uint32_t)cy * SW_SOUND_RATE;
        while (s->sample_phase >= CPU_HZ) {
            s->sample_phase -= CPU_HZ;
            emit_sample(s);
        }
    }
}

int sw_sndboard_read(sw_sndboard *s, int16_t *out, int n)
{
    uint32_t r = s->ring_r, avail = s->ring_w - r;
    if ((uint32_t)n > avail) n = (int)avail;
    for (int i = 0; i < n; i++) out[i] = s->ring[(r + (uint32_t)i) % SW_SOUND_RING];
    s->ring_r = r + (uint32_t)n;
    return n;
}
