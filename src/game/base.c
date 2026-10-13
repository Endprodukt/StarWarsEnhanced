/* base.c - WSBASE.MAC: the trench of the Death Star.
 *
 * A trench is a "pie": a list of sixteen "wedges", each a list of sections
 * of wall, short (800) or long (1000), with what stands on the four
 * panels of the left and of the right wall (two bits each: nothing, a
 * plain panel, a force field across, a gun). The first eleven waves have
 * fixed pies, later ones a random one (GNBASE). Two pointers run through
 * the pie: the near one where the player is, the far one where the trench
 * comes into view; what lies between is kept in sixteen bytes a wall
 * (PNLW, PNRW), one for every 800 of the trench.
 *
 * Ported: all of WSBASE.MAC. What stands on the walls (VWPANEL) and the
 * exhaust port's object (BJGxxx) still run from the original. Line by line after the 1983 source;
 * comments in capitals are the original's. */
#include "game/game.h"
#include "game/m6809.h"

#define PI_IZ     2         /* a pie's entry: POINTER TO WEDGE */
#define WG_TYP    0         /* WEDGE TYPE */
#define WG_PNL    1         /* PANEL LEFT DESCRIPTOR */
#define WG_PNR    2         /* PANEL RIGHT DESCRIPTOR */
#define WG_IZ     3
#define TYP_SHORT 1         /* SHORT WEDGE */
#define TYP_LONG  2         /* LONG WEDGE */
#define TYP_END   3         /* END OF TRENCH */
#define TYP_PORT  4         /* EXHAUST PORT */
#define TYP_NXT   5         /* GET NEXT WEDGE FROM PIE */

/* local tables of WSBASE.MAC (addresses from the program ROM) */
#define PIEXX   0x7C7E      /* RANDOM: the format of a random pie */
#define TWDGXX  0x7C9E      /* the wedges to choose from */
#define TWDGXN  0x11        /* how many of them */
#define TPIE    0x7CC0      /* TABLE FULL OF PIES ( THIS SURE MAKES ME HUNGRY! ) */
#define TPIEZ   0x7CD6
#define TGPROB  0x84B6      /* GUN SHOOTING PROB: TIMER MASK(0F=1 SEC), PROB COMPARE (0C0 = 25%) */
#define TBSBL   0x8696      /* BASE BOTTOM LINES: Y and Z of six lines along the trench */
#define TBSBLZ  0x86AE
#define TBSBF   0x8725      /* FAR END LINE POINTS */
#define TBSBFZ  0x8735

/* The length of a wedge by its type. */
static uint16_t wedge_length(uint8_t typ)
{
    return typ == TYP_SHORT ? 0x800 : 0x1000;   /* ?SHORT WEDGE? ELSE IS LONG WEDGE */
}

/* IWEDGE: INIT FAR WEDGE INFO */
static void iwedge(sw_machine *m)
{
    uint16_t u = R16(SW_WG_FRP);
    uint8_t b = R8(u + WG_TYP);
    if (b == TYP_END) {                     /* ?END OF TRENCH? */
        W16(SW_BS_ELC, R16(SW_WG_FRL));     /* NEW FAR LOCATION SETS END LOCATION */
        W8(SW_BS_EFL, 0xff);                /* SET END FLAG */
        return;
    }
    if (b == TYP_PORT) {                    /* ?GENUINE THERMAL EXHAUST PORT? */
        W16(SW_BS_PLC, R16(SW_WG_FRL));     /* LOCATION OF THE PORT */
        W8(SW_BS_PFL, 0xff);                /* SET PANEL FLAG */
        W8(SW_PT_LZF, 0);                   /* SETUP PROTON TORP INFO: NO FIRE VIA LAZAR YET */
        W8(SW_PT_LIV, 0);                   /* PROTON TORP NOT LIVE YET */
        if (R8(SW_Q_FRC) == 0) {            /* ?DID THE PLAYER USE THE FORCE IN THE TRENCH? */
            W8(SW_Q_FRC, R8(SW_Q_FRC) + 1); /* MARK SUCCESS */
            sw_scrfrc(m);                   /* SCORE ONE FOR ASSERTIVENESS TRAINING(FORCEFUL) */
        }
    }

    /* ADD CURRENT LOC TO CURRENT SIZE: CALC FUTURE LOC */
    W16(SW_WG_FTL, wedge_length(R8(u + WG_TYP)) + R16(SW_WG_FRL));

    b = (R8(SW_WG_FRL) >> 3) & 0x0f;        /* LOC OF NEW FAR PANEL /8 TO HANDLE 800 LENGTHS:
                                             * DEPTH ONLY SEES 8000 FORWARD */
    W8(SW_PNLW + b, R8(u + WG_PNL));        /* SAVE NEW PANEL DESCRIPTORS */
    W8(SW_PNRW + b, R8(u + WG_PNR));        /* RIGHT WALL */
}

