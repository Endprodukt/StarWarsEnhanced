/* dogfight.c - see dogfight.h. */
#include "dogfight/dogfight.h"

#include "core/sw_symbols.h"
#include "core/tags.h"
#include "game/game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VG_SCALE    (255.0 / 256.0 / 4.0)   /* VG units to the lines' units at the normal scale (VGSCAL) */
#define VG_OFFY     (-104.0)                /* VGOFFY: the middle of the view below the middle of the screen */
#define PERSPECTIVE 512.0
#define W_UNIT      8388608.0               /* how near (1/X) in the units of the game's notes */
#define NEAR        30.0
/* where the sight is: out of the middle of the view as the yoke has it
 * (in functions that have the dogfight d) */
#define SIGHT_X     ((double)d->sight_x)
#define SIGHT_Y     (VG_OFFY * VG_SCALE + (double)d->sight_y)
#define SIGHT_REACH_X 40.0                  /* how far it goes out with the yoke at its end */
#define SIGHT_REACH_Y 32.0

/* the box, and how fast the ship is (units a second) */
#define BOX         8000.0
#define SPEED_MIN   450.0
#define SPEED_MID   900.0
#define SPEED_MAX   1500.0
/* turning, radians a second at full yoke */
#define YAW_RATE    1.3
#define PITCH_RATE  1.1
#define ROLL_RATE   2.6
#define BACK_RATE   1.6                     /* turning back at a wall */
/* the lasers */
#define LASER_RANGE 9000.0
#define LASER_SHOW  0.15                    /* seconds a shot is seen */
#define LASER_EVERY 0.22                    /* the trigger held fires again */
/* the fireballs */
#define SHOT_SPEED  1500.0
#define SHOT_LIFE   6.0
#define SHOT_HITS   130.0                   /* how near one has to come to hit the ship */
#define SHIELDS     6

/* the Death Star, far off beyond the box */
static const double DEATH_STAR[3] = { 70000.0, 18000.0, 9000.0 };

/* the fourth choice, in the game's own units of the page (TDTH of PHESDS) */
#define CHOICE_X 400
#define CHOICE_Y (-300)

enum { BLU = 1, GRN = 2, TRQ = 3, RED = 4, YLW = 6, WHT = 7 };
enum { M_TIE, M_TI1, M_TI2, M_TI3, M_VADER, M_XWING };
enum { K_NONE, K_TIE, K_INTERCEPTOR, K_VADER };

/* requests for the sound board (docs/SOUNDS.md) */
enum {
    S_SPKFOA = 0x05, S_SPKHAV = 0x08, S_SPKHIT = 0x09, S_SPKR2T = 0x0f, S_SPKRED = 0x10,
    S_PMDAR = 0x1d, S_PMTH5 = 0x24, S_AUDPB = 0x2a, S_AUDRY = 0x32, S_AUDSH = 0x33,
    S_AUDSS = 0x34, S_AUDSX = 0x35, S_AUDTC = 0x36, S_AUDXL = 0x3a,
};

static const int KIND_HP[4] = { 0, 1, 2, 3 };
static const long KIND_SCORE[4] = { 0, 1000, 1500, 2000 };
static const double KIND_SIZE[4] = { 0, 230.0, 220.0, 250.0 };   /* for the laser to hit */

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352du;
    v ^= v >> 15; v *= 0x846ca68bu;
    return v ^ (v >> 16);
}

static double rnd(sw_dogfight *d)
{
    d->rng ^= d->rng << 13;
    d->rng ^= d->rng >> 17;
    d->rng ^= d->rng << 5;
    return (double)(d->rng & 0xffffff) / 16777216.0;
}

static void sound(sw_dogfight *d, int request)
{
    int next = (d->sound_w + 1) % (int)sizeof d->sound;
    if (next == d->sound_r) return;         /* (too many at once: this one is dropped) */
    d->sound[d->sound_w] = (uint8_t)request;
    d->sound_w = next;
}

int sw_dogfight_sound(sw_dogfight *d)
{
    if (d->sound_r == d->sound_w) return -1;
    int r = d->sound[d->sound_r];
    d->sound_r = (d->sound_r + 1) % (int)sizeof d->sound;
    return r;
}

/* --- vectors ------------------------------------------------------------------------------ */

static double dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void norm(double *a)
{
    double n = sqrt(dot(a, a));
    if (n > 1e-12) { a[0] /= n; a[1] /= n; a[2] /= n; }
}

static double dist(const double *a, const double *b)
{
    double v[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return sqrt(dot(v, v));
}

/* a and b turned by angle in their plane: a towards b */
static void turn(double *a, double *b, double angle)
{
    double c = cos(angle), s = sin(angle);
    for (int i = 0; i < 3; i++) {
        double x = a[i] * c + b[i] * s, y = b[i] * c - a[i] * s;
        a[i] = x;
        b[i] = y;
    }
}

/* the axes square to each other again (u = f x r: up, with r to the right) */
static void square3(double *f, double *r, double *u)
{
    norm(f);
    double k = dot(r, f);
    for (int i = 0; i < 3; i++) r[i] -= k * f[i];
    norm(r);
    u[0] = f[1] * r[2] - f[2] * r[1];
    u[1] = f[2] * r[0] - f[0] * r[2];
    u[2] = f[0] * r[1] - f[1] * r[0];
}

/* Turns a body's nose towards the direction `want` (a unit vector) by at
 * most `step` radians, its other axes with it. */
static void aim(double *f, double *r, double *u, const double *want, double step)
{
    double c = dot(f, want);
    double side[3] = { want[0] - c * f[0], want[1] - c * f[1], want[2] - c * f[2] };
    double n = sqrt(dot(side, side));
    if (n < 1e-9) {
        if (c > 0.0) return;                /* (already so) */
        for (int i = 0; i < 3; i++) side[i] = u[i];
    } else {
        for (int i = 0; i < 3; i++) side[i] /= n;
    }
    double a = acos(c > 1.0 ? 1.0 : c < -1.0 ? -1.0 : c);
    if (step > a) step = a;
    double axis[3] = { f[1] * side[2] - f[2] * side[1], f[2] * side[0] - f[0] * side[2], f[0] * side[1] - f[1] * side[0] };
    double *vs[2] = { r, u };
    for (int k = 0; k < 2; k++) {
        double *v = vs[k], ca = cos(step), sa = sin(step), ad = dot(axis, v);
        double cr[3] = { axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0] };
        for (int i = 0; i < 3; i++) v[i] = v[i] * ca + cr[i] * sa + axis[i] * ad * (1.0 - ca);
    }
    turn(f, side, step);
    square3(f, r, u);
}

/* --- the display list ----------------------------------------------------------------------- */

static uint16_t rom16(const sw_machine *m, uint16_t a)
{
    return (uint16_t)((m->rom->vrom[(a - 0x3000) & 0xfff] << 8) | m->rom->vrom[(a - 0x2fff) & 0xfff]);
}

static void emit(sw_dogfight *d, uint16_t w)
{
    if (d->at >= 0x2ffc) return;            /* (VG RAM is full: the rest is left out) */
    d->space[d->at] = (uint8_t)(w >> 8);
    d->space[d->at + 1] = (uint8_t)w;
    d->tags[d->at >> 1] = d->tag;
    d->at += 2;
}

