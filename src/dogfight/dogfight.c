/* dogfight.c - see dogfight.h. */
#include "dogfight/dogfight.h"

#include "core/sw_symbols.h"
#include "core/tags.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VG_SCALE    (255.0 / 256.0 / 4.0)   /* VG units to the lines' units at the normal scale (VGSCAL) */
#define VG_OFFY     (-104.0)                /* VGOFFY: the middle of the view below the middle of the screen */
#define PERSPECTIVE 512.0

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

/* the Death Star, far off beyond the box */
static const double DEATH_STAR[3] = { 70000.0, 18000.0, 9000.0 };

/* the fourth choice, in the game's own units of the page (TDTH of PHESDS) */
#define CHOICE_X 400
#define CHOICE_Y (-300)

enum { BLU = 1, GRN = 2, TRQ = 3, RED = 4, YLW = 6, WHT = 7 };

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352du;
    v ^= v >> 15; v *= 0x846ca68bu;
    return v ^ (v >> 16);
}

/* --- vectors ------------------------------------------------------------------------------ */

static double dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void norm(double *a)
{
    double n = sqrt(dot(a, a));
    if (n > 1e-12) { a[0] /= n; a[1] /= n; a[2] /= n; }
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

/* the axes square to each other again */
static void square(sw_dogfight *d)
{
    norm(d->f);
    double k = dot(d->r, d->f);
    for (int i = 0; i < 3; i++) d->r[i] -= k * d->f[i];
    norm(d->r);
    d->u[0] = d->f[1] * d->r[2] - d->f[2] * d->r[1];        /* u = f x r: up, with r to the right */
    d->u[1] = d->f[2] * d->r[0] - d->f[0] * d->r[2];
    d->u[2] = d->f[0] * d->r[1] - d->f[1] * d->r[0];
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
static void text_mid(sw_dogfight *d, const sw_machine *m, double x, double y, unsigned c, unsigned lum, uint16_t scale,
                     double width, const char *s)
{
    text(d, m, x - 0.5 * width * (double)strlen(s), y, c, lum, scale, s);
}

/* Where a point of space is on the screen, as the ship sees it; 0 if
 * behind it. */
static int project(const sw_dogfight *d, const double *p, double *sx, double *sy, double *depth)
{
    double v[3] = { p[0] - d->pos[0], p[1] - d->pos[1], p[2] - d->pos[2] };
    double x = dot(v, d->f), y = dot(v, d->r), z = dot(v, d->u);
    if (x < 1.0) return 0;
    *sx = PERSPECTIVE * y / x * VG_SCALE;
    *sy = (PERSPECTIVE * z / x + VG_OFFY) * VG_SCALE;
    if (depth) *depth = x;
    return 1;
}

static int on_screen(double x, double y, double margin)
{
    return x > -127.5 - margin && x < 127.5 + margin && y > -153.0 - margin && y < 150.0 + margin;
}

/* --- the dogfight ---------------------------------------------------------------------------- */

void sw_dogfight_start(sw_dogfight *d, const sw_machine *m)
{
    memset(d->space, 0, 0x3000);
    if (m->rom) memcpy(d->space + 0x3000, m->rom->vrom, sizeof m->rom->vrom);
    d->active = 1;
    d->t = 0.0;
    d->pos[0] = -BOX * 0.6; d->pos[1] = 0.0; d->pos[2] = 0.0;
    d->f[0] = 1.0; d->f[1] = 0.0; d->f[2] = 0.0;
    d->r[0] = 0.0; d->r[1] = 1.0; d->r[2] = 0.0;
    square(d);
    d->speed = SPEED_MID;
    d->throttle = 0.0f;
    d->yaw_rate = d->pitch_rate = d->roll_rate = 0.0f;
    d->turning_back = 0;
    for (int i = 0; i < 48; i++) {
        uint32_t h = hash((uint32_t)i * 7919u + 3u);
        d->dust[i][0] = (float)((double)(h & 0x3ff) / 1023.0 * 3000.0 - 1500.0);
        d->dust[i][1] = (float)((double)((h >> 10) & 0x3ff) / 1023.0 * 3000.0 - 1500.0);
        d->dust[i][2] = (float)((double)((h >> 20) & 0x3ff) / 1023.0 * 3000.0 - 1500.0);
    }
}

static float ease(float v, float to, double k)
{
    return v + (to - v) * (float)(k > 1.0 ? 1.0 : k);
}

void sw_dogfight_step(sw_dogfight *d, const sw_dogfight_input *in, double dt)
{
    if (!d->active || dt <= 0.0) return;
    if (dt > 0.1) dt = 0.1;
    d->t += dt;

    /* the throttle: a button held pulls it its way, twice as fast while it
     * is still on the other side of the middle; let go, it goes back */
    int roll = in->faster && in->slower;
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
        double c = dot(d->f, home);
        double side[3] = { home[0] - c * d->f[0], home[1] - c * d->f[1], home[2] - c * d->f[2] };
        double n = sqrt(dot(side, side));
        if (n > 1e-6) {
            for (int i = 0; i < 3; i++) side[i] /= n;
            double a = acos(c > 1.0 ? 1.0 : c < -1.0 ? -1.0 : c), step = BACK_RATE * dt;
            if (step > a) step = a;
            /* turn f towards `side`, and r and u with it (about the axis f x side) */
            double axis[3] = { d->f[1] * side[2] - d->f[2] * side[1], d->f[2] * side[0] - d->f[0] * side[2],
                               d->f[0] * side[1] - d->f[1] * side[0] };
            double *vs[2] = { d->r, d->u };
            for (int k = 0; k < 2; k++) {
                double *v = vs[k], ca = cos(step), sa = sin(step), ad = dot(axis, v);
                double cr[3] = { axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0] };
                for (int i = 0; i < 3; i++) v[i] = v[i] * ca + cr[i] * sa + axis[i] * ad * (1.0 - ca);
            }
            turn(d->f, side, step);
        }
    } else {
        /* the yoke turns and pitches; with both thumb buttons it rolls */
        float yaw = roll ? 0.0f : in->yaw * (float)YAW_RATE;
        float rl = roll ? in->yaw * (float)ROLL_RATE : 0.0f;
        d->yaw_rate = ease(d->yaw_rate, yaw, dt * 5.0);
        d->pitch_rate = ease(d->pitch_rate, in->pitch * (float)PITCH_RATE, dt * 5.0);
        d->roll_rate = ease(d->roll_rate, rl, dt * 5.0);
        turn(d->f, d->r, d->yaw_rate * dt);         /* to the right: f towards r */
        turn(d->f, d->u, d->pitch_rate * dt);       /* up: f towards u */
        turn(d->r, d->u, -d->roll_rate * dt);       /* rolling right: r goes down */
    }
    square(d);
    for (int i = 0; i < 3; i++) d->pos[i] += d->f[i] * d->speed * dt;
}