/* NWFAR: ROOM EXISTS, FIND NEW FAR */
static void nwfar(sw_machine *m)
{
    uint16_t u = R16(SW_WG_FRP);
    W16(SW_WG_FRL, wedge_length(R8(u + WG_TYP)) + R16(SW_WG_FRL));  /* NEW LOC FOR NEXT FAR WEDGE */

    u += WG_IZ;
    if (R8(u + WG_TYP) == TYP_NXT) {        /* ?CONTINUE TO NEXT PIE SECTION? */
        u = (uint16_t)(R16(SW_PI_FRW) + PI_IZ);
        W16(SW_PI_FRW, u);                  /* GET NEXT PI SECTION */
        u = R16(u);
    }
    W16(SW_WG_FRP, u);                      /* NEXT FAR WEDGE */
    iwedge(m);
}

/* DOFAR: UPDATE FAR INFO IF ROOM. PAINFUL MATH -- 8000 WRAPAROUND HANDLER */
void sw_dofar(sw_machine *m)
{
    if (R8(SW_BS_EFL) != 0) return;         /* ?STILL IN VALID TRENCH? */
    uint16_t u = (uint16_t)(R16(SW_WG_FRP) + WG_IZ);    /* POINT TO POTENTIAL FUTURE WEDGE */
    if (R8(u + WG_TYP) == TYP_NXT)          /* ?GOTO NEXT PIE? */
        u = R16(R16(SW_PI_FRW) + PI_IZ);    /* NEXT WEDGE POINTER */
    uint8_t b = R8(u + WG_TYP);
    uint16_t d;
    if (b == TYP_SHORT) d = 0x800;
    else if (b == TYP_END) d = 0;           /* ?END WEDGE? DO HIM IMMEDIATELY */
    else d = 0x1000;                        /* ALL ELSE NEEDS 1000 SPACE FOR SAFETY */
    d = (uint16_t)(d + R16(SW_WG_FTL));     /* LOC + LENGTH GIVES NEW FUTURE POSITION */
    d = (uint16_t)(d - R16(SW_WG_NRL));     /* MINUS NEAR LOC FOR DISTANCE SPREAD */
    if (d <= 0x6000) nwfar(m);              /* ?WILL ALL OF NEW WEDGES FIT INTO 8000? */
}

/* NWBASE: the trench of this wave from its start */
void sw_nwbase(sw_machine *m)
{
    W8(SW_PT_LZF, 0);                       /* SETUP PROTON TORP INFO: NO FIRE VIA LAZAR YET */
    W8(SW_PT_LIV, 0);                       /* PROTON TORP NOT LIVE YET */

    for (uint16_t x = SW_PANEL; x < SW_PANELZ; x++)
        W8(x, 0);                           /* CLEAR OUT ALL PANELS */

    uint16_t x = (uint16_t)(TPIE + (uint8_t)(R8(SW_BS_WAV) << 1));  /* WHICH TRENCH TO USE */
    uint16_t u;
    if (x >= TPIEZ) u = SW_RPIE;            /* ?PAST END OF TABLE? THEN DO RANDOM PIE */
    else u = R16(x);                        /* ELSE USE PRE-DEFINED PIES: START OF PIE */
    W16(SW_PI_NRW, u);                      /* NEAR POINTER STARTS HERE */
    W16(SW_PI_FRW, u);                      /* FAR POINTER BEGINS HERE ALSO */
    u = R16(u);                             /* U-->NOW SETUP WEDGE INFO */
    W8(SW_WG_FCU, 0);                       /* START COLOR AT SAME SPOT */
    W16(SW_WG_NRL, 0);                      /* NEAR LOC STARTS AT ZERO */
    W16(SW_WG_FRL, 0);                      /* FAR LOC STARTS AT ZERO */
    W8(SW_BS_EFL, 0);                       /* NO END IN SIGHT YET */
    W8(SW_BS_PFL, 0);                       /* NO PORT IN SIGHT YET */
    W16(SW_WG_NRP, u);
    W16(SW_WG_FRP, u);                      /* FAR POINTER STARTS AT BEGINNING */

    iwedge(m);                              /* INIT THE WEDGE HERE */
    nwfar(m);                               /* GET A NEW FAR(MIN PIE SIZE IS TWO WEDGES) */

    for (int i = 0; i < 8; i++)             /* START OFF WITH SOME WEDGES DEFINED */
        sw_dofar(m);                        /* GEN NEXT FAR IF ROOM */
}

