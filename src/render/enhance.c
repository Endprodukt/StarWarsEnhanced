/* enhance.c - see enhance.h. */
#include "render/enhance.h"

#include "game/game.h"
#include "game/m6809.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The game's perspective (measured against the math box, which agrees to
 * the last digit): a point P of the world is seen at
 *     V = R (P - T) / 2          R: the player's unit vectors, T: his place
 * (math box PRE2), a point of an object's shape adds R times itself, and
 * on the screen that is 512 V.y / V.x across and 512 V.z / V.x up, in the
 * units of the vector words; 1/X as the divider gives it is 800000h / V.x.
 * The list is drawn at VGSCAL and VGOFFY above the middle. */
#define PERSPECTIVE 512.0
#define W_UNIT      8388608.0
#define VG_SCALE    (255.0f / 256.0f / 4.0f)
#define VG_OFFY     (-104.0f)           /* VGOFFY */
#define NEAR        48.0                /* nothing nearer is drawn */
/* the edges of the screen (VGEDGL/R/B/T at the scale the game draws at) */
#define SCREEN_X0   -127.5f
#define SCREEN_X1   127.5f
#define SCREEN_Y0   -153.0f
#define SCREEN_Y1   150.0f

/* the maze of the surface (WSGRND.MAC; game/ground.c) */
#define TGDPTR      0xA182
#define ZGDPTR      0xA1CE
#define TGD_X       0
#define TGD_Y       2
#define TGD_PC      4
#define TGD_SQ      5
#define TGD_RM      6
#define TGD_IZ      7
#define PC_BNK      3
#define TYP_VW      0x02
#define TYP_DM      0x04
#define TYP_BK      0x10
#define GD_CENTER   0x1e00              /* the height a building's shape is hung at */

enum { BLU = 1, GRN = 2, TRQ = 3, RED = 4, YLW = 6, WHT = 7 };

typedef struct vec { double x, y, z; } vec;

static int16_t mb16(const sw_machine *m, unsigned a)
{
    return (int16_t)((m->mbox.ram[(a - 0x5000) & 0xfff] << 8) | m->mbox.ram[(a - 0x5000 + 1) & 0xfff]);
}

static uint8_t ram8(const sw_machine *m, unsigned a) { return m->ram[(a - 0x4800) & 0x7ff]; }

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352du;
    v ^= v >> 15; v *= 0x846ca68bu;
    return v ^ (v >> 16);
}

/* R times a vector. */
static vec turn(const sw_enhance *e, vec p)
{
    vec v;
    v.x = e->r[0][0] * p.x + e->r[0][1] * p.y + e->r[0][2] * p.z;
    v.y = e->r[1][0] * p.x + e->r[1][1] * p.y + e->r[1][2] * p.z;
    v.z = e->r[2][0] * p.x + e->r[2][1] * p.y + e->r[2][2] * p.z;
    return v;
}

static vec add(vec a, vec b) { vec v = { a.x + b.x, a.y + b.y, a.z + b.z }; return v; }
static vec vec3(double x, double y, double z) { vec v = { x, y, z }; return v; }

static uint16_t w_of(double x)
{
    double w = W_UNIT / x;
    return (uint16_t)(w > 65535.0 ? 65535.0 : w < 1.0 ? 1.0 : w);
}

/* A line between two points as the player sees them (V), cut at the
 * nearest he sees and at the edges of the screen. */
