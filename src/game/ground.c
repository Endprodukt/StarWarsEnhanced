/* ground.c - WSGRND.MAC: the surface of the Death Star with its towers
 * and bunkers.
 *
 * A wave's ground is a "maze": a list of up to 32 buildings (place on the
 * map, kind, the pass in which it appears, a byte of RAM for its state).
 * The map wraps every 8000 forward, and with every pass (GD.SEQ) more of
 * it is switched on. Kinds: a tower with a bunker as its hat (shoots
 * forward and to both sides), a "bishop" (to both sides) and a lone
 * bunker. The laser can take the hat off a tower or a bunker out; the
 * tower's stub stays and can only be flown around.
 *
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

/* a building's state (GD$TYP, a byte in GDRAM) */
#define TYP_LV 0x01         /* ALIVE */
#define TYP_VW 0x02         /* SETUP TO VIEW THIS BUILDING */
#define TYP_DM 0x04         /* DAMAGED -- NO GUNS */
#define TYP_AM 0x08         /* AIM SAYS TO SHOOT SOON */
#define TYP_BK 0x10         /* BUNKER (OR HAT ON TOWERS) IS VISIBLE */

/* a building in the maze (TGD$xx) */
#define TGD_X  0            /* X LOC */
#define TGD_Y  2            /* Y LOC */
#define TGD_PC 4            /* PICTURE TYPE */
#define TGD_SQ 5            /* SEQUENCE TO TURN ON */
#define TGD_RM 6            /* RAM OFFSET */
#define TGD_IZ 7

#define PC_TWR 1            /* TOWER PICTURE */
#define PC_BSH 2            /* BISHOP PICTURE TYPE */
#define PC_BNK 3            /* BUNKER */

/* local tables (addresses from the program ROM) */
#define TTWRS   0x98CB      /* TABLE OF NUMBER OF TOWERS IN GROUND, by the wave, BCD */
#define TTWRSZ  0x98DE
#define TGDPTR  0xA182      /* the mazes by the wave: start and end */
#define ZGDPTR  0xA1CE
#define MAP_IZ  4
#define MAP_EQ  19          /* TOTAL # OF MAPS */
#define MAP_RPT 13          /* REPEAT POINT FOR WRAP AROUND */

#define GD_MDT  ((0x1c00 - 0x200) / 2 + 0x200)  /* MIDDLE OF ALLOWED TOWER TRAVERSE(VERTICAL) */
#define UNIT    (120 * 2)   /* a unit of the shapes: SCALE*PSUB2 */

/* The state byte of the building at X. */
static uint16_t state_of(sw_machine *m, uint16_t x)
{
    return (uint16_t)(SW_GDRAM + (int8_t)R8(x + TGD_RM));
}

/* The entry of the current wave in the table of mazes. */
static uint16_t maze(sw_machine *m)
{
    uint16_t x = (uint16_t)(TGDPTR + (uint8_t)(R8(SW_GD_WAV) << 2));    /* *4 TO INDEX INTO TABLE */
    if (x >= ZGDPTR) x = ZGDPTR - MAP_IZ;   /* USE LAST ENTRY IF OUT OF RANGE */
    return x;
}

/* IGRND: INIT FOR SEEING GROUND */
void sw_igrnd(sw_machine *m)
{
    for (uint16_t u = SW_GDRAM; u < SW_GDRAMZ; u++)
        W8(u, 0);                           /* CLEAR OUT TYP -- NOT YET IN SEQUENCE */

    if (R8(SW_GD_WAV) >= MAP_EQ) {          /* LIMIT WAVE #: ?REQUESTED WAVE TOO HI? */
        /* # OF REPEAT MAPS TO CHOOSE FROM, ADDED TO BASE REPEAT MAP:
         * GIVES RANDOM SELECTION FROM TOP MAPS */
        W8(SW_GD_WAV, (uint8_t)(((MAP_EQ - MAP_RPT) * R8(SW_P_RND1)) >> 8) + MAP_RPT);
    }

    uint16_t x = (uint16_t)(TTWRS + R8(SW_GD_WAV));     /* SETUP NUMBER OF TOWERS FOR THIS WAVE */
    if (x >= TTWRSZ) x = TTWRSZ - 1;        /* PAST END OF TABLE */
    W8(SW_GD_TWL, R8(x));                   /* # OF TOWERS LEFT, BCD */

    W16(SW_TWRMUL, R16(SW_TSCTWR));         /* SETUP PROGRESSIVE SCORE TO FIRST VALUE, BCD */
    W8(SW_TWRMUL + 2, R8(SW_TSCTWR + 2));

    W8(SW_GD_SEQ, 0);                       /* SEQUENCE TO START WITH */
}