/* GNBASE: GEN A NEW BASE PIE */
void sw_gnbase(sw_machine *m)
{
    uint16_t u = SW_RPIE;                   /* NEW RAM PIE */
    uint16_t x = PIEXX;                     /* FORMAT CONTROL */
    for (; u < SW_RPIEZ; u += 2, x += 2)    /* LOOP INIT FOR DIVIDER PANELS */
        W16(u, R16(x));
    for (u = SW_RPIE + 4; u < SW_RPIEZ; u += 4) {   /* FILL 'ER UP, SKIP PAST DIVIDER PANEL */
        uint8_t a = (uint8_t)((TWDGXN * R8(SW_P_RND1)) >> 8);   /* # OF ENTRIES */
        W16(u, R16(TWDGXX + (uint8_t)(a << 1)));    /* PUT ENTRY AWAY */
    }
}

/* One wall of BSGUN: the panels of a section from the bottom up. */
static void wall_guns(sw_machine *m, uint16_t wall, uint16_t y0, int right)
{
    W16(SW_M_Z0, (uint16_t)-0x0200);        /* START AT MID OF BOTTOM PANEL */
    W16(SW_M_Y0, y0);                       /* GUN BARREL ON THE WALL */
    uint8_t a = R8(wall + R8(SW_BS_DSP));
    do {
        W8(SW_BS_FLG, a);
        if ((a & 0xc0) == 0xc0) {           /* ?FOUND A PANEL BUNKER? */
            int32_t d = (int16_t)R16(SW_M_S1 + M_TZ) - (int16_t)R16(SW_M_Z0);
            if (d >= 0) {                   /* ?PLAYER ABOVE BUNKER? */
                int fire = 0;
                d = (int16_t)(uint16_t)d - 0x400;
                if (d < 0) {                /* ?AND CLOSE BY? */
                    fire = R8(SW_P_RND1) >= R8(SW_BS_PRB);  /* ?PROB SAYS TO FIRE THIS ONE? */
                } else {                    /* ELSE PLAYER IS FAR ABOVE, SHOOT LESS OFTEN */
                    d = (int16_t)(uint16_t)d - 0x400;
                    if (d < 0) {            /* ?WITHIN ONE LOWER RANGE? */
                        uint8_t r = R8(SW_P_RND1);
                        r = (uint8_t)((r * R8(SW_P_RND1)) >> 8);
                        fire = r >= R8(SW_BS_PRB);
                    }
                }
                if (fire) {
                    if (right) sw_frprgn(m);    /* FIRE RIGHT PANEL GUN */
                    else sw_frplgn(m);          /* FIRE LEFT PANEL GUN */
                }
            }
        }
        W16(SW_M_Z0, R16(SW_M_Z0) - 0x400); /* GO DOWN ONE PANEL */
        a = (uint8_t)(R8(SW_BS_FLG) << 2);
    } while (a != 0);                       /* LOOP TILL NO MORE PANELS IN THIS SECTION */
}

