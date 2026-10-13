/* laser.c - WSLAZR.MAC: the X-wing's lasers.
 *
 * The interrupt notes a pull of a trigger (VG.LON); TSTLAZ turns that
 * into eight frames of fire and latches where the sight was. VWLAZ draws
 * a pair of beams (left and right pair in turn) from the guns to that
 * point in halving steps, and at the end the hit or the splash. The
 * collision routines pick what the beam hit from what the view routines
 * found under the sight (CL.xDS: the distances of the nearest ship, shot,
 * bunker, tower).
 *
 * Ported so far: space and ground. The trench (CLBLZ, LZCPNW) follows with
 * its phase. Line by line after the 1983 source; comments in capitals are
 * the original's. */
#include "game/game.h"
#include "game/m6809.h"

#define TLZFR  0xB04F       /* COLORS FOR LAZAR FIRE (local: LAZVW's LDU) */
#define TMPDX  0x4801       /* TEMP DX,2,DY,2 */
#define TMPDY  0x4803

/* TSTLAZ: TEST IF LAZARS SHOULD BE ON IN MAINLINE.
 * LZ.HIT >0 ==> STICK LASAR ONTO HIT FOR A WHILE, <0 ==> GLANCED OFF */
void sw_tstlaz(sw_machine *m)
{
    if ((int8_t)R8(SW_LZ_HIT) > 0) {        /* CHECK IF A HIT HAS OCCURRED LAST FRAME */
        W8(SW_LZ_HIT, R8(SW_LZ_HIT) - 1);   /* TIME IT OUT */
        W8(SW_LZ_EDG, 0);                   /* LAZAR FREEZES IN POSITION */
        W8(SW_LZ_ON, 0);                    /* AND NO SHOW REGULAR LAZARS */
    } else {
        W8(SW_LZ_HIT, 0);
    }
    W8(SW_LZ_ON, 0);                        /* AND STOP ALL LAZARS */

    if (R8(SW_VG_LON) != 0) {               /* GET ON SWITCH FROM IRQ */
        W8(SW_LZ_ALT, R8(SW_LZ_ALT) + 1);   /* EDGED ALTERNATION */
        W8(SW_LZ_HIT, 0);                   /* STOP THE HIT PICTURE, ALLOW SWEEPING LAZARS */
        W8(SW_LZ_EDG, 8);
        (void)R8(SW_VG_LON);                /* (CLR reads first) */
        W8(SW_VG_LON, 0);                   /* PREPARE IRQ FOR NEXT MAINLINE */
    }
    uint8_t a = R8(SW_LZ_EDG);
    if ((int8_t)a > 0) {                    /* ?HAVE AN EDG TO DETECT? */
        W8(SW_LZ_EDG, a - 1);
        W8(SW_LZ_ON, a);                    /* TURN ON LAZARS TOO */
        W16(SW_LZ_RSX, R16(SW_VG_RSX));     /* 16 BIT REAL SITE VALUE, FOR LAZAR */
        W16(SW_LZ_RSY, R16(SW_VG_RSY));
        W16(SW_LZ_CX, R16(SW_VG_CX));       /* 10 BIT CURRENT SITE VALUE, FOR LAZAR */
        W16(SW_LZ_CY, R16(SW_VG_CY));
    }

    if (R8(SW_LZ_EDG) == 8 - 1) {           /* CHEAP DETECT OF EVERY EDGE */
        if (R8(SW_Q_FRC) == 0)              /* ?WAS TRYING TO USE THE FORCE? */
            W8(SW_Q_FRC, 0xff);             /* THEN NO MORE USING THE FORCE */
        sw_doxmt(m, SW_IDAXL);              /* START XWING LASAR */
    }

    W8(SW_CL_ADS, 0xff);                    /* SETUP FOR COLLISION, NONE YET: ALIEN DISTANCE NEGATIVE */
    W8(SW_CL_BDS, 0xff);                    /* BUNKER */
    W8(SW_CL_GDS, 0xff);                    /* GUN */
    W8(SW_CL_TDS, 0xff);                    /* TOWER */
}

/* Half of what is left to the sight, rounded up when positive; the rest
 * stays in the temporary. */
