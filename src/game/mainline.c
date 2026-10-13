/* mainline.c - WSMAIN.MAC: the main line and the phases of the game.
 *
 * WAITFRAME lets one game frame run for every 12th interrupt (GMSYNC),
 * IFRAME starts it, and the phase table TPHASE picks what the frame does:
 * every phase has an initializer (PHIxxx) and an executor (PHExxx), the
 * executor's number being the initializer's plus one.
 *
 * Ported so far: the attract mode (banner, flight instructions, scoring,
 * high scores), the start of a game up to the choice of a Death Star, and
 * entering initials. The other phases follow (docs/PLAN.md); reaching one
 * of them goes back to the banner for now.
 *
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

#include <stdio.h>

/* --- parts of a frame --------------------------------------------------------- */

/* VGTOSITE: PULLS IRQ RHEO VALS INTO SITE VALS FOR NORMAL USE */
static void vgtosite(sw_machine *m)
{
    W16(SW_SI_RSY, R16(SW_VG_RSY));         /* PITCH GOES TO REAL UP/DOWN Y */
    W16(SW_SI_RSX, R16(SW_VG_RSX));         /* YAW GOES TO REAL LEFT/RIGHT X */
    W16(SW_SI_CY, R16(SW_VG_CY));
    W16(SW_SI_CX, R16(SW_VG_CX));
}

/* IFRAME: INIT GAME FRAME */
static void iframe(sw_machine *m)
{
    W8(SW_FRAME + 1, R8(SW_FRAME + 1) + 1);
    if (R8(SW_FRAME + 1) == 0) {            /* ?OVERFLOW? */
        W8(SW_FRAME, R8(SW_FRAME) + 1);     /* KEEP TRACK */
        if (R8(SW_FRAME) == 0) W8(SW_FRAME, 0x80);      /* ?WORD OVERFLOW? THEN STICK */
    }

    if (R8(SW_SSCRDT) == 0) {
        W8(SW_COINSP, 0);                   /* 0 ==> START OVER AGAIN */
    } else if (R8(SW_COINSP) == 0) {        /* ?FIRST TIME? */
        sw_doxmt(m, SW_IDSFOR);             /* 'THE FORCE WILL BE WITH YOU' */
        W8(SW_COINSP, R8(SW_SSCRDT));       /* REMEMBER WHY WE SPOKE */
    } else if (R8(SW_COINSP) < R8(SW_SSCRDT)) {     /* ?GOT SOME NEW CREDIT AGAIN? */
        sw_doxmt(m, SW_IDSALW);             /* 'ALWAYS' */
        W8(SW_COINSP, 0xff);                /* SAY NO MORE TILL COINS GONE */
    }

    vgtosite(m);                            /* PULL VG(IRQ) CURSOR COORDS INTO SI.* */

    /* GET EDGE OF SWITCHES-- LFIRE,RFIRE,AUX,TEST,COINS */
    W8(SW_GN_SWO, R8(SW_GN_SWN));           /* PREVIOUS NEW VALUE IS CURRENT OLD VALUE */
    uint8_t a = (uint8_t)((R8(SW_R_FIRE) & (uint8_t)~SW_THMBT) | (R8(SW_R_THMB) & SW_THMBT));
    a &= SW_FIREBT | SW_THMBT | SW_AUXBIT;  /* ONLY CONCERNED WITH GUN CONTROLS */
    W8(SW_GN_SWN, a);
    W8(SW_GN_SWE, (a ^ R8(SW_GN_SWO)) & R8(SW_GN_SWO));     /* 1==>FALLING EDGE (WENT ON) */
}

/* IVGEN1: Y --> the VG buffer the main line holds; INIT VECTOR GENERATOR */
static void ivgen1(sw_machine *m)
{
    m->y = (uint16_t)((R8(SW_VGSYNC) << 8) | ((SW_VGRAM & 0xff) + SW_VGVARB));
    TAG(SW_TAG_INIT, 0);
    VG(SW_VJINIT);
}

/* DOWNVG: DOWNLOAD FAKEVGROM TO VGRAM */
void sw_downvg(sw_machine *m)
{
    uint16_t x = SW_FAKEVG, u = SW_VGPSRO;
    do {
        W16(u, R16(x));
        x += 2; u += 2;
    } while (u < SW_VGRROM);                /* STOP AT VG REAL ROM */
}

/* VGDONE: end the display list and give the buffer up. */
static void vgdone(sw_machine *m)
{
    TAG(SW_TAG_NONE, 0);
    VG(SW_VGCNTR);                          /* CENTER */
    VG(0x2020);                             /* ASK FOR HALT */
    W16(m->y, 0x2020);                      /* FORCE HALT, POINT TO LAST ADDRESS USED */
    m->y++;
    if ((uint8_t)((m->y >> 8) - R8(SW_VGSYNC)) >= SW_VGBFSZ / 0x100) {     /* ?OVERFLOW? */
        sw_downvg(m);                          /* RELOAD RAM COPY OF PSUEDOVGRAM */
        W16(SW_VGBFSZ + SW_VGRAM - 2, 0x2020);
        W16(SW_VGBFSZ + SW_VGRAM - 4, 0x2020);
        W16(2 * SW_VGBFSZ + SW_VGRAM - 2, 0x2020);
        W16(2 * SW_VGBFSZ + SW_VGRAM - 4, 0x2020);
    }
    W8(SW_VGSYNC, 0xff);                    /* MINUS INDICATES DONE WITH BUFFER */
}

/* OLD1SHP: USE OLD UNIT VECTORS */
static void old1shp(sw_machine *m)
{
    W8(SW_S_GLW, 0);
    W8(SW_S_ROL, 0);
    W8(SW_VG_LON, 0);
    W8(SW_LZ_ON, 0);                        /* TURN OFF LAZARS */
    W8(SW_LZ_EDG, 0);
    W8(SW_LZ_HIT, 0);
    W16(SW_GD_ACT, 0);
    W8(SW_RHEOR + RH_RSD, 0);               /* SPECIAL ROLL */
    W8(SW_RHEOP + RH_RSD, 0);               /* CLEAR OUT MAGIC RESIDUE */
    W8(SW_RHEOY + RH_RSD, 0);
    W16(SW_RHEOY + RH_POS, 0);              /* RESTART IN CENTER OF SCREEN */
    W16(SW_RHEOP + RH_POS, 0);
}

/* NW1SHP: RESET UNITVECTORS ALSO */
static void nw1shp(sw_machine *m)
{
    sw_unitv(m, SW_M_S1);
    W16(SW_M_S1 + M_TX, 0);
    W16(SW_M_S1 + M_TY, 0);
    W16(SW_M_S1 + M_TZ, 0);
    old1shp(m);
}