static int vg(double v) { return (int)lrint(v / VG_SCALE); }

/* a vector (VG units), brightness zz 0 (a move) .. 7 */
static void vctr(sw_dogfight *d, int dx, int dy, int zz)
{
    emit(d, (uint16_t)(dy & 0x1fff));
    emit(d, (uint16_t)(((zz & 7) << 13) | (dx & 0x1fff)));
}

/* the beam to a place on the screen (in the lines' units) */
static void move_to(sw_dogfight *d, double x, double y)
{
    emit(d, SW_VGCNTR);
    vctr(d, vg(x), vg(y), 0);
}

static void color(sw_dogfight *d, unsigned c, unsigned lum) { emit(d, (uint16_t)(0x6000 | (c << 8) | (lum & 0xff))); }

/* Lettering in the game's own font (MESOUT's table of letter routines). */
static void text(sw_dogfight *d, const sw_machine *m, double x, double y, unsigned c, unsigned lum, uint16_t scale, const char *s)
{
    move_to(d, x, y);
    color(d, c, lum);
    emit(d, scale);
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        uint16_t t;
        if (ch >= 'A' && ch <= 'Z') t = (uint16_t)(SW_VGMSGA + 0x16 + 2 * (ch - 'A'));
        else if (ch >= '0' && ch <= '9') t = (uint16_t)(SW_VGMSGA + 2 + 2 * (ch - '0'));
        else if (ch == '.') t = SW_VGMSGA + 0x4e;
        else if (ch == '-') t = SW_VGMSGA + 0x58;
        else if (ch == ',') t = SW_VGMSGA + 0x4c;
        else t = SW_VGMSGA;                 /* (a space) */
        emit(d, rom16(m, t));
    }
    emit(d, SW_VGSCAL);
}

/* Lettering centered on x: the game's letters are all one width. */
#define LETTER 5.8
static void text_mid(sw_dogfight *d, const sw_machine *m, double x, double y, unsigned c, unsigned lum, uint16_t scale,
                     double width, const char *s)
{
    text(d, m, x - 0.5 * width * (double)strlen(s), y, c, lum, scale, s);
}

/* a point of space as the ship sees it: ahead, to the right, up */
static void view_of(const sw_dogfight *d, const double *p, double *v)
{
    double a[3] = { p[0] - d->pos[0], p[1] - d->pos[1], p[2] - d->pos[2] };
    v[0] = dot(a, d->f);
    v[1] = dot(a, d->r);
    v[2] = dot(a, d->u);
}

static void screen_of(const double *v, double *x, double *y)
{
    *x = PERSPECTIVE * v[1] / v[0] * VG_SCALE;
    *y = (PERSPECTIVE * v[2] / v[0] + VG_OFFY) * VG_SCALE;
}

/* Where a point of space is on the screen, as the ship sees it; 0 if
 * behind it. */
static int project(const sw_dogfight *d, const double *p, double *sx, double *sy, double *depth)
{
    double v[3];
    view_of(d, p, v);
    if (v[0] < 1.0) return 0;
    screen_of(v, sx, sy);
    if (depth) *depth = v[0];
    return 1;
}

static int on_screen(double x, double y, double margin)
{
    return x > -127.5 - margin && x < 127.5 + margin && y > -153.0 - margin && y < 150.0 + margin;
}

/* --- lines in space ------------------------------------------------------------------------ */

static sw_line *new_line(sw_dogfight *d)
{
    sw_avg_list *l = &d->list;
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 4096;
        sw_line *p = (sw_line *)realloc(l->lines, (size_t)cap * sizeof *p);
        if (!p) return NULL;
        l->lines = p;
        l->cap = cap;
    }
    sw_line *n = &l->lines[l->count++];
    memset(n, 0, sizeof *n);
    return n;
}

static uint16_t w_of(double x)
{
    double w = W_UNIT / x;
    return (uint16_t)(w > 65535.0 ? 65535.0 : w < 1.0 ? 1.0 : w);
}

/* A line between two points of space, cut at the nearest the ship sees
 * and at the edges of the screen. Marked as the game marks the lines of
 * its objects (model + 1, its points + 1, the thing drawn), so that
 * render/solid hides what is behind. */
static void line3(sw_dogfight *d, const double *pa, const double *pb, unsigned c, float lum, int model, int p0, int p1)
{
    double a[3], b[3], a3[3], b3[3];
    view_of(d, pa, a);
    view_of(d, pb, b);
    memcpy(a3, a, sizeof a3);
    memcpy(b3, b, sizeof b3);
    if (a[0] < NEAR && b[0] < NEAR) return;
    if (a[0] < NEAR) { double k = (NEAR - a[0]) / (b[0] - a[0]); for (int i = 0; i < 3; i++) a[i] += (b[i] - a[i]) * k; }
    if (b[0] < NEAR) { double k = (NEAR - b[0]) / (a[0] - b[0]); for (int i = 0; i < 3; i++) b[i] += (a[i] - b[i]) * k; }
    double x0, y0, x1, y1;
    screen_of(a, &x0, &y0);
    screen_of(b, &x1, &y1);
    double t0 = 0.0, t1 = 1.0, dx = x1 - x0, dy = y1 - y0;
    double p[4] = { -dx, dx, -dy, dy }, q[4] = { x0 + 127.5, 127.5 - x0, y0 + 153.0, 150.0 - y0 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) { if (q[i] < 0.0) return; continue; }
        double r = q[i] / p[i];
        if (p[i] < 0.0) { if (r > t1) return; if (r > t0) t0 = r; }
        else { if (r < t0) return; if (r < t1) t1 = r; }
    }
    if (lum < 4.0f) return;
    sw_line *l = new_line(d);
    if (!l) return;
    l->x0 = (float)(x0 + dx * t0); l->y0 = (float)(y0 + dy * t0);
    l->x1 = (float)(x0 + dx * t1); l->y1 = (float)(y0 + dy * t1);
    l->color = (uint8_t)c;
    l->zz = 7;
    l->intensity = (uint8_t)(lum > 255.0f ? 255.0f : lum);
    l->id = ((uint64_t)SW_TAG(SW_TAG_OBJECT, 0x3ff) << 48) | (0x80000000u + d->line_id++);
    l->w0 = w_of(a[0] + (b[0] - a[0]) * t0);
    l->w1 = w_of(a[0] + (b[0] - a[0]) * t1);
    l->inst = d->inst;
    l->model = (uint8_t)model;
    /* its ends in space as well (render/view.h, the same projection): the
     * faces of a ship cut at the edge of the screen or near are still known */
    int fits = 1;
    for (int i = 0; i < 3; i++) fits &= fabs(a3[i]) < 32000.0 && fabs(b3[i]) < 32000.0;
    if (fits && p0 && p1) {
        l->d3 = 1;
        for (int i = 0; i < 3; i++) { l->v0[i] = (int16_t)lrint(a3[i]); l->v1[i] = (int16_t)lrint(b3[i]); }
        l->p0 = (uint8_t)p0;
        l->p1 = (uint8_t)p1;
    } else {
        l->p0 = (uint8_t)(t0 > 0.0 || a3[0] < NEAR ? 0 : p0);
        l->p1 = (uint8_t)(t1 < 1.0 || b3[0] < NEAR ? 0 : p1);
    }
}