/* BSGUN: TRY FOR A BASE GUN */
static void bsgun(sw_machine *m)
{
    /* GET BEGINNING OF CLOSEST PANEL, (DONT SHOOT IF TOO CLOSE)+(OFFSET TO CENTER) */
    W16(SW_M_X0, (uint16_t)((uint8_t)((R8(SW_M_S1 + M_TX) & 0xf8) + 0x20 + 4) << 8));
    uint16_t d;
    do {                                    /* TEST EACH PANEL IN TURN */
        W8(SW_BS_DSP, (R8(SW_M_X0) >> 3) & 0x0f);
        wall_guns(m, SW_PNLW, (uint16_t)-0x380, 0);     /* PROCESS LEFT WALL PANELS */
        wall_guns(m, SW_PNRW, 0x380, 1);                /* PROCESS RIGHT WALL PANELS */

        d = (uint16_t)(R16(SW_M_X0) + 0x800);   /* GET NEXT PANEL */
        W16(SW_M_X0, d);
        d = (uint16_t)(d - R16(SW_M_S1 + M_TX));
    } while (d < 0x6000);                   /* FURTHEST AWAY FIRING BUNKER: FINISH WITH LAST PANEL */
}

/* DOBASE: the trench fires */
void sw_dobase(sw_machine *m)
{
    if (R8(SW_PT_LIV) != 0) return;         /* ?NO PROTON YET? THEN ALLOW SHOTS */
    uint8_t b = R8(SW_WV_HRD);
    if (b > 7) b = 7;                       /* ?MAX DIFFICULTY? */
    uint16_t u = (uint16_t)(TGPROB + (b << 1)); /* ACCESS INTO PROBABILITY TABLE */
    if ((R8(SW_FRAME + 1) & R8(u)) == 0) {  /* ?APPROPRIATE TIME INTERVAL? */
        W8(SW_BS_PRB, R8(u + 1));           /* PROB FOR ANY ONE GUN TO FIRE */
        bsgun(m);                           /* TRY FOR A BASE GUN */
    }
}

/* A point of the trench (M.X0, Y0, Z0) as the viewer sees it, ready for
 * the perspective. */
static void presub_x0(sw_machine *m)
{
    W16(SW_M_BIC, SW_MSX0BC);
    sw_mgowt(m, SW_MSPSB2);                 /* PRESUB 2 */
    W16(SW_M_DVD, R16(SW_M_XP));            /* FORCED POSITIVE BY TABLE */
}

/* How far down the trench the far lines go: to its end if that is in view. */
static uint16_t far_x(sw_machine *m)
{
    if (R8(SW_BS_EFL) != 0) {               /* ?END IN VIEW YET? */
        uint16_t d = (uint16_t)(R16(SW_BS_ELC) - R16(SW_M_S1 + M_TX));
        if (d <= 0x7000) return d;          /* LESS THAN 7000 AWAY? */
    }
    return 0x7000;                          /* ELSE NO END IN VIEW */
}

/* BSVBOT: VIEW BOTTOM OF BASE TRENCH */
static void bsvbot(sw_machine *m)
{
    W16(SW_M_U1 + M_TX, 0);                 /* KEEP VIEWER AT START OF TRENCH */
    VG((VGCGRN << 8) + SW_VGCOPC - 0x10);
    for (uint16_t u = TBSBL; u < TBSBLZ; u += 4) {  /* BOTTOM LINE TABLE */
        TAG(SW_TAG_TRENCH, (u - TBSBL) / 4);
        W16(SW_M_X0, far_x(m));
        W16(SW_M_Y0, R16(u));               /* Y OFFSET */
        W16(SW_M_Z0, R16(u + 2));           /* Z OFFSET */
        presub_x0(m);
        sw_objcen(m);                       /* START FROM CENTER SCREEN */
        W16(SW_M_X0, 0x200);                /* CLOSEST PRESUB2 HANDLES WITH RECIPROCAL VG */

        uint16_t d = sw_absd((uint16_t)(R16(SW_M_Y0) - R16(SW_M_S1 + M_TY)));   /* CLIP CLOSE LINES */
        if ((int16_t)d > (int16_t)R16(SW_M_X0)) W16(SW_M_X0, d);    /* ?NEED TO MATCH Y COORD? */
        d = sw_absd((uint16_t)(R16(SW_M_Z0) - R16(SW_M_S1 + M_TZ)));            /* NOW CLIP Z COORD */
        if ((int16_t)d > (int16_t)R16(SW_M_X0)) W16(SW_M_X0, d);    /* ?NEED TO CLIP TO MATCH Z COORD? */

        presub_x0(m);
        sw_bjlnv(m);                        /* DRAW VISIBLE LINE */
        VG(SW_VGCNTR);                      /* RECENTER */
    }
    W16(SW_M_U1 + M_TX, R16(SW_M_S1 + M_TX));   /* RESTORE X */
}