/* TRANU1: TRANSPOSE U1 UNIT VECTORS */
static void tranu1(sw_machine *m)
{
    static const int8_t pair[3][2] = { { M_AY, M_BX }, { M_AZ, M_CX }, { M_BZ, M_CY } };
    for (int i = 0; i < 3; i++) {
        uint16_t d = R16(SW_M_U1 + pair[i][0]);
        uint16_t u = R16(SW_M_U1 + pair[i][1]);
        W16(SW_M_U1 + pair[i][1], d);
        W16(SW_M_U1 + pair[i][0], u);
    }
}

/* GRLZIN: INIT VALS FOR LATER COLLISION. M.X0: where the laser points on
 * the screen as if the ship were level; M.X0+4 later the distance of the
 * building looked at. */
static void grlzin(sw_machine *m)
{
    tranu1(m);                              /* TRANSPOSE U1 TO GET UNROTATED LAZAR VIDEO LOC */

    W16(SW_M_X0, 0);
    W16(SW_M_Y0, R16(SW_LZ_CX));            /* LEFT/RIGHT VID X IS MATH Y */
    W16(SW_M_Z0, R16(SW_LZ_CY));            /* UP/DOWN VID Y IS MATH Z */
    W16(SW_M_BIC, SW_MSX0BC);
    sw_mgowt(m, SW_MSPSUB);
    W16(SW_M_X0, R16(SW_M_YP));             /* FOR LATER SAFEKEEPING */
    uint16_t d = R16(SW_M_ZP);
    W16(SW_M_Y0, d);                        /* LAZAR VIDEO LOC IF SHIP NOT ROTATED */
    if ((d & 0x8000) && R8(SW_LZ_ON) != 0) {    /* ?LOWER THAN CENTER OF SCREEN? */
        W8(SW_LZ_HIT, 0xff);                /* THEN AM SWEEPING DIRT GROUND -- SHOW SPLASH */
        W8(SW_SP_TYP, 0x01 + 0x02);         /* UPPER HALF */
        W16(SW_SP_COL, (VGCGRN << 8) + SW_VGCOPC);
        /* HITE BENEATH HORIZON IN VID UNITS; BUT ROTATION MAY MAKE IT 1.4 TIMES BIGGER.
         * HORIZON IS FAR AWAY, BOTTOM OF SCREEN IS CLOSE */
        W8(SW_SP_LOC, (uint8_t)(R16(SW_M_ZP) >> 2) + 0xc0);
    }

    tranu1(m);                              /* UN-TRANSPOSE MATRIX BACK TO NORMAL */
}

/* This building is the nearest one the laser points at so far: note it. */
static void nearest(sw_machine *m, uint16_t dist, uint16_t ptr, uint16_t x)
{
    uint16_t d = R16(SW_M_Z0);
    if (d < R16(dist)) {                    /* ?CLOSEST SO FAR? */
        W16(dist, d);
        W16(ptr, x);                        /* AND SAVE POINTER FOR LATER */
    }
}

