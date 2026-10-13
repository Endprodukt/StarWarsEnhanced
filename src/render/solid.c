/* solid.c - see solid.h. */
#include "render/solid.h"

#include "game/game.h"
#include "render/view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PTS      64
#define MAX_MEDGES   256
#define FLATNESS     0.2f       /* how far out of its plane a ring's points may be, of its size */
#define MIN_AREA     0.5f       /* a face seen edge-on covers nothing (AVG units squared) */
#define W_REL        0.03f      /* a line is behind a face if it is this much farther away ... */
#define W_ABS        0.2f       /* ... and this (the quotients are whole numbers) */
#define W_FRONT      200000.0f  /* the X-wing's own nose and guns: before everything */
#define W_BACK       0.5f       /* the logo and what has no distance: before the stars only */
#define MIN_PIECE    0.15f      /* what is left of a line and shorter is dropped */
#define MAX_CUTS     96
/* Cells of an outline's grid per AVG unit. The logo is large and changes
 * with every picture (it is made anew each time, which on the cabinet's
 * Pi took 22 ms at the fine grid): for it the coarse one. */
#define RES_FINE     2.0f
#define RES_LOGO     1.0f
/* the edges of the screen (VGEDGL/R/B/T at the scale the game draws at) */
#define SCREEN_X0    -127.5f
#define SCREEN_X1    127.5f
#define SCREEN_Y0    -153.0f
#define SCREEN_Y1    150.0f

/* --- the faces of an object ------------------------------------------------------- */

typedef struct finder {
    const int16_t (*pts)[3];
    uint8_t  adj[MAX_PTS][MAX_PTS];
    int      n_pts;
    int      path[SW_SOLID_FACE_MAX];
    sw_solid_face *faces;
    int      n, cap;
} finder;

/* A ring of lines is a face if its points lie in a plane, near enough: the
 * hull of a ship is curved, and a face of it a little too. */
static int flat(const finder *f, const int *ring, int n)
{
    double nx = 0, ny = 0, nz = 0, cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) {
        const int16_t *p = f->pts[ring[i]], *q = f->pts[ring[(i + 1) % n]];
        nx += (double)(p[1] - q[1]) * (p[2] + q[2]);        /* Newell: the area as a vector */
        ny += (double)(p[2] - q[2]) * (p[0] + q[0]);
        nz += (double)(p[0] - q[0]) * (p[1] + q[1]);
        cx += p[0]; cy += p[1]; cz += p[2];
    }
    double len = sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1.0) return 0;
    nx /= len; ny /= len; nz /= len;
    cx /= n; cy /= n; cz /= n;
    double off = 0;
    for (int i = 0; i < n; i++) {
        const int16_t *p = f->pts[ring[i]];
        double d = fabs((p[0] - cx) * nx + (p[1] - cy) * ny + (p[2] - cz) * nz);
        if (d > off) off = d;
    }
    return off <= FLATNESS * sqrt(len * 0.5);
}

static void add_face(finder *f, const int *ring, int n)
{
    if (!flat(f, ring, n)) return;
    if (f->n == f->cap) {
        int cap = f->cap ? f->cap * 2 : 32;
        sw_solid_face *p = (sw_solid_face *)realloc(f->faces, (size_t)cap * sizeof *p);
        if (!p) return;
        f->faces = p;
        f->cap = cap;
    }
    sw_solid_face *face = &f->faces[f->n++];
    face->n = (uint8_t)n;
    for (int i = 0; i < n; i++) face->pt[i] = (uint8_t)ring[i];
}

/* Every ring of lines without a line across it that starts at path[0],
 * its lowest point, each found once. */
static void rings(finder *f, int len)
{
    int s = f->path[0], v = f->path[len - 1];
    for (int u = s + 1; u < f->n_pts; u++) {
        if (!f->adj[v][u]) continue;
        int ok = 1;
        for (int i = 0; i < len && ok; i++)
            if (f->path[i] == u || (i >= 1 && i < len - 1 && f->adj[u][f->path[i]])) ok = 0;
        if (!ok) continue;
        f->path[len] = u;
        if (len >= 2 && f->adj[u][s]) {
            if (f->path[1] < u) add_face(f, f->path, len + 1);
        } else if (len + 1 < SW_SOLID_FACE_MAX) {
            rings(f, len + 1);
        }
    }
}