/* a point of a model on a body: its x ahead, y to the left, z up */
static void place(const sw_df_body *b, const double *q, double size, double *p)
{
    for (int i = 0; i < 3; i++) p[i] = b->pos[i] + (b->f[i] * q[0] - b->r[i] * q[1] + b->u[i] * q[2]) * size;
}

/* one of the ROM's models, every line */
static void draw_model(sw_dogfight *d, int k, const sw_df_body *b, double size, unsigned c, float lum, int rom_id)
{
    if (++d->inst == 0) d->inst = 0x8000;
    for (int i = 0; i < d->mne[k]; i++) {
        int e0 = d->me[k][i][0], e1 = d->me[k][i][1];
        if (e0 >= d->mnp[k] || e1 >= d->mnp[k]) continue;
        double q0[3] = { d->mp[k][e0][0], d->mp[k][e0][1], d->mp[k][e0][2] };
        double q1[3] = { d->mp[k][e1][0], d->mp[k][e1][1], d->mp[k][e1][2] };
        double a[3], bb[3];
        place(b, q0, size, a);
        place(b, q1, size, bb);
        line3(d, a, bb, c, lum, rom_id + 1, e0 + 1, e1 + 1);
    }
}

/* --- the TIE interceptor: a model of its own -------------------------------------------------- */

/* Its points (ahead, left, up; in the units of the ROM's TIE fighter, whose
 * wings stand 208 to each side): on each side a wing of four daggers bent
 * forward (0-7, 8-15), the ball of the cockpit (rings of six at its front
 * and back, 16-27), its window (28-33), and a pylon on each side (34-39). */
#define INT_PTS 40
static double int_pt[INT_PTS][3];
static int int_ready;

static void interceptor_build(void)
{
    static const double W[8][2] = {             /* the wing's outline: ahead, up */
        { -70, 0 }, { -115, 195 }, { 0, 80 }, { 160, 235 }, { 60, 0 }, { 160, -235 }, { 0, -80 }, { -115, -195 },
    };
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < 8; i++) {
            int_pt[s * 8 + i][0] = W[i][0];
            int_pt[s * 8 + i][1] = s ? -205.0 : 205.0;
            int_pt[s * 8 + i][2] = W[i][1];
        }
    for (int i = 0; i < 6; i++) {
        double a = i * 1.0471976;
        int_pt[16 + i][0] = 42.0;  int_pt[16 + i][1] = 55.0 * cos(a); int_pt[16 + i][2] = 55.0 * sin(a);
        int_pt[22 + i][0] = -42.0; int_pt[22 + i][1] = 55.0 * cos(a); int_pt[22 + i][2] = 55.0 * sin(a);
        int_pt[28 + i][0] = 50.0;  int_pt[28 + i][1] = 24.0 * cos(a); int_pt[28 + i][2] = 24.0 * sin(a);
    }
    for (int s = 0; s < 2; s++) {
        double y = s ? -1.0 : 1.0;
        int_pt[34 + s * 3][0] = 0.0; int_pt[34 + s * 3][1] = 55.0 * y;  int_pt[34 + s * 3][2] = 10.0;
        int_pt[35 + s * 3][0] = 0.0; int_pt[35 + s * 3][1] = 205.0 * y; int_pt[35 + s * 3][2] = 10.0;
        int_pt[36 + s * 3][0] = 0.0; int_pt[36 + s * 3][1] = 205.0 * y; int_pt[36 + s * 3][2] = -10.0;
    }
    int_ready = 1;
}

/* a face of the interceptor on the screen, for render/solid */
static void int_face(sw_dogfight *d, const sw_df_body *b, const int *pts, int n)
{
    if (d->n_faces == SW_DF_FACES) return;
    sw_solid_extra *f = &d->faces[d->n_faces];
    for (int i = 0; i < n; i++) {
        double p[3], v[3], x, y;
        place(b, int_pt[pts[i]], 1.0, p);
        view_of(d, p, v);
        if (v[0] < NEAR) return;
        screen_of(v, &x, &y);
        f->x[i] = (float)x;
        f->y[i] = (float)y;
        f->w[i] = (float)w_of(v[0]);
    }
    f->inst = d->inst;
    f->n = (uint8_t)n;
    d->n_faces++;
}

static void draw_interceptor(sw_dogfight *d, const sw_df_body *b, float lum)
{
    if (!int_ready) interceptor_build();
    if (++d->inst == 0) d->inst = 0x8000;
    double p[INT_PTS][3];
    for (int i = 0; i < INT_PTS; i++) place(b, int_pt[i], 1.0, p[i]);
    for (int s = 0; s < 2; s++) {
        int w = s * 8;
        for (int i = 0; i < 8; i++) line3(d, p[w + i], p[w + (i + 1) % 8], GRN, lum, 0, 0, 0);
        line3(d, p[w + 0], p[w + 4], GRN, lum * 0.6f, 0, 0, 0);                  /* its spar */
        line3(d, p[34 + s * 3], p[35 + s * 3], GRN, lum, 0, 0, 0);               /* the pylon */
        line3(d, p[34 + s * 3], p[36 + s * 3], GRN, lum * 0.7f, 0, 0, 0);
        /* the wing's faces: four convex pieces */
        int q0[3] = { w + 0, w + 1, w + 2 }, q1[4] = { w + 0, w + 2, w + 3, w + 4 };
        int q2[4] = { w + 0, w + 4, w + 5, w + 6 }, q3[3] = { w + 0, w + 6, w + 7 };
        int_face(d, b, q0, 3);
        int_face(d, b, q1, 4);
        int_face(d, b, q2, 4);
        int_face(d, b, q3, 3);
    }
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        line3(d, p[16 + i], p[16 + j], GRN, lum, 0, 0, 0);
        line3(d, p[22 + i], p[22 + j], GRN, lum, 0, 0, 0);
        line3(d, p[16 + i], p[22 + i], GRN, lum * 0.8f, 0, 0, 0);
        line3(d, p[28 + i], p[28 + j], GRN, lum * 0.8f, 0, 0, 0);
        int side[4] = { 16 + i, 16 + j, 22 + j, 22 + i };
        int_face(d, b, side, 4);
    }
    int front[6] = { 16, 17, 18, 19, 20, 21 }, back[6] = { 22, 23, 24, 25, 26, 27 };
    int_face(d, b, front, 6);
    int_face(d, b, back, 6);
}

/* --- the dogfight ---------------------------------------------------------------------------- */

static void body_facing(sw_df_body *b, const double *pos, const double *to)
{
    for (int i = 0; i < 3; i++) { b->pos[i] = pos[i]; b->f[i] = to[i] - pos[i]; }
    norm(b->f);
    double up[3] = { 0.0, 0.0, 1.0 };
    if (fabs(b->f[2]) > 0.9) { up[0] = 1.0; up[2] = 0.0; }
    b->r[0] = b->f[1] * up[2] - b->f[2] * up[1];             /* r = f x up: to the right */
    b->r[1] = b->f[2] * up[0] - b->f[0] * up[2];
    b->r[2] = b->f[0] * up[1] - b->f[1] * up[0];
    square3(b->f, b->r, b->u);
}