static void line(sw_enhance *e, vec a, vec b, unsigned color, float lum, unsigned section)
{
    if (a.x < NEAR && b.x < NEAR) return;
    if (a.x < NEAR) { double k = (NEAR - a.x) / (b.x - a.x); a.y += (b.y - a.y) * k; a.z += (b.z - a.z) * k; a.x = NEAR; }
    if (b.x < NEAR) { double k = (NEAR - b.x) / (a.x - b.x); b.y += (a.y - b.y) * k; b.z += (a.z - b.z) * k; b.x = NEAR; }
    float x0 = (float)(PERSPECTIVE * a.y / a.x) * VG_SCALE, y0 = ((float)(PERSPECTIVE * a.z / a.x) + VG_OFFY) * VG_SCALE;
    float x1 = (float)(PERSPECTIVE * b.y / b.x) * VG_SCALE, y1 = ((float)(PERSPECTIVE * b.z / b.x) + VG_OFFY) * VG_SCALE;
    float w0 = (float)(W_UNIT / a.x), w1 = (float)(W_UNIT / b.x);
    /* Liang-Barsky against the screen */
    float t0 = 0.0f, t1 = 1.0f, dx = x1 - x0, dy = y1 - y0;
    float p[4] = { -dx, dx, -dy, dy };
    float q[4] = { x0 - SCREEN_X0, SCREEN_X1 - x0, y0 - SCREEN_Y0, SCREEN_Y1 - y0 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) { if (q[i] < 0.0f) return; continue; }
        float r = q[i] / p[i];
        if (p[i] < 0.0f) { if (r > t1) return; if (r > t0) t0 = r; }
        else { if (r < t0) return; if (r < t1) t1 = r; }
    }
    if (lum < 4.0f) return;
    if (e->n == e->cap) {
        int cap = e->cap ? e->cap * 2 : 2048;
        sw_line *o = (sw_line *)realloc(e->out, (size_t)cap * sizeof *o);
        if (!o) return;
        e->out = o;
        e->cap = cap;
    }
    sw_line *l = &e->out[e->n++];
    memset(l, 0, sizeof *l);
    l->x0 = x0 + dx * t0; l->y0 = y0 + dy * t0;
    l->x1 = x0 + dx * t1; l->y1 = y0 + dy * t1;
    l->color = (uint8_t)color;
    l->zz = 7;
    l->intensity = (uint8_t)(lum > 255.0f ? 255.0f : lum);
    l->id = ((uint64_t)SW_TAG(section, 0x3ff) << 48) | (0x80000000u + e->line_id++);
    float wa = w0 + (w1 - w0) * t0, wb = w0 + (w1 - w0) * t1;
    l->w0 = (uint16_t)(wa > 65535.0f ? 65535.0f : wa < 1.0f ? 1.0f : wa);
    l->w1 = (uint16_t)(wb > 65535.0f ? 65535.0f : wb < 1.0f ? 1.0f : wb);
    l->inst = e->inst;
}

/* A face of four corners (V), for what is behind it not to show. */
static void face(sw_enhance *e, const vec *corner)
{
    /* what of it is nearer than the nearest seen is cut off */
    vec c[6];
    int nc = 0;
    for (int i = 0; i < 4; i++) {
        const vec *a = &corner[i], *b = &corner[(i + 1) & 3];
        if (a->x >= NEAR) c[nc++] = *a;
        if ((a->x >= NEAR) != (b->x >= NEAR)) {
            double k = (NEAR - a->x) / (b->x - a->x);
            c[nc++] = vec3(NEAR, a->y + (b->y - a->y) * k, a->z + (b->z - a->z) * k);
        }
    }
    if (nc < 3) return;
    if (e->n_faces == e->cap_faces) {
        int cap = e->cap_faces ? e->cap_faces * 2 : 256;
        sw_solid_extra *f = (sw_solid_extra *)realloc(e->faces, (size_t)cap * sizeof *f);
        if (!f) return;
        e->faces = f;
        e->cap_faces = cap;
    }
    sw_solid_extra *f = &e->faces[e->n_faces++];
    f->inst = e->inst;
    f->n = (uint8_t)nc;
    for (int i = 0; i < nc; i++) {
        f->x[i] = (float)(PERSPECTIVE * c[i].y / c[i].x) * VG_SCALE;
        f->y[i] = ((float)(PERSPECTIVE * c[i].z / c[i].x) + VG_OFFY) * VG_SCALE;
        f->w[i] = (float)w_of(c[i].x);
    }
}

/* --- the view ------------------------------------------------------------------------ */

static void view_of(sw_enhance *e, const sw_machine *m)
{
    static const int off[3][3] = { { M_AX, M_BX, M_CX }, { M_AY, M_BY, M_CY }, { M_AZ, M_BZ, M_CZ } };
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) e->r[a][b] = mb16(m, (unsigned)(SW_M_U1 + off[a][b])) / 16384.0;
    e->t[0] = (uint16_t)mb16(m, SW_M_S1 + M_TX);
    e->t[1] = mb16(m, SW_M_S1 + M_TY);
    e->t[2] = mb16(m, SW_M_S1 + M_TZ);
}

