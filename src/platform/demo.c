/* demo.c - see demo.h. */
#include "platform/demo.h"

#include "core/sched.h"
#include "core/sw_symbols.h"
#include "core/tags.h"
#include "render/enhance.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_SECONDS 112.0      /* space, the surface, the trench of the Death Star in the middle, its exhaust port */

static uint8_t ram(const sw_machine *m, uint16_t a) { return m->ram[(a - 0x4800) & 0x7ff]; }
static uint16_t ram16(const sw_machine *m, uint16_t a) { return (uint16_t)(ram(m, a) << 8 | ram(m, (uint16_t)(a + 1))); }

/* --- what the pilot knows of the game ------------------------------------------ */

/* The player's ship in the math box's RAM (M$S1, game/m6809.h): where it
 * is (TX forward, TY to the right, TZ up) and how fast it goes forward. */
#define S1_TX (SW_M_S1 + 0x08)
#define S1_TY (SW_M_S1 + 0x0a)
#define S1_TZ (SW_M_S1 + 0x0c)
#define S1_VX (SW_M_S1 - 0x0a)
static int16_t ship(const sw_machine *m, uint16_t a) { return (int16_t)mb_get(&m->mbox, a); }

/* A program ROM word, as the main line reads it (game/ground.c: the mazes). */
static uint8_t rom8(const sw_machine *m, uint16_t a)
{
    if (a < 0x8000 && (m->latch[4] & 0x80)) return m->rom->main[0x10000 + (a - 0x6000)];
    return m->rom->main[a];
}
static uint16_t rom16(const sw_machine *m, uint16_t a) { return (uint16_t)(rom8(m, a) << 8 | rom8(m, (uint16_t)(a + 1))); }

/* an enemy's shot (G$TYP of GUN, game/guns.c): alive and armed */
#define G_TYP  3
#define G_IZE  6
#define SHOT_LIVE 0x01

/* the buildings of the surface (game/ground.c) */
#define TGDPTR 0xA182       /* the mazes by the wave: start and end */
#define ZGDPTR 0xA1CE
#define TGD_X  0            /* forward */
#define TGD_Y  2            /* to the side */
#define TGD_PC 4
#define TGD_SQ 5
#define TGD_RM 6
#define TGD_IZ 7
#define PC_BNK 3            /* a lone bunker (else a tower) */
#define GD_DM  0x04         /* damaged: no hat, no guns */
#define GD_VW  0x02         /* set up to be seen */
#define GD_BK  0x10         /* the bunker (a tower's hat) is there */

/* where the sight can go (the yoke's reach, VGCURx: +-128 is +-512) */
#define REACH_L (4.0f * (int8_t)(uint8_t)SW_VGCURL)
#define REACH_R (4.0f * (int8_t)SW_VGCURR)
#define REACH_B (4.0f * (int8_t)(uint8_t)SW_VGCURB)
#define REACH_T (4.0f * (int8_t)SW_VGCURT)

static float vg13(uint16_t v) { return (float)((int16_t)(uint16_t)(v << 3) >> 3); }

/* --- the demo itself --------------------------------------------------------------- */

static void start(sw_demo *d, sw_machine *m)
{
    d->saved = (sw_machine *)malloc(sizeof *d->saved);
    if (!d->saved) return;
    if (!d->space) d->space = (uint8_t *)malloc(SW_AVG_SPACE_SIZE);
    if (!d->space) { free(d->saved); d->saved = NULL; return; }
    *d->saved = *m;
    d->active = 1;
    d->irq = 0;
    d->parts = 0;
    d->begun = 0;
    d->view = d->first_view;
    d->vg_seen = m->vg_starts;
    d->fire = 0;
    d->dodge = 0;
    memset(d->shot, 0, sizeof d->shot);
    d->things = 0;
    if (ram(m, SW_SSCRDT) == 0) m->ram[SW_SSCRDT - 0x4800] = 1;     /* a credit, for the demo only */
}