static void begin_wave(sw_dogfight *d)
{
    d->wave++;
    d->wave_t = d->t;
    int n = 2 + d->wave;
    if (n > 14) n = 14;
    int vader = d->wave % 5 == 0 ? 1 : 0;
    int inter = d->wave >= 3 ? (d->wave - 1) / 2 : 0;
    if (inter > n - 1 - vader) inter = n - 1 - vader;
    d->pending[K_VADER] = vader;
    d->pending[K_INTERCEPTOR] = inter;
    d->pending[K_TIE] = n - vader - inter;
    if (vader) {
        sound(d, S_PMDAR);                  /* Darth Vader's theme, and his voice */
        sound(d, S_SPKHAV);
    }
}

/* Brings in one waiting enemy, if there is room: at the walls of the box,
 * away from the ship. */
static void spawn(sw_dogfight *d)
{
    int kind = d->pending[K_VADER] ? K_VADER : d->pending[K_INTERCEPTOR] && rnd(d) < 0.5 ? K_INTERCEPTOR :
               d->pending[K_TIE] ? K_TIE : d->pending[K_INTERCEPTOR] ? K_INTERCEPTOR : K_NONE;
    if (kind == K_NONE) return;
    int slot = -1, alive = 0;
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        if (d->enemy[i].kind) alive++;
        else if (slot < 0) slot = i;
    }
    if (slot < 0 || alive >= 8) return;
    double pos[3] = { BOX * 0.95, 0.0, 0.0 };
    for (int tries = 0; tries < 20; tries++) {
        double az = rnd(d) * 6.2831853, el = (rnd(d) - 0.5) * 1.2;
        pos[0] = BOX * 0.95 * cos(el) * cos(az);
        pos[1] = BOX * 0.95 * cos(el) * sin(az);
        pos[2] = BOX * 0.6 * sin(el);
        if (dist(pos, d->pos) > 5000.0) break;
    }
    sw_df_enemy *e = &d->enemy[slot];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->hp = KIND_HP[kind];
    body_facing(&e->b, pos, d->pos);
    double hard = 1.0 + 0.03 * (d->wave - 1);
    if (hard > 1.6) hard = 1.6;
    e->speed = (kind == K_INTERCEPTOR ? 1150.0 : kind == K_VADER ? 1050.0 : 950.0) * hard;
    e->turn = (kind == K_INTERCEPTOR ? 1.7 : kind == K_VADER ? 1.4 : 1.1) * hard;
    e->fire_t = d->t + 2.0 + rnd(d) * 2.0;
    d->pending[kind]--;
}

static void explode(sw_dogfight *d, const sw_df_body *b, double speed)
{
    static const int parts[6] = { M_TI1, M_TI2, M_TI3, M_TI1, M_TI2, M_TI3 };
    for (int k = 0; k < 6; k++) {
        for (int i = 0; i < SW_DF_PIECES; i++) {
            sw_df_piece *p = &d->piece[i];
            if (p->live) continue;
            p->live = 1;
            p->model = parts[k];
            p->b = *b;
            p->t = d->t;
            for (int j = 0; j < 3; j++) {
                p->vel[j] = b->f[j] * speed * 0.4 + (rnd(d) - 0.5) * 900.0;
                p->spin[j] = (rnd(d) - 0.5) * 8.0;
            }
            break;
        }
    }
    sound(d, S_AUDSX);
}

void sw_dogfight_start(sw_dogfight *d, const sw_machine *m)
{
    sw_line *keep = d->list.lines;
    int cap = d->list.cap;
    memset(d, 0, sizeof *d);
    d->list.lines = keep;
    d->list.cap = cap;
    if (m->rom) memcpy(d->space + 0x3000, m->rom->vrom, sizeof m->rom->vrom);
    static const unsigned ids[SW_DF_MODELS] = { SW_TDSTIE, SW_TDSTI1, SW_TDSTI2, SW_TDSTI3, SW_TDSRTH, SW_TDSXW };
    for (int k = 0; k < SW_DF_MODELS; k++) {
        int ne = 0;
        d->mnp[k] = m->rom ? sw_obj_model(m, ids[k], d->mp[k], 64, d->me[k], 160, &ne) : 0;
        d->mne[k] = ne < 160 ? ne : 160;
    }
    d->active = 1;
    d->rng = 0x2545f491u;
    d->pos[0] = -BOX * 0.6;
    d->f[0] = 1.0;
    d->r[1] = 1.0;
    square3(d->f, d->r, d->u);
    d->speed = SPEED_MID;
    d->shields = SHIELDS;
    d->chase_t = 6.0;
    for (int i = 0; i < 48; i++) {
        uint32_t h = hash((uint32_t)i * 7919u + 3u);
        d->dust[i][0] = (float)((double)(h & 0x3ff) / 1023.0 * 3000.0 - 1500.0);
        d->dust[i][1] = (float)((double)((h >> 10) & 0x3ff) / 1023.0 * 3000.0 - 1500.0);
        d->dust[i][2] = (float)((double)((h >> 20) & 0x3ff) / 1023.0 * 3000.0 - 1500.0);
    }
    sound(d, S_SPKRED);                     /* "Red Five standing by" */
    sound(d, S_PMTH5);
    begin_wave(d);
}

static float ease(float v, float to, double k)
{
    return v + (to - v) * (float)(k > 1.0 ? 1.0 : k);
}

/* The player's shot: the nearest enemy or fireball under the sight within
 * range is hit. */
static void shoot(sw_dogfight *d)
{
    d->laser_t = d->t;
    d->laser_side ^= 1;
    sound(d, S_AUDXL);
    double best = LASER_RANGE;
    int hit_enemy = -1, hit_shot = -1;
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        sw_df_enemy *e = &d->enemy[i];
        double x, y, z;
        if (!e->kind || !project(d, e->b.pos, &x, &y, &z) || z > best) continue;
        double r = PERSPECTIVE * KIND_SIZE[e->kind] / z * VG_SCALE;
        if (r < 3.0) r = 3.0;
        if (hypot(x - SIGHT_X, y - SIGHT_Y) < r) { best = z; hit_enemy = i; hit_shot = -1; }
    }
    for (int i = 0; i < SW_DF_SHOTS; i++) {
        sw_df_shot *s = &d->shot[i];
        double x, y, z;
        if (!s->live || !project(d, s->pos, &x, &y, &z) || z > best) continue;
        double r = PERSPECTIVE * 90.0 / z * VG_SCALE;
        if (r < 4.0) r = 4.0;
        if (hypot(x - SIGHT_X, y - SIGHT_Y) < r) { best = z; hit_shot = i; hit_enemy = -1; }
    }
    if (hit_shot >= 0) {
        d->shot[hit_shot].live = 0;
        d->score += 33;
        sound(d, S_AUDSS);
    }
    if (hit_enemy >= 0) {
        sw_df_enemy *e = &d->enemy[hit_enemy];
        e->hit_t = d->t;
        if (--e->hp <= 0) {
            d->score += KIND_SCORE[e->kind];
            explode(d, &e->b, e->speed);
            e->kind = K_NONE;
        } else {
            sound(d, S_AUDSS);
        }
    }
}

