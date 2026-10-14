/* enhance.c - see enhance.h. */
#include "render/enhance.h"

#include "core/sched.h"
#include "game/game.h"
#include "game/m6809.h"
#include "render/view.h"

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
static uint16_t ram16(const sw_machine *m, unsigned a) { return (uint16_t)((ram8(m, a) << 8) | ram8(m, a + 1)); }

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
    if (e->shrink != 1.0f) {
        x0 = e->foot_x + (x0 - e->foot_x) * e->shrink; y0 = e->foot_y + (y0 - e->foot_y) * e->shrink;
        x1 = e->foot_x + (x1 - e->foot_x) * e->shrink; y1 = e->foot_y + (y1 - e->foot_y) * e->shrink;
    }
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

/* A face of n corners (V, up to 6), for what is behind it not to show. */
static void face_n(sw_enhance *e, const vec *corner, int n)
{
    /* what of it is nearer than the nearest seen is cut off */
    vec c[8];
    int nc = 0;
    for (int i = 0; i < n; i++) {
        const vec *a = &corner[i], *b = &corner[(i + 1) % n];
        if (a->x >= NEAR) c[nc++] = *a;
        if ((a->x >= NEAR) != (b->x >= NEAR)) {
            double k = (NEAR - a->x) / (b->x - a->x);
            c[nc++] = vec3(NEAR, a->y + (b->y - a->y) * k, a->z + (b->z - a->z) * k);
        }
    }
    if (nc < 3) return;
    if (nc > 6) nc = 6;                     /* (a hexagon cut by the near plane: its last corner or two lost) */
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
        if (e->shrink != 1.0f) {
            f->x[i] = e->foot_x + (f->x[i] - e->foot_x) * e->shrink;
            f->y[i] = e->foot_y + (f->y[i] - e->foot_y) * e->shrink;
        }
    }
}

static void face(sw_enhance *e, const vec *corner) { face_n(e, corner, 4); }

/* --- the view ------------------------------------------------------------------------ */

static void view_of(sw_enhance *e, const sw_machine *m)
{
    static const int off[3][3] = { { M_AX, M_BX, M_CX }, { M_AY, M_BY, M_CY }, { M_AZ, M_BZ, M_CZ } };
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) e->r[a][b] = mb16(m, (unsigned)(SW_M_U1 + off[a][b])) / 16384.0;
    /* (the viewer's place as the view used it: the move after the view
     * has taken the player's own a step further) */
    e->t[0] = (uint16_t)mb16(m, SW_M_U1 + M_TX);
    e->t[1] = mb16(m, SW_M_U1 + M_TY);
    e->t[2] = mb16(m, SW_M_U1 + M_TZ);
}

/* --- stars ---------------------------------------------------------------------------- */

/* trench: 1 in the trench, whose walls stand 1024 to each side of its
 * middle and reach from 4096 below up to the height 0 (TBSBL of
 * WSBASE.MAC): a star shows if the way to it leaves the trench through its
 * open top. */