/* --- stars ---------------------------------------------------------------------------- */

/* trench: 1 in the trench, whose walls stand 1024 to each side of its
 * middle and reach from 4096 below up to the height 0 (TBSBL of
 * WSBASE.MAC): a star shows if the way to it leaves the trench through its
 * open top. */
static void stars(sw_enhance *e, int trench)
{
    int n = trench ? SW_ENHANCE_STARS : 96;
    for (int i = 0; i < n; i++) {
        if (trench && e->t[2] < 0.0) {
            double up = -e->t[2] / e->star[i][2];           /* how far along it is at the top of the walls */
            if (fabs(e->t[1] + e->star[i][1] * up) >= 1024.0) continue;
        }
        vec v = turn(e, vec3(e->star[i][0], e->star[i][1], e->star[i][2]));
        if (v.x < 0.05) continue;
        v.x *= 1e6; v.y *= 1e6; v.z *= 1e6;     /* as good as infinitely far */
        int before = e->n;
        line(e, v, v, WHT, (float)e->star_lum[i], SW_TAG_STAR);
        if (e->n > before) e->out[before].w0 = e->out[before].w1 = 0;
    }
}

/* --- the ground ----------------------------------------------------------------------- */

/* A line on the ground between two places of the world, given as how far
 * ahead of the player and where across; in pieces, dimmer with distance. */
static void ground_line(sw_enhance *e, double ax, double ay, double bx, double by, float lum)
{
    int pieces = (int)(fabs(bx - ax) / 6000.0) + 1;
    for (int i = 0; i < pieces; i++) {
        double k0 = (double)i / pieces, k1 = (double)(i + 1) / pieces;
        vec p0 = vec3((ax + (bx - ax) * k0) / 2.0, (ay + (by - ay) * k0 - e->t[1]) / 2.0, -e->t[2] / 2.0);
        vec p1 = vec3((ax + (bx - ax) * k1) / 2.0, (ay + (by - ay) * k1 - e->t[1]) / 2.0, -e->t[2] / 2.0);
        double far = (p0.x + p1.x) * 0.5;
        float fade = (float)(1.0 - far / 26000.0);
        if (fade <= 0.0f) continue;
        line(e, turn(e, p0), turn(e, p1), GRN, lum * fade * fade, SW_TAG_TRENCH);
    }
}

/* The plating of the Death Star: plates of 2000h by 2000h laid like
 * bricks, some of them divided again; what divides is the same every time
 * the player comes by (it is made of the plate's place). */
static void surface(sw_enhance *e)
{
    const double PLATE = 8192.0, FAR = 52000.0;
    double tx = e->t[0], ty = e->t[1];
    double first = floor(tx / PLATE) * PLATE;
    int reach = 9;                              /* plates to each side */
    double y0 = floor(ty / PLATE) * PLATE;
    /* the long lines: along the way */
    for (int j = -reach; j <= reach + 1; j++) {
        double y = y0 + j * PLATE;
        double from = fabs(y - ty) * 0.55;      /* (what is beside the screen is not drawn) */
        if (from < 300.0) from = 300.0;
        if (from < FAR) ground_line(e, from, y, FAR, y, 150.0f);
    }
    /* the lines across, and what divides a plate */
    for (int i = 0; first + i * PLATE - tx < FAR; i++) {
        double x = first + i * PLATE - tx;      /* ahead of the player */
        if (x > 200.0) {
            double half = x * 1.15 + 2000.0;
            ground_line(e, x, ty - half, x, ty + half, 150.0f);
        }
        if (x > 26000.0) continue;
        long row = (long)floor((first + i * PLATE) / PLATE);
        for (int j = -reach; j <= reach; j++) {
            double y = y0 + j * PLATE;
            if (fabs(y + PLATE * 0.5 - ty) > (x + PLATE) * 1.2 + 3000.0) continue;
            uint32_t h = hash((uint32_t)(row & 7) * 73856093u ^ (uint32_t)((long)floor(y / PLATE) & 7) * 19349663u);
            double xa = x, xb = x + PLATE;
            if (xb < 250.0) continue;
            if (xa < 250.0) xa = 250.0;
            if ((h & 3) == 0) {                 /* a seam along, a third in */
                double ys = y + PLATE * (0.3 + 0.4 * ((h >> 4) & 1));
                ground_line(e, xa, ys, xb, ys, 90.0f);
            } else if ((h & 3) == 1) {          /* a seam across half the plate */
                double xs = x + PLATE * 0.5;
                if (xs > 250.0) ground_line(e, xs, y, xs, y + PLATE * (0.5 + 0.5 * ((h >> 5) & 1)), 90.0f);
            } else if ((h & 7) == 2) {          /* a hatch */
                double cx = x + PLATE * 0.5, cy = y + PLATE * 0.5, s = PLATE * 0.14;
                if (cx - s > 250.0) {
                    ground_line(e, cx - s, cy - s, cx + s, cy - s, 110.0f);
                    ground_line(e, cx + s, cy - s, cx + s, cy + s, 110.0f);
                    ground_line(e, cx + s, cy + s, cx - s, cy + s, 110.0f);
                    ground_line(e, cx - s, cy + s, cx - s, cy - s, 110.0f);
                }
            }
        }
    }
}

