/* guns.c - WSGUNS.MAC: the enemies' shots.
 *
 * A shot is a gun record (G$xx) with a position in the math box. In space
 * it is kept relative to the player and creeps towards him (MOVAM); shot
 * by the laser it stays where it is and fades (MOVST). VWGUN draws the
 * shots, finds the one under the sight for the laser and the one that has
 * reached the ship, which then glows on the shields (GNAHIT, GLOWVW).
 *
 * Ported so far: what the space wave needs. The shots of bunkers, towers
 * and panels and the proton torpedo follow with their phases; their move
 * routines still run from the original. Line by line after the 1983
 * source; comments in capitals are the original's. */
#include "game/game.h"
#include "game/m6809.h"

#define TYP_LV 0x01         /* LIVE SHELL, FULLY ARMED */
#define TYP_PR 0x02         /* PRETTY, BUT DISARMED */
#define TYP_UN 0x04         /* UNIVERSE COORDS FOR GROUND SHOT, PANEL SHOT */
#define TYP_GL 0x10         /* GLOWING AGAINST PLAYERS SHIELDS */
#define TYP_HR 0x20         /* GUN HAS BEEN HURT BY PLAYER */

#define TMOVE  0xA675       /* MOVE ROUTINES (local: MVGUN's LDU) */
#define MOV_ST 0
#define MOV_AM 1
#define MOV_LV 2
#define ZMOVE  (11 * 2)

#define TMPOCT 0x4801       /* TEMP OCT,2,SIZ,2 */
#define TMPSIZ 0x4803
#define TMPBIN 0x4801       /* TEMP BIN,1,LIN,1 */
#define TMPLIN 0x4802

/* ASRD3 */
static uint16_t asrd3(uint16_t d)
{
    return (uint16_t)((int16_t)d >> 3);
}

/* FRAGUN: FIRE ALIENS GUN. X-->ALIEN PARM, U-->GUN PARM */
void sw_fragun(sw_machine *m, uint16_t x, uint16_t u)
{
    W16(SW_GN_PTR, u);
    W8(u + G_TMR, 0x40);
    W8(u + G_TYP, TYP_LV);
    W8(u + G_MOV, MOV_AM);

    if ((int8_t)R8(SW_Q_SHK) > 0) {         /* ?STILL WAITING FOR 'CANT SHAKE HIM'? */
        if (x == R16(SW_AM_PTR)) {          /* ?IS THIS THE ONE WE ARE POINTING AT? */
            uint8_t a = (uint8_t)(R8(SW_Q_SHK) - 1);
            W8(SW_Q_SHK, a);                /* COUNT DOWN ONE LESS SHOT */
            if (a == 0) sw_doxmt(m, SW_IDSSHK);/* I CANT SHAKE HIM */
        }
    }

    u = R16(u + G_UVP);                     /* U-->GUN UVP */
    x = R16(x + A_UVP);                     /* X-->ALIEN UVP */
    W16(u + 0, R16(x + M_TX) - R16(SW_M_S1 + M_TX));    /* POSITION SHOT RELATIVE TO PLAYER */
    W16(u + 2, R16(x + M_TY) - R16(SW_M_S1 + M_TY));    /* Y POS */
    W16(u + 4, R16(x + M_TZ) - R16(SW_M_S1 + M_TZ));    /* Z POS */

    sw_doxmt(m, SW_IDATC);                  /* TIE FIGHTER CANNONS */
}

/* The timer of a shot; nonzero when it has run out and the shot is gone. */
static int timed_out(sw_machine *m, uint16_t x)
{
    uint8_t a = R8(x + G_TMR);
    W8(x + G_TMR, a - 1);
    if ((int8_t)a > 1) return 0;            /* (DEC, IFLE) */
    W8(x + G_TYP, 0);
    W8(x + G_TMR, 0);
    return 1;
}

/* MOVST: STAY IN PLACE */
static void movst(sw_machine *m, uint16_t x)
{
    timed_out(m, x);
}