void sw_solid_init(sw_solid *s, const sw_machine *m)
{
    static int16_t pts[MAX_PTS][3];
    static uint8_t edges[MAX_MEDGES][2];
    static finder f;
    if (!m->rom) return;
    for (unsigned id = 0; id < SW_SOLID_MODELS; id++) {
        int ne = 0;
        int np = sw_obj_model(m, id, pts, MAX_PTS, edges, MAX_MEDGES, &ne);
        memset(&f, 0, sizeof f);
        f.pts = (const int16_t (*)[3])pts;
        f.n_pts = np;
        for (int i = 0; i < ne; i++) {
            if (edges[i][0] >= np || edges[i][1] >= np || edges[i][0] == edges[i][1]) continue;
            f.adj[edges[i][0]][edges[i][1]] = f.adj[edges[i][1]][edges[i][0]] = 1;
        }
        for (int a = 0; a < np; a++) {
            f.path[0] = a;
            rings(&f, 1);
        }
        free(s->model[id].faces);
        s->model[id].faces = f.faces;
        s->model[id].n = f.n;
    }
}

void sw_solid_free(sw_solid *s)
{
    for (int i = 0; i < SW_SOLID_MODELS; i++) free(s->model[i].faces);
    free(s->polys);
    free(s->edges);
    free(s->out);
    free(s->stack);
    for (int i = 0; i < SW_SOLID_MASKS; i++) free(s->mask[i].cells);
    memset(s, 0, sizeof *s);
}

/* --- what covers, in a picture ------------------------------------------------------ */

static sw_solid_poly *new_poly(sw_solid *s)
{
    if (s->n_polys == s->cap_polys) {
        int cap = s->cap_polys ? s->cap_polys * 2 : 256;
        sw_solid_poly *p = (sw_solid_poly *)realloc(s->polys, (size_t)cap * sizeof *p);
        if (!p) return NULL;
        s->polys = p;
        s->cap_polys = cap;
    }
    sw_solid_poly *p = &s->polys[s->n_polys];
    memset(p, 0, sizeof *p);
    p->first = s->n_edges;
    p->x0 = p->y0 = 1e9f;
    p->x1 = p->y1 = -1e9f;
    return p;
}

static int add_edge(sw_solid *s, sw_solid_poly *p, float x0, float y0, float x1, float y1)
{
    if (s->n_edges == s->cap_edges) {
        int cap = s->cap_edges ? s->cap_edges * 2 : 2048;
        float (*e)[4] = (float (*)[4])realloc(s->edges, (size_t)cap * sizeof *e);
        if (!e) return -1;
        s->edges = e;
        s->cap_edges = cap;
    }
    float *e = s->edges[s->n_edges++];
    e[0] = x0; e[1] = y0; e[2] = x1; e[3] = y1;
    p->n++;
    if (x0 < p->x0) p->x0 = x0;
    if (x1 < p->x0) p->x0 = x1;
    if (x0 > p->x1) p->x1 = x0;
    if (x1 > p->x1) p->x1 = x1;
    if (y0 < p->y0) p->y0 = y0;
    if (y1 < p->y0) p->y0 = y1;
    if (y0 > p->y1) p->y1 = y0;
    if (y1 > p->y1) p->y1 = y1;
    return 0;
}