/* BSVFAR: VIEW FAR END OF BASE TRENCH */
static void bsvfar(sw_machine *m)
{
    W16(SW_M_U1 + M_TX, 0);                 /* KEEP VIEWER AT START OF TRENCH */
    VG((VGCGRN << 8) + SW_VGCOPC - 0x30);
    TAG(SW_TAG_TRENCH, 8);
    W16(SW_M_X0, far_x(m));
    for (uint16_t u = TBSBF; u < TBSBFZ; u += 4) {  /* FAR END LINE TABLE */
        W16(SW_M_Y0, R16(u));               /* Y OFFSET */
        W16(SW_M_Z0, R16(u + 2));           /* Z OFFSET */
        presub_x0(m);
        if (u == TBSBF) sw_objcen(m);       /* START FROM CENTER SCREEN */
        else sw_bjlnv(m);                   /* DRAW VISIBLE LINE */
    }
    VG(SW_VGCNTR);                          /* RECENTER */
    W16(SW_M_U1 + M_TX, R16(SW_M_S1 + M_TX));   /* RESTORE X */
}

/* BSVSDW: VIEW WHOLE OF ONE SIDE: an upright line where each wedge begins */
static void bsvsdw(sw_machine *m, int key)
{
    uint16_t u;
    uint16_t d = R16(SW_WG_CRL);
    W16(SW_M_X0, d);
    d = (uint16_t)(d - R16(SW_M_U1 + M_TX));
    int first = 1;                          /* 0: 10$, 1: the first one, 2: 80$ */
    if (d & 0x8000) {
        first = 2;                          /* SKIP FIRST ONE IF PAST IT (BEWARE WRAPAROUND) */
    } else if ((int16_t)d >= 0x800) {
        first = 0;                          /* PROCESS FIRST ONE NORMALLY IF FAR ENUF AWAY */
    } else {
        d = (uint16_t)(d << 1);             /* *2 FOR GENEROUS DROP */
        d = (uint16_t)(0 - d);              /* REVERSE SUBTRACT */
        d = (uint16_t)(d + R16(SW_M_U1 + M_TZ));    /* ALLOW >45 DEGREE DROP */
        if ((int16_t)d < -0x1000) d = (uint16_t)-0x1000;    /* LIMIT TO BOTTOM */
        W16(SW_M_Z0, d);
    }
    for (;;) {                              /* ---LOOP PER OBJECT */
        if (first == 0) {
            W16(SW_M_X0, R16(SW_WG_CRL));
            W16(SW_M_Z0, (uint16_t)-0x1000);    /* BOTTOM EDGE */
        }
        if (first != 2) {
            if (R8(SW_BS_EFL) != 0 &&       /* ?END IN VIEW? */
                !((uint16_t)(R16(SW_M_X0) - R16(SW_BS_ELC)) & 0x8000))
                return;                     /* EXIT IF AT END OF TRENCH */
            /* CHECK AGAINST NORMAL END TOO */
            if (!((uint16_t)(R16(SW_M_X0) - R16(SW_M_S1 + M_TX) - 0x7000) & 0x8000))
                return;                     /* EXIT IF BEYOND MAX RANGE */

            W16(SW_M_BIC, SW_MSX0BC);
            sw_mgowt(m, SW_MSPSB2);         /* PRESUB 2 */
            /* CLIP THE LINE OUT: NO GOOD IF NOT VISIBLE */
            if ((int16_t)sw_absd(R16(SW_M_YP)) - (int16_t)R16(SW_M_XP) <= 0) {
                TAG(SW_TAG_TRENCH, key + (R8(SW_M_X0) >> 3));
                W16(SW_M_DVD, R16(SW_M_XP));    /* FORCED POSITIVE BY TABLE */
                W16(SW_BJ_CX, 0);           /* INIT VECTOR POS TO ZERO */
                W16(SW_BJ_CY, (uint16_t)(0 - SW_VGOFFY));
                sw_bjlnb(m);                /* START FROM CENTER SCREEN */
                W16(SW_M_Z0, 0);            /* TOP OF SIDE PANELS */
                presub_x0(m);
                sw_bjlnv(m);                /* DRAW VISIBLE LINE */
                VG(SW_VGCNTR);              /* RECENTER */
            }
        }
        first = 0;
        /* 80$ */
        u = R16(SW_WG_CRP);
        d = (uint16_t)(wedge_length(R8(u + WG_TYP)) + R16(SW_WG_CRL));
        W16(SW_WG_CRL, d);
        if ((uint16_t)(d - R16(SW_M_U1 + M_TX)) & 0x8000) return;   /* EXIT IF WRAPPED TOO FAR */
        u += WG_IZ;
        uint8_t b = R8(u + WG_TYP);
        if (b == TYP_END) return;           /* EXIT IF END */
        if (b == TYP_NXT) {
            u = (uint16_t)(R16(SW_PI_CRW) + PI_IZ);
            W16(SW_PI_CRW, u);
            u = R16(u);
        }
        W16(SW_WG_CRP, u);
    }
}