/* --- towers and bunkers ---------------------------------------------------------------- */

/* A point of a shape, turned about the upright axis as e->spin has it. */
static vec spun(const sw_enhance *e, double x, double y, double z)
{
    return vec3(x * e->spin_c - y * e->spin_s, x * e->spin_s + y * e->spin_c, z);
}

/* A block of a building: square, a corner towards the player as the ROM's
 * tower has an edge there, its corners h0 from the middle at the height z0
 * and h1 at z1 (the shape's own units, as the points of the ROM's tower:
 * seen from ahead it is exactly as wide), around the place c (V). What of it is drawn: its upright edges, the ring
 * at its foot, the ring at its top, seams across its sides. */
enum { B_FOOT = 1, B_TOP = 2, B_SEAMS = 4, B_PLATES = 8 };

static void block(sw_enhance *e, vec c, double h0, double z0, double h1, double z1, unsigned color, float lum,
                  unsigned what, float ring, float seam)
{
    static const int sx[4] = { -1, 0, 1, 0 }, sy[4] = { 0, -1, 0, 1 };
    vec lo[4], hi[4];
    for (int i = 0; i < 4; i++) {
        lo[i] = add(c, turn(e, spun(e, sx[i] * h0, sy[i] * h0, z0)));
        hi[i] = add(c, turn(e, spun(e, sx[i] * h1, sy[i] * h1, z1)));
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        line(e, lo[i], hi[i], color, lum, SW_TAG_OBJECT);
        if (what & B_FOOT) line(e, lo[i], lo[j], color, lum, SW_TAG_OBJECT);
        if (what & B_TOP) line(e, hi[i], hi[j], color, lum * ring, SW_TAG_OBJECT);
        vec side[4] = { lo[i], lo[j], hi[j], hi[i] };
        face(e, side);
        if ((what & B_SEAMS) && seam > 0.0f) {
            /* a seam across, and one upright that stops at it */
            double k = 0.45 + 0.1 * (i & 1);
            vec a = add(c, turn(e, vec3(sx[i] * (h0 + (h1 - h0) * k), sy[i] * (h0 + (h1 - h0) * k), z0 + (z1 - z0) * k)));
            vec b = add(c, turn(e, vec3(sx[j] * (h0 + (h1 - h0) * k), sy[j] * (h0 + (h1 - h0) * k), z0 + (z1 - z0) * k)));
            line(e, a, b, color, lum * 0.55f * seam, SW_TAG_OBJECT);
            if (what & B_PLATES) {
                double u = 0.35 + 0.3 * ((i >> 1) & 1);
                vec m0 = vec3(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u);
                vec m1 = (i & 1) ? vec3(lo[i].x + (lo[j].x - lo[i].x) * u, lo[i].y + (lo[j].y - lo[i].y) * u, lo[i].z + (lo[j].z - lo[i].z) * u)
                                 : vec3(hi[i].x + (hi[j].x - hi[i].x) * u, hi[i].y + (hi[j].y - hi[i].y) * u, hi[i].z + (hi[j].z - hi[i].z) * u);
                line(e, m0, m1, color, lum * 0.55f * seam, SW_TAG_OBJECT);
            }
        }
    }
    vec top[4] = { hi[0], hi[1], hi[2], hi[3] };
    face(e, top);
}