static void lose_shield(sw_dogfight *d)
{
    if (d->over) return;
    d->shields--;
    d->hit_t = d->t;
    sound(d, S_AUDSH);
    if (d->shields == SHIELDS - 1) sound(d, S_SPKHIT);         /* "I'm hit, but not bad" */
    if (d->shields == 1) sound(d, S_SPKR2T);                    /* "R2, try and increase the power" */
    if (d->shields <= 0) {
        d->shields = 0;
        d->over = 1;
        d->over_t = d->t;
        sound(d, S_SPKFOA);                 /* "The Force will be with you, always" */
    }
}

static void enemies_step(sw_dogfight *d, double dt)
{
    double hard = 1.0 + 0.07 * (d->wave - 1);
    if (hard > 2.5) hard = 2.5;
    int alive = 0;
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        sw_df_enemy *e = &d->enemy[i];
        if (!e->kind) continue;
        alive++;
        double to[3];
        for (int k = 0; k < 3; k++) to[k] = d->pos[k] + d->f[k] * d->speed * 0.6 - e->b.pos[k];
        double far = sqrt(dot(to, to));
        norm(to);
        double want[3] = { to[0], to[1], to[2] };
        if (e->mode == 0 && (far < 1100.0 || (far < 1800.0 && dot(e->b.f, to) < 0.0))) {
            /* close in: break away, somewhere aside */
            e->mode = 1;
            e->mode_t = d->t + 1.5 + rnd(d) * 1.5;
            double side[3] = { rnd(d) - 0.5, rnd(d) - 0.5, rnd(d) - 0.5 };
            for (int k = 0; k < 3; k++) e->away[k] = e->b.f[k] + side[k] * 1.6;
            norm(e->away);
        }
        if (e->mode == 1) {
            for (int k = 0; k < 3; k++) want[k] = e->away[k];
            if (d->t > e->mode_t) e->mode = 0;
        }
        if (fabs(e->b.pos[0]) > BOX * 1.15 || fabs(e->b.pos[1]) > BOX * 1.15 || fabs(e->b.pos[2]) > BOX * 1.15) {
            for (int k = 0; k < 3; k++) want[k] = -e->b.pos[k];
            norm(want);
        }
        aim(e->b.f, e->b.r, e->b.u, want, e->turn * dt);
        for (int k = 0; k < 3; k++) e->b.pos[k] += e->b.f[k] * e->speed * dt;
        /* it passes close: its sound */
        double near = dist(e->b.pos, d->pos);
        if (near < 700.0 && !e->passed) { e->passed = 1; sound(d, S_AUDPB); }
        if (near > 1500.0) e->passed = 0;
        /* it rams the ship */
        if (near < 110.0) {
            explode(d, &e->b, e->speed);
            e->kind = K_NONE;
            lose_shield(d);
            continue;
        }
        /* it fires: the ship before its nose, not too far */
        if (!d->over && e->mode == 0 && d->t > e->fire_t && far < 5500.0 && dot(e->b.f, to) > 0.93) {
            for (int s = 0; s < SW_DF_SHOTS; s++) {
                sw_df_shot *sh = &d->shot[s];
                if (sh->live) continue;
                sh->live = 1;
                sh->t = d->t;
                double lead = far / SHOT_SPEED, at[3];
                for (int k = 0; k < 3; k++) at[k] = d->pos[k] + d->f[k] * d->speed * lead;
                for (int k = 0; k < 3; k++) { sh->pos[k] = e->b.pos[k] + e->b.f[k] * 120.0; sh->vel[k] = at[k] - sh->pos[k]; }
                norm(sh->vel);
                for (int k = 0; k < 3; k++) sh->vel[k] *= SHOT_SPEED;
                break;
            }
            sound(d, S_AUDTC);
            double every = (e->kind == K_VADER ? 1.6 : e->kind == K_INTERCEPTOR ? 2.2 : 2.8) / hard;
            e->fire_t = d->t + every * (0.7 + 0.6 * rnd(d));
        }
    }
    /* the fireballs */
    for (int s = 0; s < SW_DF_SHOTS; s++) {
        sw_df_shot *sh = &d->shot[s];
        if (!sh->live) continue;
        for (int k = 0; k < 3; k++) sh->pos[k] += sh->vel[k] * dt;
        if (d->t - sh->t > SHOT_LIFE) { sh->live = 0; continue; }
        if (dist(sh->pos, d->pos) < SHOT_HITS) { sh->live = 0; lose_shield(d); }
    }
    /* the pieces of what blew up */
    for (int i = 0; i < SW_DF_PIECES; i++) {
        sw_df_piece *p = &d->piece[i];
        if (!p->live) continue;
        if (d->t - p->t > 2.5) { p->live = 0; continue; }
        for (int k = 0; k < 3; k++) p->b.pos[k] += p->vel[k] * dt;
        turn(p->b.f, p->b.r, p->spin[0] * dt);
        turn(p->b.f, p->b.u, p->spin[1] * dt);
        turn(p->b.r, p->b.u, p->spin[2] * dt);
        square3(p->b.f, p->b.r, p->b.u);
    }
    /* the wave: in, and when it is over, R2 mends a shield and the next comes */
    int waiting = d->pending[K_TIE] + d->pending[K_INTERCEPTOR] + d->pending[K_VADER];
    if (waiting && d->t - d->wave_t > 2.5) spawn(d);
    if (!waiting && !alive && !d->over && d->t - d->wave_t > 3.0) {
        if (d->shields < SHIELDS) {
            d->shields++;
            d->mend_t = d->t;
            sound(d, S_AUDRY);
        }
        begin_wave(d);
    }
}