/* GRLZCL: CHECK FOR ACTUAL COLLISION of the laser with the building in view */
static void grlzcl(sw_machine *m)
{
    uint16_t x = R16(SW_S2_PRM);
    /* CALC 1/X WITH LIN SCALE INCLUDED */
    W16(SW_M_DVD, R16(SW_M_Z0) << 1);       /* PULL VIEWED OBJECT DISTANCE: PSUB2 DOES DIV BY 2. GEN 1/X */
    W16(SW_M_YP, R16(x + TGD_Y) - R16(SW_M_U1 + M_TY)); /* OBJECT Y: TRANSLATE WITHOUT ROTATION */
    W16(SW_M_ZP, 0 - R16(SW_M_U1 + M_TZ));  /* Z, BOTTOM OF BUNKERS,TOWERS */
    W16(SW_M_XP, R16(SW_M_QUO));            /* GET X FOR PROPER SCREEN CONVERSION */
    sw_mgowt(m, SW_MSPERS);
    W16(SW_M_YT, R16(SW_M_YP));             /* FOR SAVETY */
    W16(SW_M_ZT, R16(SW_M_ZP));             /* Y,Z CONVERTED TO SCREEN COORDS */

    if (R8(x + TGD_PC) != PC_BNK) {         /* DIFFERENT HITES FOR DIFFERENT TOWERS: ?TOWER? */
        W16(SW_M_YP, 4 * UNIT);             /* DOUBLE RADIUS OF OBJECT */
        W16(SW_M_ZP, 0x58 * UNIT);          /* DOUBLE HITE: Z HITE(MAX) */
    } else {
        /* FIRST FIND IF BUNKER IS STILL ALIVE */
        if (R8(state_of(m, x)) & TYP_DM) return;    /* EXIT SINCE BUNKER ALREADY DAMAGED */
        W16(SW_M_YP, 7 * UNIT);             /* DOUBLE BUNKER HITE */
        W16(SW_M_ZP, 6 * UNIT);             /* DOUBLE BUNKER HITE */
    }
    uint16_t d = (uint16_t)(R16(SW_M_XP) - 0x100);  /* GET 1/X: ADJUST FOR LINEAR SCALE EFFECT */
    if (d & 0x8000) d = 0;                  /* BUT DONT GO OVERBOARD */
    W16(SW_M_XP, d);
    sw_mgowt(m, SW_MSPERS);                 /* DO THE MULTIPLYS */

    /* NOW TEST UNROTATED VIDEO LOC: plus the RADIUS OF COLLISION and the
     * SITE RADIUS FOR FUDGE, against the UNROTATED LAZAR SITE LOCATION */
    d = (uint16_t)(R16(SW_M_YT) + R16(SW_M_YP) + 10);
    int32_t t = (int16_t)d - (int16_t)R16(SW_M_X0);
    if (t < 0) return;                      /* 1?TO ONE SIDE OF TOWER? */
    d = (uint16_t)((int16_t)(uint16_t)t >> 1);      /* THEN CHANGE DELTA DIAMETER TO RADIUS */
    d = (uint16_t)(d - 10);                 /* SITE DIAMETER/2 FOR FUDGE */
    if ((int16_t)d - (int16_t)R16(SW_M_YP) > 0) return;     /* 2?WITHIN BOTH BOUNDS? THEN A LEFT/RIGHT HIT! */

    /* NOW CHECK Z */
    d = (uint16_t)(R16(SW_M_Y0) - R16(SW_M_ZT));    /* COMPARE WITH UNROTATED LAZAR SCREEN */
    if (d & 0x8000) return;                 /* 3?SHOOTING ABOVE GROUND LEVEL? */
    if ((int16_t)d - (int16_t)R16(SW_M_ZP) > 0) return;     /* 4?WITHIN LIMITS? (HITE DIAMETER) */

    if (R8(x + TGD_PC) == PC_BNK) {         /* 5?BUNKER? */
        nearest(m, SW_CL_BDS, SW_CL_BP, x);
        return;
    }
    /* ELSE IS TOWER */
    W16(SW_M_YP, 0x52 * UNIT);              /* HITE TO BUNKER ON TOP */
    sw_mgowt(m, SW_MSPERS);                 /* BLITZES ZP */
    /* LAZAR HITE, SITE RADIUS FOR FUDGE */
    d = (uint16_t)(R16(SW_M_Y0) + 10 - R16(SW_M_ZT));
    if ((int16_t)d - (int16_t)R16(SW_M_YP) >= 0) {  /* 6?WITHIN BUNKER ON TOP? */
        if (R8(state_of(m, x)) & TYP_DM) return;    /* EXIT SINCE BUNKER ALREADY DAMAGED */
        nearest(m, SW_CL_BDS, SW_CL_BP, x);
    } else {                                /* ELSE SPLASHED TOWER */
        nearest(m, SW_CL_TDS, SW_CL_TP, x);
    }
}

/* Flown into a building: the shields glow. */
static void crash(sw_machine *m)
{
    if (!(R8(SW_S_GAS) & 0x80)) {           /* ?STILL IN REGULAR GAMEPLAY? */
        sw_bg1glw(m);                       /* START GLOWING SHIELDS */
        sw_doxmt(m, SW_IDACR);              /* AND CRASH INTO TOWER */
    }
}