/* A face in the picture: its n corners on the screen with their 1/X. */
static void picture_face(sw_solid *s, const float *x, const float *y, const float *w, int n, uint16_t inst, uint64_t mask)
{
    float area = 0, cx = 0, cy = 0, cw = 0;
    for (int i = 0; i < n; i++) {
        int b = (i + 1) % n;
        area += x[i] * y[b] - x[b] * y[i];
        cx += x[i]; cy += y[i]; cw += w[i];
    }
    if (fabsf(area) * 0.5f < MIN_AREA) return;
    sw_solid_poly *p = new_poly(s);
    if (!p) return;
    cx /= (float)n; cy /= (float)n; cw /= (float)n;
    /* 1/X across the face: the plane through its points, least squares */
    float sxx = 0, sxy = 0, syy = 0, sxw = 0, syw = 0;
    for (int i = 0; i < n; i++) {
        int b = (i + 1) % n;
        float dx = x[i] - cx, dy = y[i] - cy, dw = w[i] - cw;
        sxx += dx * dx; sxy += dx * dy; syy += dy * dy; sxw += dx * dw; syw += dy * dw;
        if (w[i] > p->wmax) p->wmax = w[i];
        if (add_edge(s, p, x[i], y[i], x[b], y[b]) != 0) return;
    }
    float det = sxx * syy - sxy * sxy;
    if (fabsf(det) > 1e-3f) {
        p->a = (sxw * syy - syw * sxy) / det;
        p->b = (syw * sxx - sxw * sxy) / det;
    }
    p->c = cw - p->a * cx - p->b * cy;
    p->inst = inst;
    p->points = mask;
    s->n_polys++;
}

/* The faces of one object drawn: lines[0..n-1] are its lines, and tell
 * where its points are on the screen and how far away. */
static void object_faces(sw_solid *s, const sw_line *lines, int n)
{
    float px[MAX_PTS] = { 0 }, py[MAX_PTS] = { 0 }, pw[MAX_PTS] = { 0 };
    static int16_t pv[MAX_PTS][3];
    uint64_t known = 0, known3 = 0;
    unsigned model = lines[0].model - 1u;
    if (model >= SW_SOLID_MODELS) return;
    for (int i = 0; i < n; i++) {
        const sw_line *l = &lines[i];
        if (l->d3 && l->p0 && l->p1 && l->p0 <= MAX_PTS && l->p1 <= MAX_PTS) {
            /* its points in space: the line itself may have been cut */
            memcpy(pv[l->p0 - 1], l->v0, sizeof pv[0]);
            memcpy(pv[l->p1 - 1], l->v1, sizeof pv[0]);
            known3 |= (1ull << (l->p0 - 1)) | (1ull << (l->p1 - 1));
            continue;
        }
        if (l->p0 && l->p0 <= MAX_PTS) {
            px[l->p0 - 1] = l->x0; py[l->p0 - 1] = l->y0; pw[l->p0 - 1] = (float)l->w0;
            known |= 1ull << (l->p0 - 1);
        }
        if (l->p1 && l->p1 <= MAX_PTS) {
            px[l->p1 - 1] = l->x1; py[l->p1 - 1] = l->y1; pw[l->p1 - 1] = (float)l->w1;
            known |= 1ull << (l->p1 - 1);
        }
    }
    for (int k = 0; k < s->model[model].n; k++) {
        const sw_solid_face *f = &s->model[model].faces[k];
        uint64_t mask = 0;
        float fx[SW_SOLID_FACE_MAX], fy[SW_SOLID_FACE_MAX], fw[SW_SOLID_FACE_MAX];
        for (int i = 0; i < f->n; i++) {
            int a = f->pt[i];
            mask |= 1ull << a;
            fx[i] = px[a]; fy[i] = py[a]; fw[i] = pw[a];
        }
        if ((mask & known3) == mask) {
            /* in space: what of the face is nearer than the nearest seen is cut off */
            float cx[SW_SOLID_FACE_MAX * 2], cy[SW_SOLID_FACE_MAX * 2], cw[SW_SOLID_FACE_MAX * 2];
            int nc = 0;
            for (int i = 0; i < f->n; i++) {
                const int16_t *a = pv[f->pt[i]], *b = pv[f->pt[(i + 1) % f->n]];
                if (a[0] >= SW_VIEW_NEAR) { sw_view_project(a[0], a[1], a[2], &cx[nc], &cy[nc], &cw[nc]); nc++; }
                if ((a[0] >= SW_VIEW_NEAR) != (b[0] >= SW_VIEW_NEAR)) {
                    double q = (SW_VIEW_NEAR - a[0]) / (double)(b[0] - a[0]);
                    sw_view_project(SW_VIEW_NEAR, a[1] + (b[1] - a[1]) * q, a[2] + (b[2] - a[2]) * q, &cx[nc], &cy[nc], &cw[nc]);
                    nc++;
                }
            }
            if (nc >= 3) picture_face(s, cx, cy, cw, nc, lines[0].inst, mask);
            continue;
        }
        if ((mask & known) != mask) continue;       /* (not all of it drawn: a damaged ship) */
        picture_face(s, fx, fy, fw, f->n, lines[0].inst, mask);
    }
}