void sw_dogfight_step(sw_dogfight *d, const sw_dogfight_input *in, double dt)
{
    if (!d->active || dt <= 0.0) return;
    if (dt > 0.1) dt = 0.1;
    d->t += dt;
    if (d->over && d->t - d->over_t > 7.0) d->finished = 1;

    /* the throttle: a button held pulls it its way, twice as fast while it
     * is still on the other side of the middle; let go, it goes back */
    int roll = in->faster && in->slower && !d->over;
    if (in->faster && !roll) d->throttle += (float)(dt * (d->throttle < 0.0f ? 3.0 : 1.5));
    else if (in->slower && !roll) d->throttle -= (float)(dt * (d->throttle > 0.0f ? 3.0 : 1.5));
    else if (!roll) d->throttle = ease(d->throttle, 0.0f, dt * 0.8);
    if (d->throttle > 1.0f) d->throttle = 1.0f;
    if (d->throttle < -1.0f) d->throttle = -1.0f;
    double target = SPEED_MID + (d->throttle >= 0.0f ? d->throttle * (SPEED_MAX - SPEED_MID) : d->throttle * (SPEED_MID - SPEED_MIN));
    d->speed += (target - d->speed) * (dt * 1.5 > 1.0 ? 1.0 : dt * 1.5);

    /* at a wall of the box the ship turns back by itself */
    int outside = fabs(d->pos[0]) > BOX || fabs(d->pos[1]) > BOX || fabs(d->pos[2]) > BOX;
    double home[3] = { -d->pos[0], -d->pos[1], -d->pos[2] };
    norm(home);
    if (outside && !d->turning_back) { d->turning_back = 1; d->back_since = d->t; }
    if (d->turning_back) {
        int inside = fabs(d->pos[0]) < BOX * 0.9 && fabs(d->pos[1]) < BOX * 0.9 && fabs(d->pos[2]) < BOX * 0.9;
        if (inside && dot(d->f, home) > 0.85) d->turning_back = 0;
    }
    if (d->turning_back) {
        /* nose towards the middle of the box, the yoke not listened to */
        d->yaw_rate = ease(d->yaw_rate, 0.0f, dt * 6.0);
        d->pitch_rate = ease(d->pitch_rate, 0.0f, dt * 6.0);
        d->roll_rate = ease(d->roll_rate, 0.0f, dt * 6.0);
        aim(d->f, d->r, d->u, home, BACK_RATE * dt);
        d->sight_x = ease(d->sight_x, 0.0f, dt * 6.0);
        d->sight_y = ease(d->sight_y, 0.0f, dt * 6.0);
    } else {
        /* The yoke moves the sight, at once and softly; the ship turns as
         * far out as the sight is, gently near the middle (small aims
         * without turning), fully at its reach. With both thumb buttons the
         * yoke rolls the ship instead. */
        float tx = roll || d->over ? 0.0f : in->yaw * (float)SIGHT_REACH_X;
        float ty = d->over ? 0.0f : in->pitch * (float)SIGHT_REACH_Y;
        d->sight_x = ease(d->sight_x, tx, dt * 12.0);
        d->sight_y = ease(d->sight_y, ty, dt * 12.0);
        float nx = d->sight_x / (float)SIGHT_REACH_X, ny = d->sight_y / (float)SIGHT_REACH_Y;
        float cx = nx * (0.3f + 0.7f * fabsf(nx)), cy = ny * (0.3f + 0.7f * fabsf(ny));
        float yaw = cx * (float)YAW_RATE;
        float rl = roll ? in->yaw * (float)ROLL_RATE : 0.0f;
        if (d->over) rl = 0.0f;
        d->yaw_rate = ease(d->yaw_rate, yaw, dt * 5.0);
        d->pitch_rate = ease(d->pitch_rate, cy * (float)PITCH_RATE, dt * 5.0);
        d->roll_rate = ease(d->roll_rate, rl, dt * 5.0);
        turn(d->f, d->r, d->yaw_rate * dt);         /* to the right: f towards r */
        turn(d->f, d->u, d->pitch_rate * dt);       /* up: f towards u */
        turn(d->r, d->u, -d->roll_rate * dt);       /* rolling right: r goes down */
        square3(d->f, d->r, d->u);
    }
    for (int i = 0; i < 3; i++) d->pos[i] += d->f[i] * d->speed * dt;

    /* the lasers: a shot for each pull of the trigger, and again and again while it is held */
    if (!d->over && in->fire && d->t >= d->fire_next) {
        shoot(d);
        d->fire_next = d->t + LASER_EVERY;
    }
    if (!in->fire) d->fire_next = 0.0;
    enemies_step(d, dt);
}

void sw_dogfight_attitude(const sw_dogfight *d, float *bank, float *pitch)
{
    float b = d->roll_rate / (float)ROLL_RATE + 0.6f * d->yaw_rate / (float)YAW_RATE;
    float p = d->pitch_rate / (float)PITCH_RATE;
    *bank = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;
    *pitch = p > 1.0f ? 1.0f : p < -1.0f ? -1.0f : p;
}

/* --- the picture ----------------------------------------------------------------------------- */

/* Far off, now and then: an X-wing chased by TIE fighters across the sky
 * (only to be seen). Where the chase is at its time t (0..1), and which
 * way it goes. */
static void chase_at(double t, uint32_t h, double *p, double *fw)
{
    double az = (double)(h & 0xffff) / 65536.0 * 6.2831853, el = ((double)((h >> 16) & 0xff) / 255.0 - 0.5) * 0.8;
    double c[3] = { cos(el) * cos(az), cos(el) * sin(az), sin(el) };
    double side[3] = { -sin(az), cos(az), 0.0 };
    double ph = (double)(h & 7);
    double s = (t * 2.0 - 1.0) * 16000.0, bob = 2500.0 * sin(t * 7.0 + ph), wob = 1800.0 * sin(t * 4.3);
    for (int i = 0; i < 3; i++) {
        double up = i == 2 ? 1.0 : 0.0;
        p[i] = c[i] * 26000.0 + side[i] * s + up * bob + c[i] * wob;
        fw[i] = side[i] * 32000.0 + up * 2500.0 * 7.0 * cos(t * 7.0 + ph) + c[i] * 1800.0 * 4.3 * cos(t * 4.3);
    }
    norm(fw);
}

static void chase(sw_dogfight *d)
{
    const double RUN = 11.0, EVERY = 19.0;
    double t = (d->t - d->chase_t) / RUN;
    if (t < 0.0) return;
    if (t > 1.0) {
        if (d->t - d->chase_t > EVERY) d->chase_t = d->t;
        return;
    }
    uint32_t h = hash((uint32_t)(d->chase_t * 10.0) + 17u);
    float lum = 120.0f;
    if (t < 0.1) lum *= (float)(t / 0.1);
    if (t > 0.9) lum *= (float)((1.0 - t) / 0.1);
    /* (one time TIE fighters after an X-wing, the next X-wings after a TIE) */
    int rebels_chase = (h >> 20) & 1;
    int chasers = rebels_chase ? 1 + (int)((h >> 21) & 1) : 2;
    double lead[3], lf[3];
    chase_at(t, h, lead, lf);
    for (int k = 0; k <= chasers; k++) {
        double p[3], fw[3];
        chase_at(t - 0.045 * k, h, p, fw);
        if (k) p[2] += k == 1 ? 900.0 : -700.0;
        sw_df_body b;
        double to[3] = { p[0] + fw[0], p[1] + fw[1], p[2] + fw[2] };
        body_facing(&b, p, to);
        int xwing = (k == 0) != rebels_chase;
        if (xwing) draw_model(d, M_XWING, &b, 1.0, WHT, lum, SW_TDSXW);
        else draw_model(d, M_TIE, &b, 1.0, GRN, lum * 0.8f, SW_TDSTIE);
        /* now and then a bolt of a chaser after the one it chases (red
         * from an X-wing, as in the films; green from a TIE) */
        double ph = fmod(d->t * 1.3 + k * 0.4, 1.0);
        if (k && ph < 0.25) {
            double a = ph / 0.25, b1 = a + 0.12 > 1.0 ? 1.0 : a + 0.12, s0[3], s1[3];
            for (int i = 0; i < 3; i++) {
                s0[i] = p[i] + (lead[i] - p[i]) * a;
                s1[i] = p[i] + (lead[i] - p[i]) * b1;
            }
            line3(d, s0, s1, xwing ? RED : GRN, lum + 60.0f, 0, 0, 0);
        }
    }
}

