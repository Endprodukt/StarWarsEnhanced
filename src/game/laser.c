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
 * Line by line after the 1983 source; comments in capitals are
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

/* --- the trench -------------------------------------------------------------------- */

#define TMPTX  0x4801       /* TEMP TX,2,TY,2,TZ,2: where the beam ends */
#define TMPTY  0x4803
#define TMPTZ  0x4805
#define TMPMSK 0x4801       /* TEMP MSK,1,FLG,1 */
#define TMPFLG 0x4802
#define LZD    0x80         /* 80 MAX DIAMETER of the laser */
#define LZR    (LZD / 2)    /* RADIUS IS HALF DIAMETER */

/* The splash where the beam ends on the trench: how big by how far. */
static void splash_at(sw_machine *m, uint8_t typ)
{
    W8(SW_LZ_HIT, 0xff);                    /* AT LEAST HIT THE BOTTOM, THE SIDES */
    W8(SW_SP_TYP, typ);
    uint16_t d = (uint16_t)((uint8_t)(R8(TMPTX) - R8(SW_M_U1 + M_TX)) * 3);    /* RELATIVE DISTANCE AWAY */
    W8(SW_SP_LOC, d >= 0x00e0 ? 0xe0 : (uint8_t)d);     /* LIMIT THE SCALE */
    W16(SW_SP_COL, (VGCGRN << 8) + SW_VGCOPC);
}

/* LZHPN: HIT A PANEL */
static void lzhpn(sw_machine *m, uint16_t x)
{
    uint8_t a = R8(TMPMSK) & R8(x);         /* CHECK FOR TYPE OF PANEL */
    if (a == 0) return;                     /* ?ALIVE? */
    W8(TMPFLG, a);                          /* SAVEIT, IS 1,2,3 IN THE SLOT */

    a = (uint8_t)(R8(TMPMSK) << 1) & R8(TMPMSK);    /* GEN A TYPE 2 IN CORRECT SLOT */
    if (a == R8(TMPFLG)) return;            /* ?NOT A FORCE FIELD? */
    W8(SW_LZ_HIT, 4);                       /* STICK HERE AND SHOW A HIT */
    W8(x, R8(x) ^ R8(TMPFLG));              /* GET RID OF PANEL */
    if (a > R8(TMPFLG)) sw_scrpnl(m);       /* MUST HAVE BEEN A 1, I.E. A DECORATIVE PANEL */
    else sw_scrtrt(m);                      /* MUST BE A TURRET: KILLED A TURRET */
    sw_doxmt(m, SW_IDASX);
}

/* LZCPNW: CHECK COLLIDE WITH WIDE WALL PANELS. X: the wall */
static void lzcpnw(sw_machine *m, uint16_t x)
{
    if (R8(SW_LZ_ON) == 0) return;          /* EXIT IF LAZARS NOT ON */
    x += (R8(SW_LZ_PNX) >> 3) & 0x0f;       /* PANEL EVERY 800(16 PANELS DEEP): POINT INTO PANEL FLAG RAM */
    int32_t d = (R16(SW_LZ_PNX) & 0x07ff) - (0x200 - LZR);
    if (d < 0) return;                      /* TOO SHORT ON X */
    if (d - (0x400 + LZD) > 0) return;      /* TOO LONG ON X */
    W8(TMPMSK, 3);                          /* SET UP MASK */
    /* CHECK FOR PROPER WIDTH: LEAST EDGE OF FIRST PANEL */
    uint16_t w = (uint16_t)(R16(SW_LZ_PNW) - (0x80 - LZR));
    do {
        d = (int16_t)w - (0x300 + LZD);     /* GREATER EDGE OF PANEL */
        w = (uint16_t)d;
        if (d <= 0) {
            if ((int16_t)w + (0x300 + LZD) < 0) return;     /* SIZE OF PANEL: MISSED */
            lzhpn(m, x);                    /* HIT A PANEL */
            return;
        }
        w = (uint16_t)(w - (0x100 - LZD));  /* ADVANCE TO NEXT PANEL */
        W8(TMPMSK, R8(TMPMSK) << 1);
        W8(TMPMSK, R8(TMPMSK) << 1);        /* SLIDE MASK FOR NEXT PANEL */
    } while (R8(TMPMSK) != 0);              /* LOOP TILL NO LIVE PANELS */
}