static unsigned section_of(const sw_line *l) { return (unsigned)(l->id >> 58); }

/* What is drawn as an outline covers what is behind it: the letters of
 * the logo, the parts of the X-wing, the Death Star. Their lines are not
 * always rings (the X-wing's run off the screen), so the shape is found on
 * a grid: the lines of one instruction of the list are walls, the edge of
 * the screen is one too, and what cannot be reached from outside is the
 * shape. The X-wing's parts never change: a shape is kept while its lines
 * stay the same. */
static void mask_build(sw_solid *s, sw_solid_mask *k, const sw_line *lines, int n, unsigned min_zz)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        const sw_line *l = &lines[i];
        if (l->x0 < x0) x0 = l->x0;
        if (l->x1 < x0) x0 = l->x1;
        if (l->x0 > x1) x1 = l->x0;
        if (l->x1 > x1) x1 = l->x1;
        if (l->y0 < y0) y0 = l->y0;
        if (l->y1 < y0) y0 = l->y1;
        if (l->y0 > y1) y1 = l->y0;
        if (l->y1 > y1) y1 = l->y1;
    }
    /* beyond the edge of the screen there is nothing to cover */
    int wall_l = x0 < SCREEN_X0, wall_r = x1 > SCREEN_X1, wall_b = y0 < SCREEN_Y0, wall_t = y1 > SCREEN_Y1;
    if (wall_l) x0 = SCREEN_X0;
    if (wall_r) x1 = SCREEN_X1;
    if (wall_b) y0 = SCREEN_Y0;
    if (wall_t) y1 = SCREEN_Y1;
    k->w = k->h = 0;
    if (x1 <= x0 || y1 <= y0) return;
    const float MASK_RES = k->res;
    int w = (int)((x1 - x0) * MASK_RES) + 4, h = (int)((y1 - y0) * MASK_RES) + 4;
    if (w * h > k->cap) {
        uint8_t *c = (uint8_t *)realloc(k->cells, (size_t)(w * h));
        if (!c) return;
        k->cells = c;
        k->cap = w * h;
    }
    if (w * h > s->cap_stack) {
        int *st = (int *)realloc(s->stack, (size_t)(w * h) * sizeof *st);
        if (!st) return;
        s->stack = st;
        s->cap_stack = w * h;
    }
    k->w = w; k->h = h;
    k->ox = x0 - 1.5f / MASK_RES;           /* a cell of room all around */
    k->oy = y0 - 1.5f / MASK_RES;
    memset(k->cells, 0, (size_t)(w * h));
    for (int i = 0; i < n; i++) {
        const sw_line *l = &lines[i];
        if (l->zz < min_zz) continue;
        float dx = l->x1 - l->x0, dy = l->y1 - l->y0;
        int steps = (int)(sqrtf(dx * dx + dy * dy) * MASK_RES * 4.0f) + 1;
        for (int q = 0; q <= steps; q++) {
            int cx = (int)((l->x0 + dx * (float)q / (float)steps - k->ox) * MASK_RES);
            int cy = (int)((l->y0 + dy * (float)q / (float)steps - k->oy) * MASK_RES);
            if (cx >= 0 && cy >= 0 && cx < w && cy < h) k->cells[cy * w + cx] = 1;
        }
    }
    /* what can be reached from the rim is outside (2) */
    int top = 0;
    for (int x = 0; x < w; x++) {
        if (wall_b) k->cells[x] = 1;
        if (wall_t) k->cells[(h - 1) * w + x] = 1;
    }
    for (int y = 0; y < h; y++) {
        if (wall_l) k->cells[y * w] = 1;
        if (wall_r) k->cells[y * w + w - 1] = 1;
    }
    for (int c = 0; c < w * h; c++) {
        int x = c % w, y = c / w;
        if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && !k->cells[c]) {
            k->cells[c] = 2;
            s->stack[top++] = c;
        }
    }
    while (top) {
        int c = s->stack[--top];
        int x = c % w, y = c / w;
        if (x > 0 && !k->cells[c - 1]) { k->cells[c - 1] = 2; s->stack[top++] = c - 1; }
        if (x < w - 1 && !k->cells[c + 1]) { k->cells[c + 1] = 2; s->stack[top++] = c + 1; }
        if (y > 0 && !k->cells[c - w]) { k->cells[c - w] = 2; s->stack[top++] = c - w; }
        if (y < h - 1 && !k->cells[c + w]) { k->cells[c + w] = 2; s->stack[top++] = c + w; }
    }
}

