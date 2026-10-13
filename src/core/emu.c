#include "core/emu.h"

#include "game/game.h"
#include "sound/sndboard.h"

#include <string.h>

/* Where the main line waits for the next frame: LSR GMSYNC in WAITFRAME. */
#define WAITFRAME_LSR (SW_WAITFR + 10)

/* --- the bus ------------------------------------------------------------------ */

/* The I/O registers answer on 32 addresses each (MAME's map of the board). */
static uint16_t io_address(uint16_t a)
{
    if (a >= 0x4600 && a < 0x4700) {
        if (a >= 0x4680 && a < 0x46a0) return (uint16_t)(0x4680 | (a & 7));     /* the output latch */
        if (a >= 0x46c0 && a < 0x46e0) return (uint16_t)(0x46c0 | (a & 3));     /* A/D channel */
        return a & 0xffe0;
    }
    return a;
}

static uint8_t bus_read(void *user, uint16_t a)
{
    sw_emu *e = (sw_emu *)user;
    sw_machine *m = e->m;
    if (a >= 0x4800 && a < 0x5000) {
        if (m->ram_read_hook) m->ram[a - 0x4800] = m->ram_read_hook(m->ram_read_user, a, m->ram[a - 0x4800]);
        if (e->replay_on && ((a >= 0x4809 && a <= 0x4840) || a == 0x4851 || a == 0x4852 ||
                             (a >= 0x4866 && a <= 0x4878) || a == 0x4af4 || (a >= 0x4b04 && a <= 0x4b07))) {
            /* What the original read here, the interrupt having run meanwhile. */
            if (e->replay_used < e->replay_n) {
                const uint8_t *r = e->replay + 3 * e->replay_used;
                if ((uint16_t)(r[0] | (r[1] << 8)) == a) {
                    e->replay_used++;
                    m->ram[a - 0x4800] = r[2];
                } else {
                    e->replay_bad++;
                }
            } else {
                e->replay_bad++;
            }
        }
        return m->ram[a - 0x4800];
    }
    if ((a & 0xffe0) == 0x4320)             /* D7 math box running, D6 VG halted */
        return (uint8_t)((m->in.in1 & 0x3f) | (m->vg_left == 0 ? 0x40 : 0) | (e->math_left ? 0x80 : 0));
    return sw_rd8(m, a);
}

static void bus_write(void *user, uint16_t a, uint8_t v)
{
    sw_emu *e = (sw_emu *)user;
    sw_machine *m = e->m;
    if (a >= 0x4800 && a < 0x5000) { m->ram[a - 0x4800] = v; return; }
    if (a < 0x3000) {
        m->vgram[a] = v;
        if (!(a & 1)) {
            /* For the renderer (core/tags.h): the word belongs to the routine
             * that is writing it, known by where it was called from; a new
             * call of it begins a new piece. */
            uint16_t s = e->cpu.s;
            uint16_t ret = s >= 0x4800 && s < 0x4fff ? (uint16_t)((m->ram[s - 0x4800] << 8) | m->ram[s + 1 - 0x4800]) : 0;
            m->vgtag[a >> 1] = SW_TAG(SW_TAG_CODE, ret ^ (ret >> 7));
            m->vgstart[a >> 1] = (uint8_t)(ret != e->vg_ret || s != e->vg_s || e->vg_returned);
            e->vg_s = s;
            e->vg_ret = ret;
            e->vg_returned = 0;
        }
        return;
    }
    a = io_address(a);
    if (a == SW_IRCLR) { e->cpu.irq = 0; return; }
    sw_wr8(m, a, v);
    if (a == SW_M_PCGO) e->math_left = e->math_time[v];
    if (a == 0x4400) {                      /* a request to the sound board, taken at once */
        m->snd_q[m->snd_w++ % sizeof m->snd_q] = v;
        if (!e->snd) m->sound_out_full = 0;         /* nobody there to take it */
    }
    if (a == SW_W_RSTS && e->snd) sw_sndboard_reset(e->snd);   /* HARDWARE RESET SOUND BOARD */
}

/* --- set-up --------------------------------------------------------------------- */

/* How long each math box program runs: 5 master clock cycles a micro-step
 * and 33 more for a multiply (MAME, starwars_state::run_mproc). The
 * micro-code has no branches, so a program's time depends only on where it
 * starts. */
static void math_times(sw_emu *e)
{
    uint16_t prom[1024];
    sw_mbox_join_proms(e->m->rom->mbox_prom, prom);
    for (int pc = 0; pc < 256; pc++) {
        unsigned mpa = (unsigned)pc << 2, t = 0;
        for (int steps = 0; steps < 1024; steps++) {
            uint16_t iw = prom[mpa & 0x3ff];
            t += 5;
            if (iw & 0x2000) t += 33;       /* LDC: the multiplier runs */
            if (iw & 0x0400) break;         /* HALT */
            mpa = (mpa & 0x300) | ((mpa + 1) & 0xff);
        }
        e->math_time[pc] = (uint16_t)(t > 0xffff ? 0xffff : t);
    }
}

void sw_emu_init(sw_emu *e, sw_machine *m)
{
    memset(e, 0, sizeof *e);
    e->m = m;
    math_times(e);
    m6809_init(&e->cpu, bus_read, bus_write, e);
    m6809_reset(&e->cpu);
}

/* --- running -------------------------------------------------------------------- */