/* A wall of CLBLZ's 10$: the beam has passed the side by D. Where along
 * the wall and how high? */
static void wall_hit(sw_machine *m, uint16_t dvn, uint16_t dvd, uint16_t y, uint8_t typ, uint16_t wall)
{
    W16(SW_M_DVN, dvn);                     /* % OF LINE BENEATH SIDE */
    W16(SW_M_DVD, dvd);                     /* LENGTH OF LINE: UNSIGNED DIVIDE ONLY */
    W16(SW_M_ZP, R16(TMPTZ) - R16(SW_M_U1 + M_TZ));     /* POTENTIAL Z VALUES */
    W16(SW_M_YP, R16(TMPTX) - R16(SW_M_U1 + M_TX));     /* POTENTIAL X VALUES: MISNAMED--DELTA X */
    W16(SW_M_XP, R16(SW_M_QUO));            /* GET THE FRACTION OF LINE: GEN %*DZ,%*DX */
    sw_mgowt(m, SW_MSPERS);
    /* FARTHEST POSSIBLE Z: INTERSECTED NOT QUITE THAT FAR */
    uint16_t d = (uint16_t)(R16(TMPTZ) - R16(SW_M_ZP));
    if (!(d & 0x8000)) {
        if ((int16_t)d > 0) return;         /* ?BENEATH TOP? IF OUTSIDE THEN STOP TRYING */
    } else {
        if ((int16_t)d < -0x1000) return;   /* ?ABOVE BOTTOM? */
    }
    W16(TMPTZ, d);                          /* Z: ACTUAL END OF LAZAR ON SIDE */
    W16(SW_LZ_PNW, d + 0x1000);             /* BASE IT AT ZERO: WIDTH */
    d = (uint16_t)(R16(TMPTX) - R16(SW_M_YP));  /* MISNAMED: X OFFSET */
    W16(TMPTX, d);                          /* COLLIDED X POSITION */
    W16(SW_LZ_PNX, d);                      /* X DEPTH */
    W16(TMPTY, y);                          /* Y POSITION: the SIDE OF TRENCH */
    splash_at(m, typ);
    lzcpnw(m, wall);                        /* ?COLLIDE WITH PANELS? */
}