/* MOVAM: AIM AT PLAYER */
static void movam(sw_machine *m, uint16_t x)
{
    if (timed_out(m, x)) return;
    uint16_t u = R16(x + G_UVP);            /* POINT TO MATH INFO */
    for (int i = 0; i < 6; i += 2) {        /* X, Y, Z */
        /* POS-(POS/4) TO HAVE IT BASHFUL TOWARDS PLAYER */
        uint16_t d = asrd3((uint16_t)(0 - R16(u + i)));
        W16(u + i, d + R16(u + i));
    }
}

/* MOVLV: LEAVE THE PLAYER */
static void movlv(sw_machine *m, uint16_t x)
{
    if (timed_out(m, x)) return;
    uint16_t u = R16(x + G_UVP);            /* POINT TO MATH INFO */
    for (int i = 0; i < 6; i += 2) {        /* X, Y, Z */
        int32_t d = (int16_t)asrd3(R16(u + i)) + (int16_t)R16(u + i);
        if (d > 32767 || d < -32768) {      /* WENT OUT OF VIEW */
            W8(x + G_TYP, 0);
            return;
        }
        W16(u + i, (uint16_t)d);
    }
}

/* MVGUN: MOVE THE GUNS, SHELLS, ETC. */
void sw_mvgun(sw_machine *m)
{
    for (uint16_t x = SW_GUN; x < SW_GUNZ; x += G_IZE) {    /* X--> GUN PACKETS */
        W16(SW_GN_PTR, x);                  /* ---LOOP FOR EACH GUN */
        if (R8(x + G_TYP) == 0) continue;
        uint8_t b = (uint8_t)(R8(x + G_MOV) << 1);          /* SAFELY FORM WORD POINTER */
        if (b >= ZMOVE) continue;           /* ELSE IS ERROR (SWI) */
        switch (b >> 1) {                   /* DO THE APPROPRIATE MOVE ROUTINE */
        case MOV_ST: movst(m, x); break;
        case MOV_AM: movam(m, x); break;
        case MOV_LV: movlv(m, x); break;
        default:
            m->rx = x;
            ORIG(R16(TMOVE + b));
            break;
        }
    }
}

/* GNHTSG: SPACE LAZAR HIT ALIEN GUN (PTHTBG: PROTON HIT BASE GUN).
 * CL.GP-->GUN BLOCK */
void sw_gnhtsg(sw_machine *m)
{
    uint16_t u = R16(SW_CL_GP);             /* U-->ALIEN GUN */
    uint8_t a = R8(u + G_TYP);
    if (a == 0) return;                     /* ?SHOT ALIVE? */
    /* KEEP IT ON UNIVERSAL COORDS, MAKE IT HURT */
    W8(u + G_TYP, (a & TYP_UN) | TYP_HR);
    W8(u + G_TMR, 0x0f);                    /* FADE TIMER */
    W8(u + G_MOV, MOV_ST);                  /* MAKE SHELL 'STAY' IN PLACE */
    sw_doxmt(m, SW_IDATCZ);                 /* TIE FIGHTER CANNON STOPPED */
    sw_scrsht(m);                           /* SCORE OUR SHOT */
    sw_doxmt(m, SW_IDASS);                  /* HIT ALIEN SHOT */
}

/* GNAHIT: ALIEN GUN HIT PLAYER. X-->GUN BLOCK.
 * MUST FOLLOW OBJCENT: USES PREVIOUS VG INSTRUCTION */
