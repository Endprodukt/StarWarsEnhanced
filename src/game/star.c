/* star.c - WSSTAR.MAC: the star field.
 *
 * Fifty stars live in the math box RAM as universe coordinates (M.STAD).
 * Each frame every star is turned into the player's view (math box PRE2,
 * then perspective); one that is behind, too close, too far or out of view
 * is replaced by a new one ahead. The visible ones are sorted into 16
 * screen sectors so the beam travels little, and drawn as relative
 * vectors. Line by line after the 1983 source; comments in capitals are
 * the original's. */
#include "game/game.h"
#include "game/m6809.h"

/* star queue per sector */
#define ST_QTL 0      /* POINTS TO TAIL */
#define ST_QHD 2      /* POINTS TO HEAD OF QU (FIFO) */
#define ST_QCX 4      /* CURRENT X VG POSITION */
#define ST_QCY 6      /* CURRENT Y FOR RELATIVE VG CALCS */
#define ST_IZQ 8
/* vectorized star points */
#define ST_PP  0      /* FORWARD POINTER */
#define ST_PX  2      /* VECTOR X COMPONENT */
#define ST_PY  4      /* VECTOR Y COMPONENT */
#define ST_IZP 6

/* STARNW: a new star somewhere ahead, on the side opposite to where the
 * old one left. */
static void starnw(sw_machine *m, uint16_t x)
{
    sw_rnd8(m);                             /* SET UP VISUAL X,Y,Z */
    uint8_t a = R8(SW_RND_S1) & 0x1f;
    uint8_t b = R8(SW_RND_S2);
    W16(x, (uint16_t)((a << 8) | b));
    uint16_t d = (uint16_t)(a * b);
    if (!(R8(SW_M_YPH) & 0x80))             /* SELECT SIGN OPPOSITE OF CURRENT */
        d = (uint16_t)(((uint8_t)-(d >> 8) << 8) | (d & 0xff));
    W16(x + 2, d);
    d = (uint16_t)((R8(SW_RND_S1) & 0x1f) * R8(SW_RND_S3));
    if (!(R8(SW_M_ZPH) & 0x80))
        d = (uint16_t)(((uint8_t)-(d >> 8) << 8) | (d & 0xff));
    W16(x + 4, d);
    W8(SW_M_BICL, R8(SW_ST_BIC));           /* REDO CURRENT STAR */
    sw_mgowt(m, SW_MSS1UN);                 /* DO POST ADD */
    W16(x, R16(SW_M_XP));                   /* STORE UNIVERSE COORDINATES */
    W16(x + 2, R16(SW_M_YP));
    W16(x + 4, R16(SW_M_ZP));
    /* BIC ALREADY UPDATED BY PREVIOUS OP */
}

/* STRNWG: a new star on the ground, far ahead. */
static void strnwg(sw_machine *m, uint16_t x)
{
    sw_rnd8(m);
    uint8_t a = (R8(SW_RND_S1) & 0x7f) | 0x70;      /* FORCE IT WAY OUT THERE */
    uint8_t b = R8(SW_RND_S2);
    uint16_t d = (uint16_t)(((a << 8) | b) + R16(SW_M_U1 + M_TX));
    W16(x, d);
    b = (uint8_t)d;                         /* B keeps the sum's low byte */
    a = R8(SW_RND_S3) & 0x7f;
    if (!(R8(SW_M_YPH) & 0x80)) a = (uint8_t)-a;    /* SELECT SIGN OPPOSITE OF CURRENT */
    W16(x + 2, (uint16_t)(((a << 8) | b) + R16(SW_M_U1 + M_TY)));
    W16(x + 4, 0);                          /* ON THE GROUND */
}