static void stars(sw_enhance *e, int trench, double now)
{
    /* over the surface a few (the owner found more too much), over the
     * trench all (only a strip of sky shows); each twinkles in its own time */
    int n = trench ? SW_ENHANCE_STARS : 64;
    for (int i = 0; i < n; i++) {
        float twinkle = 0.55f + 0.45f * (float)sin(now * (2.0 + (double)(i & 7) * 0.7) + (double)i * 1.7);
        if (trench && e->t[2] < 0.0) {
            double up = -e->t[2] / e->star[i][2];           /* how far along it is at the top of the walls */
            if (fabs(e->t[1] + e->star[i][1] * up) >= 1024.0) continue;
        }
        vec v = turn(e, vec3(e->star[i][0], e->star[i][1], e->star[i][2]));
        if (v.x < 0.05) continue;
        v.x *= 1e6; v.y *= 1e6; v.z *= 1e6;     /* as good as infinitely far */
        int before = e->n;
        line(e, v, v, WHT, (float)e->star_lum[i] * twinkle, SW_TAG_STAR);
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
        /* The plating lies near the player only, full to 6000 and gone by
         * 11000: the towers are sized as the arcade machine sizes them (a
         * linear scale with the distance, about the foot), so against a
         * ground drawn out to the horizon a far one would visibly grow out
         * of it as it came. On dark ground it does not show. */
        double far = (p0.x + p1.x) * 0.5;
        float fade = (float)((11000.0 - far) / 5000.0);
        if (fade <= 0.0f) continue;
        if (fade > 1.0f) fade = 1.0f;
        line(e, turn(e, p0), turn(e, p1), GRN, lum * fade * fade * e->plate, SW_TAG_TRENCH);
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
    const double RANGE = 15360.0;               /* in V, 3C00h: as far as the arcade machine shows them */
    const double FAR = 24576.0;                 /* 6000h: as far as they are drawn here, dark and small */
    const double BRIGHT = 2500.0;               /* from here on a building is as bright as it gets */
    const double SMALLEST = 0.08;               /* the arcade machine's sizing goes to 0 at 4000h: not below this */
    const double LOCK = 9000.0;                 /* from here on a turret has found the player */
    const int LAST_PASS = 5;                    /* the pass the game clears the ground in (S1MVGD) */
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
        /* The buildings the game shows, where it shows them (VWGRND): within
         * its range (100h to 3C00h ahead, 45 degrees to either side) those
         * of the passes so far that it has set up, which it does while they
         * are out of sight, so that within the range none comes up before
         * the player's eyes; a bunker shot away no more (the game draws it
         * in the color "off"). Beyond the range, from FAR on, the same
         * buildings are drawn already, dark and small, and fade in as they
         * come; a building the next pass brings (the pass begins when the
         * player's place wraps) only if it will still be beyond the range
         * then, for it would come up half way otherwise. The map comes
         * round every 8000h: each building is looked at on this round and
         * on the next. When the surface ends the game takes them all away,
         * and the player flies on into the dark. */
        if (pc == PC_BNK && (state & TYP_DM)) continue;
        double ahead = (double)((bx - (unsigned)e->t[0]) & 0x7fff);
        for (int lap = 0; lap < 2; lap++, ahead += 32768.0) {
            double beyond = (double)(int16_t)(uint16_t)e->t[0] + ahead - 32768.0;  /* past where the next pass starts */
            int pass = seq + (beyond >= 0.0 ? 1 : 0);
            if (pass < sq) continue;
            if (seq < sq && beyond / 2.0 < RANGE) continue;
            vec c = turn(e, vec3(ahead / 2.0, (double)(int16_t)(by - (int)e->t[1]) / 2.0, (GD_CENTER - e->t[2]) / 2.0));
            if (c.x < 256.0 || c.x >= FAR || fabs(c.y) >= c.x) continue;
            if (c.x < RANGE && !(state & TYP_VW)) continue;
            /* The towers end with the fifth pass (S1MVGD: GD.SEQ 5 sets
             * Q.KTW, and the game sets up no building it has not shown
             * yet): beyond the range nothing more comes then, and none of
             * the pass that would follow. Those it shows still go by. */
            if (c.x >= RANGE && (pass >= LAST_PASS || ram8(m, SW_Q_KTW))) continue;
            /* The farthest are dark, and each gets brighter in a straight
             * line as it comes nearer: none comes up at once, and what is
             * behind does not crowd what is before it. */
            float lum = 235.0f * ramp(c.x, FAR, BRIGHT);
            /* A building has all there is to it at every distance: nothing
             * of it is added as it comes nearer. (Only where it is so dark
             * that its seams could not be seen anyway are they left out.) */
            float steps = 1.0f, seams = lum >= 30.0f ? 1.0f : 0.0f;
            if (++e->inst == 0) e->inst = 0x8000;
            /* Sized as the arcade machine sizes a building (GDVIEW, M.GDSC):
             * on top of the perspective a linear scale with the distance,
             * 1 - XP/4000h, about its foot; a far tower is small and grows
             * as it comes. (In true perspective the towers stood larger
             * and closer together than the owner knows them.) */
            {
                unsigned a = ((unsigned)c.x >> 8) << 2;
                if (a > 255) a = 255;
                e->shrink = (float)(255 - a) / 255.0f;
                if (e->shrink < (float)SMALLEST) e->shrink = (float)SMALLEST;
                vec foot = add(c, turn(e, vec3(0.0, 0.0, -3840.0)));
                if (foot.x < 1.0) foot.x = 1.0;
                e->foot_x = (float)(PERSPECTIVE * foot.y / foot.x) * VG_SCALE;
                e->foot_y = ((float)(PERSPECTIVE * foot.z / foot.x) + VG_OFFY) * VG_SCALE;
            }
            /* The sizes are the arcade machine's, to the unit (the player
             * flies between and over them): a bunker 960 at its foot, 720
             * at its top, 720 high; a tower 960, 720, 600 and 480 at the
             * heights of its points, its hat 480 and 720 high. */
            if (pc == PC_BNK) {
                /* (seams on its sides and a hatch on its roof, but no more:
                 * there are many of them, low on the ground) */
                block(e, c, 960.0, -3840.0, 720.0, -3120.0, RED, lum, B_FOOT | B_TOP | B_SEAMS, 1.0f, seams);
                block(e, c, 430.0, -3120.0, 430.0, -3120.0, RED, lum * 0.7f, B_TOP, 1.0f, 0.0f);
                e->shrink = 1.0f;
                continue;
            }
            int hat = !(state & TYP_DM) && ((state & TYP_BK) || !(state & TYP_VW));   /* (one not set up yet will have its hat) */
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
            e->shrink = 1.0f;
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

/* --- the Millennium Falcon ---------------------------------------------------------------- */

/* A line of the model: its ends in the ship's own units (the hull's radius
 * is 1; its nose to +x, its left to +y, up +z), and the normals of the
 * faces it lies on (none, one or two): it is drawn when one of them is
 * towards the viewer, so that the far side of the hull does not show
 * through the near one. */
typedef struct mline { float a[3], b[3], n[2][3]; uint8_t nn, color; } mline;
/* A face of the model, for what is behind the ship not to show. */
typedef struct mface { float p[6][3]; uint8_t n; } mface;

/* A model: its lines and faces. The ships made here are built into one
 * at a time (`building`). */
#define MODEL_LINES 240
#define MODEL_FACES 96
typedef struct model { mline line[MODEL_LINES]; mface face[MODEL_FACES]; int lines, faces; } model;
static model falcon, xwing, *building;

static void set3(float *o, vec v) { o[0] = (float)v.x; o[1] = (float)v.y; o[2] = (float)v.z; }

static void mline_add(vec a, vec b, vec n1, vec n2, int nn, unsigned color)
{
    if (building->lines == MODEL_LINES) return;
    mline *l = &building->line[building->lines++];
    set3(l->a, a);
    set3(l->b, b);
    set3(l->n[0], n1);
    set3(l->n[1], n2);
    l->nn = (uint8_t)nn;
    l->color = (uint8_t)color;
}

static void mface_add(const vec *p, int n)
{
    if (building->faces == MODEL_FACES || n > 6) return;
    mface *f = &building->face[building->faces++];
    for (int i = 0; i < n; i++) set3(f->p[i], p[i]);
    f->n = (uint8_t)n;
}

/* A box, its edges with the normals of the two faces each lies on. */
static void mbox(double x0, double x1, double y0, double y1, double z0, double z1, unsigned color)
{
    const vec px = { 1, 0, 0 }, py = { 0, 1, 0 }, pz = { 0, 0, 1 }, mx = { -1, 0, 0 }, my = { 0, -1, 0 }, mz = { 0, 0, -1 };
    for (int i = 0; i < 4; i++) {
        double y = (i & 1) ? y1 : y0, z = (i & 2) ? z1 : z0, x = (i & 1) ? x1 : x0;
        mline_add(vec3(x0, y, z), vec3(x1, y, z), (i & 1) ? py : my, (i & 2) ? pz : mz, 2, color);
        mline_add(vec3(x, y0, z), vec3(x, y1, z), (i & 1) ? px : mx, (i & 2) ? pz : mz, 2, color);
        y = (i & 2) ? y1 : y0;
        mline_add(vec3(x, y, z0), vec3(x, y, z1), (i & 1) ? px : mx, (i & 2) ? py : my, 2, color);
    }
    vec c[4];
    c[0] = vec3(x0, y0, z1); c[1] = vec3(x1, y0, z1); c[2] = vec3(x1, y1, z1); c[3] = vec3(x0, y1, z1); mface_add(c, 4);
    c[0] = vec3(x0, y0, z0); c[1] = vec3(x1, y0, z0); c[2] = vec3(x1, y1, z0); c[3] = vec3(x0, y1, z0); mface_add(c, 4);
    c[0] = vec3(x0, y0, z0); c[1] = vec3(x1, y0, z0); c[2] = vec3(x1, y0, z1); c[3] = vec3(x0, y0, z1); mface_add(c, 4);
    c[0] = vec3(x0, y1, z0); c[1] = vec3(x1, y1, z0); c[2] = vec3(x1, y1, z1); c[3] = vec3(x0, y1, z1); mface_add(c, 4);
}

static vec unit(vec v)
{
    double n = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return n < 1e-9 ? v : vec3(v.x / n, v.y / n, v.z / n);
}

static vec cross(vec a, vec b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static vec scaled(vec v, double k) { return vec3(v.x * k, v.y * k, v.z * k); }

/* The ship: a flat hull, round but for the jaws at its front, a plateau
 * on top and one below; the cockpit a tube on its right; the dish on top
 * to the left; the engine's band across its back. */
static void falcon_build(void)
{
    const double PI = 3.14159265358979;
    const int N = 14;                           /* segments of the rim, the gap of the jaws left out */
    const double A0 = 40.0 * PI / 180.0, A1 = 320.0 * PI / 180.0;
    const double TOP = 0.17, RT = 0.62;         /* the plateaus: their height and radius */
    const vec UP = { 0, 0, 1 }, DOWN = { 0, 0, -1 };
    vec rim[15], top[15], bot[15];
    for (int i = 0; i <= N; i++) {
        double a = A0 + (A1 - A0) * i / N;
        rim[i] = vec3(cos(a), sin(a), 0.0);
        top[i] = vec3(RT * cos(a), RT * sin(a), TOP);
        bot[i] = vec3(RT * cos(a), RT * sin(a), -TOP);
    }
    for (int i = 0; i < N; i++) {
        double am = A0 + (A1 - A0) * (i + 0.5) / N, a = A0 + (A1 - A0) * i / N;
        vec up = vec3(0.45 * cos(am), 0.45 * sin(am), 1.0), dn = vec3(0.45 * cos(am), 0.45 * sin(am), -1.0);
        mline_add(rim[i], rim[i + 1], up, dn, 2, WHT);
        mline_add(top[i], top[i + 1], UP, up, 2, WHT);
        mline_add(bot[i], bot[i + 1], DOWN, dn, 2, WHT);
        if ((i & 1) == 0) {                     /* the spokes of the slopes */
            vec ua = vec3(0.45 * cos(a - 0.2), 0.45 * sin(a - 0.2), 1.0), ub = vec3(0.45 * cos(a + 0.2), 0.45 * sin(a + 0.2), 1.0);
            vec da = vec3(ua.x, ua.y, -1.0), db = vec3(ub.x, ub.y, -1.0);
            mline_add(rim[i], top[i], ua, ub, 2, WHT);
            mline_add(rim[i], bot[i], da, db, 2, WHT);
        }
        vec q[4] = { rim[i], rim[i + 1], top[i + 1], top[i] };
        mface_add(q, 4);
        vec r[4] = { rim[i], rim[i + 1], bot[i + 1], bot[i] };
        mface_add(r, 4);
    }
    /* the plateaus, in pieces of up to six corners */
    for (int k = 0; k < N; k += 4) {
        vec p[6], q[6];
        int n = 0;
        p[n] = vec3(0.0, 0.0, TOP); q[n++] = vec3(0.0, 0.0, -TOP);
        for (int i = k; i <= k + 4 && i <= N; i++) { p[n] = top[i]; q[n++] = bot[i]; }
        mface_add(p, n);
        mface_add(q, n);
    }
    /* the front: the hull's rim and plateaus run in to the jaws and the notch between them */
    mline_add(rim[0], vec3(0.72, 0.6, 0.0), vec3(0.5, 0.5, 1.0), vec3(0.5, 0.5, -1.0), 2, WHT);
    mline_add(rim[N], vec3(0.72, -0.6, 0.0), vec3(0.5, -0.5, 1.0), vec3(0.5, -0.5, -1.0), 2, WHT);
    mline_add(top[0], vec3(0.6, 0.2, 0.12), UP, vec3(0.6, 0.3, 1.0), 2, WHT);
    mline_add(top[N], vec3(0.6, -0.2, 0.12), UP, vec3(0.6, -0.3, 1.0), 2, WHT);
    mline_add(bot[0], vec3(0.6, 0.2, -0.12), DOWN, vec3(0.6, 0.3, -1.0), 2, WHT);
    mline_add(bot[N], vec3(0.6, -0.2, -0.12), DOWN, vec3(0.6, -0.3, -1.0), 2, WHT);
    mline_add(vec3(0.6, -0.2, 0.12), vec3(0.6, 0.2, 0.12), vec3(1, 0, 0), UP, 2, WHT);
    mline_add(vec3(0.6, -0.2, -0.12), vec3(0.6, 0.2, -0.12), vec3(1, 0, 0), DOWN, 2, WHT);
    mline_add(vec3(0.6, 0.2, -0.12), vec3(0.6, 0.2, 0.12), vec3(1, 0, 0), vec3(0, -1, 0), 2, WHT);
    mline_add(vec3(0.6, -0.2, -0.12), vec3(0.6, -0.2, 0.12), vec3(1, 0, 0), vec3(0, 1, 0), 2, WHT);
    {
        vec p[6] = { top[N], top[0], vec3(0.6, 0.2, 0.12), vec3(0.6, -0.2, 0.12) };
        mface_add(p, 4);
        vec q[6] = { bot[N], bot[0], vec3(0.6, 0.2, -0.12), vec3(0.6, -0.2, -0.12) };
        mface_add(q, 4);
    }
    /* the jaws */
    mbox(0.72, 1.38, 0.2, 0.6, -0.09, 0.09, WHT);
    mbox(0.72, 1.38, -0.6, -0.2, -0.09, 0.09, WHT);
    /* on top: the hub, panel seams out from it, two pits; below, seams */
    for (int i = 0; i < 6; i++) {
        double a = i * PI / 3.0, b = a + PI / 3.0;
        mline_add(vec3(0.18 * cos(a), 0.18 * sin(a), TOP + 0.03), vec3(0.18 * cos(b), 0.18 * sin(b), TOP + 0.03), UP, UP, 1, WHT);
    }
    for (int i = 1; i < 6; i++) {
        double a = i * PI / 3.0;
        mline_add(vec3(0.18 * cos(a), 0.18 * sin(a), TOP), vec3(RT * cos(a), RT * sin(a), TOP), UP, UP, 1, WHT);
    }
    for (int s = -1; s <= 1; s += 2) {
        double cx = -0.42, cy = s * 0.3, h = 0.07;
        mline_add(vec3(cx - h, cy - h, TOP), vec3(cx + h, cy - h, TOP), UP, UP, 1, WHT);
        mline_add(vec3(cx + h, cy - h, TOP), vec3(cx + h, cy + h, TOP), UP, UP, 1, WHT);
        mline_add(vec3(cx + h, cy + h, TOP), vec3(cx - h, cy + h, TOP), UP, UP, 1, WHT);
        mline_add(vec3(cx - h, cy + h, TOP), vec3(cx - h, cy - h, TOP), UP, UP, 1, WHT);
    }
    for (int i = 0; i < 3; i++) {
        double a = PI * (0.5 + i * 0.5);
        mline_add(vec3(0.0, 0.0, -TOP), vec3(RT * cos(a), RT * sin(a), -TOP), DOWN, DOWN, 1, WHT);
    }
    /* the cockpit: a tube on the right, six-sided, its window at its end */
    {
        vec c0 = vec3(0.2, -0.74, 0.07), c1 = vec3(0.98, -0.9, 0.1);
        vec d = unit(vec3(c1.x - c0.x, c1.y - c0.y, c1.z - c0.z));
        vec s = unit(cross(UP, d)), u = cross(d, s);
        const double R = 0.09;
        vec ring0[6], ring1[6], ring2[6], rad[6];
        for (int i = 0; i < 6; i++) {
            double a = i * PI / 3.0;
            rad[i] = add(scaled(s, cos(a)), scaled(u, sin(a)));
            ring0[i] = add(c0, scaled(rad[i], R));
            ring1[i] = add(c1, scaled(rad[i], R));
            ring2[i] = add(vec3(c1.x - d.x * 0.1, c1.y - d.y * 0.1, c1.z - d.z * 0.1), scaled(rad[i], R));
        }
        for (int i = 0; i < 6; i++) {
            int j = (i + 1) % 6;
            vec nm = add(rad[i], rad[j]);
            mline_add(ring0[i], ring1[i], rad[i], rad[i], 1, WHT);
            mline_add(ring0[i], ring0[j], nm, scaled(d, -1.0), 2, WHT);
            mline_add(ring1[i], ring1[j], nm, d, 2, TRQ);
            mline_add(ring2[i], ring2[j], nm, nm, 1, TRQ);
            vec q[4] = { ring0[i], ring0[j], ring1[j], ring1[i] };
            mface_add(q, 4);
        }
    }
    /* the dish, on a stem, looking ahead and up */
    {
        vec c = vec3(-0.05, 0.3, 0.3), e1 = vec3(0.0, 1.0, 0.0), e2 = vec3(-0.6, 0.0, 0.8);
        const vec NONE = { 0, 0, 0 };
        mline_add(vec3(-0.05, 0.3, TOP), c, NONE, NONE, 0, WHT);
        vec p[8];
        for (int i = 0; i < 8; i++) {
            double a = i * PI / 4.0;
            p[i] = add(c, add(scaled(e1, 0.13 * cos(a)), scaled(e2, 0.13 * sin(a))));
        }
        for (int i = 0; i < 8; i++) mline_add(p[i], p[(i + 1) % 8], NONE, NONE, 0, WHT);
        mline_add(p[0], p[4], NONE, NONE, 0, WHT);
        mline_add(p[2], p[6], NONE, NONE, 0, WHT);
    }
    /* the engine's band across the back, blue */
    {
        const vec BACK = { -1, 0, 0 };
        const double X = -0.985, W = 0.55, H = 0.06;
        mline_add(vec3(X, -W, -H), vec3(X, W, -H), BACK, BACK, 1, BLU);
        mline_add(vec3(X, -W, H), vec3(X, W, H), BACK, BACK, 1, BLU);
        mline_add(vec3(X, -W, -H), vec3(X, -W, H), BACK, BACK, 1, BLU);
        mline_add(vec3(X, W, -H), vec3(X, W, H), BACK, BACK, 1, BLU);
        for (int i = -1; i <= 1; i++) mline_add(vec3(X, i * 0.28, -H), vec3(X, i * 0.28, H), BACK, BACK, 1, BLU);
    }
}

/* A ship at `at` from the player, looking along f with its left to l and
 * its up to u, its own units times `size`: in the world's axes (world 1,
 * turned as the player is), or in the view's own (world 0: x ahead, y to
 * the right on the screen, z up). With `hide`, the faces also go where
 * what is behind them is cut away by the enhancement itself (hide()). */
static void hide_face(sw_enhance *e, const vec *c, int n);
static void hide(sw_enhance *e, int lasers);
static void model_draw(sw_enhance *e, const model *md, vec at, vec f, vec l, vec u, double size, float lum,
                       int world, int hide)
{
    if (++e->inst == 0) e->inst = 0x8000;
    for (int i = 0; i < md->lines; i++) {
        const mline *ml = &md->line[i];
        vec p[2];
        for (int k = 0; k < 2; k++) {
            const float *q = k ? ml->b : ml->a;
            p[k] = add(at, add(scaled(f, q[0] * size), add(scaled(l, q[1] * size), scaled(u, q[2] * size))));
        }
        if (ml->nn) {
            /* towards the viewer, who is at the origin, from the line's middle */
            vec mid = scaled(add(p[0], p[1]), -0.5);
            int seen = 0;
            for (int k = 0; k < ml->nn && !seen; k++) {
                vec n = add(scaled(f, ml->n[k][0]), add(scaled(l, ml->n[k][1]), scaled(u, ml->n[k][2])));
                seen = n.x * mid.x + n.y * mid.y + n.z * mid.z > 0.0;
            }
            if (!seen) continue;
        }
        float lm = ml->color == BLU ? lum * 1.2f : ml->color == TRQ ? lum * 0.9f : ml->color == RED ? lum * 0.85f : lum;
        if (world) line(e, turn(e, p[0]), turn(e, p[1]), ml->color, lm, SW_TAG_OBJECT);
        else line(e, p[0], p[1], ml->color, lm, hide ? SW_TAG_NONE : SW_TAG_OBJECT);
    }
    for (int i = 0; i < md->faces; i++) {
        const mface *mf = &md->face[i];
        vec c[6];
        for (int k = 0; k < mf->n; k++) {
            c[k] = add(at, add(scaled(f, mf->p[k][0] * size), add(scaled(l, mf->p[k][1] * size), scaled(u, mf->p[k][2] * size))));
            if (world) c[k] = turn(e, c[k]);
        }
        if (hide) hide_face(e, c, mf->n);
        else face_n(e, c, mf->n);
    }
}

/* A byte of the game's memory, RAM or ROM (the trench's tables lie in both). */
static uint8_t any8(const sw_machine *m, unsigned a)
{
    if (a >= 0x4800 && a < 0x5000) return ram8(m, a);
    if (a >= 0x6000 && m->rom) return (uint8_t)(sw_rom16(m, (uint16_t)a) >> 8);
    return 0;
}

/* How far ahead the exhaust port of this trench is, from the wedges still
 * to come (WSBASE.MAC: the near wedge, the ones after it in its pie, the
 * next pies), or -1 if there is none before the trench ends. The game
 * itself learns of it only as its far wedge, 7000h ahead. */
static double port_ahead(const sw_machine *m)
{
    enum { WG_TYP = 0, WG_IZ = 3, PI_IZ = 2, TYP_SHORT = 1, TYP_END = 3, TYP_PORT = 4, TYP_NXT = 5 };
    unsigned u = ram16(m, SW_WG_NRP), pie = ram16(m, SW_PI_NRW);
    double loc = (double)(int16_t)ram16(m, SW_WG_NRL);
    for (int i = 0; i < 64; i++) {
        uint8_t typ = any8(m, u + WG_TYP);
        if (typ == TYP_PORT) return loc - (double)(int16_t)mb16(m, SW_M_S1 + M_TX);
        if (typ == TYP_END || typ == 0) return -1.0;
        loc += typ == TYP_SHORT ? 0x800 : 0x1000;
        u += WG_IZ;
        if (any8(m, u + WG_TYP) == TYP_NXT) {
            pie += PI_IZ;
            u = (unsigned)((any8(m, pie) << 8) | any8(m, pie + 1));
        }
    }
    return -1.0;
}

/* Shortly before the exhaust port the Falcon comes from behind and above
 * the player, passes over him and flies on down the trench ahead, a
 * little lower, weaving a little and banked into the weave. Once in each
 * run at the port: it begins when the player is BEFORE seconds from the
 * port at his speed, and is far ahead and gone as he reaches it. */
static void falcon_fly(sw_enhance *e, const sw_machine *m, double now)
{
    const double RUN = 3.2, BEFORE = 3.4;       /* seconds */
    if (e->falcon_at == 0.0 && !e->falcon_done) {
        double fps = SW_IRQ_HZ / (m->frame_irqs ? m->frame_irqs : 12);     /* (13 a second in the trench) */
        double dist = port_ahead(m), speed = (double)mb16(m, SW_M_S1 + M_VX) * fps;
        if (dist > 0.0 && speed > 0.0 && dist / speed < BEFORE) e->falcon_at = now;
    }
    if (e->falcon_at == 0.0) return;
    double t = now - e->falcon_at;
    if (t < 0.0 || t > RUN) {
        e->falcon_done = 1;
        e->falcon_at = 0.0;
        return;
    }
    /* its way, in the world's units: x ahead of the player, y across the
     * trench (0 its middle), z up (0 the tops of the walls) */
    double x = -1600.0 + 6500.0 * t, y = 300.0 * sin(t * 1.1 + 0.4), z = 2500.0 - 280.0 * t;
    double dy = 300.0 * 1.1 * cos(t * 1.1 + 0.4);
    vec f = unit(vec3(6500.0, dy, -280.0));
    vec l = unit(vec3(-f.y, f.x, 0.0)), u = cross(f, l);
    double bank = -0.0025 * dy;                 /* banked into the weave */
    vec lb = add(scaled(l, cos(bank)), scaled(u, sin(bank))), ub = add(scaled(u, cos(bank)), scaled(l, -sin(bank)));
    vec at = vec3(x / 2.0, (y - e->t[1]) / 2.0, (z - e->t[2]) / 2.0);
    float lum = 230.0f;
    if (t < 0.25) lum *= (float)(t / 0.25);
    if (t > RUN - 0.5) lum *= (float)((RUN - t) / 0.5);
    model_draw(e, &falcon, at, f, lb, ub, 300.0, lum, 1, 0);
}

/* --- the X-wing, seen from outside ---------------------------------------------------------- */

/* A face of a solid being built. */
typedef struct poly { vec p[6]; int n; } poly;

/* A convex solid of faces: each edge once, with the normals of its faces
 * (outwards, from the solid's middle), and the faces themselves. */
static void msolid(const poly *f, int nf, unsigned color)
{
    vec c = vec3(0.0, 0.0, 0.0);
    int np = 0;
    for (int i = 0; i < nf; i++)
        for (int k = 0; k < f[i].n; k++) { c = add(c, f[i].p[k]); np++; }
    c = scaled(c, 1.0 / np);
    vec nrm[16];
    struct { vec a, b; int f0, f1; } ed[64];
    int ne = 0;
    for (int i = 0; i < nf && i < 16; i++) {
        const vec *p = f[i].p;
        vec nv = unit(cross(vec3(p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z),
                            vec3(p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z)));
        if (nv.x * (p[0].x - c.x) + nv.y * (p[0].y - c.y) + nv.z * (p[0].z - c.z) < 0.0) nv = scaled(nv, -1.0);
        nrm[i] = nv;
        mface_add(p, f[i].n);
        for (int k = 0; k < f[i].n; k++) {
            vec a = p[k], b = p[(k + 1) % f[i].n];
            int found = -1;
            for (int q = 0; q < ne && found < 0; q++) {
                double d1 = fabs(ed[q].a.x - b.x) + fabs(ed[q].a.y - b.y) + fabs(ed[q].a.z - b.z);
                double d2 = fabs(ed[q].b.x - a.x) + fabs(ed[q].b.y - a.y) + fabs(ed[q].b.z - a.z);
                double d3 = fabs(ed[q].a.x - a.x) + fabs(ed[q].a.y - a.y) + fabs(ed[q].a.z - a.z);
                double d4 = fabs(ed[q].b.x - b.x) + fabs(ed[q].b.y - b.y) + fabs(ed[q].b.z - b.z);
                if ((d1 < 1e-6 && d2 < 1e-6) || (d3 < 1e-6 && d4 < 1e-6)) found = q;
            }
            if (found >= 0) ed[found].f1 = i;
            else if (ne < 64) { ed[ne].a = a; ed[ne].b = b; ed[ne].f0 = i; ed[ne].f1 = -1; ne++; }
        }
    }
    for (int q = 0; q < ne; q++)
        mline_add(ed[q].a, ed[q].b, nrm[ed[q].f0], ed[q].f1 >= 0 ? nrm[ed[q].f1] : nrm[ed[q].f0], ed[q].f1 >= 0 ? 2 : 1, color);
}

/* A box of a solid's faces. */
static void mbox_solid(double x0, double x1, double y0, double y1, double z0, double z1, unsigned color)
{
    poly f[6] = {
        { { vec3(x0, y0, z1), vec3(x1, y0, z1), vec3(x1, y1, z1), vec3(x0, y1, z1) }, 4 },
        { { vec3(x0, y0, z0), vec3(x1, y0, z0), vec3(x1, y1, z0), vec3(x0, y1, z0) }, 4 },
        { { vec3(x0, y0, z0), vec3(x1, y0, z0), vec3(x1, y0, z1), vec3(x0, y0, z1) }, 4 },
        { { vec3(x0, y1, z0), vec3(x1, y1, z0), vec3(x1, y1, z1), vec3(x0, y1, z1) }, 4 },
        { { vec3(x0, y0, z0), vec3(x0, y1, z0), vec3(x0, y1, z1), vec3(x0, y0, z1) }, 4 },
        { { vec3(x1, y0, z0), vec3(x1, y1, z0), vec3(x1, y1, z1), vec3(x1, y0, z1) }, 4 },
    };
    msolid(f, 6, color);
}

/* The X-wing, in its own units: from its tail at -0.62 to the tip of its
 * nose at 1, its left to +y, up +z; the tips of its four guns at
 * XWING_GUN_X, +-XWING_GUN_Y, +-XWING_GUN_Z. */
#define XWING_GUN_X 0.86
#define XWING_GUN_Y 0.95
#define XWING_GUN_Z 0.28
static void xwing_build(void)
{
    const double PI = 3.14159265358979;
    const vec NONE = { 0, 0, 0 }, UP = { 0, 0, 1 }, BACK = { -1, 0, 0 };
    const double XT = -0.62, XC = 0.30, XN = 1.0, W = 0.11, H = 0.10, WN = 0.035, HN = 0.03;
    /* the fuselage, and the nose tapering from it */
    mbox_solid(XT, XC, -W, W, -H, H, WHT);
    {
        poly f[5] = {
            { { vec3(XC, -W, H), vec3(XC, W, H), vec3(XN, WN, HN), vec3(XN, -WN, HN) }, 4 },
            { { vec3(XC, -W, -H), vec3(XC, W, -H), vec3(XN, WN, -HN), vec3(XN, -WN, -HN) }, 4 },
            { { vec3(XC, W, -H), vec3(XC, W, H), vec3(XN, WN, HN), vec3(XN, WN, -HN) }, 4 },
            { { vec3(XC, -W, -H), vec3(XC, -W, H), vec3(XN, -WN, HN), vec3(XN, -WN, -HN) }, 4 },
            { { vec3(XN, -WN, -HN), vec3(XN, WN, -HN), vec3(XN, WN, HN), vec3(XN, -WN, HN) }, 4 },
        };
        msolid(f, 5, WHT);
        /* the red band round the nose */
        double xb = 0.62, kb = (xb - XC) / (XN - XC), wb = W + (WN - W) * kb, hb = H + (HN - H) * kb;
        mline_add(vec3(xb, -wb, hb), vec3(xb, wb, hb), UP, UP, 1, RED);
        mline_add(vec3(xb, wb, -hb), vec3(xb, wb, hb), vec3(0, 1, 0), vec3(0, 1, 0), 1, RED);
        mline_add(vec3(xb, -wb, -hb), vec3(xb, -wb, hb), vec3(0, -1, 0), vec3(0, -1, 0), 1, RED);
    }
    /* the canopy, glass */
    {
        const double C0 = 0.0, C1 = 0.32, CW = 0.08, T0 = 0.04, T1 = 0.18, TW = 0.06, CH = 0.07;
        poly f[5] = {
            { { vec3(T0, -TW, H + CH), vec3(T1, -TW, H + CH), vec3(T1, TW, H + CH), vec3(T0, TW, H + CH) }, 4 },
            { { vec3(C1, -CW, H), vec3(C1, CW, H), vec3(T1, TW, H + CH), vec3(T1, -TW, H + CH) }, 4 },
            { { vec3(C0, -CW, H), vec3(C0, CW, H), vec3(T0, TW, H + CH), vec3(T0, -TW, H + CH) }, 4 },
            { { vec3(C0, -CW, H), vec3(C1, -CW, H), vec3(T1, -TW, H + CH), vec3(T0, -TW, H + CH) }, 4 },
            { { vec3(C0, CW, H), vec3(C1, CW, H), vec3(T1, TW, H + CH), vec3(T0, TW, H + CH) }, 4 },
        };
        msolid(f, 5, TRQ);
    }
    /* the astromech behind it: a dome */
    for (int i = 0; i < 8; i++) {
        double a = i * PI / 4.0, b = a + PI / 4.0;
        mline_add(vec3(-0.14 + 0.05 * cos(a), 0.05 * sin(a), H + 0.01), vec3(-0.14 + 0.05 * cos(b), 0.05 * sin(b), H + 0.01), UP, UP, 1, BLU);
    }
    /* the four wings, in an X, with red stripes; the engines at their
     * roots, the guns at their tips */
    for (int sy = -1; sy <= 1; sy += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            double rz = sz * 0.04, tz = sz * XWING_GUN_Z;
            vec r0 = vec3(-0.56, sy * W, rz), r1 = vec3(-0.06, sy * W, rz);
            vec t0 = vec3(-0.52, sy * XWING_GUN_Y, tz), t1 = vec3(-0.30, sy * XWING_GUN_Y, tz);
            mline_add(r0, t0, NONE, NONE, 0, WHT);
            mline_add(r1, t1, NONE, NONE, 0, WHT);
            mline_add(t0, t1, NONE, NONE, 0, WHT);
            vec q[4] = { r0, r1, t1, t0 };
            mface_add(q, 4);
            for (int k = 0; k < 2; k++) {
                double u0 = 0.30 + 0.08 * k, u1 = u0 + 0.30;
                vec a = vec3(-0.40 + 0.03 * k, sy * (W + (XWING_GUN_Y - W) * u0), rz + (tz - rz) * u0);
                vec b = vec3(-0.38 + 0.03 * k, sy * (W + (XWING_GUN_Y - W) * u1), rz + (tz - rz) * u1);
                mline_add(a, b, NONE, NONE, 0, RED);
            }
            /* the engine: a six-sided tube, glowing red at its back */
            {
                double cy = sy * 0.19, cz = sz * 0.12, R = 0.075, X0 = -0.68, X1 = -0.12;
                vec ring0[6], ring1[6];
                for (int i = 0; i < 6; i++) {
                    double a = i * PI / 3.0 + PI / 6.0;
                    ring0[i] = vec3(X0, cy + R * cos(a), cz + R * sin(a));
                    ring1[i] = vec3(X1, cy + R * 0.85 * cos(a), cz + R * 0.85 * sin(a));
                }
                poly f[8];
                for (int i = 0; i < 6; i++) {
                    int j = (i + 1) % 6;
                    f[i].n = 4;
                    f[i].p[0] = ring0[i]; f[i].p[1] = ring0[j]; f[i].p[2] = ring1[j]; f[i].p[3] = ring1[i];
                }
                f[6].n = 6;
                f[7].n = 6;
                for (int i = 0; i < 6; i++) { f[6].p[i] = ring0[i]; f[7].p[i] = ring1[i]; }
                msolid(f, 8, WHT);
                for (int i = 0; i < 6; i++) {
                    int j = (i + 1) % 6;
                    vec a = vec3(X0 - 0.001, cy + (ring0[i].y - cy) * 0.6, cz + (ring0[i].z - cz) * 0.6);
                    vec b = vec3(X0 - 0.001, cy + (ring0[j].y - cy) * 0.6, cz + (ring0[j].z - cz) * 0.6);
                    mline_add(a, b, BACK, BACK, 1, RED);
                }
            }
            /* the gun: its housing on the tip, the barrel ahead of it */
            {
                double gy = sy * XWING_GUN_Y, gz = tz, G = 0.022;
                mbox_solid(-0.54, 0.30, gy - G, gy + G, gz - G, gz + G, WHT);
                mline_add(vec3(0.30, gy, gz), vec3(XWING_GUN_X, gy, gz), NONE, NONE, 0, WHT);
                mline_add(vec3(0.62, gy - G * 0.7, gz), vec3(0.70, gy - G * 0.7, gz), NONE, NONE, 0, WHT);
                mline_add(vec3(0.62, gy + G * 0.7, gz), vec3(0.70, gy + G * 0.7, gz), NONE, NONE, 0, WHT);
            }
        }
}

/* Seen from behind the X-wing: it flies ahead of the camera, low, banks
 * and turns a little with the yoke (where the sight is), and its lasers
 * leave from the tips of its guns, each to where the game sends it (the
 * sight, or what it hits). What is behind it is not seen. */
static void outside(sw_enhance *e, const sw_machine *m)
{
    const double D = 1000.0, SIZE = 340.0;
    double sx = (int8_t)ram8(m, SW_SI_RSX) / 128.0, sy = (int8_t)ram8(m, SW_SI_RSY) / 128.0;
    double yaw = 0.22 * sx, pitch = 0.12 * sy, roll = 0.55 * sx;
    /* its axes in the view's: ahead, to the right, up */
    vec f = vec3(cos(yaw), sin(yaw), 0.0), r = vec3(-sin(yaw), cos(yaw), 0.0), u = vec3(0.0, 0.0, 1.0);
    vec f2 = add(scaled(f, cos(pitch)), scaled(u, sin(pitch)));
    u = add(scaled(u, cos(pitch)), scaled(f, -sin(pitch)));
    f = f2;
    vec r2 = add(scaled(r, cos(roll)), scaled(u, -sin(roll)));
    u = add(scaled(u, cos(roll)), scaled(r, sin(roll)));
    r = r2;
    vec l = scaled(r, -1.0);
    vec at = vec3(D, 150.0 * sx, -400.0 + 60.0 * sy);

    /* the lasers from the guns: the beams' keys 0 to 3 are the upper left,
     * lower left, lower right and upper right gun */
    float tip[4][2];
    for (int k = 0; k < 4; k++) {
        double gy = (k < 2 ? 1.0 : -1.0) * XWING_GUN_Y, gz = (k == 0 || k == 3 ? 1.0 : -1.0) * XWING_GUN_Z;
        vec p = add(at, add(scaled(f, XWING_GUN_X * SIZE), add(scaled(l, gy * SIZE), scaled(u, gz * SIZE))));
        tip[k][0] = (float)(PERSPECTIVE * p.y / p.x) * VG_SCALE;
        tip[k][1] = ((float)(PERSPECTIVE * p.z / p.x) + VG_OFFY) * VG_SCALE;
    }
    float from[4][2], to[4][2];
    int has[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < e->n; i++) {
        unsigned tag = (unsigned)(e->out[i].id >> 48);
        if (SW_TAG_SECTION(tag) != SW_TAG_LASER || (tag & 0x3ff) > 3) continue;
        int k = (int)(tag & 0x3ff);
        if (!has[k]) { from[k][0] = e->out[i].x0; from[k][1] = e->out[i].y0; has[k] = 1; }
        to[k][0] = e->out[i].x1;
        to[k][1] = e->out[i].y1;
    }
    for (int i = 0; i < e->n; i++) {
        sw_line *ln = &e->out[i];
        unsigned tag = (unsigned)(ln->id >> 48);
        if (SW_TAG_SECTION(tag) != SW_TAG_LASER || (tag & 0x3ff) > 3) continue;
        int k = (int)(tag & 0x3ff);
        float dx = to[k][0] - from[k][0], dy = to[k][1] - from[k][1], dd = dx * dx + dy * dy;
        float ox = tip[k][0] - from[k][0], oy = tip[k][1] - from[k][1];
        for (int end = 0; end < 2; end++) {
            float *x = end ? &ln->x1 : &ln->x0, *y = end ? &ln->y1 : &ln->y0;
            float s = dd > 0.0f ? ((*x - from[k][0]) * dx + (*y - from[k][1]) * dy) / dd : 1.0f;
            if (s < 0.0f) s = 0.0f;
            if (s > 1.0f) s = 1.0f;
            *x += ox * (1.0f - s);
            *y += oy * (1.0f - s);
        }
    }

    /* the ship, and what it covers */
    uint16_t inst = e->inst;
    e->n_hide = 0;
    model_draw(e, &xwing, at, f, l, u, SIZE, 170.0f, 0, 1);
    hide(e, 0);
    e->inst = inst;
}

/* --- the cockpit ------------------------------------------------------------------------------ */

/* Seen from inside the X-wing's cockpit, in place of its nose and guns:
 * the frame of the canopy, its two struts running up to the top corners,
 * and the instrument panel below, in the screen's own units (x -127.5 to
 * 127.5, y -153 to 150). What is behind them is not seen; the lasers come
 * out from behind the panel, along the way they always take. The panel is
 * kept low: the view ahead (and where the sight goes) stays free. */
static const float COCKPIT[3][6][2] = {
    { { -128, -154 }, { 128, -154 }, { 128, -128 }, { 44, -96 }, { -44, -96 }, { -128, -128 } },  /* the panel */
    { { -128, 92 }, { -50, -98 }, { -38, -98 }, { -128, 122 } },                                   /* left strut */
    { { 38, -98 }, { 50, -98 }, { 128, 92 }, { 128, 122 } },                                       /* right strut */
};
static const int COCKPIT_N[3] = { 6, 4, 4 };

/* Where a line lies inside a convex shape, as the part [*t0, *t1] of it;
 * 0 if no part does. */
static int inside_part(const float (*p)[2], int n, float x0, float y0, float x1, float y1, float *t0, float *t1)
{
    float area = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += p[i][0] * p[j][1] - p[j][0] * p[i][1];
    }
    float s = area > 0.0f ? 1.0f : -1.0f;
    float a = 0.0f, b = 1.0f, dx = x1 - x0, dy = y1 - y0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        /* inward normal of the edge i-j */
        float nx = -(p[j][1] - p[i][1]) * s, ny = (p[j][0] - p[i][0]) * s;
        float num = nx * (x0 - p[i][0]) + ny * (y0 - p[i][1]), den = nx * dx + ny * dy;
        if (den == 0.0f) { if (num < 0.0f) return 0; continue; }
        float t = -num / den;
        if (den > 0.0f) { if (t > a) a = t; }
        else if (t < b) b = t;
        if (a >= b) return 0;
    }
    *t0 = a;
    *t1 = b;
    return 1;
}

/* A line on the screen (x, y as the lines of the picture have them). */
static void flat(sw_enhance *e, float x0, float y0, float x1, float y1, unsigned color, float lum)
{
    const double K = 1.0 / VG_SCALE;
    line(e, vec3(PERSPECTIVE, x0 * K, y0 * K - VG_OFFY), vec3(PERSPECTIVE, x1 * K, y1 * K - VG_OFFY), color, lum, SW_TAG_NONE);
}

static void flat_box(sw_enhance *e, float x0, float y0, float x1, float y1, unsigned color, float lum)
{
    flat(e, x0, y0, x1, y0, color, lum);
    flat(e, x1, y0, x1, y1, color, lum);
    flat(e, x1, y1, x0, y1, color, lum);
    flat(e, x0, y1, x0, y0, color, lum);
}

static void flat_ring(sw_enhance *e, float cx, float cy, float r, unsigned color, float lum)
{
    for (int i = 0; i < 8; i++) {
        double a = i * 0.7853982, b = a + 0.7853982;
        flat(e, cx + r * (float)cos(a), cy + r * (float)sin(a), cx + r * (float)cos(b), cy + r * (float)sin(b), color, lum);
    }
}

/* A face (corners in V) for what is behind it to be cut away by hide(). */
static void hide_face(sw_enhance *e, const vec *c, int n)
{
    if (e->n_hide == SW_ENHANCE_HIDE || n < 3 || n > 6) return;
    for (int i = 0; i < n; i++) if (c[i].x < NEAR) return;
    for (int i = 0; i < n; i++) {
        e->hide_p[e->n_hide][i][0] = (float)(PERSPECTIVE * c[i].y / c[i].x) * VG_SCALE;
        e->hide_p[e->n_hide][i][1] = ((float)(PERSPECTIVE * c[i].z / c[i].x) + VG_OFFY) * VG_SCALE;
    }
    e->hide_n[e->n_hide++] = n;
}

/* The lines so far without what the shapes in e->hide (convex, on the
 * screen) cover. Not cut: what is written on the screen, the sight, what
 * the enhancement draws in front (SW_TAG_NONE), and the lasers if
 * `lasers` is 0. */
static void hide(sw_enhance *e, int lasers)
{
    int n = e->n;
    if (e->cap_cock < 2 * n + 16) {
        sw_line *o = (sw_line *)realloc(e->cock, (size_t)(2 * n + 2048) * sizeof *o);
        if (!o) return;
        e->cock = o;
        e->cap_cock = 2 * n + 2048;
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        const sw_line *l = &e->out[i];
        unsigned sec = SW_TAG_SECTION((unsigned)(l->id >> 48));
        if (sec == SW_TAG_MESS || sec == SW_TAG_SCORE || sec == SW_TAG_SITE || sec == SW_TAG_COIN ||
            sec == SW_TAG_COUNT || sec == SW_TAG_NONE || (sec == SW_TAG_LASER && !lasers) || k >= 2 * n + 8) {
            if (k < e->cap_cock) e->cock[k++] = *l;
            continue;
        }
        /* the pieces of it left, shape by shape (each cuts at most one out) */
        float piece[16][2] = { { 0.0f, 1.0f } };
        int np = 1;
        float bx0 = l->x0 < l->x1 ? l->x0 : l->x1, bx1 = l->x0 < l->x1 ? l->x1 : l->x0;
        float by0 = l->y0 < l->y1 ? l->y0 : l->y1, by1 = l->y0 < l->y1 ? l->y1 : l->y0;
        for (int s = 0; s < e->n_hide; s++) {
            /* (not near it at all: nothing to do) */
            float px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f;
            for (int c = 0; c < e->hide_n[s]; c++) {
                float x = e->hide_p[s][c][0], y = e->hide_p[s][c][1];
                if (x < px0) px0 = x;
                if (x > px1) px1 = x;
                if (y < py0) py0 = y;
                if (y > py1) py1 = y;
            }
            if (bx1 < px0 || bx0 > px1 || by1 < py0 || by0 > py1) continue;
            int m = np;
            for (int q = 0; q < m; q++) {
                float a = piece[q][0], b = piece[q][1], t0, t1;
                if (b <= a) continue;
                float x0 = l->x0 + (l->x1 - l->x0) * a, y0 = l->y0 + (l->y1 - l->y0) * a;
                float x1 = l->x0 + (l->x1 - l->x0) * b, y1 = l->y0 + (l->y1 - l->y0) * b;
                if (!inside_part((const float (*)[2])e->hide_p[s], e->hide_n[s], x0, y0, x1, y1, &t0, &t1)) continue;
                float c0 = a + (b - a) * t0, c1 = a + (b - a) * t1;
                piece[q][1] = c0;                               /* before the shape */
                if (np < 16) { piece[np][0] = c1; piece[np][1] = b; np++; }   /* after it */
            }
        }
        for (int q = 0; q < np && k < e->cap_cock; q++) {
            float a = piece[q][0], b = piece[q][1];
            if (b - a < 1e-4f && !(a == 0.0f && b == 1.0f && l->x0 == l->x1 && l->y0 == l->y1)) continue;
            sw_line c = *l;
            c.x0 = l->x0 + (l->x1 - l->x0) * a; c.y0 = l->y0 + (l->y1 - l->y0) * a;
            c.x1 = l->x0 + (l->x1 - l->x0) * b; c.y1 = l->y0 + (l->y1 - l->y0) * b;
            c.w0 = (uint16_t)(l->w0 + ((float)l->w1 - (float)l->w0) * a);
            c.w1 = (uint16_t)(l->w0 + ((float)l->w1 - (float)l->w0) * b);
            e->cock[k++] = c;
        }
    }
    /* (the two buffers change places: what is drawn next goes after these) */
    sw_line *t = e->out;
    int cap = e->cap;
    e->out = e->cock;
    e->cap = e->cap_cock;
    e->cock = t;
    e->cap_cock = cap;
    e->n = k;
}

/* The game's lines without what the cockpit hides, then the cockpit. */
static void cockpit(sw_enhance *e)
{
    /* hidden: all but what is written on the screen and the sight */
    e->n_hide = 0;
    for (int s = 0; s < 3; s++) {
        for (int c = 0; c < COCKPIT_N[s]; c++) {
            e->hide_p[s][c][0] = COCKPIT[s][c][0];
            e->hide_p[s][c][1] = COCKPIT[s][c][1];
        }
        e->hide_n[s] = COCKPIT_N[s];
    }
    e->n_hide = 3;
    hide(e, 1);

    /* the cockpit itself */
    uint16_t inst = e->inst;
    e->inst = 0;
    const float F = 150.0f, D = 100.0f, R = 170.0f;     /* frame, details, red lights */
    /* the canopy's frame: the struts, with a rail along each, and its sill */
    for (int s = -1; s <= 1; s += 2) {
        flat(e, s * 128.0f, 92.0f, s * 50.0f, -98.0f, WHT, F);
        flat(e, s * 128.0f, 122.0f, s * 38.0f, -98.0f, WHT, F);
        flat(e, s * 128.0f, 107.0f, s * 44.0f, -98.0f, WHT, D * 0.6f);
        flat(e, s * 128.0f, -128.0f, s * 44.0f, -96.0f, WHT, F);       /* the sill */
        flat(e, s * 128.0f, -137.0f, s * 40.0f, -102.0f, WHT, F);      /* the side console's top */
        flat(e, s * 40.0f, -102.0f, s * 36.0f, -154.0f, WHT, D);       /* the middle console's side */
        /* the side console: a red frame along its slope, keys in it, two knobs */
        flat(e, s * 110.0f, -141.0f, s * 76.0f, -126.0f, RED, R * 0.7f);
        flat(e, s * 76.0f, -126.0f, s * 72.0f, -138.0f, RED, R * 0.7f);
        flat(e, s * 72.0f, -138.0f, s * 106.0f, -152.0f, RED, R * 0.7f);
        flat(e, s * 106.0f, -152.0f, s * 110.0f, -141.0f, RED, R * 0.7f);
        for (int i = 0; i < 3; i++)
            flat_box(e, s * (98.0f - i * 8.0f), -141.0f + i * 3.0f, s * (93.0f - i * 8.0f), -145.0f + i * 3.0f, WHT, D);
        flat_ring(e, s * 120.0f, -148.0f, 3.0f, WHT, D);
        flat_ring(e, s * 58.0f, -146.0f, 2.5f, WHT, D);
    }
    flat(e, -44.0f, -96.0f, 44.0f, -96.0f, WHT, F);            /* the panel's top */
    flat(e, -40.0f, -102.0f, 40.0f, -102.0f, WHT, D);
    for (int i = 0; i < 13; i++) {                              /* a row of red lights */
        float x = -30.0f + i * 5.0f;
        flat(e, x, -99.0f, x + 1.5f, -99.0f, RED, (i % 3) ? R : R * 0.6f);
    }
    flat_box(e, -18.0f, -107.0f, -5.0f, -124.0f, WHT, F);       /* the two targeting screens */
    flat_box(e, 5.0f, -107.0f, 18.0f, -124.0f, WHT, F);
    flat_box(e, -16.0f, -110.0f, -7.0f, -121.0f, WHT, D * 0.5f);
    flat_box(e, 7.0f, -110.0f, 16.0f, -121.0f, WHT, D * 0.5f);
    flat_ring(e, 4.0f, -133.0f, 2.5f, RED, R);                  /* the red button */
    flat_ring(e, -20.0f, -141.0f, 3.5f, WHT, D);                /* dials */
    flat_ring(e, -11.0f, -141.0f, 2.5f, WHT, D);
    flat_box(e, -27.0f, -128.0f, -22.0f, -132.0f, RED, R * 0.7f);
    flat_box(e, -27.0f, -135.0f, -22.0f, -139.0f, RED, R * 0.7f);
    flat_box(e, 16.0f, -130.0f, 20.0f, -150.0f, WHT, D);         /* a lever */
    for (int i = 0; i < 5; i++) flat(e, -6.0f, -129.0f - i * 4.0f, -4.5f, -129.0f - i * 4.0f, RED, R * 0.8f);
    e->inst = inst;
}

/* --- all of it ----------------------------------------------------------------------------- */

void sw_enhance_init(sw_enhance *e, const sw_machine *m)
{
    if (!falcon.lines) { building = &falcon; falcon_build(); }
    if (!xwing.lines) { building = &xwing; xwing_build(); }
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
    free(e->cock);
    memset(e, 0, sizeof *e);
}

int sw_enhance_run(sw_enhance *e, const sw_machine *m, double now, const sw_line *in, int n, const sw_line **out)
{
    e->n = e->n_faces = 0;
    e->inst = 0x8000;
    e->line_id = 0;
    e->spin_c = 1.0;
    e->spin_s = 0.0;
    e->shrink = 1.0f;
    uint8_t phase = m->view_phase;          /* (the view that drew the picture, not the phase of the moment) */
    int ground = (phase == SW_PHSGD + 1 || phase == SW_PHSG0D + 1) &&
                 (e->surface || e->stars || e->towers || e->dogfights);
    int in_trench = phase == SW_PHSBS + 1 || phase == SW_PHSB0D + 1;
    int trench = in_trench && e->stars;
    int clip = in_trench && e->clip;
    int falcon = in_trench && e->falcon;
    int logo = e->logo && phase >= SW_PHSBNR && phase <= SW_PHSHIS + 1;     /* (the attract mode) */
    if (!in_trench) e->falcon_at = 0.0, e->falcon_done = 0;
    /* the cockpit wherever the game shows the X-wing's nose and guns */
    int plane = 0;
    for (int i = 0; i < n && !plane; i++) plane = SW_TAG_SECTION((unsigned)(in[i].id >> 48)) == SW_TAG_PLANE;
    e->plane_seen = plane;
    int cock = plane && e->view == SW_VIEW_COCKPIT, outs = plane && e->view == SW_VIEW_OUTSIDE;
    if (!ground && !logo && !trench && !clip && !falcon && !cock && !outs) {
        *out = in;
        return n;
    }
    if (ground || trench || falcon) view_of(e, m);

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
        if ((cock || outs) && SW_TAG_SECTION(tag) == SW_TAG_PLANE) continue;
        e->out[e->n] = in[i];
        if (logo && SW_TAG_SECTION(tag) == SW_TAG_BANNER) e->out[e->n].color = YLW;
        if (clip && in[i].d3) {
            /* from its ends in space, cut where it leaves the player's sight */
            sw_line *l = &e->out[e->n];
            double a[3] = { l->v0[0], l->v0[1], l->v0[2] }, b[3] = { l->v1[0], l->v1[1], l->v1[2] };
            float w0, w1;
            if (!sw_view_line(a, b, &l->x0, &l->y0, &w0, &l->x1, &l->y1, &w1)) continue;
            l->w0 = (uint16_t)w0;
            l->w1 = (uint16_t)w1;
        }
        e->n++;
    }
    if (trench) stars(e, 1, now);
    if (falcon) falcon_fly(e, m, now);
    if (ground && e->stars) stars(e, 0, now);
    if (ground && e->surface) {
        /* As the ground ends the game takes its buildings away and the
         * player dives for the trench: the plating fades out with them
         * rather than sweep across the screen. */
        double dt = now - e->plate_at;
        if (dt < 0.0 || dt > 0.2) dt = 0.0;
        e->plate_at = now;
        if (ram8(m, SW_Q_KTW)) e->plate -= (float)(dt / 0.8);
        else e->plate = 1.0f;
        if (e->plate < 0.0f) e->plate = 0.0f;
        if (e->plate > 0.0f) surface(e);
    }
    if (ground && e->towers) towers(e, m, now);
    if (ground && e->dogfights && e->tie_ready) dogfights(e, now);
    if (cock) cockpit(e);
    if (outs) outside(e, m);
    *out = e->out;
    return e->n;
}