static uint16_t half_step(sw_machine *m, uint16_t tmp)
{
    uint16_t d = R16(tmp);                  /* CURRENT DELTA TO SITE */
    if (!(d & 0x8000)) d++;                 /* ROUND POSITIVE UP */
    d = (uint16_t)((int16_t)d >> 1);        /* DIVIDE BY 2 */
    W16(m->y, d);                           /* TEMP SAVE */
    W16(tmp, (uint16_t)(R16(tmp) - d));     /* REDUCE ORIGINAL APPROPRIATE AMOUNT */
    return R16(m->y);
}

/* How far off the sight still (TMPDX or TMPDY): outside -8..8? */
static int far_off(sw_machine *m, uint16_t tmp)
{
    int32_t s = (int16_t)R16(tmp) + 8;
    if (s < 0) return 1;
    return (int16_t)(uint16_t)s - 16 > 0;
}

/* LAZVW: VECTOR TO LAZAR SITE AND DISPLAY. B: OFFSET INTO COLORS */
static void lazvw(sw_machine *m, uint8_t b)
{
    uint16_t u = (uint16_t)(TLZFR + b);     /* FULL FIRE COLORS */
    if ((int8_t)R8(SW_LZ_HIT) > 0) {        /* ?HIT SOMETHING IMPORTANT? */
        /* SPREAD OUT HIT COUNT INTO FULL BRITE: USED LAZAR GOES BRITE TO DIM */
        VG(((((VGCTRQ << 8) + SW_VGCOPC) >> 8) << 8) | (uint8_t)(R8(SW_LZ_HIT) * 0x3f));
    } else {                                /* ELSE DRAW LAZAR LINES */
        do {
            VG(R16(u)); u += 2;             /* PULL THE APPROPRIATE COLOR */
            uint16_t d = half_step(m, TMPDY);
            VG(d & 0x1fff);                 /* AND VECTORIZE Y */
            d = half_step(m, TMPDX);
            VG(d | 0xe000);                 /* DEFAULT BRITE: AND VECTORIZE X */
        } while (far_off(m, TMPDX) || far_off(m, TMPDY));   /* KEEP GOING TILL WE ARRIVE AT SITE */
        VG(R16(u));                         /* SETUP APPROPRIATE COLOR */
    }
    /* DISPLAY REST OF LAZAR LINE */
    VG(R16(TMPDY) & 0x1fff);
    VG(R16(TMPDX) | 0xe000);                /* BRITE */

    int8_t hit = (int8_t)R8(SW_LZ_HIT);
    if (hit > 0) {                          /* ?HIT SOMETHING? */
        VG(SW_VJLZS);                       /* SHOW LAZAR SPLASH */
    } else if (hit < 0) {                   /* ?OR SPLASHING AGAINST SOMETHING? */
        VG(((((SW_VGSCAL - 0x100) & 0xff00) >> 8) << 8) | R8(SW_SP_LOC));   /* X DISTANCE, USE TO SCALE */
        VG(R16(SW_SP_COL));                 /* COLOR, DEPENDS ON WHAT WAS HIT */
        uint8_t q = R8(SW_SP_TYP);          /* QUADRANT DATA */
        if (q & 0x01) VG(SW_VJSPUR);        /* UP-RIGHT */
        if (q & 0x02) VG(SW_VJSPUL);        /* UP-LEFT */
        if (q & 0x04) VG(SW_VJSPDL);        /* DOWN-LEFT */
        if (q & 0x08) VG(SW_VJSPDR);        /* DOWN-RIGHT */
    }
    VG(SW_VGSCAL);                          /* RESTORE SCALE */
    VG(SW_VGCNTR);                          /* CENTER */
}

/* One beam: from the gun at (x0, y0) on the screen, shifted with the guns. */
static void beam(sw_machine *m, uint16_t y0, uint16_t dy_base, uint16_t x0, uint8_t colors, int key)
{
    TAG(SW_TAG_LASER, key);
    VG((y0 + R16(SW_LZ_GY)) & 0x1fff);      /* Y COORD FOR START; GUNS SHIFT AROUND */
    W16(TMPDY, R16(SW_LZ_CY) - dy_base - R16(SW_LZ_GY));   /* DELTA LEFT TO DRAW */
    VG((x0 + R16(SW_LZ_GX)) & 0x1fff);      /* X COORD FOR START */
    W16(TMPDX, R16(SW_LZ_CX) - x0 - R16(SW_LZ_GX));
    lazvw(m, colors);
}