/* The turret on a tower: a box at the height z with two barrels (as
 * bright as `detail` says, 0 to 1), turned by yaw about its upright axis
 * (0: the guns towards where the player comes from). */
static void turret(sw_enhance *e, vec c, double z, double h, unsigned color, float lum, float detail, double yaw)
{
    e->spin_c = cos(yaw);
    e->spin_s = sin(yaw);
    block(e, c, h, z, h, z + 720.0, color, lum, B_FOOT | B_TOP, 1.0f, 0.0f);
    for (int s = -1; s <= 1; s += 2) {
        vec a = add(c, turn(e, spun(e, -h * 0.6, s * h * 0.4, z + 420.0)));
        vec b = add(c, turn(e, spun(e, -h * 0.6 - 420.0, s * h * 0.4, z + 480.0)));
        line(e, a, b, color, lum * detail, SW_TAG_OBJECT);
    }
    e->spin_c = 1.0;
    e->spin_s = 0.0;
}

/* From 0 where v is `from` to 1 where it is `to`. */
static float ramp(double v, double from, double to)
{
    double k = (v - from) / (to - from);
    return (float)(k < 0.0 ? 0.0 : k > 1.0 ? 1.0 : k);
}

static void towers(sw_enhance *e, const sw_machine *m, double now)
{
    const double FAR = 28000.0;                 /* in V: the arcade machine shows them from 3C00h */
    const double BRIGHT = 2500.0;               /* from here on a building is as bright as it gets */
    const double LOCK = 9000.0;                 /* from here on a turret has found the player */
    const double RANGE = 15360.0;               /* 3C00h: nearer than this the game has a building, or not yet */
    unsigned mz = TGDPTR + (unsigned)((uint8_t)(ram8(m, SW_GD_WAV) << 2));
    if (mz >= ZGDPTR) mz = ZGDPTR - 4;
    unsigned first = sw_rom16(m, (uint16_t)mz), end = sw_rom16(m, (uint16_t)(mz + 2));
    int seq = (int8_t)ram8(m, SW_GD_SEQ);
    if (end <= first || end - first > 64 * TGD_IZ) return;
    for (unsigned x = first; x < end; x += TGD_IZ) {
        unsigned bx = sw_rom16(m, (uint16_t)(x + TGD_X));
        int by = (int16_t)sw_rom16(m, (uint16_t)(x + TGD_Y));
        unsigned pc = sw_rom16(m, (uint16_t)(x + TGD_PC)) >> 8;
        int sq = (int8_t)(sw_rom16(m, (uint16_t)(x + TGD_SQ)) >> 8);
        int rm = (int8_t)(sw_rom16(m, (uint16_t)(x + TGD_RM)) >> 8);
        uint8_t state = ram8(m, (unsigned)(SW_GDRAM + rm));
        if (pc == PC_BNK && (state & TYP_DM)) continue;     /* (a bunker shot away) */
        /* the map comes round every 8000h: the building as it is ahead
         * now, and as it is the next time round */
        double ahead = (double)((bx - (unsigned)e->t[0]) & 0x7fff);
        for (int lap = 0; lap < 2; lap++, ahead += 32768.0) {
            /* (the next pass starts when the player's place wraps) */
            double beyond = (double)(int16_t)(uint16_t)e->t[0] + ahead - 32768.0;  /* past where the next pass starts */
            int pass = seq + (beyond >= 0.0 ? 1 : 0);
            if (pass < sq) continue;
            /* A building the next pass brings is drawn ahead of time only
             * if it will be out of the game's range when that pass starts:
             * one within it the game does not show until the round after,
             * and it would go out before the player's eyes. */
            if (seq < sq && beyond / 2.0 < RANGE) continue;
            vec c = turn(e, vec3(ahead / 2.0, (double)(int16_t)(by - (int)e->t[1]) / 2.0, (GD_CENTER - e->t[2]) / 2.0));
            if (c.x < 40.0 || c.x > FAR || fabs(c.y) > c.x * 1.25 + 1500.0) continue;
            /* Within its range the game shows a building only once it has
             * set it up, which it does while the building is out of sight:
             * so none comes up before the player's eyes. Beyond that range
             * every building of the pass is drawn, set up or not, or it
             * would come up at once, half way, when the game gets to it. */
            if (c.x < RANGE && !(state & TYP_VW)) continue;
            /* The farthest are dark, and each gets brighter in a straight
             * line as it comes nearer: what is behind does not crowd what
             * is before it. */
            float lum = 10.0f + 235.0f * ramp(c.x, FAR, BRIGHT);
            /* A building has all there is to it at every distance: nothing
             * of it is added as it comes nearer. (Only where it is so dark
             * that its seams could not be seen anyway are they left out.) */
            float steps = 1.0f, seams = lum >= 30.0f ? 1.0f : 0.0f;
            if (++e->inst == 0) e->inst = 0x8000;
            /* The sizes are the arcade machine's, to the unit (the player
             * flies between and over them): a bunker 960 at its foot, 720
             * at its top, 720 high; a tower 960, 720, 600 and 480 at the
             * heights of its points, its hat 480 and 720 high. */
            if (pc == PC_BNK) {
                block(e, c, 960.0, -3840.0, 720.0, -3120.0, RED, lum, B_FOOT | B_TOP | B_SEAMS | B_PLATES, 1.0f, seams);
                block(e, c, 430.0, -3120.0, 430.0, -3120.0, RED, lum * 0.7f, B_TOP, steps, 0.0f);     /* a hatch on its roof */
                continue;
            }
            /* (one the game has not set up yet will have its hat) */
            int hat = !(state & TYP_DM) && ((state & TYP_BK) || !(state & TYP_VW));
            /* three steps, each set in a little from the one below */
            block(e, c, 960.0, -3840.0, 720.0, -3120.0, YLW, lum, B_FOOT | B_TOP | B_SEAMS, steps, seams);
            block(e, c, 670.0, -3120.0, 600.0, -1440.0, YLW, lum, B_TOP | B_SEAMS | B_PLATES, steps, seams);
            block(e, c, 550.0, -1440.0, 480.0, 6000.0, YLW, lum, B_TOP | B_SEAMS | B_PLATES, 1.0f, seams);
            if (hat) {
                /* The turret turns to and fro, looking for the player; as
                 * he comes into its range it finds him and follows him. */
                double side = (double)(int16_t)(by - (int)e->t[1]);
                double seek = 1.1 * sin(now * 0.9 + (double)(x & 31));
                double aim = atan2(side, ahead);
                double found = ramp(c.x, LOCK + 3000.0, LOCK);
                float tl = lum * 1.15f;
                turret(e, c, 6000.0, 480.0, WHT, tl > 255.0f ? 255.0f : tl, steps, seek + (aim - seek) * found);
            }
        }
    }
}

