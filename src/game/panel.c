/* panel.c - WSPANL.MAC: what stands on the walls of the trench.
 *
 * Every 800 of a wall has up to four things, one above the other (two
 * bits each in PNLW, PNRW): a decorative panel (1), a force field across
 * the trench (2) or a gun (3). They are drawn kind by kind, the left wall
 * and then the right one with the object flipped. A force field the
 * player flies into costs a shield; the fields are coloured in turn so
 * that their distance can be told.
 *
 * The objects themselves go through the ground object routines (BJGxxx),
 * which still run from the original. Line by line after the 1983 source;
 * comments in capitals are the original's. */
#include "game/game.h"
#include "game/m6809.h"

/* TFFCUE: COLOR CUE THE WEDGES FOR DEPTH VISION */
static const uint16_t TFFCUE[3] = {
    (VGCYLW << 8) + SW_VGCOPC, (VGCTRQ << 8) + SW_VGCOPC, (VGCPRP << 8) + SW_VGCOPC,
};
#define NFFCUE 3

/* The player and a force field at M.X0, M.Z0 on his side of the trench:
 * flown into it? */
static void force_field(sw_machine *m, int right)
{
    int16_t ty = (int16_t)R16(SW_M_U1 + M_TY);  /* THEN CHECK LEFT/RIGHT */
    if (right ? ty < 0 : ty > 0) return;    /* ?ON LEFT SIDE? ?ON RIGHT SIDE? */
    /* TOP OF FORCE FIELD: ?FORCE FIELD ABOVE PLAYER? */
    int32_t d = (int16_t)(uint16_t)(R16(SW_M_Z0) + 0x200) - (int16_t)R16(SW_M_U1 + M_TZ);
    if (d < 0) return;
    if ((int16_t)(uint16_t)d - 0x400 > 0) return;   /* ?BUT NOT TOO FAR? */
    /* HALF OF FF DEPTH, MORE THAN MAX SPEED: ?WITHIN FIRST HALF OF FORCE FIELD? */
    if ((uint16_t)(R16(SW_M_X0) - R16(SW_M_U1 + M_TX)) > 0x400) return;

    W8(SW_BJ_ID, SW_TDSWFG);                /* THEN A HIT, CHANGE THE PICTURE TO BRITE */
    if (!(R8(SW_S_GAS) & 0x80)) {           /* ?STILL IN NORMAL GAMEPLAY? */
        sw_bg1glw(m);                       /* BEGIN GLOWING PLAYERS SHIP */
        sw_doxmt(m, SW_IDACR);              /* CRASH INTO FORCE FIELD */
    }
    if (R8(SW_S_ROL) == 0) {                /* ?FINISHED WITH PRIOR COLLIDE? */
        uint8_t a = 78;
        if (R8(SW_P_RND1) & 0x80) a = (uint8_t)-78; /* FLIP OTHER DIRECTION FOR VARIETY */
        W8(SW_S_ROL, a);
    }
}