void sw_demo_stop(sw_demo *d, sw_machine *m)
{
    if (!d->active) return;
    *m = *d->saved;             /* as it was: the attract mode going on to the high scores */
    free(d->saved);
    d->saved = NULL;
    free(d->space);
    d->space = NULL;
    sw_avg_list_free(&d->seen);
    d->active = 0;
    d->ended = 1;
    d->first_view = (d->first_view + 1) % SW_VIEWS;
    /* the demo's sound stops: the board is reset, as by request 00 */
    m->sound_out = 0;
    m->sound_out_full = 1;
}

/* --- what the pilot sees ------------------------------------------------------------ */

/* The picture the game has drawn last, as lines (the same list the
 * renderer runs, render/picture.c): its shots and its objects, each as the
 * box around its lines, in the sight's units. */
static void see(sw_demo *d, const sw_machine *m)
{
    if (m->vg_starts == d->vg_seen) return;
    d->vg_seen = m->vg_starts;
    memcpy(d->space, m->vgram, sizeof m->vgram);
    memcpy(d->space + 0x3000, m->rom->vrom, sizeof m->rom->vrom);
    sw_avg_run_noted(d->space, 0, m->vgtag, m->vgstart, (const sw_avg_note *)m->vgnote, &d->seen, 0);

    float box[SW_DEMO_SHOTS + SW_DEMO_THINGS][4];
    memset(d->shot, 0, sizeof d->shot);
    d->things = 0;
    for (int i = 0; i < d->seen.count; i++) {
        const sw_line *l = &d->seen.lines[i];
        uint16_t tag = (uint16_t)(l->id >> 48), key = tag & 0x3ff;
        int k = -1;
        if (SW_TAG_SECTION(tag) == SW_TAG_SHOT && key < SW_DEMO_SHOTS) {
            k = key;
        } else if (SW_TAG_SECTION(tag) == SW_TAG_OBJECT) {
            for (int j = 0; j < d->things && k < 0; j++)
                if (d->thing[j].key == key) k = SW_DEMO_SHOTS + j;
            if (k < 0 && d->things < SW_DEMO_THINGS) {
                k = SW_DEMO_SHOTS + d->things++;
                memset(&d->thing[k - SW_DEMO_SHOTS], 0, sizeof d->thing[0]);
                d->thing[k - SW_DEMO_SHOTS].key = key;
            }
        }
        if (k < 0) continue;
        sw_demo_thing *t = k < SW_DEMO_SHOTS ? &d->shot[k] : &d->thing[k - SW_DEMO_SHOTS];
        /* (the lines are a quarter of the sight's units, and lie VGOFFY lower) */
        float x0 = 4.0f * fminf(l->x0, l->x1), x1 = 4.0f * fmaxf(l->x0, l->x1);
        float y0 = 4.0f * fminf(l->y0, l->y1) - (int16_t)SW_VGOFFY, y1 = 4.0f * fmaxf(l->y0, l->y1) - (int16_t)SW_VGOFFY;
        if (t->r == 0.0f) {
            t->r = 1.0f;
            box[k][0] = x0; box[k][1] = x1; box[k][2] = y0; box[k][3] = y1;
        } else {
            box[k][0] = fminf(box[k][0], x0); box[k][1] = fmaxf(box[k][1], x1);
            box[k][2] = fminf(box[k][2], y0); box[k][3] = fmaxf(box[k][3], y1);
        }
        if (l->w0 > t->w) t->w = l->w0;
        if (l->w1 > t->w) t->w = l->w1;
    }
    for (int k = 0; k < SW_DEMO_SHOTS + d->things; k++) {
        sw_demo_thing *t = k < SW_DEMO_SHOTS ? &d->shot[k] : &d->thing[k - SW_DEMO_SHOTS];
        if (t->r == 0.0f) continue;
        t->x = 0.5f * (box[k][0] + box[k][1]);
        t->y = 0.5f * (box[k][2] + box[k][3]);
        t->r = 0.5f * fmaxf(box[k][1] - box[k][0], box[k][3] - box[k][2]) + 1.0f;
        t->top = box[k][3];
    }
}