/* IPGEN: INIT GENERAL PARMS */
static void ipgen(sw_machine *m)
{
    uint8_t a = R8(SW_P_RND1);
    if (a == R8(SW_P_RND1)) {               /* ?# GENERATOR STUCK? */
        W8(SW_P_IRND, a);                   /* INIT POLY COUNTER */
        W8(SW_P_IRND, 0x80);                /* START IT RUNNING */
    }

    W8(SW_ST_BRT, 0x80);                    /* STAR BRITENESS */
    uint16_t x = SW_ALIEN, u = SW_M_AL;
    uint8_t b = SW_MSALBC;
    do {
        W16(x + 0, u);                      /* A$UVP */
        W8(x + 2, b);                       /* A$BIC */
        u += 0x20;
        b += 4;
        x += 25;                            /* A$IZE */
    } while (x < SW_ALIENZ);

    x = SW_GUN; u = SW_M_GN; b = SW_MSGNBC;
    do {
        W16(x + 0, u);                      /* G$UVP */
        W8(x + 2, b);                       /* G$BIC */
        W8(x + 3, 0);                       /* G$TYP */
        u += 8;
        b += 1;
        x += 6;                             /* G$IZE */
    } while (x < SW_GUNZ);

    sw_nwnshp(m);
    sw_adbobj(m, SW_TDSRTH);                /* ADD DARTH TO THE LIST */
    sw_adbobj(m, SW_TDSGND);                /* ALSO GROUND OBJECT */
}

/* IPARM */
static void iparm(sw_machine *m)
{
    ipgen(m);                               /* INIT PARAMETERS, GENERAL */
    nw1shp(m);                              /* INIT PLAYERS SHIP */
}

static void imath(sw_machine *m)
{
    W16(SW_M_ZRO, 0);
    W16(SW_M_ONE, 0x4000);                  /* CONSTANT '1' */
    W16(SW_M_HLF, 0xe000);                  /* CONSTANT DIVIDE BY 2 */
    W16(SW_M_C80, 0x0080);                  /* ROUNDING CONSTANTS */
    W16(SW_M_C40, 0x0040);
    W16(SW_M_SINH, 0x021f);                 /* SIN,COS ANGLES FOR .895 DEGREES */
    W16(SW_M_COS, 0x3ff7);
    W16(SW_M_SCL, 0x4000);                  /* SCALE */
    W16(SW_M_DVN, 0x0200);                  /* RECIPROCAL PERSPECTIVE */
}

static void istar(sw_machine *m)
{
    W8(SW_ST_BRT, 0x80);                    /* INITIAL BRITENESS */
    uint16_t x = SW_M_STAD;
    do {
        uint8_t a = R8(SW_P_RND2);          /* REVERSE OF INITING ORDER FOR RANDOMNESS */
        uint8_t b = R8(SW_P_RND1);          /* TRY TO POPULATE EVENLY */
        W16(x + 0, (uint16_t)((a << 8) | b));
        uint16_t d = (uint16_t)(a * b);
        d = (uint16_t)((R8(SW_P_RND2) << 8) | (d & 0xff));
        W16(x + 2, d);
        d = (uint16_t)((d >> 8) * (d & 0xff));
        d = (uint16_t)((R8(SW_P_RND2) << 8) | (d & 0xff));
        W16(x + 4, d);
        x += 8;
    } while (x < SW_M_STAZ);
}

/* BINBCD: CONVERT POSITIVE BINARY TO BCD. INPUT BINARY IN D, NUMBER OF
 * SHIFTS IN X. OUTPUT IS BCD IN BCD,BCD+1,BCD+2 */
static void binbcd(sw_machine *m, uint16_t d, uint16_t x)
{
    W16(SW_BIN, d);
    W16(SW_BCD, 0);
    W8(SW_BCD + 2, 0);
    do {
        d = R16(SW_BIN);
        int c = d >> 15;
        W16(SW_BIN, d << 1);
        W8(SW_BCD + 2, sw_bcd_adc(R8(SW_BCD + 2), R8(SW_BCD + 2), &c));
        W8(SW_BCD + 1, sw_bcd_adc(R8(SW_BCD + 1), R8(SW_BCD + 1), &c));
        W8(SW_BCD, (R8(SW_BCD) << 1) | c);
    } while (--x != 0);
}

/* STRTCK: CHECK FOR START SWITCH AND SELF TEST ( STATS ). THIS ROUTINE
 * SHOULD ONLY BE CALLED DURING ATTRACT PHASES */
static void strtck(sw_machine *m)
{
    int8_t b = (int8_t)R8(SW_SI_RSX);       /* SITE POSITION X */
    if (b <= (int8_t)0xa0) {                /* EXTREME LEFT? YES, MAY GO TO INSTRUCTION PHASE */
        if (R8(SW_PHASE) != SW_PHSINS + 1) W8(SW_PHASE, SW_PHSINS);
    } else if (b >= 0x60) {                 /* EXTREME RIGHT? THEN MAYBE WE'LL GO TO HIGH SCORE TABLE */
        if (R8(SW_PHASE) != SW_PHSHIS + 1) W8(SW_PHASE, SW_PHSHIS);
    }
    if ((R8(SW_OPTS0 + 1) & 0x03) == 0)     /* COIN MODE: FREE PLAY */
        W8(SW_SSCRDT, 0x01);                /* GIVE FREE CREDIT ( NO 2 COIN MINIMUM ) */
    if (R8(SW_SSCRDT) != 0) {               /* MAYBE WE'LL START A GAME */
        if (R8(SW_GN_SWE) & SW_STRTBT) {    /* START SWITCH(ES)? YES, OH BOY!! */
            W8(SW_PHASE, SW_PHSSG1);        /* START THE GAME */
            W8(SW_SSCRDT, R8(SW_SSCRDT) - 1);
        }
    }
    /* TSTCK */
    if ((R8(SW_IN_SW0) & SW_TESTBT) == 0)   /* CHECK FOR SELF-TEST SWITCH */
        W8(SW_PHASE, SW_PHSSTS);            /* GO TO STATS PHASE */
}

/* --- what the phases show ------------------------------------------------------- */