/* The shape of lines[0..n-1], of those at least as bright as min_zz: the
 * one kept from the picture before if they are the same. */
static void use_mask(sw_solid *s, const sw_line *lines, int n, uint64_t group, unsigned min_zz, int front)
{
    float sum = 0;
    for (int i = 0; i < n; i++)
        sum += lines[i].x0 * 3.0f + lines[i].y0 * 5.0f + lines[i].x1 * 7.0f + lines[i].y1 * 11.0f;
    sw_solid_mask *k = NULL, *old = NULL;
    for (int q = 0; q < SW_SOLID_MASKS && !k; q++) {
        sw_solid_mask *c = &s->mask[q];
        if (c->active) continue;
        if (c->group == group && c->count == n && c->sum == sum && c->used) k = c;
        else if (!old || c->used < old->used) old = c;
    }
    if (!k && old) {
        k = old;
        k->group = group;
        k->count = n;
        k->sum = sum;
        k->res = section_of(&lines[0]) == SW_TAG_BANNER ? RES_LOGO : RES_FINE;
        mask_build(s, k, lines, n, min_zz);
    }
    if (!k) return;
    unsigned sec = section_of(&lines[0]);
    k->active = 1;
    k->used = s->run;
    k->c = sec == SW_TAG_PLANE ? W_FRONT : lines[0].w1 ? (float)lines[0].w1 : W_BACK;
    k->section = (uint8_t)sec;
    k->front = (uint8_t)front;
    k->zz = (uint8_t)min_zz;
}

static void outlines(sw_solid *s, const sw_line *lines, int n)
{
    for (int i = 0; i < SW_SOLID_MASKS; i++) s->mask[i].active = 0;
    s->run++;
    int i = 0;
    while (i < n) {
        unsigned sec = section_of(&lines[i]);
        int j = i;
        if (sec == SW_TAG_BANNER) {
            /* The logo is one thing, its letters run into each other. They
             * are blocks: the bright lines are the front of them, and what
             * is behind the front of the sides is not seen either. */
            unsigned zz = 0;
            for (; j < n && section_of(&lines[j]) == sec; j++)
                if (lines[j].zz > zz) zz = lines[j].zz;
            use_mask(s, lines + i, j - i, (uint64_t)sec << 58, 0, 0);
            use_mask(s, lines + i, j - i, ((uint64_t)sec << 58) | 1, zz, 1);
        } else if (sec == SW_TAG_PLANE || sec == SW_TAG_DSTAR) {
            uint64_t group = lines[i].id >> 16;
            while (j < n && (lines[j].id >> 16) == group) j++;
            use_mask(s, lines + i, j - i, group, 0, 0);
        } else {
            j++;
        }
        i = j;
    }
}

/* --- cutting the lines ---------------------------------------------------------------- */

static int inside(const sw_solid *s, const sw_solid_poly *p, float x, float y)
{
    int in = 0;
    for (int i = 0; i < p->n; i++) {
        const float *e = s->edges[p->first + i];
        if ((e[1] > y) != (e[3] > y) && x < e[0] + (y - e[1]) * (e[2] - e[0]) / (e[3] - e[1])) in ^= 1;
    }
    return in;
}