/* The shot to shoot down first: the nearest of those alive that can still
 * hit (one the sight cannot reach goes by: game/guns.c, VW1GUN). */
static const sw_demo_thing *threat(const sw_demo *d, const sw_machine *m)
{
    const sw_demo_thing *best = NULL;
    for (int k = 0; k < SW_DEMO_SHOTS; k++) {
        const sw_demo_thing *t = &d->shot[k];
        if (t->r == 0.0f || !(ram(m, (uint16_t)(SW_GUN + k * G_IZE + G_TYP)) & SHOT_LIVE)) continue;
        if (t->x <= REACH_L || t->x >= REACH_R || t->y <= REACH_B || t->y >= REACH_T) continue;
        if (!best || t->w > best->w) best = t;
    }
    return best;
}

/* The nearest object in the picture (in space: a TIE fighter, or Vader). */
static const sw_demo_thing *nearest(const sw_demo *d)
{
    const sw_demo_thing *best = NULL;
    for (int j = 0; j < d->things; j++)
        if (!best || d->thing[j].w > best->w) best = &d->thing[j];
    return best;
}

/* --- over the surface ------------------------------------------------------------- */

/* Where the yoke has the sight when the ship next moves (the main line
 * takes VG.RSX, VG.RSY, the sight's units / 4, once a frame), sent to `to`
 * (the sight's units): most of the way from where it is now, as it slews
 * there within a few interrupts. */
static float sight_then(const sw_machine *m, uint16_t a, float to)
{
    float now = (int8_t)ram(m, a);
    return now + 0.85f * (to / 4.0f - now);
}

/* The wave's maze of buildings (GD.WAV: game/ground.c, MAZE). */
static void maze(const sw_machine *m, uint16_t *from, uint16_t *to)
{
    uint16_t x = (uint16_t)(TGDPTR + (uint8_t)(ram(m, SW_GD_WAV) << 2));
    if (x >= ZGDPTR) x = ZGDPTR - 4;
    *from = rom16(m, x);
    *to = rom16(m, (uint16_t)(x + 2));
}

/* Over the surface with the sight held at x, y: in how many frames the
 * ship flies into a tower (0: not within the next second). The tower is
 * hit when it is near (half its distance within the ship's speed of 200)
 * and still in view, no more to the side than ahead (GDVIEW). To the side
 * as the ship sees it: it banks with the sight, and when hit (S1TWGD), and
 * the bank turns the tower's middle, high above it, into the side (as the
 * math box has it, measured: twice YP = cos r * dy + sin r * (TZ - 1E00),
 * r the bank GD.ACT at 78 to 5 degrees). The ship goes VX ahead a frame,
 * VX * x / 128 to the side and VX * y / 256 up (S1MVGD, in the sight's
 * units / 4); the bank goes to twice the sight's x and the roll of a hit
 * (S.ROL * 32, which runs out a count a frame), 16 a frame, 50 while
 * rolling from a hit (the first frame: sight_then). `room`: how much
 * farther to the side than that counts as a hit as well. */