/* GDVIEW: the building at S2.PRM in view, and the player against it */
static void gdview(sw_machine *m)
{
    uint16_t x = R16(SW_S2_PRM);
    int bunker = R8(x + TGD_PC) == PC_BNK;  /* ? A BUNKER PICTURE? ELSE A TOWER PIC */
    W8(SW_BJ_ID, bunker ? SW_TDSBNK : SW_TDSTWR);   /* UPDATE OBJECT ROUTINES ABOUT IT */

    /* THEN ASSUME VISIBLE NORMALLY: BASE COLOR, INTO MBOX FOR LATER D-PAGE USE */
    W16(SW_M_GDCB, (VGCYLW << 8) + SW_VGCOPC);

    uint8_t a = R8(state_of(m, x));
    uint16_t d;
    if (!bunker) {                          /* ?NOT A LONE BUNKER? THEN MUST BE A TOWER OR BISHOP */
        if ((a & TYP_DM) || !(a & TYP_BK)) {    /* ?DAMAGED? ?OR WITH A GONE BUNKER ON TOP? */
            W8(SW_BJ_ID, SW_TDSSTB);        /* THEN DRAW JUST THE STUB */
            d = (SW_VGCOFF << 8) + SW_VGCOPC;   /* NO HAT DRAWN ON TOP */
        } else {                            /* ELSE TOWER HAS A HAT */
            d = (VGCWHT << 8) + SW_VGCOPC;  /* SO DRAW IT SPECIAL WHITE */
        }
    } else {                                /* ELSE IS LONE BUNKER */
        if (!(a & TYP_DM)) d = (VGCRED << 8) + SW_VGCOPC - 0x20;    /* ?NOT DAMAGED? */
        else d = (SW_VGCOFF << 8) + SW_VGCOPC;                      /* ELSE DAMAGED: SHOW IT */
    }
    W16(SW_M_GDCT, d);                      /* TOP COLOR OF LASAR TOWERS, BUNKER COLOR */

    W8(SW_M_GDSC, 0x72);                    /* LIN SCALE TO REDUCE SIZE OF FAR OBJECTS */
    a = (uint8_t)(R8(SW_M_XP) << 2);        /* 3F MAX: BUMP UP TO UNSIGNED FF */
    W8(SW_M_GDSC + 1, a);
    /* RANGE OF BRITENESS, FLIP FOR BRITENESS, DIMMEST BRITENESS: INTO COLOR INSTRUCTIONS */
    W8(SW_M_GDCB + 1, (uint8_t)(((uint8_t)~a * 0x40) >> 8) + 0x40);

    /* GET PLAYER SPEED PLUS OFFSET: ?GETTING CLOSE TO TOWER? */
    d = (uint16_t)((R16(SW_M_S1 + M_VX) << 1) + 0x400);
    if ((int16_t)d - (int16_t)R16(SW_M_XP) >= 0) {
        if (!bunker) {                      /* ?A TOWER? */
            if (!(R8(SW_S_GAS) & 0x80))     /* ?STILL IN NORMAL GAMEPLAY? */
                sw_doxmt(m, SW_IDAPF);      /* TOWER PFFFTS */
            d = (uint16_t)(R16(SW_M_XP) - 0x200);
            if ((int16_t)d - (int16_t)R16(SW_M_S1 + M_VX) <= 0) {   /* ?CLOSE ENUF FOR COLLISION? */
                W16(SW_M_GDCB, SW_VJFLS);   /* VERY BRITE HIT */
                W16(SW_M_GDCT, (SW_VGCOFF << 8) + SW_VGCOPC);       /* TOP COLOR OF LASAR TOWERS */
                crash(m);
                a = R8(SW_S_ROL);
                if (a == 0)                 /* ?ANY PREVIOUS DIRECTION PREFERRED? */
                    a = (uint8_t)(0 - R8(SW_M_YP)); /* IF NOT, THEN SELECT ONE */
                /* AMOUNT TO ROLL; ?WHICH DIRECTION TO CHOOSE? */
                W8(SW_S_ROL, (a & 0x80) ? (uint8_t)-0x20 : 0x20);
            }
        } else {                            /* ELSE IS BUNKER */
            /* HITE*SCALE*PSUB2+SCREEN HITE FOR CLOSE HIT */
            if ((int16_t)R16(SW_M_U1 + M_TZ) - (6 * UNIT + 0x200) <= 0) {   /* ?CLOSE OVER TOP OF BUNKER? */
                sw_doxmt(m, SW_IDAPF);      /* BUNKER PFFFTS */
                x = R16(SW_S2_PRM);
                if (!(R8(state_of(m, x)) & TYP_DM) &&       /* ?CHECK COLLISION SINCE BLOWN AWAY? */
                    (int16_t)R16(SW_M_U1 + M_TZ) - 6 * UNIT < 0) {  /* ?TOO CLOSE OVER TOP OF BUNKER? */
                    d = (uint16_t)(R16(SW_M_XP) - 0x400);
                    if ((int16_t)d - (int16_t)R16(SW_M_S1 + M_VX) <= 0) {   /* ?CLOSE ENUF FOR COLLISION? */
                        W16(SW_M_GDCT, SW_VJFLS);   /* VERY BRITE HIT: TOP COLOR FOR BUNKER */
                        crash(m);
                        if (R8(SW_S_ROL) == 0) {    /* ?READY FOR ANOTHER HIT? (BE POLITE) */
                            a = 19;         /* ROLLING COUNT FOR PARTIAL ROLL */
                            if (!(R8(SW_M_YP) & 0x80)) a = (uint8_t)-19;    /* HIT RIGHT SHOULDER ROLLS US LEFT */
                            W8(SW_S_ROL, a + R8(SW_S_ROL)); /* ROLLS ACCUMULATE -- LOSE CONTROL */
                        }
                    }
                }
            }
        }
    }

    if ((int8_t)R8(SW_M_XP) - 8 <= 0)       /* FIRST 1000 AWAY: ?TOO CLOSE FOR DISPLAY COMFORT? */
        sw_bjgclp(m);                       /* CLIP CLOSE OBJECTS */
    else
        sw_bjgpnt(m);                       /* TRANSLATE AND PROJECT GROUND OBJECT'S POINTS */
    sw_bjgdrw(m);                           /* DRAW THE OBJECT, NEEDS ORIGINAL M.XP,YP,ZP */
    grlzcl(m);                              /* PROCESS LAZAR COLLISION */
}

