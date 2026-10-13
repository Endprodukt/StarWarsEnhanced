/* shipview.c - WSMAIN.MAC: the enemy ships and the far Death Star as the
 * player sees them (SNVW, S2VW, VWDTHA).
 *
 * For each ship: where it is in the player's frame (math box PRE2); how
 * near (for the choreography's status flags and the pass-by sounds);
 * whether it is in view; whether the sight is on it (the nearest such ship
 * is the one a laser hits, CL.AP); then its points through the math box
 * and its lines into the display list.
 *
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

#define TVWCL   0x7A08      /* TABLES OF SHIP COLORS, by how much the ship glows (local: S2VW's LDU) */

/* the choreography's status flags (C$xx) */
#define C_PN 0x0400         /* PLAYER IS NEAR THE ALIEN */
#define C_PS 0x0800         /* PLAYER HAS ALIEN IN SITES */
#define C_PV 0x1000         /* PLAYER HAS ALIEN IN VIEW */
#define C_PM 0x2000         /* PLAYER IS MIDDLIN NEAR TO ALIEN */

/* TEMP SIZ,2,OCT,2,XD,2,YD,2 */
#define TMPSIZ 0x4801
#define TMPOCT 0x4803
#define TMPXD  0x4805
#define TMPYD  0x4807

static int lsla_overflows(uint8_t a) { return ((a >> 7) ^ (a >> 6)) & 1; }

static void chset(sw_machine *m, uint16_t x, uint16_t flags)
{
    W16(x + A_CHST, R16(x + A_CHST) | flags);
}

