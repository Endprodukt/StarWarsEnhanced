#include "render/smooth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Longer than this between two game frames (a phase's set-up, a stall):
 * nothing to move between. */
#define MAX_INTERVAL_S 0.25
/* A line that moved further than this in one frame (AVG units; the screen
 * is 255 wide) did not move, it was put somewhere else. */
#define MAX_STEP 40.0f
/* Lines that share an id are paired by nearness, up to this many pairs. */
#define MAX_PAIRS 40000

typedef struct pair { float d; int a, b; } pair;

void sw_smooth_free(sw_smooth *s)
{
    sw_avg_list_free(&s->prev);
    sw_avg_list_free(&s->cur);
    free(s->match);
    free(s->taken);
    free(s->pairs);
    free(s->refs[0]);
    free(s->refs[1]);
    free(s->out);
    memset(s, 0, sizeof *s);
}

static int copy_list(sw_avg_list *dst, const sw_avg_list *src)
{
    if (dst->cap < src->count) {
        sw_line *p = (sw_line *)realloc(dst->lines, (size_t)src->count * sizeof *p);
        if (!p) return -1;
        dst->lines = p;
        dst->cap = src->count;
    }
    if (src->count) memcpy(dst->lines, src->lines, (size_t)src->count * sizeof *src->lines);
    dst->count = src->count;
    return 0;
}

static int ref_cmp(const void *a, const void *b)
{
    const sw_smooth_ref *x = (const sw_smooth_ref *)a, *y = (const sw_smooth_ref *)b;
    if (x->id != y->id) return x->id < y->id ? -1 : 1;
    return x->index - y->index;
}

