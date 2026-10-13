/* splash.c - TCSPLS.MAC: the laser splash, four quadrants of two random
 * short-vector strokes each, rebuilt at every VG restart. */
#include "game/game.h"

#include <string.h>

/* .SPLSH XSTART,YSTART,XEND,YEND (upper-right quadrant, 4-bit positive):
 * a blank short vector to the start, the line (Z=7), a blank one back to
 * the center. MACRO-11 evaluates left to right: <YEND-YSTART/2.&^H0F> is
 * ((YEND-YSTART)/2)&0F. */
#define SPLSH(xs, ys, xe, ye)                                              \
    0x40 + (((ys) / 2) & 0x0f), ((xs) / 2) & 0x0f,                         \
    0x40 + ((((ye) - (ys)) / 2) & 0x0f), 0xe0 + ((((xe) - (xs)) / 2) & 0x0f), \
    0x40 + ((-(ye) / 2) & 0x1f), (-(xe) / 2) & 0x1f

static const uint8_t TSPLSH[16 * 6] = {
    SPLSH(0, 4, 2, 30),   SPLSH(2, 12, 4, 24),  SPLSH(4, 18, 6, 24),  SPLSH(2, 6, 8, 30),
    SPLSH(2, 4, 10, 20),  SPLSH(4, 6, 16, 22),  SPLSH(8, 10, 12, 16), SPLSH(10, 12, 18, 22),
    SPLSH(4, 4, 16, 16),  SPLSH(10, 8, 20, 16), SPLSH(6, 4, 30, 20),  SPLSH(4, 2, 24, 12),
    SPLSH(6, 2, 18, 6),   SPLSH(16, 4, 24, 6),  SPLSH(12, 2, 24, 4),  SPLSH(4, 0, 30, 2),
};

static const uint8_t *rndvec(sw_machine *m)
{
    return &TSPLSH[(sw_rd8(m, SW_P_RND) & 0x0f) * 6];
}

/* One random stroke at u, mirrored as the quadrant needs. */
static uint16_t quad(sw_machine *m, uint16_t u, int neg_x, int neg_y)
{
    const uint8_t *x = rndvec(m);
    for (int i = 0; i < 3; i++) {
        uint8_t a = x[i * 2], b = x[i * 2 + 1];
        if (neg_y) a = (uint8_t)(((uint8_t)-a & 0x1f) | 0x40);   /* negative Y, still SVEC */
        if (neg_x) {
            b = (uint8_t)-b;
            b = (i == 1) ? (uint8_t)(b | 0xe0) : (uint8_t)(b & 0x1f);   /* Z 7 on the line */
        }
        sw_vg_wr16(m, u, (uint16_t)((a << 8) | b));
        u += 2;
    }
    return u;
}

void sw_splash(sw_machine *m)
{
    static const struct { uint16_t at; int nx, ny; } Q[4] = {
        { SW_VRSPUR, 0, 0 }, { SW_VRSPUL, 1, 0 }, { SW_VRSPDL, 1, 1 }, { SW_VRSPDR, 0, 1 },
    };
    for (int q = 0; q < 4; q++) {
        uint16_t u = quad(m, Q[q].at, Q[q].nx, Q[q].ny);
        u = quad(m, u, Q[q].nx, Q[q].ny);
        sw_vg_wr16(m, u, SW_VGRTSL);
    }
}

int sw_splash_verify(const sw_machine *m)
{
    /* The table follows the routine (SPLASH at D660). */
    const uint8_t *r = m->rom->main;
    for (uint32_t a = SW_SPLASH; a + sizeof TSPLSH <= 0x10000 && a < SW_SPLASH + 0x200; a++)
        if (memcmp(r + a, TSPLSH, sizeof TSPLSH) == 0) return 0;
    return -1;
}