/* VWMES: the score, the last score added, the wave number */
static void vwmes(sw_machine *m)
{
    TAG(SW_TAG_SCORE, 0);
    VG(SW_VJTITL);
    VG((VGCGRN << 8) + SW_VGCOPC);          /* BRITE */
    VG(SW_VGRW3 & 0x1fff);                  /* HI UP */
    VG((SW_VGLIML + 0 * SW_VGSCW) & 0x1fff);    /* LEFT */
    W8(SW_BT_SUP, 6);                       /* ZERO SUPPRESS TOP 6 DIGITS */
    sw_vw8dig(m, SW_S_SC87);                /* COMPLETE WITH COMMAS */
    VG(SW_VGCNTR);

    /* SHOW LAST SCORE ADDED IN */
    uint8_t b = R8(SW_SC_ADT);              /* TIMER */
    if (b != 0) {                           /* STILL VISIBLE, SHOW IT */
        b -= 0x08;
        if (b < 0x20) b = 0x00;
        W8(SW_SC_ADT, b);                   /* USE TIMER FOR BRIGHTNESS */
        TAG(SW_TAG_SCORE, 1);
        b >>= 1;
        VG((((SW_VGCOPC >> 8) + VGCYLW) << 8) | b);
        VG(SW_VGRW5 & 0x1fff);              /* Y POS */
        VG((SW_VGLIML + 2 * SW_VGSCW) & 0x1fff);    /* X POS */
        W8(SW_BT_SUP, 0x05);                /* SUPRESS 5 LEADING ZEROES */
        sw_vw6dig(m, SW_SC_ADD - 1);        /* X-->8 DIGIT FIELD, EVEN THO ONLY LO 6 SHOW */
        VG(SW_VGCNTR);
    }

    /* SHOW WAVE NUMBER */
    TAG(SW_TAG_SCORE, 2);
    VG((VGCGRN << 8) + SW_VGCOPC);          /* COLOR BRITE */
    VG(SW_VGRW1 & 0x1fff);                  /* Y UP HI */
    VG((SW_VGLIMR - (2 + 5) * SW_VGSCW) & 0x1fff);  /* X RIGHT */
    W8(SW_BT_SUP, 1);                       /* ZERO SUPPRES TOP 1 DIGIT */
    sw_vwbyte(m, R8(SW_GM_DWA));            /* BCD WAVE NUMBER */
    VG(SW_VGCNTR);
}

/* VWCOIN: "INSERT COINS", "GAME OVER", the price and the credits */
static void vwcoin(sw_machine *m)
{
    /* DO THE FIRST OF TWO LINES */
    if (R8(SW_SSCRDT) == 0) {               /* NO CREDIT--FLASH "GAME OVER"+"INSERT COINS" */
        if ((R8(SW_FRAME + 1) & 0x10) == 0) sw_domess(m, SW_MS_INS);
        else sw_domess(m, SW_MS_GVR);
    } else {                                /* WE GOT THEIR MONEY SO TELL THEM TO... */
        sw_domess(m, SW_MS_STR);            /* "PRESS FIRE TO START" */
    }

    /* NOW DO SECOND OF TWO LINES */
    if (R8(SW_SSCRDT) == 0 &&               /* ?NO CREDITS YET? */
        (R8(SW_SCNCT) == 0 || (R8(SW_FRAMEL) & 0x10) == 0)) {
        /* INSERT COINS GOES WITH COIN COST: NOW TELL THEM HOW MUCH ITS GONNA COST */
        sw_domess(m, (R8(SW_OPTS0 + 1) & 0x03) + SW_MS_FRE);    /* 0=FREE,1=2 PLAYS/COIN,2=1PLAY/COIN,3=2COINS/PLAY */
        return;
    }
    /* GAME OVER GOES WITH # OF CREDITS: SHOW HOW MANY "CREDITS" THEY HAVE */
    if ((uint8_t)(R8(SW_SSCRDT) + R8(SW_SCNCT)) == 1) sw_domess(m, SW_MS_CRD + 1);    /* SINGULAR CREDIT */
    else sw_domess(m, SW_MS_CRD);
    TAG(SW_TAG_COIN, 0);
    VG(0x01b0 & 0x1fff);
    uint16_t d = (uint16_t)-0x80;
    if (R8(SW_SCNCT) != 0) d -= 24;         /* ?HAVE A PARTIAL CREDIT? THEN BACK UP, PREPARE FOR HALF SIGN */
    VG(d & 0x1fff);
    uint8_t a = R8(SW_SSCRDT);              /* 00-12 HEX */
    if (a >= 0x0a) a += 0x06;               /* CONVERT 0A-12 TO 10-18 */
    W8(SW_BT_SUP, 0x02);                    /* SUPPRESS BOTH DIGITS */
    sw_vwbyte(m, a);
    if (R8(SW_SCNCT) != 0) VG(SW_VJHALF);   /* ?STILL HAVE A PARTIAL CREDIT? THEN SAY SO */
    VG(SW_VGCNTR);                          /* ALWAYS DO A CENTER AFTER YOU'RE DONE */
}

/* VWPAGE: VIEW INSTRUCTION OR SCORING SCREEN, a line more every 8 frames */
static void vwpage(sw_machine *m)
{
    uint16_t d = (uint16_t)(R16(SW_PH_TIM) - 1);    /* UPDATE PHASE TIMER */
    W16(SW_PH_TIM, d);
    if ((d & 0x07) == 0) {                  /* READY TO START NEXT LINE? */
        uint8_t a = R8(SW_NXTMES);
        if (!(a & 0x80)) sw_messon(m, a);   /* A NEGATIVE VALUE MEANS NO MORE LINES */

        if (R8(SW_NXTMES) == SW_MS_FLI + 3)     /* SHIELD STRENGTH MESSAGE: INSERT CORRECT STRENGTH */
            sw_messon(m, (R8(SW_OPTS1 + 1) & 0x03) + SW_MS_FLZ + 1);   /* SHOULD GIVE STRENGTH FROM 6 TO 9 */

        a = (uint8_t)(R8(SW_NXTMES) + 1);   /* INC NEXT MESSAGE */
        if (a >= R8(SW_LSTMES)) a = 0x80;   /* THAT'S ALL FOLKS: FLAG IT AS NO MORE */
        W8(SW_NXTMES, a);
    }

    d = (uint16_t)(R16(SW_PH_YOF) - 0x0008);    /* UPDATE MESSAGE Y OFFSET ( SCROLL ) */
    if (d & 0x8000) d = 0x0000;             /* TOO FAR */
    W16(SW_PH_YOF, d);

    if (R16(SW_PH_TIM) < 0x00c0) {          /* START FADING MESSAGES OUT */
        uint8_t b = (uint8_t)(R8(SW_PH_COL + 1) - 0x01);    /* DECREMENT Z */
        if (b < 0x10) {                     /* DIM ENOUGH AT 10 DON'T WASTE TIME GOING TO 0 */
            W16(SW_PH_TIM, 0x0000);         /* PHASE OVER */
            b = 0x00;
        }
        W8(SW_PH_COL + 1, b);
    }
    W16(SW_MSYOFF, R16(SW_PH_YOF));
    TAG(SW_TAG_MESS, 0x3ff);
    VG(R16(SW_PH_COL));                     /* COLOR AND BRIGHTNESS */
    sw_vwmes2(m);                           /* VIEW MESAGES (NO COLOR COMMAND ) */
}