/* A fireball: a sparkle of spokes, white about a red heart, turning and
 * flickering, as large as it is near. (The game's own, VJGNB, takes its
 * sparkle from patterns the game keeps in VG RAM each frame: not to be had
 * here.) */
static void fireball(sw_dogfight *d, double x, double y, double z, int n)
{
    double r = PERSPECTIVE * 80.0 / z * VG_SCALE;
    if (r < 1.5) r = 1.5;
    if (r > 14.0) r = 14.0;
    double spin = d->t * 7.0 + n * 1.3, flick = 0.75 + 0.25 * sin(d->t * 31.0 + n * 2.1);
    for (int k = 0; k < 8; k++) {
        double a = spin + k * 0.7853982, len = r * (k & 1 ? 0.6 : 1.0) * flick;
        move_to(d, x, y);
        color(d, k & 1 ? RED : WHT, k & 1 ? 0xc0 : 0xe0);
        vctr(d, vg(cos(a) * len), vg(sin(a) * len), 7);
    }
}

static void number(char *s, long v)
{
    char tmp[16];
    int n = 0;
    if (v <= 0) tmp[n++] = '0';
    for (; v > 0 && n < 15; v /= 10) tmp[n++] = (char)('0' + v % 10);
    for (int i = 0; i < n; i++) s[i] = tmp[n - 1 - i];
    s[n] = 0;
}

int sw_dogfight_lines(sw_dogfight *d, const sw_machine *m, const sw_line **out)
{
    d->at = 0;
    d->n_faces = 0;
    d->inst = 0x8000;
    d->line_id = 0;
    d->tag = SW_TAG(SW_TAG_INIT, 0);
    emit(d, SW_VGSCAL);

    /* the stars, as good as infinitely far: they turn with the ship but do
     * not come nearer */
    d->tag = SW_TAG(SW_TAG_STARSEC, 0x3ff);
    color(d, WHT, 0x80);
    for (int i = 0; i < 160; i++) {
        uint32_t h = hash((uint32_t)i * 2654435761u + 99u);
        double az = (double)(h & 0xffff) / 65536.0 * 6.2831853, el = asin((double)((h >> 16) & 0xffff) / 32768.0 - 1.0);
        double p[3] = { d->pos[0] + 1e7 * cos(el) * cos(az), d->pos[1] + 1e7 * cos(el) * sin(az), d->pos[2] + 1e7 * sin(el) };
        double x, y;
        if (!project(d, p, &x, &y, NULL) || !on_screen(x, y, 0.0)) continue;
        d->tag = SW_TAG(SW_TAG_STAR, i & 0x3ff);
        move_to(d, x, y);
        emit(d, rom16(m, SW_VGSTAR));
    }
    /* specks near the ship: one sees it go */
    color(d, WHT, 0x40);
    for (int i = 0; i < 48; i++) {
        double p[3];
        for (int k = 0; k < 3; k++) {
            double v = d->dust[i][k] - d->pos[k];
            v -= 3000.0 * floor((v + 1500.0) / 3000.0);    /* (they come round again: a cube of them about the ship) */
            p[k] = d->pos[k] + v;
        }
        double x, y, z;
        if (!project(d, p, &x, &y, &z) || z < 60.0 || !on_screen(x, y, 0.0)) continue;
        d->tag = SW_TAG(SW_TAG_STAR, (160 + i) & 0x3ff);
        move_to(d, x, y);
        emit(d, rom16(m, SW_VGSTAR));
    }

    /* the Death Star, as the game draws it as it comes near (DTHVW) */
    {
        double x, y;
        if (project(d, DEATH_STAR, &x, &y, NULL) && on_screen(x, y, 60.0)) {
            const uint16_t scale = SW_VGSCAL + 0x300 + 0x40;     /* (its stamps are big: the game draws them small at first) */
            static const uint16_t stamps[5] = { SW_VJBCIR, SW_VJBTRN, SW_VJBDSH, SW_VJBNSD, SW_VJBFRM };
            d->tag = SW_TAG(SW_TAG_DSTAR, 3);
            color(d, GRN, 0x80);
            for (int i = 0; i < 5; i++) {
                move_to(d, x, y);
                emit(d, scale);
                emit(d, stamps[i]);
                emit(d, SW_VGSCAL);
            }
        }
    }

    /* the fireballs */
    for (int s = 0; s < SW_DF_SHOTS; s++) {
        double x, y, z;
        if (!d->shot[s].live || !project(d, d->shot[s].pos, &x, &y, &z) || z < 40.0 || !on_screen(x, y, 4.0)) continue;
        d->tag = SW_TAG(SW_TAG_SHOT, s);
        fireball(d, x, y, z, s);
    }

    /* the lasers: from the guns to the sight, the left pair or the right */
    if (d->laser_t > 0.0 && d->t - d->laser_t < LASER_SHOW) {
        static const double GUN[4][2] = { { -104.6, -25.9 }, { -105.8, -143.7 }, { 105.8, -143.7 }, { 104.6, -25.9 } };
        float k = (float)(1.0 - (d->t - d->laser_t) / LASER_SHOW);
        for (int g = 0; g < 4; g++) {
            if ((g < 2) != (d->laser_side == 0)) continue;
            d->tag = SW_TAG(SW_TAG_LASER, g);
            double mx = GUN[g][0] + (SIGHT_X - GUN[g][0]) * 0.55, my = GUN[g][1] + (SIGHT_Y - GUN[g][1]) * 0.55;
            move_to(d, GUN[g][0], GUN[g][1]);
            color(d, BLU, (unsigned)(255.0f * k));
            vctr(d, vg(mx - GUN[g][0]), vg(my - GUN[g][1]), 7);
            color(d, TRQ, (unsigned)(255.0f * k));
            vctr(d, vg(SIGHT_X - mx), vg(SIGHT_Y - my), 7);
        }
    }

    /* the sight, in the middle of the view: one aims with the nose */
    d->tag = SW_TAG(SW_TAG_SITE, 0);
    move_to(d, SIGHT_X, SIGHT_Y);
    color(d, TRQ, 0x80);
    emit(d, rom16(m, SW_VGSITE));

    /* the X-wing's nose and guns (VWPLAN, the sight in the middle) */
    static const uint16_t parts[5] = { SW_VJSTBM, SW_VJSTTR, SW_VJSTBL, SW_VJSTTL, SW_VJSTBR };
    for (int i = 0; i < 5; i++) {
        d->tag = SW_TAG(SW_TAG_PLANE, i);
        emit(d, SW_VGCNTR);
        emit(d, parts[i]);
    }

    /* what is written on the screen: the score, the shields, the wave, the speed */
    d->tag = SW_TAG(SW_TAG_MESS, 0x3ff);
    {
        char s[24];
        text(d, m, -122.0, 138.0, RED, 0x80, SW_VGSCAL, "SCORE");
        number(s, d->score);
        text(d, m, -122.0, 129.0, GRN, 0xa0, SW_VGSCAL, s);
        int lost = d->hit_t > 0.0 && d->t - d->hit_t < 0.6, mended = d->mend_t > 0.0 && d->t - d->mend_t < 2.0;
        s[0] = (char)('0' + d->shields);
        s[1] = 0;
        text_mid(d, m, 0.0, 138.0, lost ? RED : mended ? TRQ : GRN, lost ? 0xff : 0x90, SW_VGSCAL, LETTER, s);
        text_mid(d, m, 0.0, 129.0, lost ? RED : GRN, 0x80, SW_VGSCAL, LETTER, "SHIELD");
        number(s, d->wave);
        text(d, m, 84.0, 138.0, GRN, 0xa0, SW_VGSCAL, s);
        text(d, m, 96.0, 138.0, RED, 0x80, SW_VGSCAL, "WAVE");
        /* the throttle: a bar from slowest to fastest */
        double x0 = 76.0, y = 124.0, w = 46.0;
        move_to(d, x0, y);
        color(d, GRN, 0x60);
        vctr(d, vg(w), 0, 5);
        double at = x0 + w * (d->throttle + 1.0) * 0.5;
        move_to(d, at, y - 3.0);
        color(d, YLW, 0xa0);
        vctr(d, 0, vg(6.0), 7);
    }
    if (d->over) {
        char s[24];
        text_mid(d, m, 0.0, 60.0, RED, 0xc0, SW_VGSCAL, LETTER, "GAME OVER");
        number(s, d->score);
        text_mid(d, m, 0.0, 48.0, GRN, 0xa0, SW_VGSCAL, LETTER, s);
    } else if (d->t - d->wave_t < 2.5) {
        char s[24] = "WAVE ";
        number(s + 5, d->wave);
        text_mid(d, m, 0.0, 70.0, RED, 0xa0, SW_VGSCAL, LETTER, d->wave == 1 && d->t < 2.5 ? "DOGFIGHT" : s);
        if (d->wave % 5 == 0) text_mid(d, m, 0.0, 60.0, RED, 0x80, SW_VGSCAL, LETTER, "DARTH VADER");
    } else if (d->mend_t > 0.0 && d->t - d->mend_t < 2.0) {
        text_mid(d, m, 0.0, 70.0, TRQ, 0xa0, SW_VGSCAL, LETTER, "R2 MENDS A SHIELD");
    }
    if (d->turning_back && !d->over && fmod(d->t - d->back_since, 0.8) < 0.55)
        text_mid(d, m, 0.0, 60.0, RED, 0xc0, SW_VGSCAL, LETTER, "WHERE ARE YOU GOING");

    /* where the nearest enemy is, when none is in sight: an arrow about the sight */
    {
        int best = -1, seen = 0;
        double bd = 1e18;
        for (int i = 0; i < SW_DF_ENEMIES; i++) {
            if (!d->enemy[i].kind) continue;
            double x, y;
            if (project(d, d->enemy[i].b.pos, &x, &y, NULL) && on_screen(x, y, -10.0)) seen = 1;
            double dd = dist(d->enemy[i].b.pos, d->pos);
            if (dd < bd) { bd = dd; best = i; }
        }
        if (best >= 0 && !seen && !d->over) {
            double v[3];
            view_of(d, d->enemy[best].b.pos, v);
            double ax = v[1], ay = v[2], n = hypot(ax, ay);
            if (n > 1e-6) {
                ax /= n;
                ay /= n;
                double cx = SIGHT_X + ax * 60.0, cy = SIGHT_Y + ay * 60.0, px = -ay, py = ax;
                move_to(d, cx + ax * 7.0, cy + ay * 7.0);
                color(d, RED, 0xc0);
                vctr(d, vg(-ax * 7.0 + px * 4.0), vg(-ay * 7.0 + py * 4.0), 7);
                vctr(d, vg(-px * 8.0), vg(-py * 8.0), 7);
                vctr(d, vg(ax * 7.0 + px * 4.0), vg(ay * 7.0 + py * 4.0), 7);
            }
        }
    }

    emit(d, 0x2000);                        /* HALT */
    sw_avg_run_tagged(d->space, 0, d->tags, NULL, &d->list, 60000);

    /* the ships, in true perspective (after the list: their lines are added) */
    chase(d);
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        sw_df_enemy *e = &d->enemy[i];
        if (!e->kind) continue;
        int flash = d->t - e->hit_t < 0.12 && e->hit_t > 0.0;
        float lum = flash ? 255.0f : e->kind == K_VADER ? 210.0f : 180.0f;
        if (e->kind == K_INTERCEPTOR) draw_interceptor(d, &e->b, lum);
        else if (e->kind == K_VADER) draw_model(d, M_VADER, &e->b, 1.0, flash ? WHT : GRN, lum, SW_TDSRTH);
        else draw_model(d, M_TIE, &e->b, 1.0, GRN, lum, SW_TDSTIE);
    }
    for (int i = 0; i < SW_DF_PIECES; i++) {
        sw_df_piece *p = &d->piece[i];
        if (!p->live) continue;
        static const int rom[4] = { 0, SW_TDSTI1, SW_TDSTI2, SW_TDSTI3 };
        draw_model(d, p->model, &p->b, 1.0, GRN, (float)(200.0 * (1.0 - (d->t - p->t) / 2.5)), rom[p->model]);
    }
    *out = d->list.lines;
    return d->list.count;
}