static int pair_cmp(const void *a, const void *b)
{
    float d = ((const pair *)a)->d - ((const pair *)b)->d;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static sw_smooth_ref *sorted(sw_smooth *s, int which, const sw_avg_list *l)
{
    if (s->refs_cap[which] < l->count) {
        sw_smooth_ref *p = (sw_smooth_ref *)realloc(s->refs[which], (size_t)l->count * sizeof *p);
        if (!p) return NULL;
        s->refs[which] = p;
        s->refs_cap[which] = l->count;
    }
    for (int i = 0; i < l->count; i++) {
        s->refs[which][i].id = l->lines[i].id;
        s->refs[which][i].index = i;
    }
    if (l->count) qsort(s->refs[which], (size_t)l->count, sizeof *s->refs[which], ref_cmp);
    return s->refs[which];
}

/* How far a line of prev is from one of cur, if they are the same line. */
static float distance(const sw_line *p, const sw_line *c)
{
    float d0 = fabsf(c->x0 - p->x0) + fabsf(c->y0 - p->y0);
    float d1 = fabsf(c->x1 - p->x1) + fabsf(c->y1 - p->y1);
    return d0 > d1 ? d0 : d1;
}

/* ka lines of prev and kb of cur with the same id: the nearest pairs first. */
static void match_nearest(sw_smooth *s, const sw_smooth_ref *a, int ka, const sw_smooth_ref *b, int kb)
{
    int n = 0;
    if (s->pairs_cap < ka * kb) {
        pair *p = (pair *)realloc(s->pairs, (size_t)(ka * kb) * sizeof *p);
        if (!p) return;
        s->pairs = (void *)p;
        s->pairs_cap = ka * kb;
    }
    pair *pairs = (pair *)s->pairs;
    for (int i = 0; i < ka; i++) {
        const sw_line *p = &s->prev.lines[a[i].index];
        for (int j = 0; j < kb; j++) {
            const sw_line *c = &s->cur.lines[b[j].index];
            if (p->color != c->color) continue;
            float d = distance(p, c);
            if (d > MAX_STEP) continue;
            pairs[n].d = d;
            pairs[n].a = a[i].index;
            pairs[n].b = b[j].index;
            n++;
        }
    }
    qsort(pairs, (size_t)n, sizeof *pairs, pair_cmp);
    for (int k = 0; k < n; k++) {
        if (s->taken[pairs[k].a] || s->match[pairs[k].b] >= 0) continue;
        s->taken[pairs[k].a] = 1;
        s->match[pairs[k].b] = pairs[k].a;
    }
}

/* For each line of cur its partner in prev: the one with the same id; of
 * several with the same id the nearest. */
static void match_lists(sw_smooth *s)
{
    int n = s->cur.count, np = s->prev.count;
    if (s->match_cap < n) {
        int *p = (int *)realloc(s->match, (size_t)n * sizeof *p);
        if (!p) { s->match_cap = 0; s->match = NULL; return; }
        s->match = p;
        s->match_cap = n;
    }
    if (s->taken_cap < np) {
        uint8_t *p = (uint8_t *)realloc(s->taken, (size_t)np);
        if (!p) return;
        s->taken = p;
        s->taken_cap = np;
    }
    for (int i = 0; i < n; i++) s->match[i] = -1;
    if (np) memset(s->taken, 0, (size_t)np);
    const sw_smooth_ref *a = sorted(s, 0, &s->prev), *b = sorted(s, 1, &s->cur);
    if (!a || !b) return;
    int i = 0, j = 0;
    while (i < np && j < n) {
        if (a[i].id < b[j].id) { i++; continue; }
        if (a[i].id > b[j].id) { j++; continue; }
        uint64_t id = a[i].id;
        int i1 = i, j1 = j;
        while (i1 < np && a[i1].id == id) i1++;
        while (j1 < n && b[j1].id == id) j1++;
        if (id != 0) {
            if ((long)(i1 - i) * (j1 - j) <= MAX_PAIRS) {
                match_nearest(s, a + i, i1 - i, b + j, j1 - j);
            } else {
                for (int k = 0; i + k < i1 && j + k < j1; k++) s->match[b[j + k].index] = a[i + k].index;
            }
        }
        i = i1;
        j = j1;
    }
}

void sw_smooth_key(sw_smooth *s, const sw_avg_list *lines, double t, int new_frame,
                   float sight_x, float sight_y)
{
    s->sight_prev[0] = s->sight_cur[0];
    s->sight_prev[1] = s->sight_cur[1];
    s->t_sight_prev = s->t_sight_cur;
    s->sight_cur[0] = sight_x;
    s->sight_cur[1] = sight_y;
    s->t_sight_cur = t;

    if (new_frame || s->frames == 0) {
        sw_avg_list swap = s->prev;
        s->prev = s->cur;
        s->cur = swap;
        s->t_prev = s->t_cur;
        s->t_cur = t;
        s->frames++;
    }
    copy_list(&s->cur, lines);
    s->sight_key[0] = sight_x;
    s->sight_key[1] = sight_y;
    match_lists(s);
}

static float clamp01(double v) { return v < 0.0 ? 0.0f : v > 1.0 ? 1.0f : (float)v; }

int sw_smooth_frame(sw_smooth *s, double t, int mode, const sw_line **lines)
{
    int n = s->cur.count;
    if (s->out_cap < n) {
        sw_line *p = (sw_line *)realloc(s->out, (size_t)n * sizeof *p);
        if (!p) { *lines = s->cur.lines; return n; }
        s->out = p;
        s->out_cap = n;
    }
    *lines = s->out;

    /* How far along from the frame before (k = 0) to the last one (k = 1),
     * or beyond it (k up to 2). */
    double interval = s->t_cur - s->t_prev;
    int moving = mode != 0 && s->frames >= 2 && interval > 0.0 && interval < MAX_INTERVAL_S && s->match;
    float a = moving ? clamp01((t - s->t_cur) / interval) : 0.0f;
    float k = mode == 2 ? 1.0f + a : a;

    /* The sight, between or beyond its last two positions the same way. */
    double si = s->t_sight_cur - s->t_sight_prev;
    float sk = 1.0f;
    if (mode != 0 && si > 0.0 && si < MAX_INTERVAL_S) {
        float sa = clamp01((t - s->t_sight_cur) / si);
        sk = mode == 2 ? 1.0f + sa : sa;
    }
    float sdx = s->sight_prev[0] + (s->sight_cur[0] - s->sight_prev[0]) * sk - s->sight_key[0];
    float sdy = s->sight_prev[1] + (s->sight_cur[1] - s->sight_prev[1]) * sk - s->sight_key[1];

    for (int i = 0; i < n; i++) {
        const sw_line *c = &s->cur.lines[i];
        sw_line *o = &s->out[i];
        *o = *c;
        if (s->sight_routine && ((c->id >> 16) & 0xffff) == s->sight_routine) {
            /* the cursor: where the yoke has it now */
            o->x0 += sdx; o->x1 += sdx;
            o->y0 += sdy; o->y1 += sdy;
            continue;
        }
        if (!moving || s->match[i] < 0) continue;
        const sw_line *p = &s->prev.lines[s->match[i]];
        if (p->color != c->color || distance(p, c) > MAX_STEP) continue;
        o->x0 = p->x0 + (c->x0 - p->x0) * k;
        o->y0 = p->y0 + (c->y0 - p->y0) * k;
        o->x1 = p->x1 + (c->x1 - p->x1) * k;
        o->y1 = p->y1 + (c->y1 - p->y1) * k;
        float in = (float)p->intensity + ((float)c->intensity - (float)p->intensity) * k;
        o->intensity = (uint8_t)(in < 0.0f ? 0.0f : in > 255.0f ? 255.0f : in);
    }
    return n;
}