static int tower_ahead(const sw_machine *m, float x, float y, float room)
{
    uint16_t from, to;
    maze(m, &from, &to);
    int vx = ship(m, S1_VX);
    if (vx < 0x100) vx = 0x100;
    uint16_t tx = (uint16_t)ship(m, S1_TX);
    float ty = 0.0f, tz = ship(m, S1_TZ), bank = (int16_t)ram16(m, SW_GD_ACT);
    int roll = (int8_t)ram(m, SW_S_ROL);
    for (int k = 1; k <= 24; k++) {
        float sx = k == 1 ? sight_then(m, SW_VG_RSX, x) : x / 4.0f, sy = k == 1 ? sight_then(m, SW_VG_RSY, y) : y / 4.0f;
        tx = (uint16_t)(tx + vx);
        ty += (float)vx * sx / 128.0f;
        tz = fminf(fmaxf(tz + (float)vx * sy / 256.0f, 0x200), 0x1c00);
        if (roll) roll += roll > 0 ? -1 : 1;
        float most = roll ? 50.0f : 16.0f, turn = 2.0f * sx + 32.0f * roll - bank;
        bank += fminf(fmaxf(turn, -most), most);
        float r = bank * (5.0f / 78.0f * 3.14159265f / 180.0f), c = cosf(r), s = sinf(r);
        for (uint16_t b = from; b < to && b - from < 32 * TGD_IZ; b += TGD_IZ) {
            if ((int8_t)ram(m, SW_GD_SEQ) < (int8_t)rom8(m, (uint16_t)(b + TGD_SQ))) continue;
            if (rom8(m, (uint16_t)(b + TGD_PC)) == PC_BNK) continue;    /* (a bunker is flown over) */
            if (!(ram(m, (uint16_t)(SW_GDRAM + (int8_t)rom8(m, (uint16_t)(b + TGD_RM)))) & GD_VW)) continue;
            int dx = (uint16_t)(rom16(m, b + TGD_X) - tx) & 0x7fff;
            if (dx < 0x200 || dx > 2 * vx + 0x400) continue;
            float dy = (float)(int16_t)(uint16_t)(rom16(m, (uint16_t)(b + TGD_Y)) - (uint16_t)ship(m, S1_TY)) - ty;
            if (fabsf(c * dy + s * (tz - 0x1e00)) < (float)dx + room) return k;
        }
    }
    return 0;
}

/* The tops of towers and the bunkers still to be shot: the nearest in the
 * picture, and where to aim (a tower's hat is the top of it). */
static int ground_target(const sw_demo *d, const sw_machine *m, float *x, float *y)
{
    uint16_t from, to;
    maze(m, &from, &to);
    const sw_demo_thing *best = NULL;
    int bunker = 0;
    for (int j = 0; j < d->things; j++) {
        const sw_demo_thing *t = &d->thing[j];
        if (t->key < 16) continue;
        int8_t rm = (int8_t)(t->key - 16);
        uint8_t state = ram(m, (uint16_t)(SW_GDRAM + rm));
        if (state & GD_DM) continue;
        int is_bunker = -1;
        for (uint16_t b = from; b < to && b - from < 32 * TGD_IZ; b += TGD_IZ)
            if ((int8_t)rom8(m, (uint16_t)(b + TGD_RM)) == rm) is_bunker = rom8(m, (uint16_t)(b + TGD_PC)) == PC_BNK;
        if (is_bunker < 0 || (!is_bunker && !(state & GD_BK))) continue;
        if (!best || t->w > best->w) { best = t; bunker = is_bunker; }
    }
    if (!best) return 0;
    *x = best->x;
    *y = bunker ? best->y : best->top - 0.06f * 2.0f * (best->top - best->y);
    return 1;
}

/* --- in the trench --------------------------------------------------------------- */

/* The trench with the sight held at x, y: in how many frames the ship
 * flies into a force field (0: not within the next frames). Each stretch
 * of 800 has on each wall up to four things one above the other (PNLW,
 * PNRW: two bits each, 2 a force field); one is hit in the first half of
 * its stretch, on its side of the trench, within 200 of its height
 * (game/panel.c). The ship goes VX ahead a frame, VX * x / 256 to the side
 * and VX * y / 128 up (S1MVBS; the first frame: sight_then).
 * `room`: how much more than that counts as a hit as well. */