static void cpyrght(sw_machine *m)
{
    sw_domess(m, SW_MS_CPY);                /* COPYRIGHT MESSAGES */
    sw_domess(m, SW_MS_CPY + 1);
    sw_domess(m, SW_MS_CPY + 2);
    sw_domess(m, SW_MS_CPZ);                /* LAST ONE */
}

/* MOVE STARS IN SOME DIRECTION */
static void smvbnr(sw_machine *m)           /* DURING BANNER */
{
    W16(SW_ST_UX, R16(SW_ST_UX) + 0x0080);  /* 45 DEGREES UP X */
    W16(SW_ST_UZ, R16(SW_ST_UZ) + 0x0080);  /* 45 DEGREES UP Z */
}
static void smvins(sw_machine *m) { W16(SW_ST_UY, R16(SW_ST_UY) - 0x0080); }    /* SIDEWAYS */
static void smvscr(sw_machine *m) { W16(SW_ST_UZ, R16(SW_ST_UZ) + 0x0080); }    /* UP */
static void smvhis(sw_machine *m) { W16(SW_ST_UX, R16(SW_ST_UX) + 0x0080); }    /* FORWARD */

/* --- the phases ------------------------------------------------------------------ */

/* The pot limits saved in the EEROM, each moved a little toward the
 * middle, so the yoke learns its range again after power-up. */
static uint8_t pot_adjust(sw_machine *m, uint16_t x)
{
    uint8_t a = 0x40;                       /* ADJUST VALUES */
    if (a > R8(x)) a = (uint8_t)(((uint8_t)(a - R8(x)) >> 3) + 1 + R8(x));
    return a;
}

static void pheres(sw_machine *m)           /* RESET */
{
    sw_eetemp(m, SW_POTSAV);                /* GET POT VALUES FROM EEROM */
    W8(SW_RHEOP + RH_LO, pot_adjust(m, SW_TEMP));
    W8(SW_RHEOP + RH_SP, pot_adjust(m, SW_TEMP + 1));
    W8(SW_RHEOY + RH_LO, pot_adjust(m, SW_TEMP + 2));
    W8(SW_RHEOY + RH_SP, pot_adjust(m, SW_TEMP + 3));
    if (m->yoke_exact) {
        /* The option yoke_autocenter (as VectorPie patches the arcade ROM):
         * no loosened calibration at power-on, but the yoke passed through
         * exactly, lowest reading 00 and no spread. The cabinet's own
         * joystick calibration has already put its rest position at 80, so
         * the sight is centered from the first frame. */
        W8(SW_RHEOP + RH_LO, 0);
        W8(SW_RHEOP + RH_SP, 0);
        W8(SW_RHEOY + RH_LO, 0);
        W8(SW_RHEOY + RH_SP, 0);
    }

    W8(SW_PHASE, SW_PHSHIS);
    W8(SW_MUSTIM, 0xff);                    /* DON'T PLAY MUSIC ON POWER UP */
    imath(m);
    iparm(m);
    sw_downvg(m);                              /* SETUP VG PSUEDO-ROM */
    istar(m);
    sw_alloff(m);                           /* TURN ALL MESSAGES OFF */
    W8(SW_S_SC87, 0);
    W8(SW_S_SC65, 0);
    W8(SW_S_SC43, 0);
    W8(SW_S_SC21, 0);
    W8(SW_GS_GLW, 0);
    W8(SW_GS_HIT, 0);
    sw_setint(m);                           /* SET UP HI SCORES AND INITIALS (HI KIDS!) */
    W8(SW_UPDFLG, 0xff);                    /* NO HIGH SCORES YET */
    W8(SW_MOOTIM, 0xff);                    /* ALLOW COIN ROUTINE TO INIT ITSELF */
}