/* BSVSID: VIEW SIDE OF BASE TRENCH */
static void bsvsid(sw_machine *m)
{
    uint16_t u = R16(SW_WG_NRP);
    uint8_t b = R8(u + WG_TYP);
    if (b == TYP_END) return;

    uint16_t d = (uint16_t)(wedge_length(b) + R16(SW_WG_NRL) - R16(SW_M_U1 + M_TX));
    if (d & 0x8000) {                       /* ?ARE WE PAST THIS WEDGE? REMEMBER WRAPAROUND */
        W16(SW_WG_NRL, wedge_length(R8(u + WG_TYP)) + R16(SW_WG_NRL));  /* UPDATE OUR LOCATION */

        u += WG_IZ;                         /* ACCESS NEXT WEDGE */
        W16(SW_WG_NRP, u);
        if (R8(u + WG_TYP) == TYP_END) return;  /* REACHED END OF WEDGES, QUIT */

        if (R8(u + WG_TYP) == TYP_NXT) {    /* ?GOTO NEXT PIE? */
            u = (uint16_t)(R16(SW_PI_NRW) + PI_IZ);
            W16(SW_PI_NRW, u);
            u = R16(u);
        }
        W16(SW_WG_NRP, u);
    }

    VG((VGCGRN << 8) + SW_VGCOPC - 0x20);

    sw_dofar(m);                            /* DO FAR WEDGE IF ROOM */

    for (int side = 0; side < 2; side++) {
        W16(SW_PI_CRW, R16(SW_PI_NRW));
        W16(SW_WG_CRP, R16(SW_WG_NRP));
        W16(SW_WG_CRL, R16(SW_WG_NRL));
        W16(SW_M_X0, R16(SW_WG_NRL));
        W16(SW_M_Y0, side ? 0x400 : (uint16_t)-0x400);  /* LEFT SIDE, RIGHT SIDE */
        bsvsdw(m, side ? 64 : 32);          /* VIEW WHOLE SIDE */
    }
}