static void run_step(sw_emu *e)
{
    sw_machine *m = e->m;
    e->op_pc = e->cpu.pc;
    m->in_irq = (e->cpu.cc & M6809_CC_I) != 0;
    int cy = m6809_step(&e->cpu);
    if (e->cpu.s > e->vg_s) e->vg_returned = 1;
    e->budget -= cy;
    uint32_t mc = (uint32_t)cy * 8;
    m->vg_left = m->vg_left > mc ? m->vg_left - mc : 0;
    e->math_left = e->math_left > mc ? e->math_left - mc : 0;
    if (e->snd && !e->hybrid) sw_sndboard_run(e->snd, cy);
}

void sw_emu_irq(sw_emu *e)
{
    if (e->yoke_exact) {
        /* The yoke passed through exactly: lowest reading 00, no spread. */
        sw_machine *m = e->m;
        m->ram[SW_RHEOP + 0 - 0x4800] = 0;
        m->ram[SW_RHEOP + 2 - 0x4800] = 0;
        m->ram[SW_RHEOY + 0 - 0x4800] = 0;
        m->ram[SW_RHEOY + 2 - 0x4800] = 0;
    }
    e->cpu.irq = 1;
    e->irqs++;
    e->budget += SW_CPU_CYCLES_PER_IRQ;
    while (e->budget > 0) run_step(e);
}

/* --- standing in for phases that are not ported ----------------------------------- */

/* The current phase, by the original's code: from MAIN (or, if the code
 * stopped in a WAITFRAME of its own last time, on from there) until it is
 * back at WAITFRAME. Returns the master clock cycles it took. */
static uint32_t original_frame(void *user)
{
    sw_emu *e = (sw_emu *)user;
    sw_machine *m = e->m;
    m6809 *c = &e->cpu;
    if (m->original_suspended) {
        c->pc = SW_WAITFR + 14;             /* CMPS #STACK: not equal, RTS into the routine */
    } else {
        c->pc = SW_MAIN;
        c->s = SW_STACK;
        c->dp = SW_DPRAM >> 8;
        c->cc = 0;
    }
    c->irq = 0;
    c->waiting = 0;
    e->math_left = 0;
    uint64_t start = c->cycles;
    e->budget = 4000000;                    /* a phase gone wrong does not hang the game */
    do {
        run_step(e);
    } while (c->pc != SW_WAITFR && e->budget > 0);
    m->original_suspended = c->s != SW_STACK;
    e->original_frames++;
    return (uint32_t)(c->cycles - start) * 8;
}

/* A subroutine of the original, to its RTS: the return address pushed is
 * WAITFRAME's, where nothing else ever returns to. */
static void original_call(void *user, uint16_t addr)
{
    sw_emu *e = (sw_emu *)user;
    sw_machine *m = e->m;
    m6809 *c = &e->cpu;
    c->a = m->ra; c->b = m->rb; c->x = m->rx; c->u = m->ru; c->y = m->y;
    c->dp = SW_DPRAM >> 8;
    c->cc = m->rcc & (uint8_t)~(M6809_CC_I | M6809_CC_F);
    c->s = SW_STACK - 2;
    m->ram[SW_STACK - 2 - 0x4800] = SW_WAITFR >> 8;
    m->ram[SW_STACK - 1 - 0x4800] = SW_WAITFR & 0xff;
    c->pc = addr;
    c->irq = 0;
    c->waiting = 0;
    uint64_t start = c->cycles;
    e->budget = 4000000;
    do {
        run_step(e);
    } while (c->pc != SW_WAITFR && e->budget > 0);
    m->ra = c->a; m->rb = c->b; m->rx = c->x; m->ru = c->u; m->y = c->y; m->rcc = c->cc;
    m->original_cycles += (uint32_t)(c->cycles - start) * 8;
}

static void sound_wait(void *user, int cycles)
{
    sw_emu *e = (sw_emu *)user;
    if (e->snd) sw_sndboard_run(e->snd, cycles);
}

void sw_emu_attach(sw_emu *e, sw_machine *m)
{
    struct sw_sndboard *snd = e->snd;
    uint8_t yoke = e->yoke_exact;
    sw_emu_init(e, m);
    e->snd = snd;
    e->yoke_exact = yoke;
    e->hybrid = 1;
    m->original_frame = original_frame;
    m->original_call = original_call;
    m->original_user = e;
    m->original_suspended = 0;
    m->sound_board = snd != NULL;
    m->sound_wait = sound_wait;
    m->sound_user = e;
}

void sw_emu_native_irq(sw_emu *e)
{
    sw_machine *m = e->m;
    e->irqs++;
    sw_irq(m);
    sw_main_poll(m);
    if (e->snd) sw_sndboard_run(e->snd, SW_CPU_CYCLES_PER_IRQ);
}

void sw_emu_get_share(const sw_emu *e, sw_emu_share *out)
{
    out->original_frames = e->original_frames;
    out->native_frames = e->m->frames - e->original_frames;
}

int sw_emu_frame(sw_emu *e, uint32_t max_cycles)
{
    m6809 *c = &e->cpu;
    /* As WAITFRAME has set things up just before its LSR GMSYNC. */
    c->pc = WAITFRAME_LSR;
    c->s = SW_STACK;
    c->dp = SW_DPRAM >> 8;
    c->x = 0;
    c->a = SW_DPRAM >> 8;
    c->b = 0x00;
    c->cc = 0;
    c->irq = 0;
    c->waiting = 0;
    e->math_left = 0;
    e->budget = (int32_t)max_cycles;
    e->replay_on = 0;
    run_step(e);                            /* the LSR that lets the frame go */
    e->replay_on = e->replay != 0;
    while (e->budget > 0) {
        if (c->pc == WAITFRAME_LSR) return 0;
        run_step(e);
    }
    return -1;
}
