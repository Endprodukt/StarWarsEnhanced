/* subframe.c - see subframe.h. */
#include "game/subframe.h"

#include "game/game.h"
#include "game/m6809.h"

#include <math.h>
#include <string.h>

/* explosion pieces (XP$xx, game/xpld.c) */
#define XP_CX  0
#define XP_TYP 12
#define XP_TMR 13
#define XP_IZE 14
#define XP_EQ  8

#define STAR_IZE 8          /* a star in the math box: X, Y, Z and a spare word */

/* A word of the machine, without the bus: the copies are not read by the
 * game. */
static uint8_t *at(sw_machine *m, uint16_t a)
{
    return a >= 0x5000 ? &m->mbox.ram[(a - 0x5000) & 0xfff] : &m->ram[(a - 0x4800) & 0x7ff];
}

static uint16_t rd(const sw_machine *m, uint16_t a)
{
    const uint8_t *p = at((sw_machine *)m, a);
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint8_t rd8(const sw_machine *m, uint16_t a)
{
    return *at((sw_machine *)m, a);
}

/* Words [addr, addr + 2 * n) of the scratch machine: on their way from a
 * to b. Things wrap around (the trench, the map of the ground), so it is
 * the signed step that is taken. A step larger than `most` is not a move
 * but a new thing in the old one's place: the words are left as in a, and
 * 0 is returned. */
static int lerp(sw_machine *s, const sw_subframe *f, uint16_t addr, int n, float t, int most)
{
    for (int i = 0; i < n; i++) {
        int d = (int16_t)(uint16_t)(rd(&f->b, (uint16_t)(addr + 2 * i)) - rd(&f->a, (uint16_t)(addr + 2 * i)));
        if (d > most || d < -most) return 0;
    }
    for (int i = 0; i < n; i++) {
        uint16_t a = rd(&f->a, (uint16_t)(addr + 2 * i));
        int d = (int16_t)(uint16_t)(rd(&f->b, (uint16_t)(addr + 2 * i)) - a);
        uint16_t v = (uint16_t)(a + (int)lrintf((float)d * t));
        uint8_t *p = at(s, (uint16_t)(addr + 2 * i));
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
    }
    return 1;
}

/* A signed byte likewise. */
static void lerp8(sw_machine *s, const sw_subframe *f, uint16_t addr, float t)
{
    int a = (int8_t)rd8(&f->a, addr);
    int d = (int8_t)rd8(&f->b, addr) - a;
    *at(s, addr) = (uint8_t)(a + (int)lrintf((float)d * t));
}

/* A set of unit vectors with its place (M$AX to M$TZ around the pointer). */
static int lerp_vectors(sw_machine *s, const sw_subframe *f, uint16_t uvp, float t)
{
    return lerp(s, f, (uint16_t)(uvp + M_AX), 16, t, 0x2000);
}

void sw_subframe_before(sw_subframe *s, const sw_machine *m)
{
    s->a = *m;
    s->valid = 0;
}

void sw_subframe_after(sw_subframe *s, const sw_machine *m)
{
    s->b = *m;
    s->valid = 1;
}

/* The copy is on its own: no 6809, no recording, no sound board, and its
 * own random numbers. */
static void detach(sw_machine *s)
{
    s->original_frame = NULL;
    s->original_call = NULL;
    s->ram_read_hook = NULL;
    s->sound_wait = NULL;
    s->sound_board = 0;
    s->frame_hook = NULL;
    s->prng = NULL;
}

void sw_subframe_next(const sw_subframe *f, sw_machine *s)
{
    *s = f->b;
    detach(s);
    /* The frame has given its buffer up: the next one writes into the
     * other, as the interrupt would hand it over. */
    uint8_t other = f->a.ram[SW_VGSYNC - 0x4800] ? 0x00 : (uint8_t)(SW_VGBFSZ >> 8);
    s->ram[SW_VGSYNC - 0x4800] = other;
    uint16_t start = (uint16_t)((other << 8) | ((SW_VGRAM & 0xff) + SW_VGVARB));
    sw_main_frame(s);
    sw_vg_wr16(s, 0x0000, (uint16_t)(0xe000 | ((start >> 1) & 0x1fff)));
}

int sw_subframe_render(const sw_subframe *f, const sw_machine *live, float t, sw_machine *s)
{
    if (!f->valid) return 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    *s = f->a;
    detach(s);                              /* (its random numbers: the same ones the frame drew) */
    /* What the interrupt keeps in VG RAM (the sight, the laser bolts) as it
     * is now. */
    memcpy(s->vgram, live->vgram, sizeof s->vgram);

    if (t > 0) {
        const sw_machine *a = &f->a, *b = &f->b;
        /* the player: his ship, the view's own place, and what the yoke
         * has turned him by since the last whole step. A frame in which
         * they jump (a phase has begun and put him somewhere else: on the
         * surface, into the trench) is a cut, not a move: no picture is
         * computed from the state before it, which would show the new
         * scene from the old place (render/picture.h shows the frames
         * themselves then). */
        if (!lerp_vectors(s, f, SW_M_S1, t)) return 0;
        if (!lerp(s, f, SW_M_U1 + M_TX, 3, t, 0x2000)) return 0;
        lerp8(s, f, SW_RHEOP + RH_RSD, t);
        lerp8(s, f, SW_RHEOY + RH_RSD, t);
        lerp8(s, f, SW_RHEOR + RH_RSD, t);
        /* the stars: they move with these, and their own places hardly */
        lerp(s, f, SW_ST_UX, 1, t, 0x4000);
        lerp(s, f, SW_ST_UY, 1, t, 0x4000);
        lerp(s, f, SW_ST_UZ, 1, t, 0x4000);
        for (uint16_t x = SW_M_STAD; x < SW_M_STAZ; x += STAR_IZE) {
            /* (one replaced in the frame, having left the view, is a new
             * star in the old one's place: not on its way there, which
             * would streak it across the screen) */
            unsigned i = ((x - SW_M_STAD) / STAR_IZE) & 63;
            if (a->star_gen[i] != b->star_gen[i]) continue;
            lerp(s, f, x, 3, t, 0x1000);
        }
        /* the other ships */
        for (uint16_t x = SW_ALIEN; x < SW_ALIENZ; x += A_IZE) {
            if (rd8(a, x + A_TYP) == 0 || rd8(a, x + A_TYP) != rd8(b, x + A_TYP)) continue;
            if (rd(a, x + A_UVP) != rd(b, x + A_UVP)) continue;
            lerp_vectors(s, f, rd(a, x + A_UVP), t);
        }
        /* shots */
        for (uint16_t x = SW_GUN; x < SW_GUNZ; x += G_IZE) {
            if (rd8(a, x + G_TYP) == 0 || rd8(a, x + G_TYP) != rd8(b, x + G_TYP)) continue;
            if (rd8(a, x + G_MOV) != rd8(b, x + G_MOV) || rd(a, x + G_UVP) != rd(b, x + G_UVP)) continue;
            lerp(s, f, rd(a, x + G_UVP), 3, t, 0x2000);
        }
        /* the pieces of what blew up, and the matrix they tumble in */
        lerp_vectors(s, f, SW_M_MUN, t);
        for (uint16_t x = SW_XPQUE; x < SW_XPQUE + XP_EQ * XP_IZE; x += XP_IZE) {
            if (rd8(a, x + XP_TMR) == 0 || rd8(b, x + XP_TMR) == 0) continue;
            if (rd8(a, x + XP_TYP) != rd8(b, x + XP_TYP)) continue;
            lerp(s, f, x + XP_CX, 3, t, 0x2000);
        }
        /* the proton torpedoes */
        if (rd8(a, SW_PT_LIV) != 0 && rd8(b, SW_PT_LIV) != 0)
            lerp(s, f, SW_PT_X, 3, t, 0x2000);
    }

    uint16_t start = (uint16_t)((s->ram[SW_VGSYNC - 0x4800] << 8) | ((SW_VGRAM & 0xff) + SW_VGVARB));
    if (!sw_main_view(s)) return 0;
    /* as the interrupt starts the vector generator on the buffer */
    sw_vg_wr16(s, 0x0000, (uint16_t)(0xe000 | ((start >> 1) & 0x1fff)));
    return 1;
}