/* S2VW: VIEW THE SHIP in S2.PRM */
static void s2vw(sw_machine *m)
{
    uint16_t x = R16(SW_S2_PRM), d, u;
    W16(SW_M_BIC, (uint8_t)(R8(x + A_BIC) + 3));
    sw_mgowt(m, SW_MSPSB2);

    /* -4000<X<4000 FOR VALID SQUARING */
    if (!lsla_overflows(R8(SW_M_XP)) && !lsla_overflows(R8(SW_M_YP)) && !lsla_overflows(R8(SW_M_ZP))) {
        u = (uint16_t)(R16(SW_M_XPS) + R16(SW_M_YPS) + R16(SW_M_ZPS));     /* CHECK FOR COLLISION */
        if (u <= 0x900) chset(m, x, C_PM);  /* ?PLAYER AT MIDDISTANCE? */
        if (u <= 0x100) chset(m, x, C_PN);  /* ?PLAYER NEARBY? */
        if (u <= 0x0a0) {
            uint8_t a = R8(SW_Q_PBB);
            if (a == 0) {                   /* ?NO SHIP CURRENTLY PASSING BY? */
                W8(SW_Q_PBB, R8(x + A_BIC));        /* THEN START THIS SHIP PASSING BY */
                sw_doxmt(m, SW_IDSELE);     /* ELEPHANT PASSBY */
                sw_doxmt(m, SW_IDAPB);      /* APPROACH OF PASSBY SOUND */
                W16(SW_Q_PBD, u);           /* INIT CURRENT DISTANCE */
            } else if (a == R8(x + A_BIC)) {        /* ?IS THIS THE SHIP CURRENTLY PASSINGBY? */
                uint16_t pbd = R16(SW_Q_PBD);       /* THEN CHECK FOR CHANGES IN DISTANCE AWAY */
                if ((int16_t)u <= (int16_t)pbd) {   /* ?NOT MOVING AWAY YET? NOTE SIGNED FLAG TEST */
                    W16(SW_Q_PBD, u);       /* THEN SAVE THIS LATEST DISTANCE */
                } else if (u >= pbd) {      /* ?MINUS FLAG STILL CLEAR? ONE TIME ONLY */
                    W8(SW_Q_PBD, 0xff);     /* SET DISTANCE TO MINUS, END PASSBY FLAG */
                    sw_doxmt(m, SW_IDAPS);  /* SHORT PASSBY EXIT DOPPLER SOUND */
                }
            }
        } else if (R8(x + A_BIC) == R8(SW_Q_PBB)) {     /* ?WAS THIS THE SHIP DOING A PASSBY? */
            W8(SW_Q_PBB, 0);                /* THEN PREPARE FOR NEXT SHIP */
        }
    }

    d = R16(SW_M_XP);
    if ((int16_t)d <= 0x10) return;
    if (d > 0x7f00) return;
    W16(SW_M_DVD, d);                       /* SAVE NEW X */
    W16(SW_M_XT, d);
    W16(SW_M_YT, R16(SW_M_YP));
    if (R16(SW_M_YPS) >= R16(SW_M_XPS)) return;     /* OUT OF VIEW */
    W16(SW_M_ZT, R16(SW_M_ZP));
    if (R16(SW_M_ZPS) >= R16(SW_M_XPS)) return;

    x = R16(SW_S2_PRM);
    chset(m, x, C_PV);                      /* WITHIN PLAYERS VIEW SCREEN */

    if (R8(SW_Q_RTHV) == 0 && R8(x + A_PIC) == SW_TDSRTH) {     /* ?DARTH NOT YET VISIBLE? ?IS THIS DARTH? */
        W8(SW_Q_RTHV, R8(SW_Q_RTHV) + 1);   /* ONE TIME ONLY */
        if (!(R8(SW_SP_WAV) & 1)) {         /* ?EVEN, SELECT A LEVEL FIRST WAVE? */
            if (R8(SW_P_RND1) & 0x80) sw_doxmt(m, SW_IDSSTA);   /* STAY IN ATTACK FORMATION */
            else sw_doxmt(m, SW_IDSIMO);    /* I'M ON THE LEADER */
        } else {                            /* ELSE ODD, HAVE LOWER ENERGY NOW */
            sw_doxmt(m, SW_IDSHAV);         /* I HAVE YOU NOW */
        }
    }

    W8(SW_M_BICL, R8(x + A_BIC));           /* IS2UV: DOWNLOAD SHIP 2 UNIT VECTORS */
    W8(SW_M_BICH, 0);
    sw_mgowt(m, SW_MSDLD2);
    sw_objcen(m);                           /* SHIP 2 VISIBLE: PROCESS CENTER OF SHIP */

    W16(SW_M_YP, 80);                       /* GET SHIP SIZE */
    sw_mgowt(m, SW_MSPERS);
    W16(TMPSIZ, R16(SW_M_YP) + 10);         /* ADDIN CURSOR SIZE */
    d = sw_absd((uint16_t)(R16(SW_BJ_CX) - R16(SW_LZ_CX)));     /* OBJECT CENTER, LAZAR CENTER */
    W16(TMPXD, d);                          /* SAVE X DISTANCE */
    W16(TMPOCT, d);                         /* PREPARE FOR OCTAGON COLLISION DETECT */
    d = sw_absd((uint16_t)(R16(SW_BJ_CY) - R16(SW_LZ_CY)));     /* NOW CHECKING Y COORDS */
    W16(TMPYD, d);
    W16(TMPOCT, d + R16(TMPOCT));           /* OCTAGON CHECK */
    if ((int16_t)R16(TMPXD) <= (int16_t)R16(TMPSIZ) &&          /* LE FOR LIBERALITY */
        (int16_t)R16(TMPYD) <= (int16_t)R16(TMPSIZ)) {          /* ?WITHIN Y ALSO? */
        d = (uint16_t)((R16(TMPSIZ) >> 1) + R16(TMPSIZ));       /* MAKE 1.5 FOR OCTAGON */
        if ((int16_t)d >= (int16_t)R16(TMPOCT)) {               /* ?ALSO WITHIN OCTAGON? */
            d = R16(SW_M_XT);               /* THEN SEE IF WE ARE THE CLOSEST ALIEN */
            if (d < R16(SW_CL_ADS)) {       /* ?ALSO THE NEAREST SHIP SO FAR? */
                W16(SW_CL_ADS, d);          /* REMEMBER ITS DISTANCE */
                W16(SW_CL_AP, R16(SW_S2_PRM));      /* THEN WE GOT A HIT: RECORD WHO WE ARE */
            }
        }
    }

    /* ?AIM IS NEARING ALIEN? ALLOW LARGER WARNING AREA */
    d = (uint16_t)(R16(TMPSIZ) + R16(TMPSIZ) + R16(TMPSIZ));
    if (d >= R16(TMPOCT)) {
        x = R16(SW_S2_PRM);
        if (R8(x + A_TYP) == 1) chset(m, x, C_PS);      /* ?ALIVE? STATUS: ALIEN IN PLAYER SITES */
    }

    x = R16(SW_S2_PRM);
    VG(R16(TVWCL + (int8_t)(uint8_t)(R8(x + A_GLW) << 1)));     /* GET GLOW COLORS */
    sw_mgowt(m, SW_MSCNCT);
    sw_objbpnt(m, R8(x + A_PIC));           /* PROCESS OBJECT POINTS */
    sw_objdrw(m);                           /* DRAW THE OBJECT LINES */
    VG(SW_VGCNTR);                          /* CENTER */
}

/* SNVW: LOOP THRU SHIPS */
void sw_snvw(sw_machine *m)
{
    uint16_t x = SW_ALIEN;
    do {
        W16(SW_S2_PRM, x);
        if (R8(x + A_TYP) != 0) {           /* ?SOMEWHAT ALIVE? */
            TAG(SW_TAG_OBJECT, (x - SW_ALIEN) / A_IZE);
            s2vw(m);                        /* VIEW THE SHIP */
        }
        x = (uint16_t)(R16(SW_S2_PRM) + A_IZE);     /* NEXT SHIP */
    } while (x < SW_ALIENZ);
}