/* VWLAZ: VIEW ANY LASARS */
void sw_vwlaz(sw_machine *m)
{
    if ((R8(SW_LZ_ON) | R8(SW_LZ_HIT)) == 0) return;    /* ?LAZARS OFF? ?OR HIT IN PROGRESS? */
    uint16_t low = (uint16_t)(SW_VGLIMB - SW_VGOFFY + 35);
    if (R8(SW_LZ_ALT) & 0x01) {             /* FIRE SWITCH EDGE? YES, FIRE LEFT LAZARS */
        beam(m, SW_VGOFFY + 0, 0, SW_VGLIML + 90, 0, 0);    /* UPPER LEFT */
        beam(m, SW_VGLIMB + 35, low, SW_VGLIML + 85, 8, 1); /* LOWER LEFT */
    }
    if ((R8(SW_LZ_ALT) & 0x01) == 0) {      /* SHOOT THEM WITH THE RIGHT SIDE */
        beam(m, SW_VGLIMB + 35, low, SW_VGLIMR - 85, 0, 2); /* LOWER RIGHT */
        beam(m, SW_VGOFFY + 0, 0, SW_VGLIMR - 90, 8, 3);    /* UPPER RIGHT */
    }
}

/* HTSG: SPACE LAZAR HIT ALIEN GUN */
static void htsg(sw_machine *m)
{
    W8(SW_LZ_HIT, 4);                       /* DISPLAY HIT FOR A WHILE */
    sw_gnhtsg(m);                          /* GUNS, HIT SPACE GUN */
}

/* HTSA: SPACE LAZAR HIT ALIEN SHIP */
static void htsa(sw_machine *m)
{
    W8(SW_LZ_HIT, 4);
    sw_cphtsa(m);                           /* CPU, HIT SPACE ALIEN */
}

/* CLSLZ: COLLIDE WITH SPACE LASAR */
void sw_clslz(sw_machine *m)
{
    if (R8(SW_LZ_ON) == 0) return;          /* ?ARE LAZARS ON? */
    uint16_t d = R16(SW_CL_GDS);            /* CHECK ALIEN GUNS FIRST */
    if (!(d & 0x8000)) {                    /* ?VALID GUN POSSIBILITY? */
        if (d < R16(SW_CL_ADS)) htsg(m);    /* IF CLOSER, THEN HIT THE GUN */
        else htsa(m);                       /* ELSE VALID ALIEN IS CLOSER */
        return;
    }
    if (!(R8(SW_CL_ADS) & 0x80)) htsa(m);   /* IF NO GUN, CHECK ALIEN: IF ALIEN VALID, HIT HIM */
}

/* CLGLZ: COLLIDE WITH GROUND LAZAR */
void sw_clglz(sw_machine *m)
{
    if (R8(SW_LZ_ON) == 0) return;
    if (!(R16(SW_CL_GDS) & 0x8000)) {       /* CHECK THE GUNS FIRST: ?GOT A GUN? */
        W8(SW_LZ_HIT, 4);
        sw_gnhtsg(m);                      /* ALWAYS HIT A GUN SINCE NO DISTANCE QUEUE */
    }
    /* CLGA: THEN CHECK FOR ALIENS NEXT */
    uint16_t d = R16(SW_CL_ADS);
    if (!(d & 0x8000) && d <= R16(SW_CL_BDS)) {
        if (d <= R16(SW_CL_TDS)) {
            W8(SW_LZ_HIT, 4);
            sw_cphtsa(m);
            return;
        }
    } else {
        /* CLGB: BUNKER TEST */
        d = R16(SW_CL_BDS);
        if (!(d & 0x8000) && d <= R16(SW_CL_TDS)) {
            W8(SW_LZ_HIT, 4);
            ORIG(SW_GDHTGB);
            return;
        }
    }
    /* CLGT */
    if (!(R16(SW_CL_TDS) & 0x8000)) {       /* ?VALID TOWER? */
        W8(SW_LZ_HIT, 0xff);                /* NO HIT, BUT SPLASH AGAINST WALL */
        W8(SW_SP_TYP, 0x0f);                /* FULL SURFACE */
        uint16_t s = (uint16_t)(R8(SW_CL_TDS) * 3);     /* RELATIVE DISTANCE AWAY */
        W8(SW_SP_LOC, s >= 0x00e0 ? 0xe0 : (uint8_t)s); /* LIMIT THE SCALE */
        W16(SW_SP_COL, (VGCYLW << 8) + SW_VGCOPC);
    }
}