/* PNVLW, PNVRW: VIEW THE LEFT or RIGHT PANELS of the kind PN.DES */
static void pnvw(sw_machine *m, int right)
{
    uint16_t wall = right ? SW_PNRW : SW_PNLW;
    W8(SW_WG_FCB, 0);                       /* NO BUMP TO NEXT FORCE FIELD CUE YET */
    W8(SW_WG_FCC, R8(SW_WG_FCU));           /* INIT CURRENT CUE COLOR */
    W8(SW_WG_FCZ, 0x80 + 0x08);             /* REGULAR MAX PLUS DELTA: BRITENESS OF CLOSEST FORCE-FIELD */

    /* SETUP POINTER TO PANEL FLAGS */
    W16(SW_S2_PRM, wall + ((R8(SW_M_U1 + M_TX) >> 3) & 0x0f));

    /* CURRENT SHIP POSITION, THEN GET INTO MIDDLE OF PANEL */
    W16(SW_M_X0, (uint16_t)((uint8_t)((R8(SW_M_U1 + M_TX) & 0xf8) + 4) << 8));
    W16(SW_M_Y0, right ? 0x400 : (uint16_t)-0x400);
    uint16_t d;
    do {                                    /* ---LOOP FOR EACH SEGMENT */
        W16(SW_M_Z0, (uint16_t)-0x0e00);    /* BASE START(MID OF LOWEST SEGMENT) */
        uint16_t x = R16(SW_S2_PRM);
        if (x >= wall + SW_PNLWSI) x -= SW_PNLWSI;  /* ?WRAPAROUND? MOVE BACK MOD(WHOLE TABLE) */
        uint8_t a = R8(x);                  /* GET FLAGS */
        W16(SW_S2_PRM, x + 1);
        do {                                /* ---LOOP EACH PANEL */
            W8(SW_PN_FLG, a);               /* WORKING FLAG BITS */
            if ((a & 3) == R8(SW_PN_DES)) { /* CHECK AGAINST DESIRED PANEL TYPE: ?CORRECT TYPE? */
                int visible = 1;
                W8(SW_BJ_ID, R8(SW_PN_ID)); /* PREPARE CORRECT OBJECT IDENTIFICATION */
                W16(SW_M_BIC, SW_MSX0BC);
                sw_mgowt(m, SW_MSPSB2);     /* GET PANELS CENTER POINT OUT THERE */

                if (R8(SW_PN_DES) == 2) {   /* ?GOT A FORCE FIELD TO COLOR CUE? */
                    W8(SW_WG_FCB, R8(SW_WG_FCB) + 1);   /* BUMP OUR COLOR CUE FOR LATER PANEL */
                    VG(TFFCUE[R8(SW_WG_FCC) % NFFCUE]);
                    W8(m->y - 1, R8(SW_WG_FCZ));    /* Z-BRITENESS TO USE: OVERLAY PREVIOUS BRIGHTNESS */
                }

                /* GET RELATIVE DISP TO PANEL CENTER */
                if ((int16_t)(uint16_t)(R16(SW_M_X0) - R16(SW_M_U1 + M_TX)) <= 0x1000) {
                    if (R8(SW_PN_ID) == SW_TDSWFF) {    /* ?DOING A FORCE FIELD? */
                        if (R16(SW_M_XP) & 0x8000) visible = 0; /* ?FRONT PART VISIBLE? EXIT IF NOT */
                        else force_field(m, right);
                    }
                    if (visible) ORIG(SW_BJBCLP);   /* ALL CLOSE PANELS GET POINTS CLIPPED */
                } else {
                    ORIG(SW_BJGPNT);        /* DO POINTS SPEEDILY */
                }
                if (visible) ORIG(SW_BJGDRW);
            }
            W16(SW_M_Z0, R16(SW_M_Z0) + 0x400); /* MOVE UP TO NEXT PANEL */
            a = R8(SW_PN_FLG) >> 2;
        } while (a != 0);                   /* LOOP TO NEXT PANEL UP */

        if (R8(SW_PN_DES) == 2) {
            /* ONE PAST CURRENT PANNEL: POINT TO OTHER PANNEL SET */
            a = R8(R16(SW_S2_PRM) + (right ? -SW_PNLWSI : SW_PNLWSI) - 1);
            do {
                if ((a & 0xc0) == 0x80) {   /* 2 OR 3? MUST BE A 2 ( FORCE FIELD ) */
                    W8(SW_WG_FCB, R8(SW_WG_FCB) + 1);   /* BUMP OUR COLOR CUE */
                    break;
                }
                a = (uint8_t)(a << 2);
            } while (a != 0);

            if (R8(SW_WG_FCB) != 0) {       /* ?HAVE AT LEAST ONE FORCE FIELD IN THIS PANEL? */
                a = (uint8_t)(R8(SW_WG_FCC) + 1);
                if (a >= NFFCUE) a = 0;     /* ?OVERFLOW TABLE SIZE? */
                W8(SW_WG_FCC, a);           /* CURRENT COLOR CUE INDEX */

                a = (uint8_t)(R8(SW_WG_FCZ) - 0x08);    /* FURTHER PANELS ARE DIMMER */
                if (a < 0x40) a = 0x40;     /* LOWER LIMIT: ?TOO DIM? OR WRAPPED? */
                W8(SW_WG_FCZ, a);           /* SAVE NEXT BRITENESS */

                W8(SW_WG_FCB, 0);
            }
        }

        d = (uint16_t)(R16(SW_M_X0) + 0x800);   /* GET NEXT SEGMENT */
        W16(SW_M_X0, d);
        d = (uint16_t)(d - R16(SW_M_U1 + M_TX));    /* GET RELATIVE DISPLACEMENT */
    } while (d < 0x7000);                   /* ?WITHIN VIEW? ---LOOP NEXT SEGMENT */
}

/* VWPANEL: VIEW THE PANELS */
void sw_vwpanel(sw_machine *m)
{
    static const uint8_t ids[3] = {
        SW_TDSWPN,                          /* DECORATIVE WALL PANEL */
        SW_TDSWFF,                          /* WALL FORCE FIELD */
        SW_TDSWGA,                          /* WALL GUN TYPE A */
    };
    if (R8(SW_WG_FCU) >= NFFCUE)            /* VERIFY CORRECT FORCE FIELD CUE VALUE: ?WRAPPED OVER TOP? */
        W8(SW_WG_FCU, 0);                   /* THEN START FRESH */

    for (int i = 0; i < 3; i++) {
        W8(SW_PN_DES, i + 1);               /* DESIRED TYPE OF PANEL TO SEE */
        W8(SW_BJ_ID, ids[i]);
        W8(SW_PN_ID, ids[i]);
        ORIG(SW_BJGROT);                    /* PRE-ROTATE ITS POINTS */
        TAG(SW_TAG_PANEL, i * 2);
        pnvw(m, 0);                         /* VIEW LEFT WALL PANELS */

        W8(SW_BJ_ID, R8(SW_PN_ID));
        ORIG(SW_BJFROT);                    /* FLIPPED PRE-ROTATE */
        TAG(SW_TAG_PANEL, i * 2 + 1);
        pnvw(m, 1);                         /* VIEW RIGHT WALL PANELS */
    }
}