static void view_stars(sw_machine *m, int ground)
{
    uint16_t x, u, d;

    if (!ground) {                          /* STARS RELATIVE MOVEMENT */
        W16(SW_M_U1 + M_TX, R16(SW_ST_UX));
        W16(SW_M_U1 + M_TY, R16(SW_ST_UY));
        W16(SW_M_U1 + M_TZ, R16(SW_ST_UZ));
    }

    for (x = SW_ST_QU; x < SW_ST_QUZ; x += 2) W16(x, 0);    /* CLEAR OUT STAR QUE */

    W8(SW_ST_GNB, 0);                       /* NO STAR TO GEN YET */
    TAG(SW_TAG_STARSEC, 0x3ff);
    if (!ground) VG((((VGCWHT << 8) + SW_VGCOPC) & 0xff00) | R8(SW_ST_BRT));   /* INCLUDE OUTSIDE BRITENESS */
    else VG((VGCGRN << 8) + SW_VGCOPC);
    W16(SW_M_BIC, SW_MSSTBC);
    W8(SW_ST_BIC, SW_MSSTBC & 0xff);
    W8(SW_ST_CNT, (uint8_t)(-(SW_MSSTBC & 0xff) + (SW_MSSTBZ & 0xff)));   /* ZBIC-START= BIC GIVES COUNT OF STARS */
    u = SW_ST_PNT;                          /* BEGIN OF CONVERTED STAR POINTS */
    x = SW_M_STAD;                          /* STAR ADDRESS */
    do {
        W16(SW_ST_RAM, x);                  /* MATH RAM POINTER */
        sw_mgowt(m, SW_MSPSB2);
        d = R16(SW_M_XP);
        if ((int16_t)d <= 0x100) goto renew;            /* ?TOO CLOSE OR IN BACK? THEN NEW STAR */
        if (!ground && d > 0x0fff) goto renew;          /* ?TOO FAR IN FRONT OR IN BACK? */
        W16(SW_M_DVD, d);
        if (R16(SW_M_YPS) >= R16(SW_M_XPS)) goto renew; /* ?WIDTH IN VIEW ? ELSE BEYOND VIEW */
        if (R16(SW_M_ZPS) >= R16(SW_M_XPS)) goto renew; /* ?HITE IN VIEW ? */
        W16(SW_M_XP, R16(SW_M_QUO));
        sw_mgowt(m, SW_MSPERS);
        {
            uint8_t b = 0, a;               /* NO SECTOR YET */
            a = R8(SW_M_ZP);
            b = (uint8_t)((b << 1) | (a & 1)); a >>= 1;     /* EVERY 256 Y */
            b = (uint8_t)((b << 1) | (a & 1));              /* SIDE FOR +-512. */
            a = R8(SW_M_YP);
            b = (uint8_t)((b << 1) | (a & 1)); a >>= 1;     /* EVERY 256 X */
            b = (uint8_t)((b << 1) | (a & 1));
            x = (uint16_t)(SW_ST_QU + ST_IZQ * b);          /* X-->QUE BLOCK */
        }
        if (R16(x + ST_QHD) == 0) W16(x + ST_QHD, u);   /* ?FIRST IN THIS QU? POINT TO FIRST STAR */
        else W16(R16(x + ST_QTL), u);       /* INITS FORWARD POINTER IN LAST (OR ONLY) ENTRY */
        W16(x + ST_QTL, u);                 /* TAIL POINTS TO LAST (OR ONLY) ENTRY */
        W16(u + ST_PP, 0);                  /* LAST ENTRY HAS ZERO POINTER */

        W16(u + ST_PY, (R16(SW_M_ZP) - R16(x + ST_QCY)) & 0x1fff);     /* Y LONG VECTOR, RELATIVE OFFSET */
        W16(u + ST_PX, (R16(SW_M_YP) - R16(x + ST_QCX)) & 0x1fff);     /* X LONG VECTOR */
        W16(x + ST_QCY, R16(SW_M_ZP));      /* NEW Y ABSOLUTE */
        W16(x + ST_QCX, R16(SW_M_YP));      /* NEXT X ABSOLUTE */

        m->star_of[((u - SW_ST_PNT) / ST_IZP) & 63] = (uint8_t)(((R16(SW_ST_RAM) - SW_M_STAD) >> 3) & 63);
        u += ST_IZP;                        /* NEXT STAR ENTRY */
        m->n_shown++;
        goto next;
renew:
        x = R16(SW_ST_RAM);
        m->n_renew++;
        m->star_gen[((x - SW_M_STAD) >> 3) & 63]++;
        if (!ground) starnw(m, x);
        else strnwg(m, x);
next:
        W8(SW_ST_BIC, R8(SW_ST_BIC) + 1);
        x = (uint16_t)(R16(SW_ST_RAM) + 8);
        W8(SW_ST_CNT, R8(SW_ST_CNT) - 1);
    } while (R8(SW_ST_CNT) != 0);

    /* DISPLAY QUEUED STARS BY SECTOR LOCATION */
    for (x = SW_ST_QU; x < SW_ST_QUZ; x += ST_IZQ) {
        u = R16(x + ST_QHD);
        if (u == 0) continue;               /* ?STARS IN THIS SECTOR? */
        TAG(SW_TAG_STARSEC, (x - SW_ST_QU) / ST_IZQ);
        VG(SW_VGOFFY & 0x1fff);
        VG(0 & 0x1fff);                     /* OFFSET X */
        do {
            unsigned star = m->star_of[((u - SW_ST_PNT) / ST_IZP) & 63];
            TAG(SW_TAG_STAR, star | ((m->star_gen[star] & 15) << 6));
            VG(R16(u + ST_PY));             /* Y PORTION */
            VG(R16(u + ST_PX));             /* X PORTION */
            VG(R16(SW_VGSTAR));             /* DRAW STAR SHAPE */
            u = R16(u + ST_PP);             /* POINTER TO NEXT STAR */
        } while (u != 0);
        TAG(SW_TAG_STARSEC, (x - SW_ST_QU) / ST_IZQ);
        VG(SW_VGCNTR);                      /* CENTER AFTER SECTOR OF STARS */
    }

    if (!ground) {                          /* RESTORE POSITION */
        W16(SW_M_U1 + M_TX, R16(SW_M_S1 + M_TX));
        W16(SW_M_U1 + M_TY, R16(SW_M_S1 + M_TY));
        W16(SW_M_U1 + M_TZ, R16(SW_M_S1 + M_TZ));
    }
}

void sw_vwstar(sw_machine *m) { view_stars(m, 0); }
void sw_vwstrg(sw_machine *m) { view_stars(m, 1); }