/* --- far off, a chase ------------------------------------------------------------------- */

/* An X-wing, as far as lines go: nose to +x. (The ROM has none: the
 * player's own ship is never seen from outside. The TIE fighters are the
 * ROM's.) */
static const float XWING[][2][3] = {
    { { 330, 0, 0 }, { -210, 0, 45 } }, { { 330, 0, 0 }, { -210, 0, -45 } }, { { -210, 0, 45 }, { -210, 0, -45 } },
    { { -110, 30, 20 }, { -170, 250, 110 } }, { { -110, -30, 20 }, { -170, -250, 110 } },
    { { -110, 30, -20 }, { -170, 250, -110 } }, { { -110, -30, -20 }, { -170, -250, -110 } },
    { { -170, 250, 110 }, { 90, 250, 110 } }, { { -170, -250, 110 }, { 90, -250, 110 } },
    { { -170, 250, -110 }, { 90, 250, -110 } }, { { -170, -250, -110 }, { 90, -250, -110 } },
};

/* Where a ship of the chase is at the time t of its run (0 to 1), from
 * the player, in V but not turned with him. */
static vec chase_place(double t, double dir, double far, uint32_t h)
{
    double ph = (double)(h & 255) / 255.0 * 6.28;
    vec p;
    p.x = far * (1.0 + 0.22 * sin(t * 4.1 + ph));
    p.y = dir * (t * 2.9 - 1.45) * far;
    p.z = far * (0.3 + 0.1 * sin(t * 7.3 + ph * 2.0) + 0.05 * sin(t * 15.0 + ph));
    return p;
}

