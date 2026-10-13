#include "render/dvgmap.h"

#include <math.h>
#include <stdlib.h>

#define BOARD_MAX 4095

void sw_map_point(const sw_display_cfg *c, float ax, float ay, int *bx, int *by)
{
    float x0 = (float)c->margin_left, x1 = (float)(BOARD_MAX - c->margin_right);
    float y0 = (float)c->margin_bottom, y1 = (float)(BOARD_MAX - c->margin_top);
    float u = (ax - c->avg_left) / (c->avg_right - c->avg_left);
    float v = (ay - c->avg_bottom) / (c->avg_top - c->avg_bottom);
    if (c->flip_x) u = 1.0f - u;
    if (c->flip_y) v = 1.0f - v;
    *bx = (int)lroundf(x0 + u * (x1 - x0));
    *by = (int)lroundf(y0 + v * (y1 - y0));
}

void sw_map_color(const sw_display_cfg *c, unsigned color, unsigned intensity,
                  uint8_t *r, uint8_t *g, uint8_t *b)
{
    float k = 0.0f;
    if (intensity) {
        k = c->floor + (1.0f - c->floor) * powf((float)intensity / 255.0f, c->gamma);
        k *= c->brightness;
        if (k > 1.0f) k = 1.0f;
    }
    const uint8_t *p = c->palette[color & 7];
    *r = (uint8_t)lroundf(p[0] * k);
    *g = (uint8_t)lroundf(p[1] * k);
    *b = (uint8_t)lroundf(p[2] * k);
}

/* A line in board units and its board color. */
typedef struct bline {
    int x0, y0, x1, y1;
    uint8_t r, g, b, defocus;
} bline;

/* Lines that follow each other without a gap, in one color: drawn in one
 * go, from either end. */
typedef struct stroke {
    int first, count;       /* lines */
    int ax, ay, bx, by;     /* where it starts and ends */
    int done;
} stroke;

static bline  *g_lines;
static stroke *g_strokes;
static int     g_lines_cap, g_strokes_cap;

static void draw_stroke(vp_dvg *d, const sw_display_cfg *c, const stroke *s, int reversed,
                        int *bx, int *by, int *have_pos, uint8_t col[3], int *have_col)
{
    for (int k = 0; k < s->count; k++) {
        const bline *l = &g_lines[reversed ? s->first + s->count - 1 - k : s->first + k];
        int x0 = reversed ? l->x1 : l->x0, y0 = reversed ? l->y1 : l->y0;
        int x1 = reversed ? l->x0 : l->x1, y1 = reversed ? l->y0 : l->y1;
        if (!*have_col || l->r != col[0] || l->g != col[1] || l->b != col[2]) {
            vp_color(d, l->r, l->g, l->b);
            col[0] = l->r; col[1] = l->g; col[2] = l->b;
            *have_col = 1;
        }
        if (!*have_pos || x0 != *bx || y0 != *by) vp_moveto(d, x0, y0);
        vp_lineto(d, x1, y1);
        if (l->defocus && c->defocus_mode == 1) {
            /* A second pass a few units off thickens the line the way the
             * defocused beam did. */
            vp_moveto(d, x0 + 6, y0 + 6);
            vp_lineto(d, x1 + 6, y1 + 6);
            x1 += 6;
            y1 += 6;
        }
        *bx = x1; *by = y1;
        *have_pos = 1;
    }
}

void sw_draw_lines(vp_dvg *d, const sw_display_cfg *c, const sw_line *lines, int count)
{
    if (g_lines_cap < count) {
        bline *p = (bline *)realloc(g_lines, (size_t)count * sizeof *p);
        stroke *q = (stroke *)realloc(g_strokes, (size_t)count * sizeof *q);
        if (p) g_lines = p;
        if (q) g_strokes = q;
        if (!p || !q) return;
        g_lines_cap = g_strokes_cap = count;
    }

    /* To board units and colors; lines too dark to show are left out. */
    int n = 0;
    for (int i = 0; i < count; i++) {
        const sw_line *ln = &lines[i];
        bline *l = &g_lines[n];
        sw_map_color(c, ln->color, ln->intensity, &l->r, &l->g, &l->b);
        if (!(l->r | l->g | l->b)) continue;
        sw_map_point(c, ln->x0, ln->y0, &l->x0, &l->y0);
        sw_map_point(c, ln->x1, ln->y1, &l->x1, &l->y1);
        l->defocus = ln->defocus;
        n++;
    }

    /* Into strokes. */
    int ns = 0;
    for (int i = 0; i < n; i++) {
        const bline *l = &g_lines[i];
        stroke *s = ns ? &g_strokes[ns - 1] : NULL;
        const bline *p = i ? &g_lines[i - 1] : NULL;
        if (s && p->x1 == l->x0 && p->y1 == l->y0 && p->r == l->r && p->g == l->g && p->b == l->b &&
            !p->defocus && !l->defocus) {
            s->count++;
            s->bx = l->x1; s->by = l->y1;
        } else {
            s = &g_strokes[ns++];
            s->first = i; s->count = 1; s->done = 0;
            s->ax = l->x0; s->ay = l->y0; s->bx = l->x1; s->by = l->y1;
        }
    }

    int have_pos = 0, bx = 2048, by = 2048, have_col = 0;
    uint8_t col[3] = { 0, 0, 0 };
    if (!c->sort_strokes) {
        for (int i = 0; i < ns; i++) draw_stroke(d, c, &g_strokes[i], 0, &bx, &by, &have_pos, col, &have_col);
        return;
    }
    /* Always the stroke next whose nearer end is closest to the beam: the
     * dark way between strokes is what costs the board its time. */
    for (int k = 0; k < ns; k++) {
        int best = -1, best_rev = 0;
        long best_d = 0;
        for (int i = 0; i < ns; i++) {
            const stroke *s = &g_strokes[i];
            if (s->done) continue;
            long dax = s->ax - bx, day = s->ay - by, dbx = s->bx - bx, dby = s->by - by;
            long da = dax * dax + day * day, db = dbx * dbx + dby * dby;
            long dd = da <= db ? da : db;
            if (best < 0 || dd < best_d) { best = i; best_d = dd; best_rev = db < da; }
        }
        g_strokes[best].done = 1;
        draw_stroke(d, c, &g_strokes[best], best_rev, &bx, &by, &have_pos, col, &have_col);
    }
}
