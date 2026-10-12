/* sched.h - the original's three clocks, run from real time.
 *
 * The arcade board has one timing source: the 6809's IRQ, at
 * 12.096 MHz / 4096 / 12 = 246.09 Hz (4.06 ms). Everything else divides it
 * (WSINT.MAC):
 *
 *   IRQ        246.09 Hz  coins, switch debounce, yoke pots
 *   VG         41.02 Hz   every 6th IRQ: buffer swap, sight cursor slew,
 *                         laser/flash/shot animation tables
 *   game       20.51 Hz   every 12th IRQ (GMTIMR = 11): the whole simulation
 *
 * Game logic moves things per tick, never by a time factor, so the native
 * port runs these at exactly these rates and only the renderer runs at the
 * monitor's own rate. The alphas say how far real time is between the last
 * tick and the next, for interpolation.
 */
#ifndef SWVP_SCHED_H
#define SWVP_SCHED_H

#include <stdint.h>

#define SW_MASTER_CLOCK_HZ 12096000.0
#define SW_IRQ_HZ          (SW_MASTER_CLOCK_HZ / 4096.0 / 12.0)
#define SW_VG_DIV          6
#define SW_GAME_DIV        12

/* More than this much real time behind (a stall, a debugger) is dropped
 * rather than caught up with: the original would simply have run slow. */
#define SW_SCHED_MAX_CATCHUP_S 0.25

typedef void (*sw_tick_fn)(void *user);

typedef struct sw_sched {
    double   acc;          /* real seconds not yet turned into IRQs */
    uint64_t irq_count;    /* IRQs run since init */
    uint64_t vg_count;
    uint64_t game_count;
    uint64_t dropped_ms;   /* real time dropped instead of caught up */
} sw_sched;

typedef struct sw_sched_hooks {
    sw_tick_fn irq;        /* each may be NULL */
    sw_tick_fn vg;
    sw_tick_fn game;
    void      *user;
} sw_sched_hooks;

void sw_sched_init(sw_sched *s);

/* Advance by dt real seconds: runs every IRQ that falls due and, on the
 * 6th and 12th, the VG and game ticks (in that order, after the IRQ).
 * Returns the number of IRQs run. */
int sw_sched_advance(sw_sched *s, double dt, const sw_sched_hooks *h);

/* 0..1: how far real time is from the last game (or VG) tick to the next. */
double sw_sched_game_alpha(const sw_sched *s);
double sw_sched_vg_alpha(const sw_sched *s);

#endif