static void ship(sw_enhance *e, vec at, vec ahead, int tie, float lum)
{
    /* its own unit vectors: ahead, to its left, up */
    double n = sqrt(ahead.x * ahead.x + ahead.y * ahead.y + ahead.z * ahead.z);
    if (n < 1e-6) return;
    vec f = vec3(ahead.x / n, ahead.y / n, ahead.z / n);
    vec l = vec3(-f.y, f.x, 0.0);
    n = sqrt(l.x * l.x + l.y * l.y);
    if (n < 1e-6) return;
    l.x /= n; l.y /= n;
    /* banked into its turn a little */
    vec u = vec3(f.y * l.z - f.z * l.y, f.z * l.x - f.x * l.z, f.x * l.y - f.y * l.x);
    const double SIZE = 1.6;                    /* (far off, small, and still to be made out) */
    int lines = tie ? e->tie_lines : (int)(sizeof XWING / sizeof XWING[0]);
    for (int i = 0; i < lines; i++) {
        vec p[2];
        for (int k = 0; k < 2; k++) {
            const float *q = tie ? e->tie[e->tie_line[i][k] & 63] : XWING[i][k];
            /* (the TIE fighter's shape looks along its x too, its wings to both sides) */
            double qx = q[0] * SIZE, qy = q[1] * SIZE, qz = q[2] * SIZE;
            vec w = vec3(at.x + f.x * qx + l.x * qy + u.x * qz, at.y + f.y * qx + l.y * qy + u.y * qz,
                         at.z + f.z * qx + l.z * qy + u.z * qz);
            p[k] = turn(e, w);
        }
        /* (the TIE fighter's many lines lie close together this small: dimmer) */
        line(e, p[0], p[1], tie ? GRN : WHT, tie ? lum * 0.55f : lum, SW_TAG_OBJECT);
    }
}

static void dogfights(sw_enhance *e, double now)
{
    const double EVERY = 13.0, RUN = 8.0;
    long pass = (long)floor(now / EVERY);
    double t = (now - (double)pass * EVERY) / RUN;
    if (t >= 1.0) return;
    uint32_t h = hash((uint32_t)pass + 77u);
    double dir = (h & 0x100) ? 1.0 : -1.0;
    double far = 21000.0 + (double)((h >> 9) & 3) * 2500.0;
    int chasers = 1 + (int)((h >> 12) & 1);     /* two ships, or three */
    float lum = 150.0f;
    if (t < 0.08) lum *= (float)(t / 0.08);
    if (t > 0.92) lum *= (float)((1.0 - t) / 0.08);
    if (++e->inst == 0) e->inst = 0x8000;
    vec lead = chase_place(t, dir, far, h), lead1 = chase_place(t + 0.004, dir, far, h);
    ship(e, lead, vec3(lead1.x - lead.x, lead1.y - lead.y, lead1.z - lead.z), 0, lum);
    for (int k = 1; k <= chasers; k++) {
        double tk = t - 0.055 * k;
        vec p = chase_place(tk, dir, far, h), p1 = chase_place(tk + 0.004, dir, far, h);
        p.z += (k == 2 ? 900.0 : -400.0);
        p.x += (k == 2 ? 1200.0 : 0.0);
        ship(e, p, vec3(p1.x - p.x, p1.y - p.y, p1.z - p.z), 1, lum);
        /* its cannons: a bolt on its way to the X-wing, now and then */
        double shot = fmod(now * 1.7 + k * 0.37, 1.0);
        if (shot < 0.3) {
            double a = shot / 0.3, b = a + 0.18;
            if (b > 1.0) b = 1.0;
            vec s0 = vec3(p.x + (lead.x - p.x) * a, p.y + (lead.y - p.y) * a, p.z + (lead.z - p.z) * a);
            vec s1 = vec3(p.x + (lead.x - p.x) * b, p.y + (lead.y - p.y) * b, p.z + (lead.z - p.z) * b);
            line(e, turn(e, s0), turn(e, s1), TRQ, lum + 60.0f, SW_TAG_OBJECT);
        }
    }
}