/* --- the fourth choice ---------------------------------------------------------------------- */

void sw_dogfight_choice_lines(sw_dogfight *d, const sw_machine *m, sw_avg_list *list)
{
    if (!m->rom) return;
    if (d->space[0x3000] == 0 && d->space[0x3001] == 0) memcpy(d->space + 0x3000, m->rom->vrom, sizeof m->rom->vrom);
    d->at = 0;
    d->tag = SW_TAG(SW_TAG_DSTAR, 4);
    emit(d, SW_VGSCAL);
    /* placed as the game places its three */
    double x = CHOICE_X * VG_SCALE, y = CHOICE_Y * VG_SCALE;
    move_to(d, x, y);
    color(d, GRN, 0x80);
    emit(d, SW_VGSCAL);
    emit(d, SW_VJBMIN);
    d->tag = SW_TAG(SW_TAG_MESS, 0x3fe);
    text_mid(d, m, x, y + 30.0, RED, 0x80, SW_VGSCAL, LETTER, "DOGFIGHT");
    text_mid(d, m, x, y + 21.0, GRN, 0x80, SW_VGSCAL, LETTER, "ENDLESS");
    emit(d, 0x2000);
    sw_avg_list l = { 0 };
    l.lines = list->lines + list->count;
    l.cap = list->cap - list->count;
    if (l.cap <= 0) return;
    sw_avg_run_tagged(d->space, 0, d->tags, NULL, &l, 4000);
    list->count += l.count;
}

int sw_dogfight_chosen(const sw_machine *m)
{
    if (m->ram[SW_PHASE - 0x4800] != SW_PHSSDS + 1) return 0;
    int cy = (int16_t)((m->ram[SW_SI_CY - 0x4800] << 8) | m->ram[SW_SI_CY + 1 - 0x4800]);
    int cx = (int16_t)((m->ram[SW_SI_CX - 0x4800] << 8) | m->ram[SW_SI_CX + 1 - 0x4800]);
    /* as the game tells which of its three the sight is on */
    int dy = abs(cy + (int16_t)SW_VGOFFY - CHOICE_Y), dx = abs(cx - CHOICE_X);
    return dy < 72 && dx < 52 && dx + dy < 80;
}