static int field_ahead(const sw_machine *m, float x, float y, float room)
{
    int vx = ship(m, S1_VX);
    if (vx < 0x100) vx = 0x100;
    uint16_t tx = (uint16_t)ship(m, S1_TX);
    float ty = ship(m, S1_TY), tz = ship(m, S1_TZ);
    for (int k = 1; k <= 16; k++) {
        float sx = k == 1 ? sight_then(m, SW_VG_RSX, x) : x / 4.0f, sy = k == 1 ? sight_then(m, SW_VG_RSY, y) : y / 4.0f;
        tx = (uint16_t)(tx + vx);
        ty = fminf(fmaxf(ty + (float)vx * sx / 256.0f, -0x1ff), 0x1ff);
        tz = fminf(fmaxf(tz + (float)vx * sy / 128.0f, -0xdff), -0x101);
        if ((tx & 0x7ff) > 0x400 + 0x100) continue;
        int seg = (tx >> 11) & 0x0f;
        for (int right = 0; right < 2; right++) {
            if (right ? ty < -room / 2.0f : ty > room / 2.0f) continue;
            uint8_t a = ram(m, (uint16_t)((right ? SW_PNRW : SW_PNLW) + seg));
            for (int h = 0; h < 4; h++, a >>= 2) {
                float z0 = -0xe00 + 0x400 * h;
                if ((a & 3) == 2 && fabsf(tz - z0) <= 0x200 + room) return k;
            }
        }
    }
    return 0;
}

/* --- steering clear --------------------------------------------------------------- */

static int hit_ahead(const sw_machine *m, int trench, float x, float y, float room)
{
    return trench ? field_ahead(m, x, y, room) : tower_ahead(m, x, y, room);
}

/* Where to hold the sight not to fly into anything (over the surface a
 * tower, in the trench a force field): where he would hold it if that is
 * clear with room to spare; else the way he took already, while it is
 * clear; else the nearest place that is, first with room to spare (the
 * sight lags, and he changes his mind), then with little; else the one
 * that hits latest. Over the surface only to the side (the height is
 * kept), in the trench either way. Worked out once a frame of the game
 * (the ship moves once a frame). Returns 1 if he has to go elsewhere than
 * he would. */
static int steer_clear(sw_demo *d, const sw_machine *m, int trench, float *x, float *y)
{
    const float wide = trench ? 0x180 : 0x500, narrow = trench ? 0x80 : 0x100;
    if (!hit_ahead(m, trench, *x, *y, wide)) {
        d->dodge = 0;
        return 0;
    }
    if (d->dodge && d->plan_frame == m->frames) {
        *x = d->plan_x;
        *y = d->plan_y;
        return 1;
    }
    float bx = *x, by = *y, bd = 1e12f;
    int found = 0, latest = -1;
    for (int pass = 0; pass < 2 && !found; pass++) {
        if (d->dodge && !hit_ahead(m, trench, d->plan_x, trench ? d->plan_y : *y, pass ? narrow : wide)) {
            found = 1;
            bx = d->plan_x;
            by = trench ? d->plan_y : *y;
            break;
        }
        for (float cy = trench ? REACH_B + 16.0f : *y; cy <= (trench ? REACH_T - 16.0f : *y); cy += 48.0f)
            for (float cx = REACH_L + 16.0f; cx <= REACH_R - 16.0f; cx += trench ? 48.0f : 32.0f) {
                int k = hit_ahead(m, trench, cx, cy, pass ? narrow : wide);
                float dd = (cx - *x) * (cx - *x) + (cy - *y) * (cy - *y);
                if (k == 0 ? !found || dd < bd : !found && pass && (k > latest || (k == latest && dd < bd))) {
                    if (k == 0) found = 1;
                    else latest = k;
                    bd = dd; bx = cx; by = cy;
                }
            }
    }
    d->dodge = 1;
    d->plan_frame = m->frames;
    d->plan_x = *x = bx;
    d->plan_y = *y = by;
    return 1;
}

/* The exhaust port ahead, the torpedoes not yet away: where the sight
 * puts the laser's end on the bottom of the trench right on it. The beam
 * goes from the ship to 7000 ahead, 7/8 of the sight's place (VG.RSX,
 * VG.RSY: the sight's units times 64) to the side and down (CLBLZ); where
 * it meets the bottom (-1000) it fires the torpedoes when within 200 of the
 * port. Returns 0 if the port is not to be shot at now. */
