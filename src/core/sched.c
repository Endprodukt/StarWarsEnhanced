#include "core/sched.h"

#include <string.h>

static const double IRQ_PERIOD = 1.0 / SW_IRQ_HZ;

void sw_sched_init(sw_sched *s)
{
    memset(s, 0, sizeof *s);
}

int sw_sched_advance(sw_sched *s, double dt, const sw_sched_hooks *h)
{
    if (dt < 0.0) dt = 0.0;
    s->acc += dt;
    if (s->acc > SW_SCHED_MAX_CATCHUP_S) {
        s->dropped_ms += (uint64_t)((s->acc - IRQ_PERIOD) * 1000.0);
        s->acc = IRQ_PERIOD;
    }

    int n = 0;
    while (s->acc >= IRQ_PERIOD) {
        s->acc -= IRQ_PERIOD;
        s->irq_count++;
        n++;
        if (h && h->irq) h->irq(h->user);
        if (s->irq_count % SW_VG_DIV == 0) {
            s->vg_count++;
            if (h && h->vg) h->vg(h->user);
        }
        if (s->irq_count % SW_GAME_DIV == 0) {
            s->game_count++;
            if (h && h->game) h->game(h->user);
        }
    }
    return n;
}

static double alpha(const sw_sched *s, unsigned div)
{
    double irqs = (double)(s->irq_count % div) + s->acc / IRQ_PERIOD;
    double a = irqs / (double)div;
    return a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
}

double sw_sched_game_alpha(const sw_sched *s) { return alpha(s, SW_GAME_DIV); }
double sw_sched_vg_alpha(const sw_sched *s)   { return alpha(s, SW_VG_DIV); }