static int cmp_float(const void *a, const void *b)
{
    float d = *(const float *)a - *(const float *)b;
    return d < 0 ? -1 : d > 0;
}

static void emit(sw_solid *s, const sw_line *l, float t0, float t1)
{
    float dx = l->x1 - l->x0, dy = l->y1 - l->y0;
    if (t0 > 0.0f || t1 < 1.0f)
        if ((t1 - t0) * sqrtf(dx * dx + dy * dy) < MIN_PIECE) return;
    if (s->n_out == s->cap_out) {
        int cap = s->cap_out ? s->cap_out * 2 : 2048;
        sw_line *o = (sw_line *)realloc(s->out, (size_t)cap * sizeof *o);
        if (!o) return;
        s->out = o;
        s->cap_out = cap;
    }
    sw_line *o = &s->out[s->n_out++];
    *o = *l;
    o->x0 = l->x0 + dx * t0; o->y0 = l->y0 + dy * t0;
    o->x1 = l->x0 + dx * t1; o->y1 = l->y0 + dy * t1;
}

/* Never covered: what is written on the screen, the sight, the beams of
 * the lasers (they start at the X-wing's guns), and what the original
 * program wrote itself. */
static int always_shown(unsigned section)
{
    switch (section) {
    case SW_TAG_STAR: case SW_TAG_STARSEC: case SW_TAG_OBJECT: case SW_TAG_SHOT: case SW_TAG_XPLD:
    case SW_TAG_TRENCH: case SW_TAG_PANEL: case SW_TAG_DSTAR: case SW_TAG_BANNER:
        return 0;
    default:
        return 1;
    }
}