static int port_aim(const sw_machine *m, float *x, float *y)
{
    if (!ram(m, SW_BS_PFL) || ram(m, SW_PT_LZF)) return 0;
    int dist = (int16_t)(uint16_t)(ram16(m, SW_BS_PLC) - (uint16_t)ship(m, S1_TX));
    if (dist < 0x600 || dist > 0x6800) return 0;
    float above = (float)ship(m, S1_TZ) + 0x1000;      /* (how high over the bottom) */
    *y = -above * 512.0f / (float)dist;
    *x = -(float)ship(m, S1_TY) * 512.0f / (float)dist;
    return *y > REACH_B + 8.0f;
}

/* --- the yoke and the trigger ------------------------------------------------------ */

/* RHLIM (game/irq.c) the other way: the pot's reading that gives the
 * value n (-7F..7F), after what the game has learned of the pot (its low
 * end, its spread). */
static uint8_t pot_for(const sw_machine *m, uint16_t blk, int n)
{
    int lo = ram(m, blk + 0), spread = ram(m, (uint16_t)(blk + 2));
    int want = n + 0x80, a = 0;
    while (a < 0xff && a + ((a * spread) >> 8) < want) a++;
    a += lo;
    return (uint8_t)(a > 0xff ? 0xff : a);
}

/* The yoke held so that the sight goes to x, y (the sight's units). */
static void hold_sight(sw_machine *m, float x, float y)
{
    x = fminf(fmaxf(x, REACH_L + 4.0f), REACH_R - 4.0f);
    y = fminf(fmaxf(y, REACH_B + 4.0f), REACH_T - 4.0f);
    m->in.adc[1] = pot_for(m, SW_RHEOY, (int)lroundf(x / 4.0f));     /* yaw: left and right */
    m->in.adc[0] = pot_for(m, SW_RHEOP, (int)lroundf(y / 4.0f));     /* pitch: up and down */
}

#define FIRE_HOLD 10        /* interrupts the trigger is held for a shot */
#define FIRE_REST 14        /* and let go before the next */

/* A pull of the trigger, if it is free (the last shot's beam gone: LZ.EDG). */
static void pull(sw_demo *d, const sw_machine *m)
{
    if ((d->fire == 0 || d->fire <= -FIRE_REST) && (int8_t)ram(m, SW_LZ_EDG) <= 1) d->fire = FIRE_HOLD;
}

/* The sight is on it (where the sight is now, not where it is going). */
static int on_it(const sw_machine *m, float x, float y, float r)
{
    float sx = vg13(ram16(m, SW_VG_CX)), sy = vg13(ram16(m, SW_VG_CY));
    return fabsf(sx - x) <= r && fabsf(sy - y) <= r;
}

/* --- the pilot -------------------------------------------------------------------- */

static void fly(sw_demo *d, sw_machine *m, uint8_t phase)
{
    see(d, m);
    double t = (double)d->irq / SW_IRQ_HZ;
    /* by himself: the sight wandering a little about the middle */
    float x = (float)(110.0 * sin(t * 0.9) + 30.0 * sin(t * 2.3));
    float y = (float)(70.0 * sin(t * 0.7 + 1.0) + 20.0 * sin(t * 1.9));
    int shoot = 0;
    float r = 12.0f;
    const sw_demo_thing *s = threat(d, m);
    int ground = phase == SW_PHSGD + 1, trench = phase == SW_PHSBS + 1;
    if (trench && port_aim(m, &x, &y)) {
        /* the exhaust port before all: it is the one chance */
        r = 10.0f; shoot = 1;
    } else if (s) {
        /* first the fireball coming: the sight on it, and fire */
        x = s->x; y = s->y; r = s->r + 10.0f; shoot = 1;
    } else if (phase == SW_PHSSP1 + 1 || phase == SW_PHSSP2 + 1) {
        const sw_demo_thing *o = nearest(d);
        if (o) { x = o->x; y = o->y; r = o->r * 0.6f + 6.0f; shoot = 1; }
    } else if (ground) {
        if (ground_target(d, m, &x, &y)) { r = 14.0f; shoot = 1; }
    } else if (trench) {
        /* down the middle, half way up */
        x = -(float)ship(m, S1_TY) * 0.5f;
        y = (-0x700 - (float)ship(m, S1_TZ)) * 0.25f;
    }

    if (ground) {
        float tz = ship(m, S1_TZ);
        if (tz < 0x900) y = fmaxf(y, 120.0f);          /* (well over the bunkers) */
        else if (tz > 0x1600) y = fminf(y, -60.0f);
    }
    if (ground || trench) {
        /* never into a tower or a force field */
        float wx = x, wy = y;
        if (steer_clear(d, m, trench, &x, &y) && (fabsf(x - wx) > 4.0f * r || fabsf(y - wy) > 4.0f * r)) shoot = 0;
    }
    hold_sight(m, x, y);
    if (shoot && on_it(m, x, y, r)) pull(d, m);
}