/* --- all of it ----------------------------------------------------------------------------- */

void sw_enhance_init(sw_enhance *e, const sw_machine *m)
{
    /* the stars: the same sky every time, none below the horizon */
    int n = (int)(sizeof e->star / sizeof e->star[0]);
    for (int i = 0; i < n; i++) {
        uint32_t h = hash((uint32_t)i * 2654435761u + 12345u);
        double az = (double)(h & 0xffff) / 65536.0 * 6.2831853;
        double el = 0.02 + (double)((h >> 16) & 0xff) / 255.0 * 1.2;
        e->star[i][0] = (float)(cos(el) * cos(az));
        e->star[i][1] = (float)(cos(el) * sin(az));
        e->star[i][2] = (float)sin(el);
        e->star_lum[i] = (uint8_t)(70 + ((h >> 24) & 0x7f));
    }
    if (m->rom) {
        static int16_t pts[64][3];
        static uint8_t edges[256][2];
        int ne;
        int np = sw_obj_model(m, SW_TDSTIE, pts, 64, edges, 256, &ne);
        for (int i = 0; i < np && i < 64; i++) {
            /* (the ROM's TIE fighter has its wings at y = -208 and 208 and looks along x) */
            e->tie[i][0] = pts[i][0]; e->tie[i][1] = pts[i][1]; e->tie[i][2] = pts[i][2];
        }
        /* the TIE fighter as the ROM has it, every line */
        e->tie_lines = ne < 160 ? ne : 160;
        for (int i = 0; i < e->tie_lines; i++) {
            e->tie_line[i][0] = edges[i][0];
            e->tie_line[i][1] = edges[i][1];
        }
        e->tie_ready = np >= 45 && ne > 0;
    }
}

void sw_enhance_free(sw_enhance *e)
{
    free(e->out);
    free(e->faces);
    memset(e, 0, sizeof *e);
}

int sw_enhance_run(sw_enhance *e, const sw_machine *m, double now, const sw_line *in, int n, const sw_line **out)
{
    e->n = e->n_faces = 0;
    e->inst = 0x8000;
    e->line_id = 0;
    e->spin_c = 1.0;
    e->spin_s = 0.0;
    uint8_t phase = ram8(m, SW_PHASE);
    int ground = (phase == SW_PHSGD + 1 || phase == SW_PHSG0D + 1) &&
                 (e->surface || e->stars || e->towers || e->dogfights);
    int trench = (phase == SW_PHSBS + 1 || phase == SW_PHSB0D + 1) && e->stars;
    int logo = e->logo && phase >= SW_PHSBNR && phase <= SW_PHSHIS + 1;     /* (the attract mode) */
    if (!ground && !logo && !trench) {
        *out = in;
        return n;
    }
    if (ground || trench) view_of(e, m);

    /* the game's picture, without its buildings if they are drawn here */
    if (e->cap < n + 64) {
        sw_line *o = (sw_line *)realloc(e->out, (size_t)(n + 2048) * sizeof *o);
        if (!o) { *out = in; return n; }
        e->out = o;
        e->cap = n + 2048;
    }
    for (int i = 0; i < n; i++) {
        unsigned tag = (unsigned)(in[i].id >> 48);
        if (ground && e->towers && SW_TAG_SECTION(tag) == SW_TAG_OBJECT && (tag & 0x3ff) >= 16) continue;
        e->out[e->n] = in[i];
        if (logo && SW_TAG_SECTION(tag) == SW_TAG_BANNER) e->out[e->n].color = YLW;
        e->n++;
    }
    if (ground && e->stars) stars(e, 0);
    if (trench) stars(e, 1);
    if (ground && e->surface) surface(e);
    if (ground && e->towers) towers(e, m, now);
    if (ground && e->dogfights && e->tie_ready) dogfights(e, now);
    *out = e->out;
    return e->n;
}
