/* picture.c - see picture.h. */
#include "render/picture.h"

#include "core/sched.h"
#include "core/sw_symbols.h"

#include <string.h>

static float vg13(uint16_t v) { return (float)((int16_t)(uint16_t)(v << 3) >> 3); }

static uint16_t ram16(const sw_machine *m, uint16_t a)
{
    return (uint16_t)((m->ram[a - 0x4800] << 8) | m->ram[a + 1 - 0x4800]);
}

/* The display list in a machine's VG RAM as lines. */
static void run_list(sw_picture *p, const sw_machine *s, uint8_t *space, sw_avg_list *out)
{
    memcpy(space, s->vgram, sizeof s->vgram);
    memcpy(space + 0x3000, p->m->rom->vrom, sizeof p->m->rom->vrom);
    sw_avg_run_tagged(space, 0, s->vgtag, s->vgstart, out, 0);
}

/* sw_machine.frame_hook: keep the machine before and after each frame. */
static void on_frame(void *u, int after)
{
    sw_picture *p = (sw_picture *)u;
    if (!after) {
        p->pair[0] = p->pair[1];
        sw_subframe_before(&p->pair[1], p->m);
        return;
    }
    sw_subframe_after(&p->pair[1], p->m);
    /* The picture the game draws next, now already: lines move towards it
     * where a phase has no view to compute the pictures between. */
    sw_subframe_next(&p->pair[1], &p->scratch);
    run_list(p, &p->scratch, p->subspace, &p->nextlist);
    sw_smooth_key(&p->ahead, &p->nextlist, p->t, 1,
                  vg13(ram16(p->m, SW_VG_CX)) * 0.25f, vg13(ram16(p->m, SW_VG_CY)) * 0.25f);
    if (p->m->steady) {
        /* Frames come like clockwork: this one is shown from now until the
         * next one runs, on its way to what that one will draw. */
        p->shown = p->pair[1];
        p->shown_at = p->t;
        p->frame_time = 12.0 / SW_IRQ_HZ;
    }
}

void sw_picture_init(sw_picture *p, sw_machine *m)
{
    p->m = m;
    p->frame_time = 0.05;
    m->frame_hook = on_frame;
    m->frame_user = p;
    /* The sight is drawn by the routine the word at VGSITE calls. */
    if (m->rom)
        p->ahead.sight_routine = p->smooth.sight_routine =
            (unsigned)(((m->rom->vrom[SW_VGSITE - 0x3000] << 8) | m->rom->vrom[SW_VGSITE - 0x2fff]) & 0x1fff);
}

void sw_picture_irq_begin(sw_picture *p, double t)
{
    p->t = t;
    p->frames_before = p->m->frames;
}

void sw_picture_irq_end(sw_picture *p)
{
    sw_machine *m = p->m;
    if (m->vg_starts == p->vg_seen) return;
    /* The vector generator was started on a picture: the game's new frame
     * if the interrupt switched buffers, else the same one with the sight
     * and the animations moved on. */
    p->vg_seen = m->vg_starts;
    run_list(p, m, p->space, &p->list);
    int new_frame = m->vgram[0] != p->vg_buffer;
    p->vg_buffer = m->vgram[0];
    if (new_frame && !m->steady) {
        /* The frame now on display: the last one run, unless the next one
         * has run already in this same interrupt. */
        p->shown = p->pair[m->frames != p->frames_before ? 0 : 1];
        p->frame_time = p->t - p->shown_at;
        if (p->frame_time < 0.03) p->frame_time = 0.03;
        if (p->frame_time > 0.15) p->frame_time = 0.15;
        p->shown_at = p->t;
    }
    sw_smooth_key(&p->smooth, &p->list, p->t, new_frame,
                  vg13(ram16(m, SW_VG_CX)) * 0.25f, vg13(ram16(m, SW_VG_CY)) * 0.25f);
}

int sw_picture_lines(sw_picture *p, double now, int smooth, const sw_line **lines)
{
    if (smooth >= 3 &&
        sw_subframe_render(&p->shown, p->m, (float)((now - p->shown_at) / p->frame_time), &p->scratch)) {
        /* computed from the state of the game (game/subframe.h) */
        run_list(p, &p->scratch, p->subspace, &p->sublist);
        *lines = p->sublist.lines;
        return p->sublist.count;
    }
    /* else lines moved (render/smooth.h): from this frame's picture to the
     * next one's, or (smooth 1, 2) those of the last frames shown */
    if (smooth >= 3 && p->ahead.frames >= 2) return sw_smooth_frame(&p->ahead, now, 1, lines);
    return sw_smooth_frame(&p->smooth, now, smooth >= 3 ? 1 : smooth, lines);
}