void sw_dogfight_attitude(const sw_dogfight *d, float *bank, float *pitch)
{
    float b = d->roll_rate / (float)ROLL_RATE + 0.6f * d->yaw_rate / (float)YAW_RATE;
    float p = d->pitch_rate / (float)PITCH_RATE;
    *bank = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;
    *pitch = p > 1.0f ? 1.0f : p < -1.0f ? -1.0f : p;
}

int sw_dogfight_lines(sw_dogfight *d, const sw_machine *m, const sw_line **out)
{
    d->at = 0;
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

    /* the sight, in the middle of the view: one aims with the nose */
    d->tag = SW_TAG(SW_TAG_SITE, 0);
    move_to(d, 0.0, VG_OFFY * VG_SCALE);
    color(d, TRQ, 0x80);
    emit(d, rom16(m, SW_VGSITE));

    /* the X-wing's nose and guns (VWPLAN, the sight in the middle) */
    static const uint16_t parts[5] = { SW_VJSTBM, SW_VJSTTR, SW_VJSTBL, SW_VJSTTL, SW_VJSTBR };
    for (int i = 0; i < 5; i++) {
        d->tag = SW_TAG(SW_TAG_PLANE, i);
        emit(d, SW_VGCNTR);
        emit(d, parts[i]);
    }

    /* what is written on the screen */
    d->tag = SW_TAG(SW_TAG_MESS, 0x3ff);
    {
        /* the throttle, top right: a bar from slowest to fastest */
        double x0 = 70.0, y = 138.0, w = 46.0;
        move_to(d, x0, y);
        color(d, GRN, 0x60);
        vctr(d, vg(w), 0, 5);
        double at = x0 + w * (d->throttle + 1.0) * 0.5;
        move_to(d, at, y - 4.0);
        color(d, YLW, 0xa0);
        vctr(d, 0, vg(8.0), 7);
        text(d, m, x0, y - 14.0, GRN, 0x80, SW_VGSCAL + 0x40, "SPEED");
    }
    if (d->t < 4.0) text_mid(d, m, 0.0, 70.0, RED, 0xa0, SW_VGSCAL, 5.8, "DOGFIGHT");
    if (d->turning_back && fmod(d->t - d->back_since, 0.8) < 0.55)
        text_mid(d, m, 0.0, 60.0, RED, 0xc0, SW_VGSCAL, 5.8, "WHERE ARE YOU GOING");

    emit(d, 0x2000);                        /* HALT */
    sw_avg_run_tagged(d->space, 0, d->tags, NULL, &d->list, 20000);
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
    text_mid(d, m, x, y + 30.0, RED, 0x80, SW_VGSCAL, 5.8, "DOGFIGHT");
    text_mid(d, m, x, y + 21.0, GRN, 0x80, SW_VGSCAL, 5.8, "ENDLESS");
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