/* VWDTHA: VIEW DETH STAR, VERSION A (MINIATURE) */
void sw_vwdtha(sw_machine *m)
{
    uint16_t ax = R16(SW_M_U1 + M_AX);      /* X POSITION AT FAR(4000,0,0) DISTANCE */
    if ((int16_t)ax <= 0) return;           /* ?IN FRONT OF PLAYER? */
    W16(SW_M_DVD, ax);                      /* PREPARE DIVIDER,PERSPECTIVE */
    uint16_t d = R16(SW_M_U1 + M_AY);
    W16(SW_M_YP, d);
    if ((int16_t)sw_absd(d) >= (int16_t)ax) return;     /* ?Y WITHIN X FIELD OF VIEW? */
    d = R16(SW_M_U1 + M_AZ);
    W16(SW_M_ZP, d);                        /* ASSUME OK, THEN TEST */
    if ((int16_t)sw_absd(d) >= (int16_t)ax) return;     /* ?Z WITHIN X FIELD OF VIEW? */
    TAG(SW_TAG_DSTAR, 3);
    sw_objcen(m);                           /* THEN PROCESS THE CENTER POINT */
    VG(SW_VGSCAL + 0x100);
    VG(SW_VJBMIN);                          /* GET JSRL TO MINNY BASE OF DETH STAR */
    VG(SW_VGSCAL);                          /* RESTORE BASE */
}

#define TMPSCL 0x4801       /* TEMP SCL,2,VX,2,VY,2 (DTHVW) */
#define TMPVX  0x4803
#define TMPVY  0x4805

/* 100$, 104$ of DTHVW: DRAW TO CENTER, SCALE, THEN DO STAMP. U:=STAMP */
static void dth_stamp(sw_machine *m, uint16_t u, int center)
{
    if (center) {
        VG(R16(TMPVY));                     /* CENTER OF PLANET ON SCREEN */
        VG(R16(TMPVX));
    }
    VG(R16(TMPSCL));                        /* SCALE THE STAMP */
    VG(u);                                  /* VIEW THE STAMP */
}

/* DTHVW: the Death Star at the scale DT.SCL */
static void dthvw(sw_machine *m)
{
    sw_objcen(m);                           /* THEN PROCESS THE CENTER POINT */
    W16(TMPVY, R16(m->y - 4));              /* SAVE CENTER POINT LOCATION */
    W16(TMPVX, R16(m->y - 2));
    W16(TMPSCL, R16(SW_DT_SCL));

    dth_stamp(m, SW_VJBCIR, 0);             /* CIRCLE */
    dth_stamp(m, SW_VJBTRN, 1);             /* TRENCH */
    dth_stamp(m, SW_VJBDSH, 1);             /* DISH */
    dth_stamp(m, SW_VJBNSD, 1);             /* INSIDE */
    dth_stamp(m, SW_VJBFRM, 1);             /* FARM */

    uint16_t d = (uint16_t)(R16(SW_DT_SCL) - 0x0300);   /* SCALE UP THE SIZE OF THE CITY BLOCKS */
    if (d < SW_VGSCAL - 0x200)              /* NO LONGER A SCALE INSTRUCTION */
        d = SW_VGSCAL - 0x200;              /* MAX SIZE */
    W16(TMPSCL, d);
    if ((d >> 8) == (SW_VGSCAL - 0x200) >> 8)   /* BINARY SCALE = 00? */
        VG((((VGCYLW << 8) + SW_VGCOLR) & 0xff00) | 0x60);  /* BRIGHTER FOR BINARY=0 */
    else
        VG((((VGCYLW << 8) + SW_VGCOLR) & 0xff00) | 0x30);  /* CITY LIGHTS */

    if ((R8(SW_SP_WAV) & 0x01) == 0) {
        dth_stamp(m, SW_VJUB1, 1);
    } else {
        dth_stamp(m, SW_VJUB2, 1);          /* CITIES 2,3, AND 4 GO TOGETHER */
        dth_stamp(m, SW_VJUB3, 1);
        dth_stamp(m, SW_VJUB4, 1);
    }
    if ((R8(SW_SP_WAV) & 0x01) == 0) {
        dth_stamp(m, SW_VJLB1, 1);
    } else {
        dth_stamp(m, SW_VJLB2, 1);
        dth_stamp(m, SW_VJLB3, 1);
        dth_stamp(m, SW_VJLB4, 1);
    }
}

/* VWDTHB: VIEW DEATH STAR, VERSION B (WITH SCALING) */
void sw_vwdthb(sw_machine *m)
{
    uint16_t ax = R16(SW_M_U1 + M_AX);      /* X POSITION AT FAR(4000,0,0) DISTANCE */
    if ((int16_t)ax <= 0) return;           /* ?IN FRONT OF PLAYER? */
    W16(SW_M_DVD, ax);                      /* PREPARE DIVIDER,PERSPECTIVE */
    uint16_t d = R16(SW_M_U1 + M_AY);
    W16(SW_M_YP, d);
    if ((int16_t)sw_absd(d) >= (int16_t)ax) return;     /* ?Y WITHIN X FIELD OF VIEW? */
    d = R16(SW_M_U1 + M_AZ);
    W16(SW_M_ZP, d);                        /* ASSUME OK, THEN TEST */
    if ((int16_t)sw_absd(d) >= (int16_t)ax) return;     /* ?Z WITHIN X FIELD OF VIEW? */
    TAG(SW_TAG_DSTAR, 3);
    dthvw(m);
}