void sw_demo_tick(sw_demo *d, sw_machine *m)
{
    uint8_t phase = ram(m, SW_PHASE);
    if (!d->active) {
        /* from the page of scores to the high scores, nobody having paid
         * (in free play the attract mode always holds its credit) */
        int free_play = (m->nvram[(SW_OPTS0 + 1) & 0xff] & 0x03) == 0;
        int paid = ram(m, SW_SSCRDT) != 0 && !free_play;
        if (d->enabled && !paid && (phase == SW_PHSHIS || phase == SW_PHSHIS + 1) && d->last_phase == SW_PHSSCR + 1)
            start(d, m);
        d->last_phase = phase;
        if (!d->active) return;
    }
    /* (the attract mode's pages: before the game has begun, or after it is
     * over; choosing the Death Star comes in between, numbered lower) */
    int attract = phase >= SW_PHSBNR && phase <= SW_PHSHIS + 1;
    if (!attract) d->begun = 1;
    if (d->interrupt || d->irq > (long)(DEMO_SECONDS * SW_IRQ_HZ) || (d->begun && attract) ||
        (!d->begun && d->irq > (long)(15.0 * SW_IRQ_HZ))) {
        d->interrupt = 0;
        sw_demo_stop(d, m);
        d->last_phase = ram(m, SW_PHASE);
        return;
    }
    d->irq++;

    /* what the demo's player does */
    long i = d->irq;
    uint8_t in0 = 0xff;
    m->in.adc[0] = m->in.adc[1] = 0x80;
    /* fire, to start: again and again until the game has begun (a push in
     * the frame that sets a page up is not looked at) */
    if (!d->begun && i % 60 >= 20 && i % 60 < 50) in0 &= (uint8_t)~SW_IN0_LFIRE;
    if (phase == SW_PHSSDS + 1 && i > 120) {
        /* the Death Star in the middle (wave 3: towers on its surface): the
         * sight a little under the middle, whatever the pots have learned */
        hold_sight(m, 0.0f, 4.0f * (0x4c - 0x80));
        if (i % 60 < 20) in0 &= (uint8_t)~SW_IN0_LFIRE;
    } else if (phase >= SW_PHSSP1) {
        fly(d, m, phase);
        if (d->fire > 0) {
            in0 &= (uint8_t)~SW_IN0_LFIRE;
            if (--d->fire == 0) d->fire = -1;
        } else if (d->fire < 0 && d->fire > -1000) {
            d->fire--;
        }
        /* what hits him costs shields, as in any game; only the last one
         * is never lost (the demo is not to end in the middle) */
        if ((int8_t)ram(m, SW_S_GAS) < 1) m->ram[SW_S_GAS - 0x4800] = 1;
    }
    m->in.in0 = in0;
    m->in.in1 = 0xff;

    /* a new part of the flight, a new view */
    if (phase != d->last_phase && (phase == SW_PHSSP1 + 1 || phase == SW_PHSGD + 1 || phase == SW_PHSBS + 1)) {
        d->view = (d->first_view + d->parts) % SW_VIEWS;
        d->parts++;
    }
    d->last_phase = phase;
}