/* GDTWRGN, GDBSHGN: TOWER GUN and BISHOP GUN: they fire as the top comes
 * up over the horizon */
static void tower_gun(sw_machine *m, uint16_t u, int bishop)
{
    /* 4000-DISTANCE TO TOWER, /4000 IS UNITY, THEN *2 FOR PRESUB2 EFFECT:
     * GET LINEAR SCALED FRACTIONS OF ORIGINAL HITE */
    W16(SW_M_XP, (uint16_t)(0x4000 - R16(SW_M_Z0)) << 1);
    W16(SW_M_ZP, 0x58 * 120);               /* VERY TOP OF BUNKER */
    W16(SW_M_YP, 6 * 120);                  /* HITE OF BUNKER PORTION */
    sw_mgowt(m, SW_MSPERS);                 /* MULT THE LIN SCALE BY TOWER SIZE */

    int32_t t = (int16_t)R16(SW_M_ZP) - (int16_t)R16(SW_M_U1 + M_TZ);  /* COMPARE AGAINST PLAYERS HITE */
    if (t < 0) {                            /* ?STILL BENEATH HORIZON? */
        W8(u, R8(u) | TYP_AM);              /* WE HAVE PROPER AIM WHENEVER WE POKE ABOVE HORIZON */
        return;
    }
    /* ELSE AM ABOVE HORIZON: CHECK AGAINST MAX SIZE OF BUNKER ITSELF.
     * SHOOT IF WE STRADDLE HORIZON, ELSE TEST IF WE USED TO BE UNDER HORIZON */
    if ((int16_t)(uint16_t)t - (int16_t)R16(SW_M_YP) > 0 && !(R8(u) & TYP_AM))
        return;                             /* ?JUMPED PAST HORIZON? THEN SHOOT ANYWAY */
    W8(u, R8(u) & (uint8_t)~TYP_AM);        /* SINCE HAVE PASSED HORIZON, CLEAR OUT AIM */
    if (!bishop && (R8(SW_P_RND1) & 0x80))  /* ?50 PERCENT CHANCE OF FIRING? */
        sw_frwgn(m, 0);                     /* THEN SHOOT A FORWARD GUN */
    if (R8(SW_P_RND1) & 0x80)               /* ?SHOOT ONE WAY DIAG OR THE OTHER? */
        sw_frwgn(m, 1);                     /* SHOOT TO RIGHT DIAGONALLY */
    if (R8(SW_P_RND1) & 0x80)
        sw_frwgn(m, -1);                    /* ELSE SHOOT LEFT DIAGONALLY */
}