static void gnahit(sw_machine *m, uint16_t x)
{
    uint16_t u = R16(x + G_UVP);
    W16(u + 0, R16(m->y - 4));              /* GET VECTOR TO FACEPLATE POSITION */
    W16(u + 2, R16(m->y - 2));

    W8(x + G_TYP, TYP_GL);
    W8(x + G_TMR, 0x0f);                    /* SHOW SHELL DISSOLVING */
    W8(x + G_MOV, MOV_ST);                  /* SHELL STAYS IN PLACE */
    sw_bg1glw(m);
    uint8_t a = R8(SW_S_ROL);
    if (a == 0) a = R8(SW_P_RND1);          /* ?NO CURRENT DIRECTION? THEN CHOOSE WHICH WAY */
    uint8_t b = 0x20;                       /* AMOUNT TO ROLL */
    if (a & 0x80) b = (uint8_t)(0 - b);     /* CHECK CURRENT DIRECTION */
    W8(SW_S_ROL, b);                        /* ROLL US IN RESPONSE */
    sw_doxmt(m, SW_IDASH);                  /* SOUND OUR BEING HIT BY A GUN */

    if (R8(SW_Q_GHIT) == 0) {               /* GUN HIT FIRST TIME */
        W8(SW_Q_GHIT, R8(SW_Q_GHIT) + 1);
        if (R8(SW_S_GAS) > 3) {             /* ONLY IF GAS WILL BE 3 OR 4 ON UP */
            sw_doxmt(m, SW_IDSHIT);        /* IM HIT BUT NOT BAD, R2 SEE WHAT YOU CAN DO */
            sw_doxmt(m, SW_IDARS);          /* R2 SHAVE */
        }
    }
}

/* GLOWVW: a shot dissolving on the shields */
static void glowvw(sw_machine *m, uint16_t x)
{
    uint16_t u = R16(x + G_UVP);            /* GET VECTOR TO FACEPLATE POSITION */
    VG(R16(u + 0));
    VG(R16(u + 2));

    /* USE TIMER TO SETUP SIZE, USE 70C0 TO 7000: MAX SCALE(SMALLEST SIZE)/TIMER */
    VG((SW_VGSCAL & 0xf000) | (uint8_t)(R8(x + G_TMR) * (0xf0 / 0x0f)));

    /* NOW SET UP THE COLOR: BRIGHTNESS DEPENDS ON TIME, ALWAYS WHITE */
    VG((((SW_VGCOLR >> 8) + VGCWHT) << 8) | (uint8_t)(R8(x + G_TMR) << 4));

    VG(SW_VJGNX);                           /* JSR TO THE SPARKLER WITHOUT CENTER LINES */

    VG(SW_VGSCAL);                          /* RESTORE SCALE */
    VG(SW_VGCNTR);                          /* CENTER */
}

/* GN1DRW: DRAW NORMAL GUN */
static void gn1drw(sw_machine *m, uint16_t x)
{
    /* ---DETERMINE SCALE */
    uint8_t bin = 8;                        /* MAX FOR BINARY SCALE: CLOSEST IS 100 */
    uint16_t d = R16(SW_M_XT);
    uint8_t lin;
    W8(TMPBIN, bin);
    for (;;) {                              /* ---LOOP FOR BINARY SCALE */
        W8(TMPBIN, --bin);
        if (bin == 0) { lin = (uint8_t)(d >> 8); break; }
        d = (uint16_t)(d << 1);
        if (d & 0x8000) { lin = (uint8_t)(d >> 8) & 0x7f; break; }  /* MAX FOR LINEAR SCALE */
    }
    W8(TMPLIN, lin);

    /* DO THE FIREBALL */
    if ((R8(x + G_TYP) & TYP_HR) == 0) {    /* SHELL HURT? NO, DO NORMAL SHELL */
        /* USE CALCULATED SCALE FOR NORMAL SHELLS */
        VG(((bin | ((SW_VGSCAL & 0xf000) >> 8)) << 8) | lin);
        VG(SW_VJGNB);                       /* JSR TO THE SPARKLER */
    } else {                                /* YES, SHELL HURT: LIMIT SCALE FOR HURT SHOTS */
        uint8_t b = lin;
        uint8_t a = (uint8_t)(bin + 1);     /* 2 * SMALLER ( USING LARGER STAMPS ) */
        if (a < 3) {                        /* DON'T LET IT GET TOO LARGE */
            a = 3;                          /* BINARY OF THREE, LINEAR OF 0 */
            b = 0;
        } else if (a >= 6) {                /* BUT MAKE SURE ITS VISIBLE FAR AWAY */
            a = 6;                          /* BINARY OF 6, LINEAR OF 0 */
            b = 0;
        }
        VG(((a | ((SW_VGSCAL & 0xf000) >> 8)) << 8) | b);

        /* FADE USING TIMER: *16 FOR Z, A LITTLE BRIGHTER; MAKE IT PURPLE */
        VG((((SW_VGCOLR >> 8) + VGCPRP) << 8) | (uint8_t)((R8(x + G_TMR) << 4) | 0x0f));
        VG(SW_VJGNX);                       /* EXPLOSION SPARKLER */
    }

    VG(SW_VGSCAL);                          /* RESTORE SCALE */
    VG(SW_VGCNTR);                          /* CENTER */
}

