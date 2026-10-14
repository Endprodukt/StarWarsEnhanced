/* dogfight.c - see dogfight.h. */
#include "dogfight/dogfight.h"

#include "core/sw_symbols.h"
#include "core/tags.h"
#include "game/game.h"

#include <math.h>
#include <stdio.h>
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
#define BOX         11000.0
#define SPEED_MIN   700.0
#define SPEED_MID   1350.0
#define SPEED_MAX   2300.0
/* turning, radians a second at full yoke */
#define YAW_RATE    1.75
#define PITCH_RATE  1.5
#define ROLL_RATE   3.3
#define BACK_RATE   2.2                     /* turning back at a wall */
/* the lasers */
#define LASER_RANGE 14000.0
#define LASER_EVERY 0.18                    /* the trigger held fires again */
/* the game's clocks (core/sched.h): the VG restarts, the game's frames */
#define VG_HZ       41.02
#define GAME_HZ     20.51
#define TLZFR       0xB04F                  /* the lasers' colors (WSLAZR's LDU): JSRLs to VRLZF0.. */
/* the proton torpedoes */
#define TORP_CHARGE 7.0                     /* seconds to charge */
#define TORP_SPEED  3400.0                  /* faster than the ship */
#define TORP_TURN   4.0                     /* how hard they home */
#define TORP_LIFE   5.0
#define TORP_HITS   260.0
/* the wingmen and the bolts of the battle */
#define ALLY_SPEED  1700.0
#define ALLY_TURN   1.7
#define ALLY_BACK   14.0                    /* seconds until a lost wingman is replaced */
#define BOLT_SPEED  7000.0
#define BOLT_LIFE   1.4
#define BOLT_LEN    260.0
#define BOLT_HITS   200.0
/* the fireballs */
#define SHOT_SPEED  2300.0
#define SHOT_LIFE   6.0
#define SHOT_HITS   130.0                   /* how near one has to come to hit the ship */
#define SHIELDS     6
#define SHIP_SIZE   1.0                     /* the enemies as large as the ROM has them */

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
    S_PMDAR = 0x1d, S_PMTH5 = 0x24, S_AUDCR = 0x26, S_AUDDR = 0x28, S_AUDPB = 0x2a, S_AUDPH = 0x2d, S_AUDRY = 0x32, S_AUDSH = 0x33,
    S_AUDSS = 0x34, S_AUDSX = 0x35, S_AUDTC = 0x36, S_AUDTCZ = 0x37, S_AUDXL = 0x3a,
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
    if (d->at >= SW_VGPSRO - 4) return;     /* (VG RAM is full: the rest is left out; above it the fake ROM) */
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

/* --- the routines in VG RAM ------------------------------------------------------------------ */

/* The game's pictures of its lasers and shots call routines that the
 * interrupt keeps at the bottom of VG RAM and changes at each restart of
 * the vector generator (DOVG, src/game/irq.c): the lasers' colors turned
 * round by one, so that the bolts travel; the splash; the fireballs'
 * sparkle (every fourth) and fuse; the flashes. The same here, in the
 * dogfight's own VG RAM, at the restarts' rate; its list begins above them
 * (VGVARB). */
static void vg_put(sw_dogfight *d, uint16_t a, uint16_t w)
{
    d->space[a] = (uint8_t)(w >> 8);
    d->space[a + 1] = (uint8_t)w;
}

static uint8_t step_ix(uint8_t b, uint8_t size)
{
    b = (uint8_t)(b + 2);
    return b >= size ? 0 : b;
}

static void vg_restart(sw_dogfight *d, const sw_machine *m, uint32_t n)
{
    uint8_t *ix = d->vg_ix;
    /* LAZAR FIRE: the 16 words of VTLZF, a new start each restart */
    uint8_t b = ix[0] = step_ix(ix[0], (uint8_t)(SW_VRLZFZ - SW_VRLZF));
    uint16_t x = (uint16_t)(SW_VRLZF + 2 + b), u = SW_VTLZF;
    do {
        x -= 2;
        vg_put(d, x, sw_rom16(m, u));
        u += 2;
    } while ((int16_t)x > (int16_t)SW_VRLZF);
    x = SW_VRLZFZ;
    while (u < SW_VTLZFZ) {
        x -= 2;
        vg_put(d, x, sw_rom16(m, u));
        u += 2;
    }
    /* LAZAR SPLASH */
    b = ix[1] = step_ix(ix[1], (uint8_t)(SW_VTLZSZ - SW_VTLZS));
    vg_put(d, SW_VRLZS, sw_rom16(m, (uint16_t)(SW_VTLZS + b)));
    /* the shots' sparkle, every fourth; their explosion with it */
    if (--d->vf_gnb <= 0) {
        d->vf_gnb = 4;
        b = ix[2] = step_ix(ix[2], (uint8_t)(SW_VTGNBZ - SW_VTGNB));
        vg_put(d, SW_VRGNB, sw_rom16(m, (uint16_t)(SW_VTGNB + b)));
        vg_put(d, SW_VRGNX, sw_rom16(m, (uint16_t)(SW_VTGNX + b)));
    }
    /* their fuse, every time */
    b = ix[3] = step_ix(ix[3], (uint8_t)(SW_VTGNTZ - SW_VTGNT));
    vg_put(d, SW_VRGNT, sw_rom16(m, (uint16_t)(SW_VTGNT + b)));
    /* the flashes */
    b = ix[4] = step_ix(ix[4], (uint8_t)(SW_VTFLSZ - SW_VTFLS));
    vg_put(d, SW_VRFLS, sw_rom16(m, (uint16_t)(SW_VTFLS + b)));
    vg_put(d, SW_VRFLS + 2, SW_VGRTSL);
    b = ix[5] = step_ix(ix[5], (uint8_t)(SW_VTMFLZ - SW_VTMFL));
    vg_put(d, SW_VRMFL, sw_rom16(m, (uint16_t)(SW_VTMFL + b)));
    vg_put(d, SW_VRMFL + 2, SW_VGRTSL);
    vg_put(d, SW_VRSITE, SW_VGRTSL);        /* (the sight is drawn from the ROM here) */
    /* the splash's strokes (as P.RND would choose them) */
    uint8_t pick[8];
    for (int i = 0; i < 8; i++) pick[i] = (uint8_t)(hash(n * 8u + (uint32_t)i + 5u) & 0x0f);
    sw_splash_into(d->space, pick);
}

/* The restarts due by the time of the picture. */
static void vg_catch_up(sw_dogfight *d, const sw_machine *m)
{
    double now = d->stage == SW_DF_HOWTO ? d->t + d->stage_t : d->t;      /* (the game waits on the how-to page; its pictures do not) */
    if (d->vg_clock > now + 1.0 || d->vg_clock < now - 0.5) d->vg_clock = now;   /* (a jump: not caught up) */
    while (d->vg_clock <= now) {
        vg_restart(d, m, (uint32_t)lrint(d->vg_clock * VG_HZ));
        d->vg_clock += 1.0 / VG_HZ;
    }
}

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

/* Drawn as the ROM draws its TIE fighter, and of the TIE fighter's own
 * parts: its ball (two octagons, points 37-52), its pylons (rings of six,
 * 25-36) and the hubs on its wings (7-12, 19-24). Only its wings are its
 * own: daggers, each wing (ahead, up, in the plane of the TIE's wing) from
 * its rear point (-170, 0) to the upper rear corner, forward to the upper
 * tip far ahead (long and narrow), back into a deep notch at the hub, out to the lower
 * tip, to the lower rear corner; the corners above and below bent in
 * towards the ball. As on the TIE fighter, spokes run from the hub to each
 * corner, and an inner frame follows the outline. */
static const double INT_WING[7][2] = {          /* ahead, up */
    { -170, 0 }, { -120, 240 }, { 300, 232 }, { 25, 28 }, { 25, -28 }, { 300, -232 }, { -120, -240 },
};
static const int INT_BENT[7] = { 0, 1, 1, 0, 0, 1, 1 };         /* bent in, towards the ball */
static const int INT_HUB[7] = { 12, 7, 8, 9, 9, 10, 11 };       /* the hub point each spoke starts from */

/* the TIE fighter's lines that are not of its wings' outline */
static int tie_inner_line(int a, int b)
{
    int wa = (a >= 1 && a <= 6) || (a >= 13 && a <= 18), wb = (b >= 1 && b <= 6) || (b >= 13 && b <= 18);
    return !wa && !wb;
}

/* a point of a wing: side 0 the TIE's y -208, side 1 +208; k its corner,
 * inner 1 for the frame inside the outline */
static void int_wing_pt(int side, int k, int inner, double *q)
{
    double x = INT_WING[k][0], z = INT_WING[k][1];
    if (inner) { x = x * 0.86 - 4.0; z *= 0.84; }
    double y = 208.0 - (INT_BENT[k] ? 62.0 : 0.0) * (1.0 - 0.15 * inner);
    q[0] = x;
    q[1] = side ? y : -y;
    q[2] = z;
}

