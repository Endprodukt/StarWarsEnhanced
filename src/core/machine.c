#include "core/machine.h"

#include "avg/avg.h"

#include <string.h>

void sw_machine_init(sw_machine *m, const sw_romset *rom)
{
    memset(m, 0, sizeof *m);
    m->rom = rom;
    m->in.in0 = 0xff;                 /* nothing pressed (active low) */
    m->in.in1 = 0xff;
    m->in.opt0 = 0x98;                /* the option switches as MAME sets them: 6 shields, */
    m->in.opt1 = 0x02;                /* moderate, 1 coin 1 play, music in the attract mode */
    m->in.adc[0] = m->in.adc[1] = 0x80;
    m->lfsr = 0x1234567;
    memset(m->nvstore, 0x0f, sizeof m->nvstore);    /* a NOVRAM never written: no valid checksum */
}

static uint8_t lfsr_read(sw_machine *m)
{
    /* x^23 + x^5 + 1, feedback inverted (MAME's description of the board's
     * PRNG); bits 8-15 are what the CPU sees. Stepped a varying amount, as
     * the free-running hardware is between reads. */
    unsigned steps = 17 + (m->lfsr & 0x1f);
    for (unsigned i = 0; i < steps; i++) {
        uint32_t fb = ((m->lfsr >> 22) ^ (m->lfsr >> 4) ^ 1u) & 1u;
        m->lfsr = ((m->lfsr << 1) | fb) & 0x7fffff;
    }
    return (uint8_t)(m->lfsr >> 8);
}

uint16_t sw_rom16(const sw_machine *m, uint16_t addr)
{
    const uint8_t *r = m->rom->main;
    uint32_t a = addr;
    if (addr >= 0x6000 && addr < 0x8000 && (m->latch[4] & 0x80)) a = 0x10000 + (addr - 0x6000);
    return (uint16_t)((r[a] << 8) | r[(a + 1) % sizeof m->rom->main]);
}

uint8_t sw_rd8(sw_machine *m, uint16_t a)
{
    m->n_rd++;
    if (a < 0x3000) return m->vgram[a];
    if (a < 0x4000) return m->rom ? m->rom->vrom[a - 0x3000] : 0;
    if (a >= 0x4800 && a < 0x5000) {
        if (m->ram_read_hook) m->ram[a - 0x4800] = m->ram_read_hook(m->ram_read_user, a, m->ram[a - 0x4800]);
        return m->ram[a - 0x4800];
    }
    if (a >= 0x5000 && a < 0x6000) return m->mbox.ram[a - 0x5000];
    if (a >= 0x6000) {
        if (!m->rom) return 0;
        if (a < 0x8000 && (m->latch[4] & 0x80)) return m->rom->main[0x10000 + (a - 0x6000)];
        return m->rom->main[a];
    }
    if (a >= 0x4500 && a < 0x4600) return m->nvram[a - 0x4500] & 0x0f;
    switch (a & 0xffe0) {
    case 0x4300: return m->in.in0;
    case 0x4320:
        /* D7 math run: never busy. D6 VG halted. */
        return (uint8_t)((m->in.in1 & 0x3f) | (m->vg_left == 0 ? 0x40 : 0));
    case 0x4340: return m->in.opt0;
    case 0x4360: return m->in.opt1;
    case 0x4380: return m->in.adc[m->adc_channel & 3];
    default: break;
    }
    switch (a) {
    case 0x4400: m->sound_in_full = 0; return m->sound_in;
    case 0x4401: return (uint8_t)((m->sound_out_full ? 0x80 : 0) | (m->sound_in_full ? 0x40 : 0));
    case 0x4700: return (uint8_t)(sw_divider_quotient(&m->div) >> 8);
    case 0x4701: return (uint8_t)sw_divider_quotient(&m->div);
    case 0x4703: return m->prng ? m->prng(m->prng_user) : lfsr_read(m);
    default: return 0;
    }
}

void sw_wr8(sw_machine *m, uint16_t a, uint8_t v)
{
    m->n_wr++;
    if (a < 0x3000) { m->vgram[a] = v; return; }
    if (a >= 0x4800 && a < 0x5000) { m->ram[a - 0x4800] = v; return; }
    if (a >= 0x5000 && a < 0x6000) { m->mbox.ram[a - 0x5000] = v; return; }
    if (a >= 0x4500 && a < 0x4600) { m->nvram[a - 0x4500] = v & 0x0f; return; }
    if (a >= 0x4680 && a < 0x4688) {
        m->latch[a - 0x4680] = v & 0x80;
        if (a == SW_NVRRCL && (v & 0x80)) memcpy(m->nvram, m->nvstore, sizeof m->nvram);   /* RECALL */
        return;
    }
    if (a == SW_NSTORE) { memcpy(m->nvstore, m->nvram, sizeof m->nvstore); m->nv_stores++; return; }
    if (a >= 0x46c0 && a < 0x46c4) { m->adc_channel = (uint8_t)(a & 3); return; }
    switch (a) {
    case 0x4400: m->sound_out = v; m->sound_out_full = 1; return;
    case 0x4600:                                    /* VGGO */
        m->vg_starts++;
        m->vg_left = m->steady ? 0 : sw_avg_cycles(m->vgram, m->rom ? m->rom->vrom : 0, 0);
        return;
    case 0x4620: m->vg_left = 0; return;            /* VGSTOP */
    case 0x4700: m->n_mb[v]++; sw_mbox_go(&m->mbox, v); return;    /* MW0: PC and go */
    case 0x4701: m->mbox.bic = (uint16_t)((m->mbox.bic & 0x0ff) | ((v & 1) << 8)); return;
    case 0x4702: m->mbox.bic = (uint16_t)((m->mbox.bic & 0x100) | v); return;
    case 0x4704: case 0x4705: case 0x4706: case 0x4707:
        sw_divider_write(&m->div, a - 0x4700, v);
        return;
    default:
        return;   /* VGRES, WDCLR, IRQCLR, NSTORE, sound reset: nothing to do */
    }
}

uint16_t sw_rd16(sw_machine *m, uint16_t a)
{
    return (uint16_t)((sw_rd8(m, a) << 8) | sw_rd8(m, (uint16_t)(a + 1)));
}

void sw_wr16(sw_machine *m, uint16_t a, uint16_t v)
{
    sw_wr8(m, a, (uint8_t)(v >> 8));
    sw_wr8(m, (uint16_t)(a + 1), (uint8_t)v);
}