static void cut(sw_solid *s, const sw_line *l)
{
    float hid[MAX_CUTS][2];
    int n_hid = 0;
    float w0 = (float)l->w0, w1 = (float)l->w1;
    if (section_of(l) == SW_TAG_BANNER) w0 = w1 = W_BACK;
    float lx0 = l->x0 < l->x1 ? l->x0 : l->x1, lx1 = l->x0 < l->x1 ? l->x1 : l->x0;
    float ly0 = l->y0 < l->y1 ? l->y0 : l->y1, ly1 = l->y0 < l->y1 ? l->y1 : l->y0;
    float wmin = w0 < w1 ? w0 : w1;
    float dx = l->x1 - l->x0, dy = l->y1 - l->y0;
    unsigned section = section_of(l);
    if (wmin >= 65535.0f) { emit(s, l, 0.0f, 1.0f); return; }   /* as near as it gets: a shot on our shields */

    for (int q = 0; q < SW_SOLID_MASKS && n_hid < MAX_CUTS; q++) {
        const sw_solid_mask *k = &s->mask[q];
        if (!k->active || !k->w) continue;
        /* a thing does not cover itself, but for the front of the logo its sides */
        int own = k->front && k->section == section && l->zz < k->zz;
        if (!own && (k->front || k->section == section)) continue;
        const float MASK_RES = k->res;
        if (lx1 < k->ox || ly1 < k->oy || lx0 > k->ox + (float)k->w / MASK_RES || ly0 > k->oy + (float)k->h / MASK_RES)
            continue;
        if (!own && wmin >= k->c) continue;
        int steps = (int)(sqrtf(dx * dx + dy * dy) * MASK_RES * 2.0f) + 1;
        int from = -1;
        for (int i = 0; i <= steps + 1; i++) {
            int in = 0;
            if (i <= steps) {
                float tm = (float)i / (float)steps;
                int cx = (int)((l->x0 + dx * tm - k->ox) * MASK_RES), cy = (int)((l->y0 + dy * tm - k->oy) * MASK_RES);
                if (cx >= 0 && cy >= 0 && cx < k->w && cy < k->h) {
                    /* (the logo's sides: not where they run along its front's lines) */
                    uint8_t cell = k->cells[cy * k->w + cx];
                    in = own ? cell == 0 : cell != 2 && w0 + (w1 - w0) * tm < k->c * (1.0f - W_REL) - W_ABS;
                }
            }
            if (in && from < 0) from = i;
            if (!in && from >= 0) {
                if (n_hid < MAX_CUTS) {
                    hid[n_hid][0] = from == 0 ? 0.0f : ((float)from - 0.5f) / (float)steps;
                    hid[n_hid][1] = i > steps ? 1.0f : ((float)i - 0.5f) / (float)steps;
                    n_hid++;
                }
                from = -1;
            }
        }
    }
    for (int k = 0; k < s->n_polys; k++) {
        const sw_solid_poly *p = &s->polys[k];
        if (lx1 < p->x0 || lx0 > p->x1 || ly1 < p->y0 || ly0 > p->y1) continue;
        if (wmin >= p->wmax) continue;                      /* nearer than all of it */
        /* a face does not cover its own lines */
        if (p->inst == l->inst && l->p0 && l->p1 &&
            (p->points >> (l->p0 - 1) & 1) && (p->points >> (l->p1 - 1) & 1)) continue;
        /* where the line crosses the shape's edge */
        float t[MAX_CUTS];
        int nt = 0;
        t[nt++] = 0.0f;
        for (int i = 0; i < p->n && nt < MAX_CUTS - 1; i++) {
            const float *e = s->edges[p->first + i];
            float ex = e[2] - e[0], ey = e[3] - e[1];
            float den = dx * ey - dy * ex;
            if (fabsf(den) < 1e-9f) continue;
            float qx = e[0] - l->x0, qy = e[1] - l->y0;
            float tl = (qx * ey - qy * ex) / den, te = (qx * dy - qy * dx) / den;
            if (tl > 0.0f && tl < 1.0f && te >= 0.0f && te <= 1.0f) t[nt++] = tl;
        }
        t[nt++] = 1.0f;
        if (nt > 3) qsort(t, (size_t)nt, sizeof t[0], cmp_float);
        for (int i = 0; i + 1 < nt && n_hid < MAX_CUTS; i++) {
            if (t[i + 1] - t[i] < 1e-5f) continue;
            float tm = (t[i] + t[i + 1]) * 0.5f;
            float x = l->x0 + dx * tm, y = l->y0 + dy * tm;
            float wp = p->a * x + p->b * y + p->c;
            if (w0 + (w1 - w0) * tm >= wp * (1.0f - W_REL) - W_ABS) continue;   /* in front of it, or on it */
            if (!inside(s, p, x, y)) continue;
            hid[n_hid][0] = t[i];
            hid[n_hid][1] = t[i + 1];
            n_hid++;
        }
    }
    if (!n_hid) { emit(s, l, 0.0f, 1.0f); return; }
    qsort(hid, (size_t)n_hid, sizeof hid[0], cmp_float);
    float at = 0.0f;
    for (int i = 0; i < n_hid; i++) {
        if (hid[i][0] > at + 1e-5f) emit(s, l, at, hid[i][0]);
        if (hid[i][1] > at) at = hid[i][1];
    }
    if (at < 1.0f - 1e-5f) emit(s, l, at, 1.0f);
}

int sw_solid_run(sw_solid *s, const sw_line *in, int n, const sw_line **out)
{
    s->n_polys = s->n_edges = s->n_out = 0;
    for (int i = 0; i < n; ) {
        if (!in[i].inst || !in[i].model) { i++; continue; }
        int j = i;
        while (j < n && in[j].inst == in[i].inst) j++;
        object_faces(s, in + i, j - i);
        i = j;
    }
    for (int i = 0; i < s->n_extra; i++)
        picture_face(s, s->extra[i].x, s->extra[i].y, s->extra[i].w, s->extra[i].n, s->extra[i].inst, 0);
    outlines(s, in, n);
    for (int i = 0; i < n; i++) {
        if (always_shown(section_of(&in[i]))) emit(s, &in[i], 0.0f, 1.0f);
        else cut(s, &in[i]);
    }
    *out = s->out;
    return s->n_out;
}