/* 10$ of CLBLZ: where the beam ends in the trench */
static void trench_beam(sw_machine *m)
{
    W16(TMPTX, 0x7000 + R16(SW_M_U1 + M_TX));       /* FARTHEST FORWARD POINT */
    /* LEFT/RIGHT: (8X-1X) FOR 7000 MAX */
    uint16_t d = (uint16_t)((int16_t)(uint16_t)(0 - R16(SW_LZ_RSX)) >> 3);
    W16(TMPTY, d + R16(SW_LZ_RSX) + R16(SW_M_U1 + M_TY));
    /* UP/DOWN: (8Y-1Y) FOR 7000 MAX VALUE */
    d = (uint16_t)((int16_t)(uint16_t)(0 - R16(SW_LZ_RSY)) >> 3);
    W16(TMPTZ, d + R16(SW_LZ_RSY) + R16(SW_M_U1 + M_TZ));

    /* *** BE SURE TO RESTORE DIVIDER UPON EXIT */
    int32_t below = -0x1000 - (int16_t)R16(TMPTZ);  /* ?PASSED BOTTOM OF TRENCH? KEEP RESULT POSITIVE */
    if (below >= 0) {
        W16(SW_M_DVN, (uint16_t)below);     /* % OF LINE BENEATH BOTTOM */
        W16(SW_M_DVD, R16(SW_M_U1 + M_TZ) - R16(TMPTZ));    /* OLD TZ: LENGTH OF LINE, UNSIGNED DIVIDE ONLY */
        W16(SW_M_YP, R16(TMPTY) - R16(SW_M_U1 + M_TY));     /* POTENTIAL Y VALUES */
        W16(SW_M_ZP, R16(TMPTX) - R16(SW_M_U1 + M_TX));     /* NEW-OLD:MAX RANGE: MISNAMED--DELTA X */
        W16(SW_M_XP, R16(SW_M_QUO));        /* GET THE FRACTION OF LINE: GEN %*DY,%*DX */
        sw_mgowt(m, SW_MSPERS);
        /* FARTHEST POSSIBLE NEW Y: INTERSECTED NOT QUITE THAT FAR */
        d = (uint16_t)(R16(TMPTY) - R16(SW_M_YP));
        /* ?WITHIN RIGHT SIDE? ?WITHIN LEFT SIDE? IF OUTSIDE THEN TRY SIDES */
        if ((d & 0x8000) ? (int16_t)d >= -0x400 : (int16_t)d <= 0x400) {
            W16(TMPTY, d);                  /* Y: ACTUAL END OF LAZAR ON BOTTOM */
            d = (uint16_t)(R16(TMPTX) - R16(SW_M_ZP));      /* NEW-%(NEW-OLD): MISNAMED: X OFFSET */
            W16(TMPTX, d);                  /* COLLIDED X POSITION */
            W16(SW_LZ_PNX, d);              /* X DEPTH */
            W16(TMPTZ, (uint16_t)-0x1000);  /* BOTTOM OF TRENCH */
            splash_at(m, 0x01 + 0x02);      /* UPPER HALF */

            if (R8(SW_BS_PFL) != 0 &&       /* ?EXHAUST PORT ON? */
                R8(SW_PT_LZF) == 0) {       /* ?PROTON TORP STILL PRIMED? */
                int32_t e = (int16_t)R16(TMPTY) + 0x200;
                if (e >= 0 && (int16_t)(uint16_t)e - 0x400 <= 0) {  /* ?WITHIN LEFT EDGE? ?WITHIN RIGHT EDGE? */
                    /* GET DELTA FROM PORTHOLE LOC */
                    e = (int16_t)(uint16_t)(R16(TMPTX) - R16(SW_BS_PLC)) + 0x200;
                    if (e >= 0 && (int16_t)(uint16_t)e - 0x400 <= 0)    /* ?ABOVE BOTTOM EDGE? ?BELOW TOP EDGE? */
                        W8(SW_PT_LZF, 1);   /* SET LAZAR FIRE FLAG */
                }
            }
            return;                         /* EXIT DONE WITH BOTTOM */
        }
    }

    /* ---SIDE CHECK */
    d = R16(TMPTY);
    if (d & 0x8000) {                       /* ---CHECK LEFT SIDE COLLISION */
        int32_t past = -0x400 - (int16_t)d; /* ?PASSED LEFT SIDE OF TRENCH? KEEP RESULT POSITIVE */
        if (past < 0) return;               /* NOT YET PASSED SIDE */
        wall_hit(m, (uint16_t)past, (uint16_t)(R16(SW_M_U1 + M_TY) - R16(TMPTY)),
                 (uint16_t)-0x400, 0x01 + 0x08, SW_PNLW);   /* RIGHT HALF; LEFT WALL PANELS */
    } else {                                /* ---CHECK RIGHT SIDE COLLISION */
        int32_t past = (int16_t)d - 0x400;  /* ?PASSED RIGHT SIDE OF TRENCH? */
        if (past < 0) return;
        wall_hit(m, (uint16_t)past, (uint16_t)(R16(TMPTY) - R16(SW_M_U1 + M_TY)),
                 0x400, 0x02 + 0x04, SW_PNRW);              /* LEFT HALF; RIGHT WALL */
    }
}

/* CLBLZ: COLLIDE WITH LAZAR, BASE STAR TRENCH */
void sw_clblz(sw_machine *m)
{
    if (R8(SW_LZ_ON) == 0) return;          /* ?LAZARS ON? */

    trench_beam(m);
    W16(SW_M_DVN, 0x200);                   /* AND IN TYPICAL NON-STRUCTURED MANNER, RESTORE DIVIDER */

    if (!(R16(SW_CL_GDS) & 0x8000)) {       /* CHECK THE GUNS ALSO: ?GOT A GUN? */
        W8(SW_LZ_HIT, 4);
        sw_gnhtsg(m);                       /* ALWAYS HIT A GUN SINCE NO DISTANCE CLUE */
    }

    if ((int8_t)R8(SW_PT_LZF) > 0) {        /* ?LAZAR GOT CLOSE ENUF TO FIRE PROTON TORPS? */
        W8(SW_PT_LZF, 0xff);                /* MARK THAT PROTON TORP HAS FIRED */
        sw_frptgn(m);                       /* THEN LAUNCH DIRECT HIT PROTON TORPS */
    }
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