static void phiins(sw_machine *m)           /* INSTRUCTION PAGE */
{
    W16(SW_PH_YOF, 0x0000);                 /* PHASE Y OFFSET (WAS 380 FOR SCROLLING) */
    W16(SW_PH_TIM, 0x0200);
    W8(SW_NXTMES, SW_MS_FLI);               /* "FLIGHT INSTRUCTIONS..." */
    W8(SW_LSTMES, SW_MS_FLZ + 1);
    W16(SW_PH_COL, VGCOLR + (VGCRED << 8));
    sw_alloff(m);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void page_phase(sw_machine *m, void (*move_stars)(sw_machine *))
{
    ivgen1(m);                              /* SETUP VECTOR GENERATOR AND Y AS POINTER */
    vwmes(m);                               /* SCORE AND STUFF */
    vwcoin(m);                              /* "INSERT COIN", ETC. */
    vwpage(m);                              /* VIEW INSTRUCTION OR SCORING SCREEN */
    sw_vwstar(m);                           /* SHOW THEM STARS */
    vgdone(m);                              /* DONE WITH THE VG IN THIS PHASE */
    move_stars(m);
    if (R16(SW_PH_TIM) & 0x8000)            /* DONE? YES, THEN GO ON TO THE NEXT PHASE */
        W8(SW_PHASE, R8(SW_PHASE) + 1);
    strtck(m);                              /* CHECK FOR START SWITCH */
}

static void pheins(sw_machine *m) { page_phase(m, smvins); }

static void phiscr(sw_machine *m)           /* SCORING PAGE */
{
    W16(SW_PH_YOF, 0x03c0);                 /* FOR SCROLLING */
    W16(SW_PH_TIM, 0x0200);
    W8(SW_NXTMES, SW_MS_SCR);               /* "SCORING" */
    W8(SW_LSTMES, SW_MS_SCZ + 1);
    W16(SW_PH_COL, VGCOLR + (VGCPRP << 8));
    sw_alloff(m);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phescr(sw_machine *m) { page_phase(m, smvscr); }

static void phihis(sw_machine *m)           /* HIGH SCORE DISPLAY */
{
    W16(SW_PH_YOF, 0x0000);
    W16(SW_MSYOFF, 0x0000);
    W16(SW_PH_TIM, 0x0100);
    W16(SW_PH_COL, VGCOLR + (VGCBLU << 8));
    sw_alloff(m);
    imath(m);                               /* RESTORE MATH BOX */
    iparm(m);
    sw_unitv(m, SW_M_U1);                   /* STARS ASSUME UV1 MATRIX: SETUP FACING FORWARD */
    sw_chkhis(m);                           /* CHECK HI SCORE FOR REASONABLENESS */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phehis(sw_machine *m)
{
    ivgen1(m);
    cpyrght(m);                             /* ***COPYRIGHT FIRST FOR SECURITY CHECK */
    sw_vwstar(m);                           /* SHOW THOSE STARS */
    vwmes(m);                               /* SCORE AND STUFF */
    vwcoin(m);                              /* "GAME OVER", ETC. */
    if (R16(SW_PH_TIM) < 0x0050) {          /* FADER? YEP, DIM IT DOWN */
        uint8_t b = (uint8_t)(R8(SW_PH_COL + 1) - 0x01);
        if (b >= 0xf0) b = 0x00;            /* PROBABLY WRAPPED */
        W8(SW_PH_COL + 1, b);
    }
    TAG(SW_TAG_HISCORE, 0x3ff);
    VG(R16(SW_PH_COL));                     /* PUT THE COLOR OUT */
    sw_vwhscr(m);                           /* PUT THE HIGH SCORES ON THE SCREEN */
    sw_vwmess(m);                           /* SHOW MESSAGES */
    vgdone(m);
    smvhis(m);                              /* MOVE THE STARS APPROPRIATLY FOR HIGH SCORES */
    uint16_t d = (uint16_t)(R16(SW_PH_TIM) - 1);    /* UPDATE TIMER */
    W16(SW_PH_TIM, d);
    if (d & 0x8000) W8(SW_PHASE, SW_PHSBNR);        /* THATS LONG ENOUGH: SHOW 'EM THE BANNER */
    strtck(m);                              /* CHECK FOR START OR STATS */
}

static void phisds(sw_machine *m)           /* SELECT A DEATH STAR */
{
    W16(SW_PH_TIM, 0x0100);
    W16(SW_PH_YOF, 0x0000);
    W16(SW_MSYOFF, 0x0000);
    W16(SW_PH_COL, VGCOLR + (VGCRED << 8));
    sw_alloff(m);
    uint8_t b = SW_MS_DS1;
    do {
        sw_messon(m, b);
        b++;
    } while (b < SW_MS_DSZ + 1);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

/* TDTH: where the three Death Stars are; each entry is y, x. */
static const int16_t TDTH[3][2] = { { 100, -400 }, { -300, 0 }, { 100, 400 } };

static void clear_gtime(sw_machine *m)      /* KEEP CORRECT ORDER (IRQ INTERACTION) */
{
    W8(SW_GTIME + 2, 0);
    W8(SW_GTIME + 1, 0);
    W8(SW_GTIME, 0);
}

static void phesds(sw_machine *m)
{
    uint16_t d = (uint16_t)(R16(SW_PH_TIM) - 1);
    W16(SW_PH_TIM, d);
    if (d & 0x8000) {                       /* PLAYER DIDN'T DECIDE, START AT EASY */
        W8(SW_GM_WAV, 0x00);
        W8(SW_PHASE, SW_PHSSG2);            /* START THE GAME */
        clear_gtime(m);                     /* CLEAR GAME TIMES AT START OF GAME */
        return;
    }
    ivgen1(m);
    for (int i = 0; i < 3; i++) {           /* TABLE FOR DEATH STAR SETUP */
        TAG(SW_TAG_DSTAR, i);
        VG(TDTH[i][0] & 0x1fff);            /* Y POS */
        VG(TDTH[i][1] & 0x1fff);            /* X POS */
        VG(SW_VGSCAL + 0);                  /* SIZE THE DEATH STAR */
        VG(SW_VJBMIN);                      /* MINIATURE BASE */
    }
    TAG(SW_TAG_SITE, 1);
    if (R8(SW_WV_LVL) & 0x80) VG(VGCOLR + (VGCTRQ << 8));   /* NOT ON ANY PLANETOID: THE STANDARD CURSOR COLOR */
    else VG(VGCOLR + (VGCYLW << 8));        /* ZEROED IN ON A FAVORITE: LET THEM SEE THE LIGHT */
    sw_vwsitc(m);                           /* SITE WITHOUT COLOR INSTRUCTIONS */
    sw_vwmess(m);                           /* LOOK AT THE MESSAGES */
    TAG(SW_TAG_COUNT, 0);
    VG(200 & 0x1fff);                       /* POSITION FOR COUNTDOWN */
    VG(-16 & 0x1fff);
    uint8_t a = (uint8_t)((uint16_t)(R16(SW_PH_TIM) << 3) >> 8);    /* RESULTS ARE 00-10 HEX */
    if (a >= 0x0a) a += 0x06;               /* CONVERT 0A-10 (HEX) TO 10-16 (DEC) */
    sw_vwbyte(m, a);
    vgdone(m);

    W8(SW_WV_LVL, 0xff);                    /* FLAG */
    for (int i = 0; i < 3; i++) {
        d = sw_absd((uint16_t)(R16(SW_SI_CY) + SW_VGOFFY - (uint16_t)TDTH[i][0]));  /* ADD BACK THE SCREEN OFFSET */
        W16(SW_TEMP, d);
        if (d >= 72) continue;              /* POSSIBLE HIT */
        d = sw_absd((uint16_t)(R16(SW_SI_CX) - (uint16_t)TDTH[i][1]));
        if (d >= 52) continue;              /* GETTING CLOSER */
        d = (uint16_t)(d + R16(SW_TEMP));
        if (d >= 80) continue;              /* DEFINITE HIT */
        W8(SW_WV_LVL, i * 2);               /* CONVERT 0->0,4->2,8->4: FLAG FOR YELLOW CURSOR */
        W8(SW_GM_WAV, i * 2);
        if (R8(SW_GN_SWE) & (SW_FIREBT | SW_THMBT)) {   /* EITHER TRIGGER OR THUMB SWITCH */
            W8(SW_PHASE, SW_PHSSG2);        /* FINISH STARTING THE GAME */
            clear_gtime(m);
        }
        return;                             /* LEAVE */
    }
}

static void phient(sw_machine *m)           /* ENTER INITIALS */
{
    W16(SW_PH_TIM, 0x0000);
    W16(SW_PH_YOF, 0x0000);
    W16(SW_MSYOFF, 0x0000);
    W16(SW_PH_COL, VGCOLR + (VGCBLU << 8));
    sw_alloff(m);
    sw_messon(m, SW_MS_HS1);
    sw_messon(m, SW_MS_HS1 + 1);
    sw_messon(m, SW_MS_HS1 + 2);
    sw_messon(m, SW_MS_HS1 + 3);
    sw_messon(m, SW_MS_HS1 + 4);
    sw_messon(m, SW_MS_HSZ);
    sw_doxmt(m, SW_IDPCNT);                 /* CANTINA MUSIC */
    sw_chkhis(m);                           /* CHECK EAROM HI SCORES FOR REASONABLENESS */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void pheent(sw_machine *m)
{
    ivgen1(m);                              /* ENTER INITIALS FOR HIGH SCORE */
    vwmes(m);
    vwcoin(m);
    sw_vwhscr(m);
    sw_vwmess(m);
    vgdone(m);

    sw_getint(m);                           /* GET THE PLAYER'S INITIALS */

    if ((R8(SW_IN_SW0) & SW_TESTBT) == 0)   /* CHECK FOR TEST SWITCH */
        W16(SW_PH_TIM, 0x0300);             /* THEN ATTEMPT TO LEAVE THIS MODE GRACEFULLY */

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d >= 0x0280) W16(SW_UPDFLG, 0xffff);        /* GIVE 'EM ABOUT 30 SECONDS */

    if (R16(SW_UPDFLG) & 0x8000) {          /* DONE */
        W8(SW_PHASE, SW_PHSHIS);            /* LOOK AT HIGH SCORE TABLE NOW */
        /* TRANSFER TOP 3 HIGH SCORES TO NON-VOLATILE RAM */
        sw_tmpee3(m, SW_SAVINT, SW_INITLS, 0x08);
        sw_tmpee3(m, SW_SAVSCR, SW_HSCORS, 11);
        sw_eecksm(m, 0x01);                 /* EEROM AREA 1 */
    }
}

/* TMUSIC: what plays behind the banner, picked at random */
static const uint8_t TMUSIC[9] = {
    SW_IDPBEN, SW_IDPCNT, SW_IDPEND, SW_IDPRRP, SW_IDPTHB, SW_IDPTH5, SW_IDPREB, SW_IDP4TH, SW_IDPDAR
};

static void phibnr(sw_machine *m)           /* BANNER, and COIN1 */
{
    W8(SW_PHASE, R8(SW_PHASE) + 1);

    sw_downvg(m);                              /* DOWNLOAD PSUEDO VGROM TO VGRAM */
    sw_alloff(m);
    W16(SW_PH_YOF, 0x0000);                 /* SET Y OFFSETS FOR MESSAGES TO 0 */
    W16(SW_MSYOFF, 0x0000);
    sw_bgbnnr(m);                           /* START UP THE BANNER */
    imath(m);                               /* RESTORE MATH BOX */
    iparm(m);
    sw_unitv(m, SW_M_U1);

    if (R8(SW_MUSTIM) == 0xff) {            /* SPECIAL FLAG FOR SKIPPING MUSIC THIS TIME */
        W8(SW_MUSTIM, R8(SW_TIMER + 2));    /* PREPARE FOR NEXT CYCLE */
    } else if (R8(SW_TIMER + 2) != R8(SW_MUSTIM)) {     /* APROX 6.66666 MINS. */
        W8(SW_MUSTIM, R8(SW_TIMER + 2));    /* FOR NEXT TIME */
        sw_eeutim(m);                       /* TIME TO UPDATE TIMEON */
        if ((R8(SW_OPTS1) & 0x04) == 0)     /* THE OPERATOR SICK AND TIRED OF THE MUSIC YET? */
            sw_doxmt(m, TMUSIC[(R8(SW_P_RND1) * 9) >> 8]);
    }
}

static void phebnr(sw_machine *m)
{
    ivgen1(m);
    sw_vwstar(m);
    sw_vwbnnr(m);                           /* VIEW BANNER */
    sw_vwspms(m);                           /* WITH CLOSE COUPLED SPECIAL MESSAGES */
    vwcoin(m);
    vwmes(m);                               /* SCORE AND STUFF */
    vgdone(m);
    smvbnr(m);                              /* MOVE THE STARS IN BACK OF THE BANNER */
    strtck(m);                              /* LOOK FOR STARTUP OR STATS OR TEST OR ? */
}

static void phisg1(sw_machine *m)           /* START GAME A */
{
    W8(SW_PH_TIM, 5);
    W8(SW_MUSTIM, 0xff);                    /* DON'T PLAY MUSIC RIGHT AFTER WE FINISH A GAME */
    sw_alloff(m);
    sw_doxmt(m, SW_IDSRED);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phesg1(sw_machine *m)
{
    W8(SW_PHASE, SW_PHSSDS);                /* SELECT DETH STAR NEXT */
    W16(SW_FRAME, 0);
    W8(SW_WV_LVL, 0);                       /* START AT LEVEL 0 */
    uint8_t a = (uint8_t)((R8(SW_OPTS1 + 1) & 0x03) + 0x06);   /* GAS OPTION: 6 TO 9 UNITS OF SHIELD STRENGTH */
    W8(SW_S_GAS, a);
    W8(SW_GS_OLD, a);                       /* SET UP OLD VALUE ALSO */
    W8(SW_GM_DIF, (R8(SW_OPTS1 + 1) >> 2) & 3);     /* CURRENT DEFAULT DIFFICULTY, STARTS AT 0(EASY) */
    W8(SW_GM_BMP, 0);                       /* DETECT RINGERS THAT START TOO EASY */
    W8(SW_GS_GLW, 0);
    W8(SW_GS_HIT, 0);
    W8(SW_S_SC87, 0);
    W8(SW_S_SC65, 0);
    W8(SW_S_SC43, 0);
    W8(SW_S_SC21, 0);
    W8(SW_SC_FWV, 0);                       /* ZERO FLAG FOR FIRST WAVE ONLY */
    W8(SW_Q_GHIT, 0);                       /* SOUNDOUT ON FIRST GUNHIT */
    W8(SW_Q_ATP, 0);                        /* NO TOPS CLEARED WHEN FIRST STARTING UP */
    if (!m->yoke_exact) {                   /* (yoke_autocenter keeps the calibration as it is) */
        W8(SW_RHEOY + RH_LO, R8(SW_RHEOY + RH_LO) + 1);     /* PERTURB RHEOSTAT LIMITS */
        W8(SW_RHEOP + RH_LO, R8(SW_RHEOP + RH_LO) + 1);
        W8(SW_RHEOY + RH_SP, R8(SW_RHEOY + RH_SP) + 1);
        W8(SW_RHEOP + RH_SP, R8(SW_RHEOP + RH_SP) + 1);
    }
}

static void phesg2(sw_machine *m)
{
    W8(SW_PHASE, SW_PHSBGN);
}

static void phisg2(sw_machine *m)           /* FINISH STARTING UP THE GAME */
{
    imath(m);
    iparm(m);
    istar(m);
    W8(SW_M_S1 + M_AX, 0xc0);               /* FACE BACKWARDS */
    W8(SW_M_S1 + M_BY, 0xc0);
    W8(SW_SBCCNT, 0);                       /* NO MORE BONUS COINS THIS TIME AROUND */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
    phesg2(m);
}

static void phebgn(sw_machine *m)
{
    W8(SW_PHASE, SW_PHSSP1);
    W8(SW_WV_LVL, 0);
}

static void phibgn(sw_machine *m)           /* BEGIN ACT */
{
    old1shp(m);                             /* USE OLD SHIPS UNIT VECTORS */

    binbcd(m, (uint16_t)((uint8_t)(R8(SW_GM_WAV) + 1) << 8), 8);    /* VIEW WAVE STARTS AT 1 */
    W8(SW_GM_DWA, R8(SW_BCD + 2));          /* DECIMAL FOR VIEWING */

    uint8_t a = R8(SW_GM_WAV);
    if (a > 31) a = 31;                     /* LIMIT */
    W8(SW_SP_WAV, a);                       /* LOCAL SPACE USE */
    a += R8(SW_GM_DIF);                     /* INCLUDE OVERALL DIFFICULTY */
    if (a > 15) a = 15;
    W8(SW_WV_HRD, a);                       /* CURRENT WAVES DIFFICULTY */

    W8(SW_PHASE, R8(SW_PHASE) + 1);
    phebgn(m);
}

/* --- the space waves ------------------------------------------------------------------ */

/* IS1UV: DOWNLOAD UNIT VECTORS TO MATH-BOX ( RESIDU ) */
static void is1uv(sw_machine *m, uint8_t a)
{
    W8(SW_M_BICL, a);
    W8(SW_M_BICH, 0);
    sw_mgowt(m, SW_MSDLD1);
}

/* VIEW: the picture of a space wave */
static void view(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);

    vwmes(m);

    if (R8(SW_SC_FWV) == 0) {               /* ?FIRST TIME THRU? THEN GIVE INSTRUCTIONS TO PLAYER */
        uint16_t d = R16(SW_PH_TIM);
        if (d < 0x20 * 5) {                 /* LOOK AT EACH MESSAGE 5 TIMES, ONE CYCLE TAKES 20 */
            if ((d & 0x10) == 0) sw_domess(m, SW_MS_SFB);   /* SHOOT FIREBALLS */
            else sw_domess(m, SW_MS_STF);   /* SHOOT TIE FIGHTERS */
        }
    }

    sw_vwgas(m);
    sw_vwdtha(m);                           /* VIEW DETH STAR, MINIATURE */
    sw_vwsite(m);
    sw_vwstar(m);
    ORIG(SW_VWXPLD);                        /* CAN SEE PARTS ONCE ORIGINAL IS GONE FROM VIEW */

    ORIG(SW_L_TSTLAZ);                      /* PREPARE FOR LAZAR HITS */
    sw_snvw(m);
    ORIG(SW_L_VWGUN);
    ORIG(SW_L_CLSLZ);                       /* PROCESS COLLISIONS */
    ORIG(SW_L_VWLAZ);                       /* AND SEE RESULT */

    sw_vwglw(m);
    vgdone(m);
}

/* What the two space phases do alike before the ships and the player
 * move. Returns 0 if the player has died (the phase is then over). */
static int space_frame(sw_machine *m)
{
    view(m);
    if (R8(SW_S_GAS) & 0x80) {              /* EXIT WHEN PLAYER DIES */
        ORIG(SW_L_PHIS0D);
        return 0;
    }
    ORIG(SW_MVGUN);                         /* MOVE GUN SHOTS */
    sw_donglw(m);
    ORIG(SW_DOXPLD);
    sw_do1glw(m);
    sw_do1gas(m);
    return 1;
}

static void phesp1(sw_machine *m)
{
    if (!space_frame(m)) return;
    ORIG(SW_CPU);                           /* COMPUTER DRONES */

    vgtosite(m);                            /* PULL IN SITE COORDS FROM IRQ RHEOSTATS */
    sw_stwsp1(m);                           /* TWIRL PLAYER */
    is1uv(m, SW_MSS1BC);
    sw_smvsp1(m);                           /* MOVE PLAYER */

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d == 2 * 20) {
        uint8_t a = R8(SW_GM_WAV);
        if ((int8_t)a >= 4 - 1 && (a & 1))  /* ?WAVE 4 OR HIGHER? ?ANDIF EVEN (ODD-1) WAVE? */
            sw_doxmt(m, SW_IDPDAR);         /* THEN DO DARTH THEME */
        else
            sw_doxmt(m, SW_IDPTH5);         /* THEME MUSIC */
    } else if (d == (2 + 7 + 1) * 20) {     /* SHORTLY AFTER THEME */
        sw_doxmt(m, SW_IDPTHB);             /* THEME B FOLLOWS MAIN THEME */
    } else if (d == (2 + 7 + 1 + 9 + 1) * 20) {     /* FOLLOWED BY DESCENT MUSIC */
        sw_doxmt(m, SW_IDPDES);
    } else if (d >= (2 + 7 + 1 + 9 + 1 + 1) * 20) { /* RIGHT AFTER DESCENT MUSIC */
        W8(SW_PHASE, SW_PHSSP2);            /* GO AHEAD AND DESCEND */
    }

    if (R8(SW_WV_LIV) < 0x03) sw_adashp(m); /* 3 ALIENS ALIVE? NO, THEN THROW ANOTHER ONE AT THE PLAYER */
}

static void phisp1(sw_machine *m)
{
    ipgen(m);                               /* JUST GENERAL PARMS, NOT PLAYER OR STARS */
    ORIG(SW_IXPLD);
    W16(SW_PH_TIM, 0);
    W8(SW_Q_RTHV, 0);                       /* DARTH IN VIEW, FIRST TIME -- SOUNDOFF */
    W8(SW_Q_SHK, 9);                        /* # OF SHOTS BEFORE 'I CANT SHAKE HIM' */
    if (R8(SW_SC_FWV) == 0)                 /* ?THE PLAYERS FIRST WAVE? */
        W16(SW_PH_TIM, 2 * 20 - 1);         /* THEN START A BIT AHEAD */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
    phesp1(m);
}

static void phesp2(sw_machine *m)
{
    if (!space_frame(m)) return;
    ORIG(SW_CPURET);                        /* COMPUTER DRONES RUN AWAY */

    vgtosite(m);
    sw_stwsp2(m);                           /* TWIRL PLAYER */
    is1uv(m, SW_MSS1BC);
    sw_smvsp1(m);                           /* MOVE PLAYER */

    for (uint16_t x = SW_ALIEN; x < SW_ALIENZ; x += A_IZE)
        if (R8(x + A_TYP) != 0) return;     /* STILL SOME RUNNING AWAY */
    W8(SW_PHASE, SW_PHSS0G);                /* GOTO DETH STAR GROUND IF NONE LEFT */
}

static void phisp2(sw_machine *m)
{
    W8(SW_Q_RTHV, R8(SW_Q_RTHV) + 1);       /* NO MORE SOUNDOFF IF DARTH GETS VISIBLE */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
    phesp2(m);
}

/* --- the main line ------------------------------------------------------------------ */

/* MAIN: EXECUTE PHASE (TPHASE) */
static void run_phase(sw_machine *m)
{
    switch (R8(SW_PHASE)) {
    case SW_PHSRES:     pheres(m); break;
    case SW_PHSBNR:
    case SW_PHSCN1:     phibnr(m); break;
    case SW_PHSBNR + 1:
    case SW_PHSCN1 + 1: phebnr(m); break;
    case SW_PHSINS:     phiins(m); break;
    case SW_PHSINS + 1: pheins(m); break;
    case SW_PHSSCR:     phiscr(m); break;
    case SW_PHSSCR + 1: phescr(m); break;
    case SW_PHSHIS:     phihis(m); break;
    case SW_PHSHIS + 1: phehis(m); break;
    case SW_PHSSDS:     phisds(m); break;
    case SW_PHSSDS + 1: phesds(m); break;
    case SW_PHSENT:     phient(m); break;
    case SW_PHSENT + 1: pheent(m); break;
    case SW_PHSSG1:     phisg1(m); break;
    case SW_PHSSG1 + 1: phesg1(m); break;
    case SW_PHSSG2:     phisg2(m); break;
    case SW_PHSSG2 + 1: phesg2(m); break;
    case SW_PHSBGN:     phibgn(m); break;
    case SW_PHSBGN + 1: phebgn(m); break;
    case SW_PHSSP1:     if (!m->original_call) goto unported; phisp1(m); break;
    case SW_PHSSP1 + 1: if (!m->original_call) goto unported; phesp1(m); break;
    case SW_PHSSP2:     if (!m->original_call) goto unported; phisp2(m); break;
    case SW_PHSSP2 + 1: if (!m->original_call) goto unported; phesp2(m); break;
    default:
    unported:
        /* A phase that is not ported yet: the original's code for it, if
         * the machine has it at hand (core/emu.h), runs this one frame. */
        if (m->original_frame) {
            m->original_cycles = m->original_frame(m->original_user);
            break;
        }
        /* Else back to the banner. */
        if (m->unported_phase != R8(SW_PHASE)) {
            m->unported_phase = R8(SW_PHASE);
            fprintf(stderr, "starwarsvp: phase %02X is not ported yet, back to the banner\n", m->unported_phase);
        }
        W8(SW_PHASE, SW_PHSBNR);
        break;
    }
}

void sw_main_frame(sw_machine *m)
{
    iframe(m);
    run_phase(m);
}

/* WAITFRAME and MAIN, as far as they can go now: call after every
 * interrupt. The original spins in these loops between interrupts. */
void sw_main_poll(sw_machine *m)
{
    for (;;) {
        if (m->main_state == SW_MAIN_BUSY_NOSYNC) {
            if (m->busy > SW_IRQ_CYCLES) {
                m->busy -= SW_IRQ_CYCLES;
                return;
            }
            m->busy = 0;
            m->main_state = SW_MAIN_WAITFRAME;
        }
        if (m->main_state == SW_MAIN_BUSY) {
            /* The 6809 is still working on the frame (game/pace.c): the
             * interrupt sees VGDONE's FF only when it would have been
             * written. */
            if (m->busy > SW_IRQ_CYCLES) {
                m->busy -= SW_IRQ_CYCLES;
                return;
            }
            m->busy = 0;
            W8(SW_VGSYNC, 0xff);
            m->main_state = SW_MAIN_WAITFRAME;
        }
        if (m->main_state == SW_MAIN_WAITFRAME) {
            /* PROCESS GAME FRAMES IN TIMELY FASHION: LSR GMSYNC, BCC WAITFRAME */
            uint8_t g = R8(SW_GMSYNC);
            W8(SW_GMSYNC, g >> 1);
            if (!(g & 1)) return;
            if (m->original_suspended && m->original_frame) {
                /* SOME FRAME NEEDS A DELAY, THEN LET IT CONTINUE: a routine of
                 * the original called WAITFRAME itself and goes on from there. */
                m->original_cycles = m->original_frame(m->original_user);
                m->frames++;
                m->busy = m->original_cycles;
                m->original_cycles = 0;
                if (m->busy > SW_IRQ_CYCLES) {
                    m->held_buffer = 0xff;
                    m->main_state = SW_MAIN_BUSY_NOSYNC;
                    return;
                }
                continue;
            }
            if (R8(SW_PT_DLY) == 0 &&      /* ?FAR ENUF INTO POWER ON? PREVENT STARTUP BLANK STARES */
                (R8(SW_IN_SW2) & 0x80) == 0) {      /* LOOK AT FREEZE SWITCH (BT7 OPTION BANK 1) */
                uint8_t a = R8(SW_VG_LON);  /* USE LEFT FIRE BIT FOR SINGLE STEPPING */
                if (!(a & SW_LFIRBT)) return;       /* NO LEFT EDGE, SO LOOP FOREVER (ALMOST) */
                W8(SW_VG_LON, a & (uint8_t)~SW_LFIRBT);     /* ELSE SINGLE STEP JUST THIS TIME */
            }
            iframe(m);
            m->main_state = SW_MAIN_WAITVG;
        }
        if (m->main_state == SW_MAIN_WAITVG) {
            if (R8(SW_VGSYNC) & 0x80) return;       /* WAIT FOR VG BUFFER: NON-NEGATIVE VALUE INDICATES BUFFER READY */
        }
        m->main_state = SW_MAIN_WAITFRAME;
        m->held_buffer = R8(SW_VGSYNC);
        run_phase(m);
        m->frames++;
        m->busy = sw_pace_frame_cycles(m);
        if (m->original_cycles) {           /* the 6809 ran the phase: its own time, counted */
            m->busy = m->original_cycles;
            m->original_cycles = 0;
        }
        if (m->busy > SW_IRQ_CYCLES && R8(SW_VGSYNC) == 0xff && !(m->held_buffer & 0x80)) {
            W8(SW_VGSYNC, m->held_buffer);
            m->main_state = SW_MAIN_BUSY;
            return;
        }
    }
}