/* GDBNKGN: BUNKER GUN */
static void bunker_gun(sw_machine *m)
{
    /* CLOSE WILL BE 40, FAR IS 0: REVERSED DISTANCE TO BUNKER,0 TO 3F */
    uint8_t a = (uint8_t)(0x40 - R8(SW_M_Z0));
    if (a < R8(SW_P_RND1)) return;          /* ?LESS CHANCE WHEN FAR AWAY? */
    /* ?SHOOT STRAIGHT AHEAD? ?SHOOT ONE WAY DIAG OR THE OTHER? (all three
     * the same routine in the end; the random numbers are still drawn) */
    if (R8(SW_P_RND1) >= 0x50) (void)R8(SW_P_RND1);
    sw_frbgn(m);
}

/* GDGUN: ATTEMPT TO FIRE GUNS FROM LAZAR BUILDINGS */
static void gdgun(sw_machine *m)
{
    if (R8(SW_S_GAS) & 0x80) return;        /* ?NOT YET IN DEATH SCENE? */
    uint16_t x = R16(SW_S2_PRM);
    uint16_t u = state_of(m, x);
    if (R8(u) & TYP_DM) return;             /* ?GUNS NOT DAMAGED YET? */
    uint8_t a = R8(x + TGD_PC);             /* GET BUILDING TYPE */
    if (a == PC_BSH) tower_gun(m, u, 1);    /* 2==>BISHOP GUN */
    else if (a > PC_BSH) bunker_gun(m);     /* >2==>BUNKER GUN */
    else tower_gun(m, u, 0);                /* <2==>TOWER GUN */
}