/* BSVPORT: VIEW THE EXHAUST PORT */
static void bsvport(sw_machine *m)
{
    W8(SW_BJ_ID, SW_TDSPOR);                /* GET A WALL PANEL FOR NOW */
    ORIG(SW_BJGROT);                        /* ROTATE IT IN FRONT OF US */

    W16(SW_M_GD + 4, (uint16_t)-0x1000);    /* Z HITE ON BOTTOM OF TRENCH */
    W16(SW_M_GD + 2, 0);                    /* Y WIDTH IN CENTER */
    W16(SW_M_GD + 0, R16(SW_BS_PLC));       /* X LOCATION */
    W16(SW_M_BIC, SW_MSGDBC);
    sw_mgowt(m, SW_MSPSB2);
    int16_t xp = (int16_t)R16(SW_M_XP);
    if (xp <= -0x400 / 2) {                 /* ?TOO FAR IN BACK? */
        W8(SW_BS_PFL, 0);                   /* TURN OFF THE PORT */
        return;
    }
    if (xp - 0x7000 > 0) return;            /* ?CLOSE ENUF TO SEE? */

    TAG(SW_TAG_TRENCH, 9);
    W16(SW_M_GDCT, SW_VJFLS);               /* SET UP BUNKERS COLOR */
    W16(SW_M_GDSC, SW_VGSCAL);              /* SET UP BUNKERS SCALE */

    if ((int16_t)R16(SW_M_XP) - 0x1000 < 0) /* ?TOO CLOSE? */
        ORIG(SW_BJBCLP);                    /* THEN PROCESS POINTS WITH CLIPPING */
    else
        ORIG(SW_BJGPNT);                    /* ELSE USE REGULAR */
    ORIG(SW_BJGDRW);                        /* DRAW THE LINES */
}

/* BSVEND: THE VERY END OF THE TRENCH */
static void bsvend(sw_machine *m)
{
    VG((VGCGRN << 8) + SW_VGCOPC);
    if (R8(SW_BS_EFL) == 0) return;         /* ?END IN VIEW YET? */
    TAG(SW_TAG_TRENCH, 10);
    uint16_t d = R16(SW_BS_ELC);
    W16(SW_M_X0, d);
    d = (uint16_t)(d - R16(SW_M_S1 + M_TX) - 0x7000);
    if (!(d & 0x8000)) d = (uint16_t)(0 - d);   /* FURTHEST AWAY IS LO, CLOSEST IS AT ZERO */
    else d = 0;
    W16(SW_M_Z0, d);
    W16(SW_M_Y0, (uint16_t)-0x400);         /* LEFT POINT */
    presub_x0(m);
    sw_objcen(m);                           /* START FROM CENTER SCREEN */
    W16(SW_M_Y0, 0x400);                    /* RIGHT POINT */
    presub_x0(m);
    sw_bjlnv(m);                            /* DRAW VISIBLE LINE */

    VG(SW_VGCNTR);                          /* RECENTER */
}

/* A wall's panels of the section left behind: is a force field among them? */
static int had_force_field(uint8_t a)
{
    do {
        /* ONE BIT IN CARRY, OTHER IN SIGN: ?2 OR 3? ?FORCE FIELD (MUST BE A 2) */
        if ((a & 0xc0) == 0x80) return 1;
        a = (uint8_t)(a << 2);              /* MOVE OVER SECOND BIT */
    } while (a != 0);                       /* NEXT PANEL */
    return 0;
}

/* VWBASE: the trench in view */
void sw_vwbase(sw_machine *m)
{
    uint8_t a = (R8(SW_M_U1 + M_TX) >> 3) & 0x0f;   /* GEN CURRENT DISP INTO PANEL BLOCKS */
    if (a != R8(SW_WG_FCD)) {               /* ?DIFFERENT FROM CURRENT WEDGE DISP? */
        uint8_t b = R8(SW_WG_FCD);          /* PROCESS OLD WEDGE */
        W8(SW_WG_FCD, a);                   /* SAVE NEW WEDGE DISP */
        /* CHECK LEFT WALL FIRST, RIGHT WALL LAST: ONE FF IS SUFFICIENT */
        if (had_force_field(R8(SW_PNLW + b)) || had_force_field(R8(SW_PNRW + b)))
            W8(SW_WG_FCU, R8(SW_WG_FCU) + 1);   /* THEN UPDATE FORCE FIELD DISTANCE CUE */
        W8(SW_PNLW + b, 0);                 /* CLEAR OUT OLD WALL */
        W8(SW_PNRW + b, 0);
    }

    sw_vwpanel(m);
    bsvbot(m);                              /* VIEW BOTTOM LINES */
    bsvsid(m);                              /* VIEW SIDES */
    bsvfar(m);

    if (R8(SW_BS_PFL) != 0) bsvport(m);     /* VIEW PORTHOLE */
    if (R8(SW_BS_EFL) != 0) bsvend(m);      /* VIEW FINAL END OF TRENCH */
}
