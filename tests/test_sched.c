/* The three clocks: rates, ratios, alphas, and the stall rule. */
#include "core/sched.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

typedef struct counts { int irq, vg, game, vg_before_game_ok; } counts;

static void on_irq(void *u) { ((counts *)u)->irq++; }
static void on_vg(void *u) { ((counts *)u)->vg++; }
static void on_game(void *u)
{
    counts *c = (counts *)u;
    c->game++;
    /* Game ticks fall on every second VG tick, after it. */
    if (c->vg == 2 * c->game) c->vg_before_game_ok++;
}

int main(void)
{
    sw_sched s;
    counts c = { 0 };
    sw_sched_hooks h = { on_irq, on_vg, on_game, &c };

    /* About ten seconds of real time in uneven 1..17 ms frames. */
    sw_sched_init(&s);
    double t = 0.0;
    int k = 0;
    while (t < 10.0) {
        double dt = (1 + (k++ * 7) % 17) / 1000.0;
        sw_sched_advance(&s, dt, &h);
        t += dt;
    }
    CHECK(fabs(c.irq - t * SW_IRQ_HZ) <= 1.0);
    CHECK(fabs(c.game - t * SW_IRQ_HZ / 12.0) <= 1.0);
    CHECK(c.vg == c.irq / 6);
    CHECK(c.game == c.irq / 12);
    CHECK(c.vg_before_game_ok == c.game);
    CHECK(fabs(SW_IRQ_HZ - 246.09375) < 1e-9);

    /* Alphas grow from 0 to 1 between ticks. */
    sw_sched_init(&s);
    sw_sched_advance(&s, 12.0 / SW_IRQ_HZ + 1e-9, NULL);
    CHECK(s.game_count == 1);
    CHECK(sw_sched_game_alpha(&s) < 0.01);
    sw_sched_advance(&s, 6.0 / SW_IRQ_HZ, NULL);
    CHECK(fabs(sw_sched_game_alpha(&s) - 0.5) < 0.01);
    CHECK(sw_sched_vg_alpha(&s) < 0.01);

    /* A stall longer than the catch-up limit is dropped, not replayed. */
    sw_sched_init(&s);
    int n = sw_sched_advance(&s, 2.0, NULL);
    CHECK(n <= 1);
    CHECK(s.dropped_ms > 1900);

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("sched: all passed\n");
    return 0;
}