/* VWGRND: the buildings in view */
void sw_vwgrnd(sw_machine *m)
{
    W8(SW_BJ_ID, SW_TDSGND);                /* THE GROUND SHAPE INTO OBJECT INDENTIFIER */
    sw_bjgrot(m);                           /* ROTATE THE GROUND OBJECT POINTS AND SAVE STARTING AT M.GD */

    W16(SW_M_U1 + M_TX, 0);                 /* SET S1 CENTER TO 0,0,0 */
    W16(SW_M_U1 + M_TY, 0);
    W16(SW_M_U1 + M_TZ, 0);

    grlzin(m);                              /* INIT LAZAR COLLISION PARMS */

    W16(SW_M_U1 + M_TX, R16(SW_M_S1 + M_TX));   /* RESTORE PLAYER'S POSITION */
    W16(SW_M_U1 + M_TY, R16(SW_M_S1 + M_TY));
    W16(SW_M_U1 + M_TZ, R16(SW_M_S1 + M_TZ));

    uint16_t x = R16(maze(m));              /* START OF MAZE */
    do {                                    /* DO EACH OBJECT */
        W16(SW_S2_PRM, x);
        if ((int8_t)R8(SW_GD_SEQ) >= (int8_t)R8(x + TGD_SQ)) {  /* ?ALLOWED IN THIS SEQUENCE? */
            int shown = 0;
            W16(SW_M_GD + 0, R16(x + TGD_X));   /* MAP IS IN TOP VIEW, X LEFT/RIGHT(MBOX Y) */
            W16(SW_M_GD + 2, R16(x + TGD_Y));
            W16(SW_M_GD + 4, GD_MDT * 2);   /* AROUND CENTER OF TOWER(PSUB2 DOES DIV OF 2) */
            W16(SW_M_BIC, SW_MSGDBC);       /* SETUP BIC FOR MATH BOX */
            sw_mgowt(m, SW_MSPSB2);         /* OBJECT WITHIN RANGE? */
            uint16_t d = R16(SW_M_XP);
            /* *** IF X SIGN CHANGES, RESTART NEW BUILDING */
            if (d & 0x8000) {               /* ?FLIP X OVER FOR TIGHTER PACKING? */
                d &= 0x3fff;                /* FLIP IT BACK IN FRONT(PSB2 DIVIDES BY 2) */
                W16(SW_M_XP, d);
            }
            W16(SW_M_Z0, d);                /* SAVE NEW DISTANCE FOR COLLISION */
            if ((int16_t)d >= 0x100 &&      /* NOT TOO CLOSE */
                d < 0x3c00 &&               /* NOT TOO FAR ==> WITIN RANGE */
                sw_absd(R16(SW_M_YP)) < R16(SW_M_XP)) {     /* CHECK FOR 45 DEGREES: ?OBJECT NOW DEFINATELY VISIBLE? */
                shown = 1;
                if (R8(state_of(m, x)) & TYP_VW) {  /* ?ALREADY FULLY SETUP TO VIEW NOW? */
                    TAG(SW_TAG_OBJECT, 16 + R8(x + TGD_RM));
                    gdview(m);              /* THEN VIEW THE BUILDING */
                    gdgun(m);               /* ALLOW BUILDINGS TO FIRE GUNS */
                }
            }
            if (!shown) {
                /* NOT CURRENTLY VISIBLE, BUT SEQUENCE OK: MAKE IT VIEWABLE WHILE BACKSTAGE */
                x = R16(SW_S2_PRM);
                uint16_t u = state_of(m, x);
                if (R8(SW_Q_KTW) != 0) {    /* ?TRANSIT GROM GROUND TO BASE? */
                    (void)R8(u);            /* (CLR reads first) */
                    W8(u, 0);               /* KILL ALL GUNS */
                } else {
                    /* WHEN BACKSTAGE, MAKE IT ALIVE, READY TO SHOOT */
                    uint8_t a = TYP_LV | TYP_AM | TYP_VW;
                    if (R8(u) & TYP_DM) {   /* ?PREVIOUSLY DAMAGED? */
                        a |= TYP_DM;        /* THEN REMAIN DAMAGED */
                        if (R8(x + TGD_PC) == PC_BNK)   /* ?DAMAGED BUILDING A BUNKER? */
                            a &= (uint8_t)~TYP_VW;      /* THEN NO VIEW THIS PREVIOUSLY DAMAGED BUNKER */
                    } else {
                        a |= TYP_BK;        /* ELSE NOT DAMAGED, SO TURN ON BUNKER PART */
                    }
                    W8(u, a);
                }
            }
        }
        x = (uint16_t)(R16(SW_S2_PRM) + TGD_IZ);
    } while (x < R16(maze(m) + 2));         /* CMPARE TO END OF CURRENT MAZE */
}

/* GDHTGB: GROUND BUNKER HIT BY LAZARS (a lone one or a tower's hat) */
void sw_gdhtgb(sw_machine *m)
{
    uint16_t x = R16(SW_CL_BP);             /* GET THE POINTER */
    /* PLACE IN FRONT OF PLAYER(MAP WRAPS EVERY 8000): KEEP IN VIEW */
    uint16_t d = (uint16_t)(R16(x + TGD_X) - R16(SW_M_S1 + M_TX)) & 0x7fff;
    W16(SW_M_XT, d + R16(SW_M_S1 + M_TX));
    W16(SW_M_YT, R16(x + TGD_Y));
    uint16_t u = state_of(m, x);            /* AND OFFSET INTO RAM */
    W8(u, R8(u) | TYP_DM);                  /* SET DAMAGED BIT */

    if (R8(x + TGD_PC) == PC_BNK) {         /* GROUND LASER BUNKER? */
        W16(SW_M_ZT, 3 * UNIT);
        sw_bgbkxp(m);
        sw_scrbnk(m);                       /* SCORE FOR HITTING A BUNKER */
    } else {                                /* MUST BE TOWER TOP */
        W16(SW_M_ZT, (0x55 + 5) * UNIT);    /* 55 IS MID HITE OF TOP, 5 IS FUDGE TO LOOK GOOD */
        sw_bgtwxp(m);
        sw_scrtwr(m);                       /* SCORE FOR HITTING A TOWER BUNKER */
    }
    sw_doxmt(m, SW_IDASX);                  /* SMALL EXPLOSION FOR HITTING BUNKER */
}