/* a face of the interceptor on the screen, for render/solid */
static void int_face(sw_dogfight *d, const double (*p)[3], int n)
{
    if (d->n_faces == SW_DF_FACES || n > 6) return;
    sw_solid_extra *f = &d->faces[d->n_faces];
    for (int i = 0; i < n; i++) {
        double v[3], x, y;
        view_of(d, p[i], v);
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
    if (++d->inst == 0) d->inst = 0x8000;
    const int k = M_TIE;
    double tie[64][3];
    for (int i = 0; i < d->mnp[k] && i < 64; i++) {
        double q[3] = { d->mp[k][i][0], d->mp[k][i][1], d->mp[k][i][2] };
        place(b, q, SHIP_SIZE, tie[i]);
    }
    /* the ball, the pylons, the hubs: the TIE fighter's own */
    for (int i = 0; i < d->mne[k]; i++) {
        int e0 = d->me[k][i][0], e1 = d->me[k][i][1];
        if (e0 >= d->mnp[k] || e1 >= d->mnp[k] || !tie_inner_line(e0, e1)) continue;
        line3(d, tie[e0], tie[e1], GRN, lum, 0, 0, 0);
    }
    /* the wings */
    for (int s = 0; s < 2; s++) {
        double out[7][3], in[7][3];
        for (int c = 0; c < 7; c++) {
            double q[3];
            int_wing_pt(s, c, 0, q);
            place(b, q, SHIP_SIZE, out[c]);
            int_wing_pt(s, c, 1, q);
            place(b, q, SHIP_SIZE, in[c]);
        }
        int hub = s ? 12 : 0;                   /* (the hubs: 7-12 on the TIE's -208 side, 19-24 on its +208) */
        for (int c = 0; c < 7; c++) {
            int n = (c + 1) % 7;
            line3(d, out[c], out[n], GRN, lum, 0, 0, 0);
            line3(d, in[c], in[n], GRN, lum * 0.55f, 0, 0, 0);
            if (c != 4) line3(d, tie[INT_HUB[c] + hub], out[c], GRN, lum * 0.8f, 0, 0, 0);   /* (one spoke into the notch) */
        }
        /* its faces: three convex pieces */
        double f1[4][3], f2[3][3], f3[4][3];
        memcpy(f1[0], out[0], sizeof f1[0]); memcpy(f1[1], out[1], sizeof f1[0]);
        memcpy(f1[2], out[2], sizeof f1[0]); memcpy(f1[3], out[3], sizeof f1[0]);
        memcpy(f2[0], out[0], sizeof f2[0]); memcpy(f2[1], out[3], sizeof f2[0]); memcpy(f2[2], out[4], sizeof f2[0]);
        memcpy(f3[0], out[0], sizeof f3[0]); memcpy(f3[1], out[4], sizeof f3[0]);
        memcpy(f3[2], out[5], sizeof f3[0]); memcpy(f3[3], out[6], sizeof f3[0]);
        int_face(d, (const double (*)[3])f1, 4);
        int_face(d, (const double (*)[3])f2, 3);
        int_face(d, (const double (*)[3])f3, 4);
    }
    /* the ball's faces: its two octagons (each in two pieces) and its sides */
    if (d->mnp[k] >= 53) {
        for (int s = 0; s < 2; s++) {
            int o = s ? 45 : 37;
            double a[6][3], c[4][3];
            for (int i = 0; i < 6; i++) memcpy(a[i], tie[o + i], sizeof a[i]);
            for (int i = 0; i < 4; i++) memcpy(c[i], tie[o + (5 + i) % 8], sizeof c[i]);
            int_face(d, (const double (*)[3])a, 6);
            int_face(d, (const double (*)[3])c, 4);
        }
        for (int i = 0; i < 8; i++) {
            double q[4][3];
            memcpy(q[0], tie[37 + i], sizeof q[0]);
            memcpy(q[1], tie[37 + (i + 1) % 8], sizeof q[0]);
            memcpy(q[2], tie[45 + (i + 1) % 8], sizeof q[0]);
            memcpy(q[3], tie[45 + i], sizeof q[0]);
            int_face(d, (const double (*)[3])q, 4);
        }
    }
}

/* --- the X-wing: a model of its own ---------------------------------------------------------------- */

/* The ROM's X-wing (TDSXW) has no lines to be had here: the game draws its
 * own only from the cockpit. Wingmen and the chases far off are drawn as
 * the outside view draws the player's (render/enhance.c, xwing_build), in
 * the units of that model (its nose at x 1, its left +y, up +z), its wings
 * open in an X. */
#define XW_SIZE 420.0                   /* (twice a TIE fighter's length, as in the films) */

static void xw_pt(const sw_df_body *b, double x, double y, double z, double *p)
{
    double q[3] = { x * XW_SIZE, y * XW_SIZE, z * XW_SIZE };
    place(b, q, 1.0, p);
}

static void xw_line(sw_dogfight *d, const sw_df_body *b, const double *a, const double *c, unsigned col, float lum)
{
    double p0[3], p1[3];
    xw_pt(b, a[0], a[1], a[2], p0);
    xw_pt(b, c[0], c[1], c[2], p1);
    line3(d, p0, p1, col, lum, 0, 0, 0);
}

static void xw_face(sw_dogfight *d, const sw_df_body *b, const double (*q)[3], int n)
{
    double p[6][3];
    for (int i = 0; i < n && i < 6; i++) xw_pt(b, q[i][0], q[i][1], q[i][2], p[i]);
    int_face(d, (const double (*)[3])p, n);
}

/* open: its S-foils, 1 open in an X, 0 closed (flat, as it speeds up);
 * burn: 0, or how far its engines' blue fire reaches back (0..1). */
static void draw_xwing(sw_dogfight *d, const sw_df_body *b, float lum, double open, double burn)
{
    if (++d->inst == 0) d->inst = 0x8000;
    const double XT = -0.62, XC = 0.30, XN = 1.0, W = 0.11, H = 0.10, WN = 0.035, HN = 0.03;
    const double GY = 0.95, GZ = 0.28;
    /* the fuselage: a box, and the nose tapering from it */
    double box[8][3], nose[4][3];
    for (int i = 0; i < 8; i++) {
        box[i][0] = i & 1 ? XC : XT;
        box[i][1] = i & 2 ? W : -W;
        box[i][2] = i & 4 ? H : -H;
    }
    for (int i = 0; i < 4; i++) {
        nose[i][0] = XN;
        nose[i][1] = i & 1 ? WN : -WN;
        nose[i][2] = i & 2 ? HN : -HN;
    }
    for (int i = 0; i < 8; i++) {
        if (!(i & 1)) xw_line(d, b, box[i], box[i | 1], WHT, lum);
        if (!(i & 2)) xw_line(d, b, box[i], box[i | 2], WHT, lum);
        if (!(i & 4)) xw_line(d, b, box[i], box[i | 4], WHT, lum);
    }
    static const int RING[4] = { 0, 1, 3, 2 };          /* (round the nose's end) */
    for (int i = 0; i < 4; i++) {
        int a = RING[i], c = RING[(i + 1) % 4];
        int ba = 1 | ((a & 1) << 1) | ((a & 2) << 1);   /* the box's front corner under nose corner a */
        xw_line(d, b, box[ba], nose[a], WHT, lum);
        xw_line(d, b, nose[a], nose[c], WHT, lum);
    }
    {
        /* the red band round the nose */
        double xb = 0.62, k = (xb - XC) / (XN - XC), wb = W + (WN - W) * k, hb = H + (HN - H) * k;
        double c[4][3] = { { xb, -wb, -hb }, { xb, wb, -hb }, { xb, wb, hb }, { xb, -wb, hb } };
        for (int i = 0; i < 4; i++) xw_line(d, b, c[i], c[(i + 1) % 4], RED, lum);
    }
    {
        /* the canopy */
        double c0[3] = { 0.0, -0.08, H }, c1[3] = { 0.04, -0.06, H + 0.07 }, c2[3] = { 0.18, -0.06, H + 0.07 }, c3[3] = { 0.32, -0.08, H };
        double e0[3] = { 0.0, 0.08, H }, e1[3] = { 0.04, 0.06, H + 0.07 }, e2[3] = { 0.18, 0.06, H + 0.07 }, e3[3] = { 0.32, 0.08, H };
        xw_line(d, b, c0, c1, TRQ, lum); xw_line(d, b, c1, c2, TRQ, lum); xw_line(d, b, c2, c3, TRQ, lum);
        xw_line(d, b, e0, e1, TRQ, lum); xw_line(d, b, e1, e2, TRQ, lum); xw_line(d, b, e2, e3, TRQ, lum);
        xw_line(d, b, c1, e1, TRQ, lum); xw_line(d, b, c2, e2, TRQ, lum);
    }
    /* its faces: the box's six and the nose's four sides */
    {
        static const int F[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
        for (int i = 0; i < 6; i++) {
            double q[4][3];
            for (int k = 0; k < 4; k++) memcpy(q[k], box[F[i][k]], sizeof q[k]);
            xw_face(d, b, (const double (*)[3])q, 4);
        }
        for (int i = 0; i < 4; i++) {
            int a = RING[i], c = RING[(i + 1) % 4];
            int ba = 1 | ((a & 1) << 1) | ((a & 2) << 1), bc = 1 | ((c & 1) << 1) | ((c & 2) << 1);
            double q[4][3];
            memcpy(q[0], box[ba], sizeof q[0]); memcpy(q[1], box[bc], sizeof q[0]);
            memcpy(q[2], nose[c], sizeof q[0]); memcpy(q[3], nose[a], sizeof q[0]);
            xw_face(d, b, (const double (*)[3])q, 4);
        }
    }
    /* the four wings, in an X: red stripes, the engine at the root, the gun at the tip */
    for (int sy = -1; sy <= 1; sy += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            double rz = sz * 0.04, tz = sz * (0.045 + (GZ - 0.045) * open);
            double r0[3] = { -0.56, sy * W, rz }, r1[3] = { -0.06, sy * W, rz };
            double t0[3] = { -0.52, sy * GY, tz }, t1[3] = { -0.30, sy * GY, tz };
            xw_line(d, b, r0, t0, WHT, lum);
            xw_line(d, b, r1, t1, WHT, lum);
            xw_line(d, b, t0, t1, WHT, lum);
            double q[4][3];
            memcpy(q[0], r0, sizeof q[0]); memcpy(q[1], r1, sizeof q[0]);
            memcpy(q[2], t1, sizeof q[0]); memcpy(q[3], t0, sizeof q[0]);
            xw_face(d, b, (const double (*)[3])q, 4);
            for (int k = 0; k < 2; k++) {
                double u0 = 0.30 + 0.08 * k, u1 = u0 + 0.30;
                double a[3] = { -0.40 + 0.03 * k, sy * (W + (GY - W) * u0), rz + (tz - rz) * u0 };
                double c[3] = { -0.38 + 0.03 * k, sy * (W + (GY - W) * u1), rz + (tz - rz) * u1 };
                xw_line(d, b, a, c, RED, lum);
            }
            /* the engine: a six-sided tube, its back glowing blue */
            double cy = sy * 0.19, cz = sz * 0.12, R = 0.075, ring0[6][3], ring1[6][3];
            for (int i = 0; i < 6; i++) {
                double an = i * 1.0471976 + 0.5235988;
                ring0[i][0] = -0.68; ring0[i][1] = cy + R * cos(an); ring0[i][2] = cz + R * sin(an);
                ring1[i][0] = -0.12; ring1[i][1] = cy + R * 0.85 * cos(an); ring1[i][2] = cz + R * 0.85 * sin(an);
            }
            for (int i = 0; i < 6; i++) {
                int j = (i + 1) % 6;
                xw_line(d, b, ring0[i], ring0[j], BLU, lum);
                xw_line(d, b, ring1[i], ring1[j], WHT, lum);
                if (!(i & 1)) xw_line(d, b, ring0[i], ring1[i], WHT, lum);
            }
            /* its fire, when it is pushed on */
            if (burn > 0.0) {
                double fl = 0.8 + 0.2 * sin(d->t * 41.0 + sy * 1.7 + sz * 2.9 + d->stage_t * 37.0);
                double tail[3] = { -0.68 - (0.25 + 0.75 * burn) * fl, cy, cz };
                double core[3] = { -0.68 - (0.12 + 0.4 * burn) * fl, cy, cz };
                for (int i = 0; i < 6; i += 2) {
                    double e[3] = { -0.69, cy + (ring0[i][1] - cy) * 0.6, cz + (ring0[i][2] - cz) * 0.6 };
                    xw_line(d, b, e, tail, BLU, lum);
                }
                double c0[3] = { -0.69, cy, cz };
                xw_line(d, b, c0, core, WHT, lum);
                /* (and its glow round the exit, growing: seen from behind, the fire itself is short) */
                double rf = R * (0.9 + 0.9 * burn * fl), xf = -0.70 - 0.08 * burn;
                for (int i = 0; i < 6; i++) {
                    double a0 = i * 1.0471976, a1 = a0 + 1.0471976;
                    double p0[3] = { xf, cy + rf * cos(a0), cz + rf * sin(a0) }, p1[3] = { xf, cy + rf * cos(a1), cz + rf * sin(a1) };
                    double q0[3] = { xf, cy + rf * 0.45 * cos(a0), cz + rf * 0.45 * sin(a0) }, q1[3] = { xf, cy + rf * 0.45 * cos(a1), cz + rf * 0.45 * sin(a1) };
                    xw_line(d, b, p0, p1, BLU, 255.0f);
                    if (burn > 0.5) xw_line(d, b, q0, q1, WHT, (float)(120.0 + 135.0 * burn));
                }
            }
            /* the gun ahead of the tip */
            double g0[3] = { -0.54, sy * GY, tz }, g1[3] = { 0.86, sy * GY, tz };
            xw_line(d, b, g0, g1, WHT, lum);
        }
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
    int n = 4 + 2 * d->wave;
    if (n > 30) n = 30;
    int vader = d->wave % 5 == 0 ? 1 : 0;
    int inter = d->wave >= 2 ? d->wave - 1 : 0;
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
    int most = 5 + d->wave / 3;
    if (most > 8) most = 8;
    if (slot < 0 || alive >= most) return;
    /* TIE fighters come three together in a V now and then, interceptors in pairs */
    int group = kind == K_TIE && d->pending[K_TIE] >= 3 && alive <= most - 3 && rnd(d) < 0.65 ? 3 :
                kind == K_INTERCEPTOR && d->pending[K_INTERCEPTOR] >= 2 && alive <= most - 2 && rnd(d) < 0.5 ? 2 : 1;
    double pos[3] = { BOX * 0.95, 0.0, 0.0 };
    for (int tries = 0; tries < 20; tries++) {
        double az = rnd(d) * 6.2831853, el = (rnd(d) - 0.5) * 1.2;
        pos[0] = BOX * 0.95 * cos(el) * cos(az);
        pos[1] = BOX * 0.95 * cos(el) * sin(az);
        pos[2] = BOX * 0.6 * sin(el);
        if (dist(pos, d->pos) > 7000.0) break;
    }
    sw_df_enemy *e = &d->enemy[slot];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->hp = KIND_HP[kind];
    body_facing(&e->b, pos, d->pos);
    double hard = 1.0 + 0.03 * (d->wave - 1);
    if (hard > 1.6) hard = 1.6;
    e->speed = (kind == K_INTERCEPTOR ? 1650.0 : kind == K_VADER ? 1500.0 : 1350.0) * hard;
    e->turn = (kind == K_INTERCEPTOR ? 2.2 : kind == K_VADER ? 1.8 : 1.45) * hard;
    e->fire_t = d->t + 1.5 + rnd(d) * 1.5;
    e->prey = -1;                           /* (all after the player: the wingmen are only to be seen) */
    d->pending[kind]--;
    for (int w = 1; w < group; w++) {
        /* its wingmen: behind it, one to each side */
        int ws = -1;
        for (int i = 0; i < SW_DF_ENEMIES && ws < 0; i++) if (!d->enemy[i].kind) ws = i;
        if (ws < 0) break;
        sw_df_enemy *g = &d->enemy[ws];
        *g = *e;
        g->mode = 2;
        g->leader = slot;
        g->slot_r = w == 1 ? -850.0 : 850.0;
        g->slot_b = 700.0;
        for (int k = 0; k < 3; k++) g->b.pos[k] = e->b.pos[k] + e->b.r[k] * g->slot_r - e->b.f[k] * g->slot_b;
        g->fire_t = d->t + 1.2 + rnd(d) * 1.0;
        d->pending[kind]--;
    }
}

/* a ship blows up at p: its flash (the oldest one's place is taken) */
static void add_blast(sw_dogfight *d, const double *p)
{
    int k = 0;
    for (int i = 1; i < 8; i++) if (d->blast_t[i] < d->blast_t[k]) k = i;
    d->blast_t[k] = d->t;
    memcpy(d->blast_at[k], p, sizeof d->blast_at[k]);
}

static void explode(sw_dogfight *d, const sw_df_body *b, double speed)
{
    add_blast(d, b->pos);
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

static void ally_place(sw_dogfight *d, int i, int beside);

void sw_dogfight_start(sw_dogfight *d, const sw_machine *m, int wave)
{
    sw_line *keep = d->list.lines;
    int cap = d->list.cap;
    sw_df_scores *scores = d->scores;
    memset(d, 0, sizeof *d);
    d->list.lines = keep;
    d->list.cap = cap;
    d->scores = scores;
    d->stage = SW_DF_PLAY;
    d->rank = -1;
    if (m->rom) {
        memcpy(d->space + 0x3000, m->rom->vrom, sizeof m->rom->vrom);
        /* DOWNVG: the "fake VG ROM" of the program ROM (the shots' sparkle
         * pictures among it) into the top of VG RAM, as the game has it */
        for (uint16_t x = SW_FAKEVG, u = SW_VGPSRO; u < SW_VGRROM; x += 2, u += 2) vg_put(d, u, sw_rom16(m, x));
    }
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
    if (wave < 0) {
        /* to look at one enemy: before the ship, nothing else */
        d->preview = -wave;
        sw_df_enemy *e = &d->enemy[0];
        e->kind = -wave;
        e->hp = 99;
        double at[3] = { d->pos[0] + 1100.0, d->pos[1], d->pos[2] - 120.0 }, to[3] = { at[0], at[1] + 1000.0, at[2] };
        body_facing(&e->b, at, to);
        d->sound_r = d->sound_w = 0;
        return;
    }
    d->wave = wave > 1 ? wave - 1 : 0;
    d->torp_charge = 0.6;                   /* (the first two soon) */
    d->far_blast_t = 2.5;
    for (int i = 0; i < SW_DF_ALLIES; i++) ally_place(d, i, 1);
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
    /* as TSTLAZ: eight frames of fire, the other pair, the sight latched */
    d->lz_alt++;
    d->lz_hit = 0;
    d->lz_hit_next = 0;
    d->lz_edg = 8;
    d->lz_cx = vg(SIGHT_X);
    d->lz_cy = vg(d->sight_y);
    sound(d, S_AUDXL);
    double best = LASER_RANGE;
    int hit_enemy = -1, hit_shot = -1;
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        sw_df_enemy *e = &d->enemy[i];
        double x, y, z;
        if (!e->kind || !project(d, e->b.pos, &x, &y, &z) || z > best) continue;
        double r = PERSPECTIVE * KIND_SIZE[e->kind] * SHIP_SIZE / z * VG_SCALE;
        if (r < 3.0) r = 3.0;
        if (hypot(x - SIGHT_X, y - SIGHT_Y) < r) { best = z; hit_enemy = i; hit_shot = -1; }
    }
    for (int i = 0; i < SW_DF_SHOTS; i++) {
        sw_df_shot *s = &d->shot[i];
        double x, y, z;
        if (!s->live || s->hurt || !project(d, s->pos, &x, &y, &z) || z > best) continue;
        double r = PERSPECTIVE * 90.0 / z * VG_SCALE;
        if (r < 4.0) r = 4.0;
        if (hypot(x - SIGHT_X, y - SIGHT_Y) < r) { best = z; hit_shot = i; hit_enemy = -1; }
    }
    if (hit_shot >= 0) {
        /* GNHTSG: it stays where it is and dissolves */
        sw_df_shot *s = &d->shot[hit_shot];
        s->hurt = 1;
        s->tmr = 0x0f;
        s->vel[0] = s->vel[1] = s->vel[2] = 0.0;
        d->lz_hit_next = 1;
        d->score += 33;
        sound(d, S_AUDTCZ);
        sound(d, S_AUDSS);
    }
    if (hit_enemy >= 0) {
        sw_df_enemy *e = &d->enemy[hit_enemy];
        d->lz_hit_next = 1;
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
        /* it blows up: the crash, the explosion, R2's swan song */
        sound(d, S_AUDCR);
        sound(d, S_AUDSX);
        sound(d, S_AUDDR);
        /* (the game's own end, PHIEGM, says "Remember... the Force will be with you, always" after the table) */
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
        /* what it is after: the player, or a wingman */
        if (e->prey >= 0 && !d->ally[e->prey].live) e->prey = -1;
        const sw_df_ally *pa = e->prey >= 0 ? &d->ally[e->prey] : NULL;
        const double *tp = pa ? pa->b.pos : d->pos, *tf = pa ? pa->b.f : d->f;
        double tspeed = pa ? pa->speed : d->speed;
        double to[3];
        for (int k = 0; k < 3; k++) to[k] = tp[k] + tf[k] * tspeed * 0.6 - e->b.pos[k];
        double far = sqrt(dot(to, to));
        norm(to);
        double want[3] = { to[0], to[1], to[2] };
        double pace = e->speed;
        if (e->mode == 2) {
            /* in formation: its place by the leader; it breaks off when the
             * leader is gone or near the ship */
            sw_df_enemy *l = &d->enemy[e->leader];
            if (!l->kind || l->mode == 2 || dist(l->b.pos, tp) < 3500.0) {
                e->mode = 0;
            } else {
                double at[3];
                for (int k = 0; k < 3; k++) at[k] = l->b.pos[k] + l->b.r[k] * e->slot_r - l->b.f[k] * e->slot_b - e->b.pos[k];
                double gap = sqrt(dot(at, at));
                for (int k = 0; k < 3; k++) want[k] = l->b.f[k] * 900.0 + at[k] * 2.0;
                norm(want);
                pace = l->speed + (gap > 60.0 ? gap * 0.8 : 0.0);
                if (pace > l->speed * 1.6) pace = l->speed * 1.6;
            }
        }
        /* keeping apart from the others */
        for (int j = 0; j < SW_DF_ENEMIES; j++) {
            sw_df_enemy *o = &d->enemy[j];
            if (j == i || !o->kind) continue;
            double v[3] = { e->b.pos[0] - o->b.pos[0], e->b.pos[1] - o->b.pos[1], e->b.pos[2] - o->b.pos[2] };
            double dd = sqrt(dot(v, v));
            int mates = (e->mode == 2 && e->leader == j) || (o->mode == 2 && o->leader == i) ||
                        (e->mode == 2 && o->mode == 2 && e->leader == o->leader);
            double keep = mates ? 600.0 : 1800.0;           /* (wingmen hold their places nearer) */
            if (dd < 1e-6 || dd > keep) continue;
            double k2 = keep / dd - 1.0;
            for (int k = 0; k < 3; k++) want[k] += v[k] / dd * k2 * 2.0;
            if (dd < 520.0) for (int k = 0; k < 3; k++) e->b.pos[k] += v[k] / dd * (520.0 - dd) * 0.5;
        }
        norm(want);
        if (e->mode == 0 && (far < 2200.0 || (far < 3400.0 && dot(e->b.f, to) < 0.0))) {
            /* close in: break away, somewhere aside, and soon come round again */
            e->mode = 1;
            e->mode_t = d->t + 0.9 + rnd(d) * 1.0;
            double side[3] = { rnd(d) - 0.5, rnd(d) - 0.5, rnd(d) - 0.5 };
            for (int k = 0; k < 3; k++) e->away[k] = e->b.f[k] + side[k] * 1.6;
            norm(e->away);
        }
        if (e->mode == 1) {
            for (int k = 0; k < 3; k++) want[k] = e->away[k];
            norm(want);
            if (d->t > e->mode_t) e->mode = 0;
        }
        /* not into the player's face: near him it turns away (the owner: they came far too close) */
        {
            double v[3] = { e->b.pos[0] - d->pos[0], e->b.pos[1] - d->pos[1], e->b.pos[2] - d->pos[2] };
            double dd = sqrt(dot(v, v));
            if (dd > 1e-6 && dd < 1600.0) {
                for (int k = 0; k < 3; k++) want[k] += v[k] / dd * (1600.0 / dd - 1.0) * 4.0;
                norm(want);
            }
        }
        if (fabs(e->b.pos[0]) > BOX * 1.15 || fabs(e->b.pos[1]) > BOX * 1.15 || fabs(e->b.pos[2]) > BOX * 1.15) {
            for (int k = 0; k < 3; k++) want[k] = -e->b.pos[k];
            norm(want);
        }
        aim(e->b.f, e->b.r, e->b.u, want, e->turn * (e->mode == 2 ? 1.6 : 1.0) * dt);
        for (int k = 0; k < 3; k++) e->b.pos[k] += e->b.f[k] * pace * dt;
        /* it passes close: its sound */
        double near = dist(e->b.pos, d->pos);
        if (near < 700.0 && !e->passed) { e->passed = 1; sound(d, S_AUDPB); }
        if (near > 1500.0) e->passed = 0;
        /* it runs into the ship: both bounce off, both are hurt */
        if (near < 230.0 && d->t - e->bump_t > 0.8) {
            double n[3] = { e->b.pos[0] - d->pos[0], e->b.pos[1] - d->pos[1], e->b.pos[2] - d->pos[2] };
            norm(n);
            for (int k = 0; k < 3; k++) {
                d->knock[k] = -n[k] * 900.0;
                e->knock[k] = n[k] * 900.0;
                e->away[k] = n[k];
            }
            e->bump_t = d->t;
            e->mode = 1;
            e->mode_t = d->t + 1.5;
            d->yaw_rate += (float)((rnd(d) - 0.5) * 2.0);           /* (a jolt) */
            d->pitch_rate += (float)((rnd(d) - 0.5) * 1.6);
            lose_shield(d);
            e->hit_t = d->t;
            if (--e->hp <= 0) {
                d->score += KIND_SCORE[e->kind];
                explode(d, &e->b, e->speed);
                e->kind = K_NONE;
                continue;
            }
        }
        for (int k = 0; k < 3; k++) {
            e->b.pos[k] += e->knock[k] * dt;
            e->knock[k] *= exp(-2.5 * dt);
        }
        /* it fires, the ship before its nose, not too far: fireballs, as
         * the game's TIE fighters fire, a few in a burst. Only so many
         * fly at once (the owner: too many came at the ship). */
        int flying = 0;
        for (int s = 0; s < SW_DF_SHOTS; s++) flying += d->shot[s].live && !d->shot[s].hurt;
        int most_shots = 3 + d->wave / 3;
        if (most_shots > 6) most_shots = 6;
        if (!d->over && e->mode != 1 && d->t > e->fire_t && flying >= most_shots) e->fire_t = d->t + 0.3 + 0.4 * rnd(d);
        if (!d->over && e->mode != 1 && d->t > e->fire_t && ((far < 8500.0 && dot(e->b.f, to) > 0.95) || e->burst > 0)) {
            if (pa) {
                for (int s = 0; s < SW_DF_BOLTS; s++) {
                    sw_df_bolt *bo = &d->bolt[s];
                    if (bo->live) continue;
                    bo->live = 1;
                    bo->red = 0;
                    bo->t = d->t;
                    for (int k = 0; k < 3; k++) { bo->pos[k] = e->b.pos[k] + e->b.f[k] * 150.0; bo->vel[k] = to[k] * BOLT_SPEED; }
                    break;
                }
            } else {
                for (int s = 0; s < SW_DF_SHOTS; s++) {
                    sw_df_shot *sh = &d->shot[s];
                    if (sh->live) continue;
                    memset(sh, 0, sizeof *sh);
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
            }
            if (e->burst <= 0) e->burst = e->kind == K_VADER ? 3 : e->kind == K_INTERCEPTOR ? 2 : 1;
            if (--e->burst > 0) {
                e->fire_t = d->t + 0.3;             /* the next of the burst */
            } else {
                double every = (e->kind == K_VADER ? 1.8 : e->kind == K_INTERCEPTOR ? 2.2 : 2.6) / hard;
                e->fire_t = d->t + every * (0.7 + 0.6 * rnd(d));
            }
        }
    }
    /* the fireballs */
    for (int s = 0; s < SW_DF_SHOTS; s++) {
        sw_df_shot *sh = &d->shot[s];
        if (!sh->live || sh->hurt) continue;        /* (the hurt dissolve by the game's frames) */
        for (int k = 0; k < 3; k++) sh->pos[k] += sh->vel[k] * dt;
        if (d->t - sh->t > SHOT_LIFE) { sh->live = 0; continue; }
        if (dist(sh->pos, d->pos) < SHOT_HITS) {
            /* on the shields: it glows there where it struck (GNAHIT, GLOWVW) */
            double x, y, z;
            if (!project(d, sh->pos, &x, &y, &z)) { x = SIGHT_X; y = SIGHT_Y; }
            sh->hurt = 2;
            sh->tmr = 0x0f;
            sh->gx = (float)(x < -100.0 ? -100.0 : x > 100.0 ? 100.0 : x);
            sh->gy = (float)(y < -130.0 ? -130.0 : y > 120.0 ? 120.0 : y);
            lose_shield(d);
        }
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
    if (waiting && d->t - d->wave_t > 2.0) spawn(d);
    if (!waiting && !alive && !d->over && d->t - d->wave_t > 3.0) {
        if (d->shields < SHIELDS) {
            d->shields++;
            d->mend_t = d->t;
            sound(d, S_AUDRY);
        }
        begin_wave(d);
    }
}

/* --- the dogfight's own high scores ---------------------------------------------------------- */

static sw_df_scores own_scores;             /* (when the program gives none) */

void sw_dogfight_scores_default(sw_df_scores *s)
{
    static const char *const NAMES[SW_DF_TABLE] = { "LUK", "WED", "BIG", "POR", "HAN", "CHW", "LEI", "BEN", "DUT", "GLD" };
    memset(s, 0, sizeof *s);
    for (int i = 0; i < SW_DF_TABLE; i++) {
        memcpy(s->name[i], NAMES[i], 4);
        s->score[i] = 30000 - 3000 * i;
        s->wave[i] = 10 - i;
    }
}

static sw_df_scores *table_of(sw_dogfight *d)
{
    if (d->scores) return d->scores;
    if (!own_scores.score[0]) sw_dogfight_scores_default(&own_scores);
    return &own_scores;
}

/* Where the score just made would stand, or -1. */
static int score_rank(sw_dogfight *d)
{
    const sw_df_scores *s = table_of(d);
    if (d->score <= 0) return -1;
    for (int i = 0; i < SW_DF_TABLE; i++) if (d->score > s->score[i]) return i;
    return -1;
}

static void score_insert(sw_dogfight *d)
{
    sw_df_scores *s = table_of(d);
    int r = d->rank;
    if (r < 0 || r >= SW_DF_TABLE) return;
    for (int i = SW_DF_TABLE - 1; i > r; i--) {
        memcpy(s->name[i], s->name[i - 1], 4);
        s->score[i] = s->score[i - 1];
        s->wave[i] = s->wave[i - 1];
    }
    memcpy(s->name[r], d->initials, 3);
    s->name[r][3] = 0;
    s->score[r] = d->score;
    s->wave[r] = d->wave;
    s->dirty = 1;
}

#define HOWTO_LONG 32.0                     /* seconds of the how-to page, if nobody pulls the trigger */

void sw_dogfight_howto(sw_dogfight *d)
{
    if (d->preview) return;
    d->stage = SW_DF_HOWTO;
    d->stage_t = 0.0;
}

/* A frame of the game's (20.5 Hz): the lasers as TSTLAZ times them, the
 * shots dissolving by their timers. */
static void game_frame(sw_dogfight *d)
{
    if (d->lz_hit_next) {                   /* a hit: the beam stays on it a while */
        d->lz_hit_next = 0;
        d->lz_hit = 4;
        d->lz_edg = 0;
    } else if (d->lz_hit > 0) {
        d->lz_hit--;
    } else if (d->lz_edg > 0) {
        d->lz_edg--;
    }
    for (int s = 0; s < SW_DF_SHOTS; s++) {
        sw_df_shot *sh = &d->shot[s];
        if (sh->live && sh->hurt && --sh->tmr <= 0) sh->live = 0;
    }
}

/* --- the proton torpedoes ------------------------------------------------------------------ */

/* The enemy a torpedo goes after: the one nearest the sight's way ahead,
 * another than `other` if there is one. */
static int torp_target(const sw_dogfight *d, int other)
{
    int best = -1;
    double bs = -1e18;
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        const sw_df_enemy *e = &d->enemy[i];
        if (!e->kind) continue;
        double v[3];
        view_of(d, e->b.pos, v);
        double s = v[0] > 100.0 ? -4.0 * hypot(v[1], v[2]) / v[0] - v[0] / 20000.0 : -50.0 - sqrt(dot(v, v)) / 20000.0;
        if (i == other) s -= 100.0;
        if (s > bs) { bs = s; best = i; }
    }
    return best;
}

static void kill_enemy(sw_dogfight *d, sw_df_enemy *e, int scored)
{
    if (scored) d->score += KIND_SCORE[e->kind];
    explode(d, &e->b, e->speed);
    e->kind = K_NONE;
}

static void torps_step(sw_dogfight *d, const sw_dogfight_input *in, double dt)
{
    if (d->torp_charge < 1.0) {
        d->torp_charge += dt / TORP_CHARGE;
        if (d->torp_charge >= 1.0) { d->torp_charge = 1.0; sound(d, S_AUDRY); }   /* (charged: R2's beeps) */
    }
    int press = in->torpedo && !d->torp_held;
    d->torp_held = in->torpedo;
    if (press && d->torp_charge >= 1.0 && !d->over) {
        /* two at once, from below the guns, each after its own enemy */
        int t0 = torp_target(d, -1), t1 = torp_target(d, t0);
        for (int i = 0; i < SW_DF_TORPS; i++) {
            sw_df_torp *tp = &d->torp[i];
            double side = i ? 1.0 : -1.0;
            tp->live = 1;
            tp->target = i ? t1 : t0;
            tp->t = d->t;
            for (int k = 0; k < 3; k++) {
                tp->pos[k] = d->pos[k] + d->f[k] * 60.0 + d->r[k] * side * 110.0 - d->u[k] * 70.0;
                tp->vel[k] = d->f[k] * (d->speed + TORP_SPEED);
            }
        }
        d->torp_charge = 0.0;
        sound(d, S_AUDPH);
    }
    for (int i = 0; i < SW_DF_TORPS; i++) {
        sw_df_torp *tp = &d->torp[i];
        if (!tp->live) continue;
        if (d->t - tp->t > TORP_LIFE) { tp->live = 0; continue; }
        if (tp->target >= 0 && !d->enemy[tp->target].kind) tp->target = torp_target(d, d->torp[i ^ 1].live ? d->torp[i ^ 1].target : -1);
        double speed = sqrt(dot(tp->vel, tp->vel)), dir[3] = { tp->vel[0], tp->vel[1], tp->vel[2] };
        norm(dir);
        if (tp->target >= 0 && d->t - tp->t > 0.12) {
            /* it homes: its way turned towards its enemy */
            double want[3];
            for (int k = 0; k < 3; k++) want[k] = d->enemy[tp->target].b.pos[k] - tp->pos[k];
            norm(want);
            for (int k = 0; k < 3; k++) dir[k] += want[k] * TORP_TURN * dt;
            norm(dir);
        }
        for (int k = 0; k < 3; k++) {
            tp->vel[k] = dir[k] * speed;
            tp->pos[k] += tp->vel[k] * dt;
        }
        /* one torpedo is enough for any of them */
        for (int j = 0; j < SW_DF_ENEMIES; j++) {
            sw_df_enemy *e = &d->enemy[j];
            if (!e->kind || dist(e->b.pos, tp->pos) > TORP_HITS) continue;
            kill_enemy(d, e, 1);
            tp->live = 0;
            break;
        }
    }
}

/* --- the wingmen and the battle -------------------------------------------------------------- */

/* A wingman: beside the player as it begins, else coming in at a wall. */
static void ally_place(sw_dogfight *d, int i, int beside)
{
    sw_df_ally *a = &d->ally[i];
    memset(a, 0, sizeof *a);
    a->live = 1;
    a->hp = 3;
    a->target = -1;
    a->speed = ALLY_SPEED;
    double p[3], to[3];
    if (beside) {
        static const double R[SW_DF_ALLIES] = { -2600.0, 2600.0, 0.0 }, U[SW_DF_ALLIES] = { 300.0, 300.0, 1600.0 },
                            B[SW_DF_ALLIES] = { 1800.0, 1800.0, 2600.0 };
        for (int k = 0; k < 3; k++) {
            p[k] = d->pos[k] + d->r[k] * R[i] + d->u[k] * U[i] - d->f[k] * B[i];
            to[k] = p[k] + d->f[k];
        }
    } else {
        double az = rnd(d) * 6.2831853;
        p[0] = BOX * 0.9 * cos(az);
        p[1] = BOX * 0.9 * sin(az);
        p[2] = BOX * 0.2 * (rnd(d) - 0.5);
        to[0] = to[1] = to[2] = 0.0;
    }
    body_facing(&a->b, p, to);
    a->fire_t = d->t + 1.0 + rnd(d);
}

static void allies_step(sw_dogfight *d, double dt)
{
    for (int i = 0; i < SW_DF_ALLIES; i++) {
        sw_df_ally *a = &d->ally[i];
        if (!a->live) {
            if (a->dead_t > 0.0 && d->t - a->dead_t > ALLY_BACK && !d->over) ally_place(d, i, 0);
            continue;
        }
        if (a->target >= 0 && !d->enemy[a->target].kind) a->target = -1;
        /* (they fly with the player, off its wings, and leave the enemies to
         * it: when they fought them too, it was all ships shooting each
         * other down, said the owner) */
        a->target = -1;
        double want[3], to[3] = { 0.0, 0.0, 0.0 }, far = 1e18, pace = a->speed;
        if (a->target >= 0) {
            const sw_df_enemy *e = &d->enemy[a->target];
            for (int k = 0; k < 3; k++) to[k] = e->b.pos[k] + e->b.f[k] * e->speed * 0.4 - a->b.pos[k];
            far = sqrt(dot(to, to));
            norm(to);
            memcpy(want, to, sizeof want);
            if (a->mode == 0 && far < 900.0) {
                a->mode = 1;
                a->mode_t = d->t + 1.0 + rnd(d);
                for (int k = 0; k < 3; k++) a->away[k] = a->b.f[k] + (rnd(d) - 0.5) * 1.6;
                norm(a->away);
            }
        } else {
            /* none left: off the player's wing */
            /* (well out to the sides and behind: they are seen when the player turns, never in his face) */
            static const double R[SW_DF_ALLIES] = { -2600.0, 2600.0, 0.0 }, U[SW_DF_ALLIES] = { 300.0, 300.0, 1600.0 },
                                B[SW_DF_ALLIES] = { 1800.0, 1800.0, 2600.0 };
            double at[3];
            for (int k = 0; k < 3; k++) at[k] = d->pos[k] + d->r[k] * R[i] + d->u[k] * U[i] - d->f[k] * B[i] - a->b.pos[k];
            double gap = sqrt(dot(at, at));
            for (int k = 0; k < 3; k++) want[k] = d->f[k] * 900.0 + at[k] * 1.5;
            norm(want);
            pace = d->speed + (gap > 80.0 ? gap * 0.6 : 0.0);
            if (pace > d->speed * 1.6 + 400.0) pace = d->speed * 1.6 + 400.0;
        }
        if (a->mode == 1) {
            memcpy(want, a->away, sizeof want);
            if (d->t > a->mode_t) a->mode = 0;
        }
        /* not into the player */
        double v[3] = { a->b.pos[0] - d->pos[0], a->b.pos[1] - d->pos[1], a->b.pos[2] - d->pos[2] };
        double dd = sqrt(dot(v, v));
        if (dd > 1e-6 && dd < 2200.0) for (int k = 0; k < 3; k++) want[k] += v[k] / dd * (2200.0 / dd - 1.0) * 4.0;
        if (fabs(a->b.pos[0]) > BOX * 1.1 || fabs(a->b.pos[1]) > BOX * 1.1 || fabs(a->b.pos[2]) > BOX * 1.1)
            for (int k = 0; k < 3; k++) want[k] = -a->b.pos[k];
        norm(want);
        aim(a->b.f, a->b.r, a->b.u, want, ALLY_TURN * dt);
        for (int k = 0; k < 3; k++) a->b.pos[k] += a->b.f[k] * pace * dt;
        /* it fires red bolts at its enemy before its nose */
        if (a->target >= 0 && a->mode == 0 && d->t > a->fire_t && far < 7000.0 && dot(a->b.f, to) > 0.96) {
            for (int s = 0; s < SW_DF_BOLTS; s++) {
                sw_df_bolt *bo = &d->bolt[s];
                if (bo->live) continue;
                bo->live = 1;
                bo->red = 1;
                bo->t = d->t;
                for (int k = 0; k < 3; k++) { bo->pos[k] = a->b.pos[k] + a->b.f[k] * 150.0; bo->vel[k] = to[k] * BOLT_SPEED; }
                break;
            }
            a->fire_t = d->t + 0.4 + rnd(d) * 0.5;
        }
    }
    /* the bolts: green ones hurt the wingmen (red ones, theirs, no longer fly) */
    for (int s = 0; s < SW_DF_BOLTS; s++) {
        sw_df_bolt *bo = &d->bolt[s];
        if (!bo->live) continue;
        for (int k = 0; k < 3; k++) bo->pos[k] += bo->vel[k] * dt;
        if (d->t - bo->t > BOLT_LIFE) { bo->live = 0; continue; }
        if (!bo->red) {
            for (int j = 0; j < SW_DF_ALLIES; j++) {
                sw_df_ally *a = &d->ally[j];
                if (!a->live || dist(a->b.pos, bo->pos) > BOLT_HITS) continue;
                bo->live = 0;
                if (--a->hp <= 0) {
                    add_blast(d, a->b.pos);
                    if (dist(a->b.pos, d->pos) < 9000.0) sound(d, S_AUDSX);
                    a->live = 0;
                    a->dead_t = d->t;
                }
                break;
            }
        }
    }
    /* far off, beyond the box, the battle goes on: now and then a ship blows up */
    if (d->t > d->far_blast_t) {
        d->far_blast_t = d->t + 3.0 + rnd(d) * 4.0;
        double p[3];
        for (int k = 0; k < 3; k++) p[k] = d->f[k] * 0.9 + (rnd(d) - 0.5) * 1.6;
        norm(p);
        double r = 20000.0 + rnd(d) * 14000.0;
        for (int k = 0; k < 3; k++) p[k] = d->pos[k] + p[k] * r;
        add_blast(d, p);
    }
}

void sw_dogfight_step(sw_dogfight *d, const sw_dogfight_input *in, double dt)
{
    if (!d->active || dt <= 0.0) return;
    if (dt > 0.1) dt = 0.1;
    int pull = in->fire && !d->fire_held;
    d->fire_held = in->fire;
    if (d->stage == SW_DF_HOWTO) {
        /* the page: on until a pull of the trigger, or its end; the game waits */
        d->stage_t += dt;
        if ((pull && d->stage_t > 1.5) || d->stage_t > HOWTO_LONG) {
            d->stage = SW_DF_PLAY;
            d->fire_next = d->t + 0.4;      /* (that pull does not fire) */
        }
        return;
    }
    d->t += dt;
    if (d->preview) {
        sw_df_body *b = &d->enemy[0].b;
        turn(b->f, b->r, 0.5 * dt);
        turn(b->f, b->u, 0.13 * sin(d->t * 0.7) * dt);
        square3(b->f, b->r, b->u);
        return;
    }
    /* the end: GAME OVER a while; a new best pilot gives his initials; then the table */
    if (d->over && d->stage == SW_DF_PLAY && d->t - d->over_t > 4.0) {
        d->rank = score_rank(d);
        d->stage = d->rank >= 0 ? SW_DF_ENTRY : SW_DF_TABLE_SHOWN;
        d->stage_t = 0.0;
        d->letter = 0;
        d->letter_t = 0.0;
        memcpy(d->initials, "A  ", 4);
    }
    if (d->stage == SW_DF_ENTRY) {
        /* the yoke to a side steps through the letters, a pull takes one */
        static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ .";
        const int NL = (int)sizeof LETTERS - 1;
        d->stage_t += dt;
        char *c = &d->initials[d->letter];
        int at = (int)(strchr(LETTERS, *c) ? strchr(LETTERS, *c) - LETTERS : 0);
        if (fabsf(in->yaw) > 0.45f) {
            if (d->t >= d->letter_t) {
                at = (at + (in->yaw > 0.0f ? 1 : NL - 1)) % NL;
                d->letter_t = d->t + (fabsf(in->yaw) > 0.85f ? 0.11 : 0.22);
            }
        } else {
            d->letter_t = 0.0;
        }
        *c = LETTERS[at];
        if (pull) {
            sound(d, S_AUDXL);
            if (++d->letter < 3) d->initials[d->letter] = d->initials[d->letter - 1];
        }
        if (d->letter >= 3 || d->stage_t > 30.0) {
            if (d->letter < 3) for (int k = d->letter + 1; k < 3; k++) d->initials[k] = ' ';
            score_insert(d);
            d->stage = SW_DF_TABLE_SHOWN;
            d->stage_t = 0.0;
        }
    } else if (d->stage == SW_DF_TABLE_SHOWN) {
        d->stage_t += dt;
        if (d->stage_t > 7.0 || (pull && d->stage_t > 2.0)) d->finished = 1;
    }

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
    for (int i = 0; i < 3; i++) {
        d->pos[i] += (d->f[i] * d->speed + d->knock[i]) * dt;
        d->knock[i] *= exp(-2.5 * dt);              /* (a bounce dies away) */
    }

    /* the lasers: a shot for each pull of the trigger, and again and again while it is held */
    if (!d->over && in->fire && d->t >= d->fire_next) {
        shoot(d);
        d->fire_next = d->t + LASER_EVERY;
    }
    if (!in->fire) d->fire_next = 0.0;
    torps_step(d, in, dt);
    enemies_step(d, dt);
    allies_step(d, dt);
    /* the game's frames due */
    if (d->game_clock > d->t + 1.0 || d->game_clock < d->t - 0.5) d->game_clock = d->t;
    while (d->game_clock <= d->t) {
        game_frame(d);
        d->game_clock += 1.0 / GAME_HZ;
    }
}

float sw_dogfight_thrust(const sw_dogfight *d)
{
    double s = d->speed >= SPEED_MID ? (d->speed - SPEED_MID) / (SPEED_MAX - SPEED_MID) : (d->speed - SPEED_MID) / (SPEED_MID - SPEED_MIN);
    return (float)(s > 1.0 ? 1.0 : s < -1.0 ? -1.0 : s);
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

static void chase_one(sw_dogfight *d, double t, uint32_t h);

/* Three of them, each in its own time, so that the battle beyond goes on. */
static void chase(sw_dogfight *d)
{
    const double RUN = 11.0;
    static const double EVERY[3] = { 14.0, 17.0, 21.0 }, LAG[3] = { 0.0, 6.5, 12.0 };
    for (int c = 0; c < 3; c++) {
        double s = d->t - d->chase_t - LAG[c];
        if (s < 0.0) continue;
        double n = floor(s / EVERY[c]), t = (s - n * EVERY[c]) / RUN;
        if (t > 1.0) continue;
        chase_one(d, t, hash((uint32_t)n * 977u + (uint32_t)c * 7919u + 17u));
    }
}

static void chase_one(sw_dogfight *d, double t, uint32_t h)
{
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
        if (xwing) draw_xwing(d, &b, lum, 1.0, 0.0);
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

/* The scale GN1DRW gives a shot at the depth z: a binary scale, one step
 * smaller for each doubling of its distance (M.XT, half the game's units),
 * and the linear scale between. */
static void shot_scale(double z, uint8_t *bin_out, uint8_t *lin_out)
{
    double xt = z * 0.5;
    uint16_t dd = (uint16_t)(xt > 32767.0 ? 32767.0 : xt < 1.0 ? 1.0 : xt);
    uint8_t bin = 8, lin;
    for (;;) {
        --bin;
        if (bin == 0) { lin = (uint8_t)(dd >> 8); break; }
        dd = (uint16_t)(dd << 1);
        if (dd & 0x8000) { lin = (uint8_t)(dd >> 8) & 0x7f; break; }
    }
    *bin_out = bin;
    *lin_out = lin;
}

/* A fireball (an enemy's, or a proton torpedo) at (x, y), z away, as
 * GN1DRW draws it: the sparkler VJGNB at its scale. Shot down (hurt), it
 * dissolves: purple, its scale held between 3 and 6, fading with its
 * timer, the sparkler without its centre lines (VJGNX). */
static void fireball(sw_dogfight *d, double x, double y, double z, int hurt, int tmr)
{
    uint8_t bin, lin;
    shot_scale(z, &bin, &lin);
    move_to(d, x, y);
    if (!hurt) {
        emit(d, (uint16_t)(((bin | ((SW_VGSCAL & 0xf000) >> 8)) << 8) | lin));
        emit(d, SW_VJGNB);                  /* JSR TO THE SPARKLER */
    } else {
        uint8_t a = (uint8_t)(bin + 1), b = lin;
        if (a < 3) { a = 3; b = 0; }        /* DON'T LET IT GET TOO LARGE */
        else if (a >= 6) { a = 6; b = 0; }  /* BUT MAKE SURE ITS VISIBLE FAR AWAY */
        emit(d, (uint16_t)(((a | ((SW_VGSCAL & 0xf000) >> 8)) << 8) | b));
        emit(d, (uint16_t)((((SW_VGCOLR >> 8) + SW_VGCPRP) << 8) | (uint8_t)((tmr << 4) | 0x0f)));
        emit(d, SW_VJGNX);                  /* EXPLOSION SPARKLER */
    }
    emit(d, SW_VGSCAL);                     /* RESTORE SCALE */
}

/* A fireball that hit the ship, dissolving on its shields where it struck
 * (GLOWVW): white, growing as its timer runs out. */
static void glow(sw_dogfight *d, double x, double y, int tmr)
{
    move_to(d, x, y);
    emit(d, (uint16_t)((SW_VGSCAL & 0xf000) | (uint8_t)(tmr * (0xf0 / 0x0f))));
    emit(d, (uint16_t)((((SW_VGCOLR >> 8) + SW_VGCWHT) << 8) | (uint8_t)(tmr << 4)));
    emit(d, SW_VJGNX);
    emit(d, SW_VGSCAL);
}

/* A ship blowing up, z away: the explosion sparkler, larger, purple to
 * white, fading over its time k (1 .. 0). */
static void blast(sw_dogfight *d, double x, double y, double z, double k)
{
    uint8_t bin, lin;
    shot_scale(z * 0.7, &bin, &lin);       /* (a ship's, larger than a shot's) */
    uint8_t a = bin;
    if (a < 3) { a = 3; lin = 0; }          /* (as large as a hurt shell at most) */
    move_to(d, x, y);
    emit(d, (uint16_t)(((a | ((SW_VGSCAL & 0xf000) >> 8)) << 8) | lin));
    unsigned tmr = (unsigned)(k * 15.0);
    emit(d, (uint16_t)((((SW_VGCOLR >> 8) + (k > 0.6 ? SW_VGCWHT : k > 0.3 ? YLW : RED)) << 8) | (uint8_t)((tmr << 4) | 0x0f)));
    emit(d, SW_VJGNX);
    emit(d, SW_VGSCAL);
}

/* Half of what is left to the sight, rounded up when positive (WSLAZR's
 * LAZVW); the rest stays. */
static int16_t half_step(int16_t *left)
{
    uint16_t v = (uint16_t)*left;
    if (!(v & 0x8000)) v++;                 /* ROUND POSITIVE UP */
    int16_t h = (int16_t)((int16_t)v >> 1); /* DIVIDE BY 2 */
    *left = (int16_t)(*left - h);
    return h;
}

static int far_off(int16_t left)
{
    int32_t s = left + 8;
    if (s < 0) return 1;
    return (int16_t)(uint16_t)s - 16 > 0;
}

/* LAZVW: the beam from where it begins to the sight, in halving steps,
 * each in the next of its colors (VRLZF0..: they travel), the rest of it
 * bright; on a hit, the beam in one, turquoise, fading, and the splash. */
static void lazvw(sw_dogfight *d, const sw_machine *m, uint8_t b, int16_t dx, int16_t dy)
{
    uint16_t u = (uint16_t)(TLZFR + b);     /* FULL FIRE COLORS */
    if (d->lz_hit > 0) {
        emit(d, (uint16_t)(((((SW_VGCTRQ << 8) + SW_VGCOPC) >> 8) << 8) | (uint8_t)(d->lz_hit * 0x3f)));
    } else {
        do {
            emit(d, sw_rom16(m, u));
            u += 2;
            emit(d, (uint16_t)(half_step(&dy) & 0x1fff));
            emit(d, (uint16_t)((uint16_t)half_step(&dx) | 0xe000));
        } while (far_off(dx) || far_off(dy));
        emit(d, sw_rom16(m, u));
    }
    emit(d, (uint16_t)((uint16_t)dy & 0x1fff));
    emit(d, (uint16_t)((uint16_t)dx | 0xe000));
    if (d->lz_hit > 0) emit(d, SW_VJLZS);   /* SHOW LAZAR SPLASH */
    emit(d, SW_VGSCAL);
    emit(d, SW_VGCNTR);
}

/* One beam, from the gun at (x0, y0) (VG units) */
static void beam(sw_dogfight *d, const sw_machine *m, uint16_t y0, uint16_t dy_base, uint16_t x0, uint8_t colors, int key)
{
    d->tag = SW_TAG(SW_TAG_LASER, key);
    emit(d, (uint16_t)(y0 & 0x1fff));
    emit(d, (uint16_t)(x0 & 0x1fff));
    lazvw(d, m, colors, (int16_t)(uint16_t)(d->lz_cx - x0), (int16_t)(uint16_t)(d->lz_cy - dy_base));
}

/* VWLAZ: the left pair or the right, in turn */
static void lasers(sw_dogfight *d, const sw_machine *m)
{
    if (d->lz_edg <= 0 && d->lz_hit <= 0) return;
    uint16_t low = (uint16_t)(SW_VGLIMB - SW_VGOFFY + 35);
    emit(d, SW_VGCNTR);
    if (d->lz_alt & 1) {
        beam(d, m, SW_VGOFFY + 0, 0, SW_VGLIML + 90, 0, 0);        /* UPPER LEFT */
        beam(d, m, SW_VGLIMB + 35, low, SW_VGLIML + 85, 8, 1);     /* LOWER LEFT */
    } else {
        beam(d, m, SW_VGLIMB + 35, low, SW_VGLIMR - 85, 0, 2);     /* LOWER RIGHT */
        beam(d, m, SW_VGOFFY + 0, 0, SW_VGLIMR - 90, 8, 3);        /* UPPER RIGHT */
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

/* The Death Star in the colors of the films rather than the arcade
 * machine's green: its plates grey (dimmed white), its trench bright, its
 * dish grey with the spokes of the superlaser green; and lit by a sun that
 * stands still in space: the side towards it bright, the other dark, with
 * a soft line between (from where each line lies on the sphere as seen). */
static const double SUN[3] = { 0.1, -0.8, 0.5 };    /* (from the left and above, as one first sees it) */

/* The lines' units are not square: the screen's 255 across and 303 up
 * fill a tube of 4 by 3, so a unit up is DS_K times shorter than one
 * across. The sphere is worked out in square units (y / DS_K). */
#define DS_K (303.0 / 255.0 * 4.0 / 3.0)

/* how lit a point of the Death Star's disc is (0.18 .. 1), the sun as the ship sees it */
static double ds_lit(double rx, double ry, const double *sv)
{
    double rr = rx * rx + ry * ry, n[3] = { rx, ry, rr < 1.0 ? sqrt(1.0 - rr) : 0.0 };
    double lit = n[0] * sv[0] + n[1] * sv[1] + n[2] * sv[2];
    return lit < -0.25 ? 0.18 : lit > 0.35 ? 1.0 : 0.18 + 0.82 * (lit + 0.25) / 0.6;
}

/* a line on the Death Star's disc, in the lines' units, lit where it lies */
static void ds_line(sw_dogfight *d, double cx, double cy, double r, const double *sv, double x0, double y0, double x1, double y1, double base)
{
    sw_line *l = new_line(d);
    if (!l) return;
    double k = ds_lit(((x0 + x1) * 0.5 - cx) / r, ((y0 + y1) * 0.5 - cy) / r, sv);
    l->x0 = (float)x0; l->y0 = (float)(y0 * DS_K);
    l->x1 = (float)x1; l->y1 = (float)(y1 * DS_K);
    l->color = WHT;
    l->zz = 7;
    l->intensity = (uint8_t)(base * k > 255.0 ? 255.0 : base * k);
    l->id = ((uint64_t)SW_TAG(SW_TAG_DSTAR, 3) << 48) | (0x40000000u + d->line_id++);
}

/* More of its surface, laid on the sphere as the stamps have it (its
 * trench, a great circle seen edge on, gives its equator): latitudes
 * along the trench, a few meridians across, and plates of the surface
 * here and there, none on the dish. */
static void death_star_details(sw_dogfight *d, double cx, double cy, double r, const double *sv, double dx, double dy, double dish_r)
{
    /* the trench: its longest bright line gives the way along the equator */
    double tx = 1.0, ty = 0.0, best = 0.0;
    for (int i = 0; i < d->list.count; i++) {
        const sw_line *l = &d->list.lines[i];
        if (SW_TAG_SECTION((unsigned)(l->id >> 48)) != SW_TAG_DSTAR || l->color != WHT || l->intensity < 60) continue;
        double ex = l->x1 - l->x0, ey = (l->y1 - l->y0) / DS_K, len = hypot(ex, ey);
        if (len > best && len > r) { best = len; tx = ex / len; ty = ey / len; }
    }
    double ax = -ty, ay = tx;                           /* the axis: across the trench */
    if (ax * (dx - cx) + ay * (dy - cy) < 0.0) { ax = -ax; ay = -ay; }  /* (the dish in the north) */
    /* a point of the sphere: latitude, longitude (0 towards the ship), on the screen */
#define DS_AT(lat, lon, X, Y) do { double cl = cos(lat); (X) = cx + r * (cl * sin(lon) * tx + sin(lat) * ax); \
                                   (Y) = cy + r * (cl * sin(lon) * ty + sin(lat) * ay); } while (0)
    const double PI = 3.14159265358979;
    /* latitudes */
    static const double LATS[6] = { -55.0, -35.0, -16.0, 16.0, 35.0, 55.0 };
    for (int i = 0; i < 6; i++) {
        double lat = LATS[i] * PI / 180.0;
        for (int k = 0; k < 8; k++) {
            double a0 = (-90.0 + k * 22.5) * PI / 180.0, a1 = a0 + 22.5 * PI / 180.0, x0, y0, x1, y1;
            DS_AT(lat, a0, x0, y0);
            DS_AT(lat, a1, x1, y1);
            if (dish_r > 0.0 && (hypot(x0 - dx, y0 - dy) < dish_r * 1.05 || hypot(x1 - dx, y1 - dy) < dish_r * 1.05)) continue;
            ds_line(d, cx, cy, r, sv, x0, y0, x1, y1, fabs(LATS[i]) < 20.0 ? 110.0 : 80.0);
        }
    }
    /* meridians */
    static const double LONS[4] = { -62.0, -30.0, 30.0, 62.0 };
    for (int i = 0; i < 4; i++) {
        double lon = LONS[i] * PI / 180.0;
        for (int k = 0; k < 10; k++) {
            double a0 = (-80.0 + k * 16.0) * PI / 180.0, a1 = a0 + 16.0 * PI / 180.0, x0, y0, x1, y1;
            if (fabs(a0) < 0.06 || fabs(a1) < 0.06) continue;     /* (not across the trench) */
            DS_AT(a0, lon, x0, y0);
            DS_AT(a1, lon, x1, y1);
            if (dish_r > 0.0 && (hypot(x0 - dx, y0 - dy) < dish_r * 1.05 || hypot(x1 - dx, y1 - dy) < dish_r * 1.05)) continue;
            ds_line(d, cx, cy, r, sv, x0, y0, x1, y1, 70.0);
        }
    }
    /* plates: small, here and there, the same every time */
    for (int i = 0; i < 40; i++) {                  /* (each plate is four lines for the beam: not too many) */
        uint32_t h = hash((uint32_t)i * 2246822519u + 7u);
        double lat = ((double)(h & 0xff) / 255.0 * 150.0 - 75.0) * PI / 180.0;
        double lon = ((double)((h >> 8) & 0xff) / 255.0 * 160.0 - 80.0) * PI / 180.0;
        double wl = (3.0 + (double)((h >> 16) & 7)) * PI / 180.0, hl = (2.0 + (double)((h >> 19) & 3)) * PI / 180.0;
        if (fabs(lat) < 0.07 || fabs(lat) - hl < 0.07) continue;      /* (the trench is there) */
        double c[4][2];
        DS_AT(lat - hl, lon - wl, c[0][0], c[0][1]);
        DS_AT(lat - hl, lon + wl, c[1][0], c[1][1]);
        DS_AT(lat + hl, lon + wl, c[2][0], c[2][1]);
        DS_AT(lat + hl, lon - wl, c[3][0], c[3][1]);
        int on_dish = 0;
        for (int k = 0; k < 4; k++) on_dish |= dish_r > 0.0 && hypot(c[k][0] - dx, c[k][1] - dy) < dish_r * 1.1;
        if (on_dish) continue;
        double base = 60.0 + (double)((h >> 22) & 31);
        for (int k = 0; k < 4; k++)
            ds_line(d, cx, cy, r, sv, c[k][0], c[k][1], c[(k + 1) % 4][0], c[(k + 1) % 4][1], base);
    }
#undef DS_AT
}

static void death_star_colors(sw_dogfight *d, float cx, float cy, float r)
{
    double sun[3] = { SUN[0], SUN[1], SUN[2] };
    norm(sun);
    /* the sun as the ship sees it: to the right, up, towards the ship */
    double sv[3] = { dot(sun, d->r), dot(sun, d->u), -dot(sun, d->f) };
    /* the dish: where its red spokes meet */
    double dx = 0.0, dy = 0.0;
    int nd = 0;
    for (int i = 0; i < d->list.count; i++) {
        const sw_line *l = &d->list.lines[i];
        if (SW_TAG_SECTION((unsigned)(l->id >> 48)) != SW_TAG_DSTAR || l->color != RED) continue;
        dx += l->x0 + l->x1;
        dy += (l->y0 + l->y1) / DS_K;
        nd += 2;
    }
    if (nd) { dx /= nd; dy /= nd; }
    double dish_r = 0.0;
    for (int i = 0; i < d->list.count; i++) {
        const sw_line *l = &d->list.lines[i];
        if (SW_TAG_SECTION((unsigned)(l->id >> 48)) != SW_TAG_DSTAR || l->color != RED) continue;
        double e0 = hypot(l->x0 - dx, l->y0 / DS_K - dy), e1 = hypot(l->x1 - dx, l->y1 / DS_K - dy);
        if (e0 > dish_r) dish_r = e0;
        if (e1 > dish_r) dish_r = e1;
    }
    for (int i = 0; i < d->list.count; i++) {
        sw_line *l = &d->list.lines[i];
        if (SW_TAG_SECTION((unsigned)(l->id >> 48)) != SW_TAG_DSTAR) continue;
        /* how lit it is where it lies */
        double mx = (l->x0 + l->x1) * 0.5 - cx, my = (l->y0 + l->y1) * 0.5 / DS_K - cy;
        double k = ds_lit(mx / r, my / r, sv);
        double base;
        unsigned c = WHT;
        if (l->color == RED) {
            /* the dish: its spokes green where they meet, its rim grey and bright */
            /* (a spoke runs from the middle out: long; the rim is made of short pieces) */
            int spoke = dish_r > 0.0 && hypot(l->x1 - l->x0, (l->y1 - l->y0) / DS_K) > dish_r * 0.55;
            c = spoke ? GRN : WHT;
            base = spoke ? 170.0 : 150.0;
            if (spoke && k < 0.55) k = 0.55;    /* (the superlaser glows, lit or not) */
        } else if (l->color == WHT) {
            base = 210.0;                       /* the trench */
            if (k < 0.4) k = 0.4;
        } else {
            base = 135.0;                       /* the plates and the outline */
        }
        l->color = (uint8_t)c;
        l->intensity = (uint8_t)(base * k > 255.0 ? 255.0 : base * k);
    }
    death_star_details(d, cx, cy, r, sv, dx, dy, dish_r);
}

/* --- how to fly it ------------------------------------------------------------------------------
 *
 * As Time Pilot 3D shows how it is flown: the yoke, in the round, and the
 * X-wing beside it doing what the yoke does. First the yoke is turned,
 * then pushed and pulled; then it turns round to show its front with the
 * two triggers, and back for the two buttons under the thumbs, each lit in
 * turn with what it does; last the roll, and START. The yoke is the
 * arcade's of the eighties: a box (the Atari logo on the side the player
 * sees), a cone out of each side, on each a grip leaning in, its trigger in
 * front, its thumb's button behind. In the units of Time Pilot 3D's page (x
 * to the right, y up, z away from the player), K of ours each. */

typedef struct hxf { double m[9], t[3]; } hxf;

static hxf hxf_mul(const hxf *a, const hxf *b)
{
    hxf r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            r.m[i * 3 + j] = a->m[i * 3] * b->m[j] + a->m[i * 3 + 1] * b->m[3 + j] + a->m[i * 3 + 2] * b->m[6 + j];
        r.t[i] = a->m[i * 3] * b->t[0] + a->m[i * 3 + 1] * b->t[1] + a->m[i * 3 + 2] * b->t[2] + a->t[i];
    }
    return r;
}

static hxf hxf_rot(int axis, double a)
{
    double c = cos(a), s = sin(a);
    hxf r = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } };
    int i = (axis + 1) % 3, j = (axis + 2) % 3;     /* (x: y z; y: z x; z: x y) */
    r.m[i * 3 + i] = c; r.m[i * 3 + j] = -s;
    r.m[j * 3 + i] = s; r.m[j * 3 + j] = c;
    return r;
}

static hxf hxf_move(double x, double y, double z, double k)
{
    hxf r = { { k, 0, 0, 0, k, 0, 0, 0, k }, { x, y, z } };
    return r;
}

#define HOWTO_K (1000.0 / 150.0)

/* a point of the page in space, as the ship (where the page is looked at from) sees it */
static void hw_at(const sw_dogfight *d, const hxf *x, double px, double py, double pz, double *w)
{
    double p[3] = { px, py, pz }, q[3];
    for (int i = 0; i < 3; i++) q[i] = x->m[i * 3] * p[0] + x->m[i * 3 + 1] * p[1] + x->m[i * 3 + 2] * p[2] + x->t[i];
    for (int i = 0; i < 3; i++) w[i] = d->pos[i] + (d->r[i] * q[0] + d->u[i] * q[1] + d->f[i] * q[2]) * HOWTO_K;
}

static void hw_line(sw_dogfight *d, const hxf *x, double x0, double y0, double z0, double x1, double y1, double z1, unsigned c, float lum)
{
    double a[3], b[3];
    hw_at(d, x, x0, y0, z0, a);
    hw_at(d, x, x1, y1, z1, b);
    line3(d, a, b, c, lum, 0, 0, 0);
}

/* the twelve edges of a box, and its six faces */
static void hw_box(sw_dogfight *d, const hxf *x, double x0, double y0, double z0, double x1, double y1, double z1, unsigned c, float lum)
{
    double p[8][3];
    if (++d->inst == 0) d->inst = 0x8000;
    for (int i = 0; i < 8; i++) hw_at(d, x, i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0, p[i]);
    for (int i = 0; i < 8; i++) {
        if (!(i & 1)) line3(d, p[i], p[i | 1], c, lum, 0, 0, 0);
        if (!(i & 2)) line3(d, p[i], p[i | 2], c, lum, 0, 0, 0);
        if (!(i & 4)) line3(d, p[i], p[i | 4], c, lum, 0, 0, 0);
    }
    static const int F[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
    for (int i = 0; i < 6; i++) {
        double q[4][3];
        for (int k = 0; k < 4; k++) memcpy(q[k], p[F[i][k]], sizeof q[k]);
        int_face(d, (const double (*)[3])q, 4);
    }
}

/* The Atari logo, on the box's side towards the player (z = z0): the
 * straight stem in the middle, the two curved ones flaring out below. */
static void hw_atari(sw_dogfight *d, const hxf *x, double z0, float lum)
{
    if (++d->inst == 0) d->inst = 0x8000;
    const double TOP = 2.7, LOW = -3.0;
    hw_line(d, x, -0.38, LOW, z0, -0.38, TOP, z0, RED, lum);
    hw_line(d, x, 0.38, LOW, z0, 0.38, TOP, z0, RED, lum);
    hw_line(d, x, -0.38, TOP, z0, 0.38, TOP, z0, RED, lum);
    hw_line(d, x, -0.38, LOW, z0, 0.38, LOW, z0, RED, lum);
    for (int s = -1; s <= 1; s += 2) {
        double pi0 = 0.75, po0 = 1.35;          /* (inner, outer edge at the top) */
        for (int k = 0; k < 6; k++) {
            double t0 = k / 6.0, t1 = (k + 1) / 6.0;
            double y0 = TOP + (LOW - TOP) * t0, y1 = TOP + (LOW - TOP) * t1;
            double i0 = pi0 + 0.85 * t0 * t0, i1 = pi0 + 0.85 * t1 * t1;
            double o0 = po0 + 2.0 * t0 * t0, o1 = po0 + 2.0 * t1 * t1;
            hw_line(d, x, s * i0, y0, z0, s * i1, y1, z0, RED, lum);
            hw_line(d, x, s * o0, y0, z0, s * o1, y1, z0, RED, lum);
        }
        hw_line(d, x, s * pi0, TOP, z0, s * po0, TOP, z0, RED, lum);
        hw_line(d, x, s * (pi0 + 0.85), LOW, z0, s * (po0 + 2.0), LOW, z0, RED, lum);
    }
}

static double smooth01(double v) { v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v; return v * v * (3.0 - 2.0 * v); }

static int howto_lines(sw_dogfight *d, const sw_machine *m, const sw_line **out)
{
    const double PI = 3.14159265358979, T_PUSH = 5.0, T_BUTTONS = 10.0, EACH = 3.5;
    const double T_ROLL = T_BUTTONS + 4.0 * EACH, T_START = T_ROLL + 4.5;
    enum { LASER, TORPEDO, FASTER, SLOWER };
    double t = d->stage_t;
    double turn_k = t < T_PUSH ? sin(2.0 * PI * t / 2.5) : t > T_ROLL && t < T_START ? 1.0 : 0.0;
    double push = t >= T_PUSH && t < T_BUTTONS ? sin(2.0 * PI * (t - T_PUSH) / 2.5) : 0.0;
    double roll_round = t > T_ROLL ? 2.0 * PI * smooth01((t - T_ROLL - 0.6) / 3.0) : 0.0;
    int step = t >= T_BUTTONS && t < T_ROLL ? (int)((t - T_BUTTONS) / EACH) : -1, rolling = t >= T_ROLL && t < T_START;
    if (step > SLOWER) step = SLOWER;
    /* which is lit: the triggers and thumb buttons, right and left */
    int lit_trig[2] = { step == LASER, step == TORPEDO }, lit_thumb[2] = { step == FASTER || rolling, step == SLOWER || rolling };
    int blink = fmod(t, 0.2) < 0.1;
    /* from the player's side, or (for a trigger) from in front of it */
    double front = 0.0, was = 0.0;
    for (int k = 0; k < 6; k++) {
        double begins = k < 4 ? T_BUTTONS + EACH * k : k == 4 ? T_ROLL : T_START;
        double want = k < 2 ? 1.0 : 0.0;
        if (t < begins) break;
        front = was + (want - was) * smooth01((t - begins) / 1.1);
        was = want;
    }
    double yaw = 0.4 + (PI - 0.8) * front;

    d->at = SW_VGVARB;
    d->n_faces = 0;
    d->ds_shown = 0;
    d->inst = 0x8000;
    d->line_id = 0;
    d->tag = SW_TAG(SW_TAG_MESS, 0x3ff);
    emit(d, SW_VGSCAL);
    text_mid(d, m, 0.0, 128.0, YLW, 0xd0, SW_VGSCAL - 0x100, LETTER * 2.0, "HOW TO FLY");
    const char *line1, *line2;
    if (t < T_PUSH) { line1 = "TURN THE YOKE"; line2 = "THE X-WING TURNS WITH IT"; }
    else if (t < T_BUTTONS) { line1 = "PULL TO CLIMB - PUSH TO DIVE"; line2 = "THE SIGHT GOES WITH THE YOKE"; }
    else if (step == LASER) { line1 = "RIGHT TRIGGER - LASERS"; line2 = "HOLD IT AND THEY GO ON FIRING"; }
    else if (step == TORPEDO) { line1 = "LEFT TRIGGER - PROTON TORPEDOES"; line2 = "TWO HOMING SHOTS WHEN CHARGED"; }
    else if (step == FASTER) { line1 = "RIGHT THUMB BUTTON - FASTER"; line2 = "LET GO AND IT SLOWS AGAIN"; }
    else if (step == SLOWER) { line1 = "LEFT THUMB BUTTON - SLOWER"; line2 = "TO TURN TIGHTER"; }
    else if (rolling) { line1 = "FASTER AND SLOWER TOGETHER"; line2 = "NOW THE YOKE ROLLS THE X-WING"; }
    else { line1 = "START CHANGES THE VIEW"; line2 = "ARCADE - COCKPIT - OUTSIDE"; }
    text_mid(d, m, 0.0, -96.0, WHT, 0xe0, SW_VGSCAL, LETTER, line1);
    text_mid(d, m, 0.0, -108.0, TRQ, 0xa0, SW_VGSCAL, LETTER, line2);
    if (t > 1.5 && fmod(t, 1.0) < 0.65) text_mid(d, m, 0.0, -136.0, RED, 0xc0, SW_VGSCAL, LETTER, "PULL THE TRIGGER TO BEGIN");

    /* the X-wing, seen from behind and a little above, doing what the yoke
     * does: its wings close as it speeds up, its engines' fire grows */
    sw_df_body xb;
    for (int k = 0; k < 3; k++) xb.pos[k] = d->pos[k] + d->f[k] * 1900.0 + d->r[k] * 820.0 + d->u[k] * 480.0;
    /* (its nose along the eye's line to it: seen from straight behind, off
     * the view's middle as it is; a little nose down, its back to the eye) */
    for (int k = 0; k < 3; k++) xb.f[k] = xb.pos[k] - d->pos[k];
    norm(xb.f);
    memcpy(xb.r, d->r, sizeof xb.r);
    memcpy(xb.u, d->u, sizeof xb.u);
    square3(xb.f, xb.r, xb.u);
    turn(xb.f, xb.u, -0.1);
    turn(xb.f, xb.r, t < T_PUSH ? turn_k * 0.35 : 0.0);
    turn(xb.f, xb.u, -push * 0.45);                 /* (pushed: the nose down) */
    turn(xb.r, xb.u, -(turn_k * (rolling ? 0.0 : 0.7) + roll_round));
    square3(xb.f, xb.r, xb.u);
    double t_fast = T_BUTTONS + 2.0 * EACH, t_slow = T_BUTTONS + 3.0 * EACH;
    double xw_open = step == FASTER ? 1.0 - smooth01((t - t_fast) / 1.0) : step == SLOWER ? smooth01((t - t_slow) / 1.0) : 1.0;
    double xw_burn = step == FASTER ? 0.3 + 0.7 * smooth01((t - t_fast) / 0.6) : step == SLOWER ? 0.05 : rolling ? 0.45 : 0.25;
    if (step == TORPEDO) {
        /* two at a time, as the game's shots look, from below its nose, ahead */
        double t0 = T_BUTTONS + EACH, ph = fmod(t - t0, 1.75) / 1.75;
        for (int g = 0; g < 2; g++) {
            double sy = g ? 1.0 : -1.0, ahead = 1.1 + ph * 16.0, k = 1.0 - ph * 0.7;
            double q[3] = { ahead * XW_SIZE, sy * 0.35 * k * XW_SIZE, -0.12 * XW_SIZE }, p[3], x, y, z;
            place(&xb, q, 1.0, p);
            if (ph < 0.95 && project(d, p, &x, &y, &z)) {
                d->tag = SW_TAG(SW_TAG_SHOT, 0x200 + g);
                fireball(d, x, y, z < 4500.0 ? 4500.0 : z, 0, 0);
            }
        }
    }
    emit(d, 0x2000);
    sw_avg_run_tagged(d->space, SW_VGVARB, d->tags, NULL, &d->list, 20000);

    /* the yoke */
    hxf base = hxf_move(-38.0, 38.0, 150.0, 1.0), k3 = hxf_move(0, 0, 0, 4.4), r;   /* (up: the view's middle is low) */
    r = hxf_rot(0, 0.3); base = hxf_mul(&base, &r);
    r = hxf_rot(1, yaw); base = hxf_mul(&base, &r);
    base = hxf_mul(&base, &k3);
    r = hxf_rot(2, -turn_k * 0.6);
    hxf box = hxf_mul(&base, &r);
    hxf gm = hxf_move(0, 0, 3.5, 1.0), grips = hxf_mul(&box, &gm);
    r = hxf_rot(0, push * 0.5); grips = hxf_mul(&grips, &r);
    hw_box(d, &box, -5.0, -4.2, 0.0, 5.0, 4.2, 7.0, WHT, 150.0f);
    hw_atari(d, &box, -0.05, 230.0f);
    for (int s = 0; s < 2; s++) {
        double side = s == 0 ? 1.0 : -1.0;          /* (0 the right grip, 1 the left) */
        hxf gt = hxf_move(9.6 * side, 0, 0, 1.0), grip = hxf_mul(&grips, &gt);
        r = hxf_rot(2, 0.22 * side); grip = hxf_mul(&grip, &r);
        /* the cone out of the box's side */
        if (++d->inst == 0) d->inst = 0x8000;
        for (int j = 0; j < 6; j++) {
            double a0 = 2.0 * PI * j / 6.0, a1 = 2.0 * PI * (j + 1) / 6.0;
            hw_line(d, &grips, 5.0 * side, 1.1 * cos(a0), 1.1 * sin(a0), 5.0 * side, 1.1 * cos(a1), 1.1 * sin(a1), BLU, 170.0f);
            hw_line(d, &grips, 8.3 * side, 2.3 * cos(a0), 2.3 * sin(a0), 8.3 * side, 2.3 * cos(a1), 2.3 * sin(a1), BLU, 170.0f);
            if (!(j & 1)) hw_line(d, &grips, 5.0 * side, 1.1 * cos(a0), 1.1 * sin(a0), 8.3 * side, 2.3 * cos(a0), 2.3 * sin(a0), BLU, 170.0f);
        }
        /* the grip, its trigger in front, its thumb's button behind */
        hw_box(d, &grip, -1.3, -6.0, -1.4, 1.3, 6.0, 1.4, TRQ, 200.0f);
        int lt = lit_trig[s] && blink, lb = lit_thumb[s] && blink;
        hw_box(d, &grip, -0.7, 1.4, 1.4, 0.7, 3.6, 2.7, lt ? WHT : RED, lt ? 255.0f : 200.0f);
        if (++d->inst == 0) d->inst = 0x8000;
        for (int j = 0; j < 8; j++) {
            double a0 = 2.0 * PI * j / 8.0, a1 = 2.0 * PI * (j + 1) / 8.0;
            hw_line(d, &grip, 0.9 * cos(a0), 4.5 + 0.9 * sin(a0), -1.75, 0.9 * cos(a1), 4.5 + 0.9 * sin(a1), -1.75,
                    lb ? WHT : YLW, lb ? 255.0f : 210.0f);
        }
    }

    /* the X-wing, its lasers, its fire */
    draw_xwing(d, &xb, 220.0f, xw_open, xw_burn);
    if (step == LASER) {
        /* a shot every 0.18 s, the pairs in turn, each bolt flying ahead
         * from its guns' tips to where they meet */
        double begun = T_BUTTONS;
        for (int k = 0; k < 4; k++) {
            int n = (int)floor((t - begun) / 0.18) - k;
            if (n < 0) continue;
            double age = t - begun - n * 0.18, s0 = 0.9 + age * 13.0;
            if (s0 > 9.0) continue;
            int right = n & 1;
            for (int g = 0; g < 2; g++) {
                double sy = right ? 1.0 : -1.0, sz = g ? 1.0 : -1.0, gy = sy * 0.95, gz = sz * 0.28;
                double k0 = 1.0 - s0 / 12.0, k1 = 1.0 - (s0 + 2.6) / 12.0;
                double a[3] = { s0, gy * k0, gz * k0 }, c[3] = { s0 + 2.6, gy * k1, gz * k1 };
                xw_line(d, &xb, a, c, TRQ, 255.0f);
            }
        }
    }
    *out = d->list.lines;
    return d->list.count;
}

int sw_dogfight_lines(sw_dogfight *d, const sw_machine *m, const sw_line **out)
{
    vg_catch_up(d, m);
    if (d->stage == SW_DF_HOWTO) return howto_lines(d, m, out);
    d->at = SW_VGVARB;                      /* (above the routines the restarts keep) */
    d->n_faces = 0;
    d->ds_shown = 0;
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
        /* (shown as long as any of its disc is: its radius on the screen is
         * about 120, whatever way one looks) */
        if (project(d, DEATH_STAR, &x, &y, NULL) && on_screen(x, y, 135.0)) {
            const uint16_t scale = SW_VGSCAL + 0x200;     /* (its stamps are big: the game draws them small at first) */
            static const uint16_t stamps[5] = { SW_VJBCIR, SW_VJBTRN, SW_VJBDSH, SW_VJBNSD, SW_VJBFRM };
            d->tag = SW_TAG(SW_TAG_DSTAR, 3);
            d->ds_x = x;
            d->ds_y = y;
            d->ds_shown = 1;
            color(d, GRN, 0x80);
            for (int i = 0; i < 5; i++) {
                d->tag = SW_TAG(SW_TAG_DSTAR, i == 0 ? 5 : 10 + i);   /* (its outline told apart: its disc is found from it) */
                move_to(d, x, y);
                emit(d, scale);
                emit(d, stamps[i]);
                emit(d, SW_VGSCAL);
            }
        }
    }

    /* far off, the battle beyond: ships blowing up */
    for (int i = 0; i < 8; i++) {
        double age = d->t - d->blast_t[i], x, y, z;
        if (d->blast_t[i] <= 0.0 || age > 0.8 || !project(d, d->blast_at[i], &x, &y, &z) || !on_screen(x, y, 10.0)) continue;
        d->tag = SW_TAG(SW_TAG_SHOT, 0x300 + i);
        blast(d, x, y, z, 1.0 - age / 0.8);
    }

    /* the fireballs, as the game draws its shots */
    for (int s = 0; s < SW_DF_SHOTS; s++) {
        const sw_df_shot *sh = &d->shot[s];
        double x, y, z;
        if (!sh->live) continue;
        d->tag = SW_TAG(SW_TAG_SHOT, s);
        if (sh->hurt == 2) { glow(d, sh->gx, sh->gy, sh->tmr); continue; }
        if (!project(d, sh->pos, &x, &y, &z) || z < 40.0 || !on_screen(x, y, 4.0)) continue;
        fireball(d, x, y, z, sh->hurt, sh->tmr);
    }
    /* the proton torpedoes: balls as the fireballs are */
    for (int i = 0; i < SW_DF_TORPS; i++) {
        double x, y, z;
        if (!d->torp[i].live || !project(d, d->torp[i].pos, &x, &y, &z) || z < 40.0 || !on_screen(x, y, 4.0)) continue;
        d->tag = SW_TAG(SW_TAG_SHOT, 0x200 + i);
        fireball(d, x, y, z < 4500.0 ? 4500.0 : z, 0, 0);     /* (just fired, not as large as a shot at one's face) */
    }

    /* the lasers, as VWLAZ draws them */
    lasers(d, m);

    /* the sight, in the middle of the view: one aims with the nose */
    int playing = d->stage == SW_DF_PLAY && !d->over;   /* (not once it is lost, nor over the initials and the table) */
    d->tag = SW_TAG(SW_TAG_SITE, 0);
    if (playing) {
        move_to(d, SIGHT_X, SIGHT_Y);
        color(d, TRQ, 0x80);
        emit(d, rom16(m, SW_VGSITE));
    }

    /* the X-wing's nose and guns (VWPLAN, the sight in the middle) */
    static const uint16_t parts[5] = { SW_VJSTBM, SW_VJSTTR, SW_VJSTBL, SW_VJSTTL, SW_VJSTBR };
    for (int i = 0; i < 5 && playing; i++) {
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
        /* the torpedoes: a bar filling as they charge; charged, it shines */
        x0 = -122.0;
        int ready = d->torp_charge >= 1.0;
        move_to(d, x0, y);
        color(d, ready ? TRQ : GRN, ready ? (fmod(d->t, 0.6) < 0.4 ? 0xff : 0x90) : 0x40);
        vctr(d, vg(w), 0, 5);
        if (!ready) {
            move_to(d, x0, y);
            color(d, YLW, 0xa0);
            vctr(d, vg(w * d->torp_charge), 0, 7);
        }
        text(d, m, x0, y - 9.0, ready ? TRQ : GRN, ready ? 0xa0 : 0x60, SW_VGSCAL, "TORPEDO");
    }
    if (d->stage == SW_DF_ENTRY) {
        /* a new best pilot: his initials, chosen with the yoke */
        char s[24];
        text_mid(d, m, 0.0, 80.0, YLW, 0xd0, SW_VGSCAL, LETTER, "A NEW BEST PILOT");
        number(s, d->score);
        text_mid(d, m, 0.0, 68.0, GRN, 0xa0, SW_VGSCAL, LETTER, s);
        for (int k = 0; k < 3; k++) {
            char c[2] = { d->initials[k], 0 };
            int on = k == d->letter && fmod(d->stage_t, 0.5) < 0.3;
            if (c[0] == ' ') c[0] = '-';
            text(d, m, -20.0 + 15.0 * k, 40.0, k == d->letter ? WHT : TRQ, on || k != d->letter ? 0xe0 : 0x60, SW_VGSCAL - 0x100, c);
        }
        text_mid(d, m, 0.0, 14.0, GRN, 0x90, SW_VGSCAL, LETTER, "YOKE - LETTER    TRIGGER - NEXT");
    } else if (d->stage == SW_DF_TABLE_SHOWN) {
        /* the dogfight's own best pilots */
        const sw_df_scores *t = table_of(d);
        text_mid(d, m, 0.0, 96.0, YLW, 0xd0, SW_VGSCAL - 0x100, LETTER * 2.0, "DOGFIGHT");
        text_mid(d, m, 0.0, 78.0, RED, 0xb0, SW_VGSCAL, LETTER, "BEST PILOTS");
        for (int i = 0; i < SW_DF_TABLE; i++) {
            char s[48], n[16], w[16];
            number(n, t->score[i]);
            number(w, t->wave[i]);
            int k = 0;
            s[k++] = (char)(i == 9 ? '1' : ' ');
            s[k++] = (char)(i == 9 ? '0' : '1' + i);
            s[k++] = ' '; s[k++] = ' ';
            for (int j = 0; j < 3; j++) s[k++] = t->name[i][j] ? t->name[i][j] : ' ';
            s[k++] = ' ';
            for (int j = (int)strlen(n); j < 7; j++) s[k++] = ' ';
            for (int j = 0; n[j]; j++) s[k++] = n[j];
            memcpy(s + k, "   WAVE ", 8); k += 8;
            for (int j = 0; w[j]; j++) s[k++] = w[j];
            s[k] = 0;
            int mine = i == d->rank;
            text_mid(d, m, 0.0, 62.0 - 11.0 * i, mine ? WHT : GRN, mine && fmod(d->stage_t, 0.5) < 0.3 ? 0xff : 0xa0, SW_VGSCAL, LETTER, s);
        }
    } else if (d->over) {
        char s[24];
        text_mid(d, m, 0.0, 60.0, RED, 0xc0, SW_VGSCAL, LETTER, "GAME OVER");
        number(s, d->score);
        text_mid(d, m, 0.0, 48.0, GRN, 0xa0, SW_VGSCAL, LETTER, s);
    } else if (d->t - d->wave_t < 2.5 && !d->preview) {
        char s[24] = "WAVE ";
        number(s + 5, d->wave);
        text_mid(d, m, 0.0, 70.0, RED, 0xa0, SW_VGSCAL, LETTER, d->wave == 1 && d->t < 2.5 ? "DOGFIGHT" : s);
        if (d->wave % 5 == 0 && d->wave) text_mid(d, m, 0.0, 60.0, RED, 0x80, SW_VGSCAL, LETTER, "DARTH VADER");
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

    /* The radar: all about the ship in a disc, the way ahead in its middle,
     * what is behind at its rim, above up, to the right right. The enemies
     * red (Vader yellow), near ones bright; the wingmen turquoise. */
    if (!d->preview && !d->over) {
        const double RX = 0.0, RY = 100.0, RR = 12.0, PI = 3.14159265358979;
        d->tag = SW_TAG(SW_TAG_MESS, 0x3fd);
        for (int ring = 0; ring < 2; ring++) {
            double r = ring ? RR * 0.5 : RR;        /* (the inner ring: square to the way ahead) */
            move_to(d, RX + r, RY);
            color(d, GRN, ring ? 0x30 : 0x60);
            double px = RX + r, py = RY;
            for (int k = 1; k <= 20; k++) {
                double a = k * PI / 10.0, x = RX + r * cos(a), y = RY + r * sin(a) * DS_K;
                vctr(d, vg(x - px), vg(y - py), 7);
                px = x;
                py = y;
            }
        }
        move_to(d, RX - 1.5, RY);
        color(d, GRN, 0x50);
        vctr(d, vg(3.0), 0, 7);
        for (int pass = 0; pass < 2; pass++) {
            int n = pass ? SW_DF_ALLIES : SW_DF_ENEMIES;
            for (int i = 0; i < n; i++) {
                const double *p;
                unsigned c, lum;
                if (pass) {
                    if (!d->ally[i].live) continue;
                    p = d->ally[i].b.pos;
                    c = TRQ;
                    lum = 0x90;
                } else {
                    if (!d->enemy[i].kind) continue;
                    p = d->enemy[i].b.pos;
                    c = d->enemy[i].kind == K_VADER ? YLW : RED;
                    lum = dist(p, d->pos) < 4000.0 ? 0xff : 0xa0;
                }
                double v[3];
                view_of(d, p, v);
                double nv = sqrt(dot(v, v)), side = hypot(v[1], v[2]);
                if (nv < 1.0) continue;
                double off = acos(v[0] / nv > 1.0 ? 1.0 : v[0] / nv < -1.0 ? -1.0 : v[0] / nv), rr = RR * off / PI;
                double ux = side > 1e-6 ? v[1] / side : 0.0, uy = side > 1e-6 ? v[2] / side : 0.0;
                move_to(d, RX + ux * rr - 0.7, RY + uy * rr * DS_K);
                color(d, c, lum);
                vctr(d, vg(1.4), 0, 7);
            }
        }
    }

    emit(d, 0x2000);                        /* HALT */
    sw_avg_run_tagged(d->space, SW_VGVARB, d->tags, NULL, &d->list, 60000);

    /* The Death Star hides the stars behind it: none in its disc (the
     * specks near the ship are before it). Its disc from its own lines. */
    {
        /* (its disc: from the points of its outline, the stamp VJBCIR: their
         * middle, and how far they are from it; the stamps are not centred
         * on the point they are put at) */
        /* (a circle through the points of its outline, the stamp VJBCIR, in
         * square units: the least squares of x^2 + y^2 + D x + E y + F) */
        double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, sn = 0, sxz = 0, syz = 0, sz = 0;
        for (int i = 0; d->ds_shown && i < d->list.count; i++) {
            const sw_line *l = &d->list.lines[i];
            unsigned tag = (unsigned)(l->id >> 48);
            if (SW_TAG_SECTION(tag) != SW_TAG_DSTAR || (tag & 0x3ff) != 5) continue;
            double xs[2] = { l->x0, l->x1 }, ys[2] = { l->y0 / DS_K, l->y1 / DS_K };
            for (int k = 0; k < 2; k++) {
                double x = xs[k], y = ys[k], z = x * x + y * y;
                sxx += x * x; sxy += x * y; syy += y * y; sx += x; sy += y; sn += 1.0;
                sxz += x * z; syz += y * z; sz += z;
            }
        }
        float cx = 0.0f, cy = 0.0f, r = 0.0f;
        {
            /* the three equations, by Cramer */
            double a[3][3] = { { sxx, sxy, sx }, { sxy, syy, sy }, { sx, sy, sn } }, b[3] = { -sxz, -syz, -sz };
            double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                         a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
            if (sn >= 6.0 && fabs(det) > 1e-9) {
                double v[3];
                for (int c = 0; c < 3; c++) {
                    double m3[3][3];
                    for (int rr = 0; rr < 3; rr++)
                        for (int cc = 0; cc < 3; cc++) m3[rr][cc] = cc == c ? b[rr] : a[rr][cc];
                    v[c] = (m3[0][0] * (m3[1][1] * m3[2][2] - m3[1][2] * m3[2][1]) - m3[0][1] * (m3[1][0] * m3[2][2] - m3[1][2] * m3[2][0]) +
                            m3[0][2] * (m3[1][0] * m3[2][1] - m3[1][1] * m3[2][0])) / det;
                }
                cx = (float)(-v[0] / 2.0);
                cy = (float)(-v[1] / 2.0);
                double rr2 = (double)cx * cx + (double)cy * cy - v[2];
                r = rr2 > 0.0 ? (float)sqrt(rr2) : 0.0f;
            }
        }
        if (r > 0.0f) {
            death_star_colors(d, cx, cy, r);
            int k = 0;
            for (int i = 0; i < d->list.count; i++) {
                const sw_line *l = &d->list.lines[i];
                unsigned tag = (unsigned)(l->id >> 48);
                int star = SW_TAG_SECTION(tag) == SW_TAG_STAR && (tag & 0x3ff) < 160;
                if (star && hypotf(l->x0 - cx, l->y0 / (float)DS_K - cy) < r * 0.97f) continue;
                d->list.lines[k++] = *l;
            }
            d->list.count = k;
        }
    }

    /* What reaches past the edge of the screen (the Death Star, half out of
     * the view) is cut there: the game's own lists never do that, and the
     * board would draw it beyond its edge. */
    {
        int k = 0;
        for (int i = 0; i < d->list.count; i++) {
            sw_line l = d->list.lines[i];
            double t0 = 0.0, t1 = 1.0, dx = l.x1 - l.x0, dy = l.y1 - l.y0;
            double p[4] = { -dx, dx, -dy, dy }, q[4] = { l.x0 + 127.5, 127.5 - l.x0, l.y0 + 153.0, 150.0 - l.y0 };
            int out = 0;
            for (int j = 0; j < 4 && !out; j++) {
                if (p[j] == 0.0) { if (q[j] < 0.0) out = 1; continue; }
                double r = q[j] / p[j];
                if (p[j] < 0.0) { if (r > t1) out = 1; else if (r > t0) t0 = r; }
                else { if (r < t0) out = 1; else if (r < t1) t1 = r; }
            }
            if (out) continue;
            float x0 = l.x0, y0 = l.y0;
            l.x0 = (float)(x0 + dx * t0); l.y0 = (float)(y0 + dy * t0);
            l.x1 = (float)(x0 + dx * t1); l.y1 = (float)(y0 + dy * t1);
            d->list.lines[k++] = l;
        }
        d->list.count = k;
    }

    /* the ships, in true perspective (after the list: their lines are added) */
    chase(d);
    for (int i = 0; i < SW_DF_ALLIES; i++)
        if (d->ally[i].live) draw_xwing(d, &d->ally[i].b, 150.0f, 1.0, 0.0);
    for (int s = 0; s < SW_DF_BOLTS; s++) {
        const sw_df_bolt *bo = &d->bolt[s];
        if (!bo->live) continue;
        double tail[3], dir[3] = { bo->vel[0], bo->vel[1], bo->vel[2] };
        norm(dir);
        for (int k = 0; k < 3; k++) tail[k] = bo->pos[k] - dir[k] * BOLT_LEN;
        line3(d, tail, bo->pos, bo->red ? RED : GRN, 230.0f, 0, 0, 0);
    }
    for (int i = 0; i < SW_DF_ENEMIES; i++) {
        sw_df_enemy *e = &d->enemy[i];
        if (!e->kind) continue;
        int flash = d->t - e->hit_t < 0.12 && e->hit_t > 0.0;
        float lum = flash ? 255.0f : e->kind == K_VADER ? 210.0f : 180.0f;
        if (e->kind == K_INTERCEPTOR) draw_interceptor(d, &e->b, lum);
        else if (e->kind == K_VADER) draw_model(d, M_VADER, &e->b, SHIP_SIZE, flash ? WHT : GRN, lum, SW_TDSRTH);
        else draw_model(d, M_TIE, &e->b, SHIP_SIZE, GRN, lum, SW_TDSTIE);
    }
    for (int i = 0; i < SW_DF_PIECES; i++) {
        sw_df_piece *p = &d->piece[i];
        if (!p->live) continue;
        static const int rom[4] = { 0, SW_TDSTI1, SW_TDSTI2, SW_TDSTI3 };
        draw_model(d, p->model, &p->b, SHIP_SIZE, GRN, (float)(200.0 * (1.0 - (d->t - p->t) / 2.5)), rom[p->model]);
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