/* RESTORE DOWNLOADED UNIT VECTOR */
static void restore_u1(sw_machine *m)
{
    W16(SW_M_U1 + M_TX, R16(SW_M_S1 + M_TX));
    W16(SW_M_U1 + M_TY, R16(SW_M_S1 + M_TY));
    W16(SW_M_U1 + M_TZ, R16(SW_M_S1 + M_TZ));
}

/* One shot of VWGUN. */
static void vw1gun(sw_machine *m, uint16_t x)
{
    uint8_t a = R8(x + G_TYP);
    if (a == 0) return;                     /* ?GUN BEING USED? */
    TAG(SW_TAG_SHOT, (x - SW_GUN) / G_IZE);
    if (a & TYP_GL) {                       /* ?GLOWING AGAINST OUR SHIELDS? */
        glowvw(m, x);                       /* THEN SHOW GUN AGAINST OUR SCREENS */
        return;
    }
    if (a & TYP_UN) {                       /* ?VIEW RELATIVE TO UNIVERSE? */
        restore_u1(m);
    } else {                                /* ELSE RELATIVE TO PLAYER */
        W16(SW_M_U1 + M_TX, 0);             /* GUNS ARE RELATIVE TO PLAYER */
        W16(SW_M_U1 + M_TY, 0);
        W16(SW_M_U1 + M_TZ, 0);
    }
    W16(SW_M_BIC, R8(x + G_BIC));
    sw_mgowt(m, SW_MSPSB2);

    uint16_t xp = R16(SW_M_XP);             /* X */
    int visible = 0;
    if ((int16_t)xp > 1 && xp <= 0x7f00) {
        W16(SW_M_DVD, xp);
        W16(SW_M_XT, xp);
        uint16_t d = R16(SW_M_YP);
        W16(SW_M_YT, d);
        if (sw_absd(d) < R16(SW_M_XP)) {
            d = R16(SW_M_ZP);
            W16(SW_M_ZT, d);
            if (sw_absd(d) < R16(SW_M_XP)) visible = 1;
        }
    }
    if (!visible) {                         /* SHOT NOT VISIBLE */
        (void)R8(x + G_TYP);
        W8(x + G_TYP, 0);                   /* KILL IT OFF FOR NOW, ELSE TURN SHOT OFF */
        return;
    }

    /* GUN SHOT IS VISIBLE */
    sw_objcen(m);                           /* PROCESS CENTER OF GUNSHOT */

    /* LOOK FOR COLLISION FIRST */
    x = R16(SW_GN_PTR);
    if (R8(x + G_TYP) & (TYP_LV | TYP_PR)) {    /* CAN ONLY HIT LIVE OR PRETTY SHOTS */
        W16(SW_M_YP, 0x80);                 /* FAT CHANCE OF COMPUTING THIS, TURNOFF GAS AND TEST IT */
        sw_mgowt(m, SW_MSPERS);
        W16(TMPSIZ, R16(SW_M_YP) + 10);     /* SIZE OF CURSOR */
        int16_t siz = (int16_t)R16(TMPSIZ);
        /* OBJECT CENTER - LAZAR CENTER */
        uint16_t d = sw_absd((uint16_t)(R16(SW_BJ_CX) - R16(SW_LZ_CX)));
        W16(TMPOCT, d);                     /* PREPARE FOR OCTAGON COLLISION DETECT */
        if ((int16_t)d <= siz) {            /* LE FOR LIBERALITY */
            d = sw_absd((uint16_t)(R16(SW_BJ_CY) - R16(SW_LZ_CY)));    /* NOW CHECKING Y COORDS */
            if ((int16_t)d <= siz) {        /* ?WITHIN Y ALSO? */
                d = (uint16_t)(d + R16(TMPOCT));    /* THEN ADD TO OCTAGON TEST */
                W16(TMPOCT, d);
                /* MAKE 1.5 FOR OCTAGON */
                uint16_t o = (uint16_t)((R16(TMPSIZ) >> 1) + R16(TMPSIZ));
                if ((int16_t)o >= (int16_t)R16(TMPOCT)) {   /* ?ALSO WITHIN OCTAGON? */
                    /* THEN SEE IF WE ARE THE CLOSEST ALIEN */
                    if (R16(SW_M_XT) < R16(SW_CL_GDS)) {    /* ?ALSO THE NEAREST GUN SO FAR? */
                        W16(SW_CL_GDS, R16(SW_M_XT));
                        W16(SW_CL_GP, R16(SW_GN_PTR));      /* THEN WE GOT A HIT: RECORD WHO WE ARE */
                    }
                }
            }
        }
    }

    if (R8(x + G_TYP) & TYP_LV) {           /* ?LIVE SHELL HAS COLLISIONS ALLOWED? */
        uint16_t d = (uint16_t)(R16(SW_M_XT) << 1);     /* SCALE BACK UP TO REGULAR COORDS */
        if (!(d & 0x8000)) {
            uint16_t v = R16(SW_M_S1 + M_VX);
            if ((int16_t)v < 0x200) v = 0x200;  /* ?PLAYER SHIP FAST ENUF? THEN USE ITS SPEED,
                                                 * ELSE MIN DISTANCE LEST SHOTS ARE TOO LARGE */
            int hit = (int16_t)d - (int16_t)v <= 0;     /* COLLISION IF WITHIN FORWARD MOVEMENT */
            d = (uint16_t)(d - v);
            if (!hit) hit = (int16_t)d - 0x110 <= 0;    /* MAX GUN SPEED PLUS FUDGE */
            if (hit) {                      /* ?COLLISION DUE TO PLAYER SPEED OR GUN SPEED? */
                int16_t c = (int16_t)R16(SW_BJ_CX);     /* CENTER OF GUNSHOT ON SCREEN */
                /* +-128*4==>=-512 SCREEN COORDS: NO CHANCE OF CURSOR HITTING IT? */
                int out = c >= 0 ? c >= (int16_t)(SW_VGCURR * 4)
                                 : c <= (int16_t)(uint16_t)(SW_VGCURL * 4);
                if (!out) {
                    c = (int16_t)R16(SW_BJ_CY);         /* CHECK UP/DOWN LIMITS */
                    out = c >= 0 ? c >= (int16_t)(SW_VGCURT * 4)
                                 : c <= (int16_t)(uint16_t)(SW_VGCURB * 4);
                }
                if (!out) {
                    if (x == R16(SW_CL_GP) &&           /* ?CAN WE HIT THIS GUN? */
                        R8(SW_LZ_ON) != 0) {            /* ?AND LAZARS ARE ON? */
                        sw_gnhtsg(m);                   /* THEN KILL THIS GUN NOW */
                        x = R16(SW_GN_PTR);             /* AND NO MORE COLLIDE WITH SHIELDS */
                    } else {                            /* ELSE IT SURELY HIT US */
                        gnahit(m, x);                   /* RECORD GUNS HIT ON OUR SHIELDS */
                        VG(SW_VGCNTR);                  /* CANCEL OUT THE OBJCENT ABOVE */
                        glowvw(m, R16(SW_GN_PTR));      /* AND GLOW THE SHELL */
                        return;
                    }
                }
            }
        }
    }
    gn1drw(m, R16(SW_GN_PTR));              /* DRAW GUN TYPE 2 */
}

/* VWGUN: the shots in view */
void sw_vwgun(sw_machine *m)
{
    for (uint16_t x = SW_GUN; x < SW_GUNZ; x += G_IZE) {    /* X--> GUN PACKETS */
        W16(SW_GN_PTR, x);
        vw1gun(m, x);
    }
    restore_u1(m);
}
