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
    uint8_t thumbs = R8(SW_R_THMB) & SW_THMBT;      /* ISOLATE THUMB BITS */
    uint8_t a = (uint8_t)((R8(SW_R_FIRE) & (uint8_t)~SW_THMBT) | thumbs);   /* GET THIS NEWEST VALUE */
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
    sw_vwxpld(m);                          /* CAN SEE PARTS ONCE ORIGINAL IS GONE FROM VIEW */

    sw_tstlaz(m);                           /* PREPARE FOR LAZAR HITS */
    sw_snvw(m);
    sw_vwgun(m);
    sw_clslz(m);                            /* PROCESS COLLISIONS */
    sw_vwlaz(m);                            /* AND SEE RESULT */

    sw_vwglw(m);
    vgdone(m);
}

/* VWMOVR: VIEW GROWING GAME OVER MESSAGE */
static void vwmovr(sw_machine *m)
{
    uint8_t a = R8(SW_PH_TIM + 1);
    if (a > 32) a = 32;                     /* STOP GROWING */
    uint8_t b = (uint8_t)(0xc0 - (uint8_t)(a * 6));     /* EXPAND AT 6 PER FRAME: SMALL IS 0C0, LARGE IS 000 */
    VG((SW_VGSCAL & 0xf000) | b);           /* FRONT PART OF SCALE INSTRUCTION */
    sw_domess(m, SW_MS_GO1);                /* "GAME OVER" IN CENTER OF SCREEN */
}

/* VEWS0D: the picture while the player dies in space */
static void vews0d(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);

    vwmes(m);
    sw_vwgas(m);
    sw_vwstar(m);
    sw_vwxpld(m);                           /* SHOW PARTS AFTER TIE GONE FROM DISPLAY */
    sw_snvw(m);
    sw_vwgun(m);
    vwmovr(m);                              /* VIEW EXPANDING GAME OVER MESSAGE */

    sw_vwglw(m);
    vgdone(m);
}

/* PHIS0D: SPACE TO ENDGAME TRANSITION */
static void phis0d(sw_machine *m)
{
    sw_doxmt(m, SW_IDSSKP);                 /* SKIP ANY OTHER VOICES */
    W8(SW_PHASE, SW_PHSS0D + 1);            /* EXECUTE THIS PHASE */
    W16(SW_PH_TIM, 0);                      /* NEW PHASE TIMER JUST STARTED */
}

static void phes0d(sw_machine *m)
{
    vews0d(m);

    sw_mvgun(m);
    sw_kpglow(m);
    sw_donglw(m);
    sw_doxpld(m);

    W16(SW_M_SIN, (uint16_t)-0x04ff);       /* FLIP DIRECTION OF ROLL CUZ WE ARE DIEING */
    W16(SW_M_COS, 0x3fce);
    sw_s1rl(m);

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d >= 2 * 20) W8(SW_PHASE, SW_PHSEGM);   /* 2 SECONDS */
}

/* PHIEGM: INIT END OF GAME. PREVIOUS PHASE HAS ALREADY SKIPPED OLD SPEECH */
static void phiegm(sw_machine *m)
{
    sw_doxmt(m, SW_IDSREM);                 /* SAY "REMEMBER..." */
    sw_doxmt(m, SW_IDSFOA);                 /* SAY FORCE WILL BE WITH YOU ALWAYS */
    W8(SW_PHASE, R8(SW_PHASE) + 1);         /* TRANSFER TO END OF GAME */
}

/* PHEEGM: END OF GAME */
static void pheegm(sw_machine *m)
{
    imath(m);
    iparm(m);
    istar(m);
    sw_unitv(m, SW_M_U1);

    ORIG(SW_EEACCT);                        /* UPDATE GAME TIME, GAMES PLAYED AND HISTORY */
    sw_update(m);                           /* CHECK FOR NEW HI SCORE */
    if (!(R8(SW_UPDFLG) & 0x80)) {          /* ?NEW HI SCORE? */
        W8(SW_PHASE, SW_PHSENT);            /* THEN ENTER INITIALS */
    } else {                                /* ELSE NO LUCK */
        sw_doxmt(m, SW_IDPBEN);             /* BEN'S THEME WHEN LOSE GAME WITH NO HIGH SCORE */
        W8(SW_PHASE, SW_PHSBNR);
    }
}

/* What the two space phases do alike before the ships and the player
 * move. Returns 0 if the player has died (the phase is then over). */
static int space_frame(sw_machine *m)
{
    view(m);
    if (R8(SW_S_GAS) & 0x80) {              /* EXIT WHEN PLAYER DIES */
        phis0d(m);
        return 0;
    }
    sw_mvgun(m);                           /* MOVE GUN SHOTS */
    sw_donglw(m);
    sw_doxpld(m);
    sw_do1glw(m);
    sw_do1gas(m);
    return 1;
}

static void phesp1(sw_machine *m)
{
    if (!space_frame(m)) return;
    sw_cpu(m);                              /* COMPUTER DRONES */

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
    sw_ixpld(m);
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
    sw_cpuret(m);                           /* COMPUTER DRONES RUN AWAY */

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

/* --- from space to the Death Star ---------------------------------------------------- */

/* S1MVHP: MOVE DURING HYPER */
static void s1mvhp(sw_machine *m)
{
    W16(SW_ST_UX, R16(SW_FRAME) << 8);      /* STARS RELATIVE MOVEMENT */
}

/* ?FIRST TIME THRU? ?AND SELECTED SPACE WAVE FIVE? */
static int first_wave_five(sw_machine *m)
{
    return R8(SW_SC_FWV) == 0 && R8(SW_SP_WAV) == 5 - 1;
}

/* VEWHPA: VIEW HYPER INTO SMALL DETH STAR */
static void vewhpa(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);
    vwmes(m);
    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwstar(m);
    sw_vwdtha(m);                           /* VIEW DETH STAR, MINIATURE */
    sw_vwxpld(m);                           /* PLACE AFTER TIE NO LONGER SHOWN */

    sw_tstlaz(m);                           /* PREPARE FOR LAZAR HITS */
    sw_snvw(m);
    sw_vwgun(m);
    sw_clslz(m);                            /* PROCESS COLLISIONS */
    sw_vwlaz(m);                            /* AND SEE RESULT */

    sw_vwglw(m);
    vgdone(m);
}

/* VEWHPB: VIEW HYPER INTO LARGE DETH STAR */
static void vewhpb(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);
    vwmes(m);
    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwstar(m);
    sw_vwdthb(m);                           /* VIEW DETH STAR, ENLARGING */
    sw_vwglw(m);
    vgdone(m);
}

static void phis0g(sw_machine *m)
{
    W16(SW_DT_SCL, SW_VGSCAL + 0x500 + 0x80);   /* START WITH MATCHING SIZE DETH STARS */
    W16(SW_DT_STP, 0x0100);                 /* STEP SIZE FOR SCALING THE DETH STAR */
    if (!first_wave_five(m)) {              /* (THEN SPECIAL PROCESSING: SAY ONLY THE PREVIOUS) */
        sw_doxmt(m, SW_IDSTHI);             /* THIS IS RED FIVE, I'M GOING IN */
        sw_doxmt(m, SW_IDARY);
    }
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phes0g(sw_machine *m)
{
    vewhpa(m);

    sw_doxpld(m);
    sw_do1glw(m);
    sw_do1gas(m);

    sw_stwsp1(m);                           /* TURN TOWARDS DETH STAR */
    s1mvhp(m);

    if ((int16_t)R16(SW_M_S1 + M_AX) >= 0x3f00) /* ?AIMING TOWARDS DETH STAR YET? */
        W8(SW_PHASE, SW_PHSS1G);            /* READY TO DO HYPER INTO DETH STAR */
}

static void phis1g(sw_machine *m)
{
    if (first_wave_five(m))
        sw_doxmt(m, SW_IDSSIZ);             /* LOOK AT THE SIZE OF THAT THING */
    sw_doxmt(m, SW_IDATH);                  /* THRUST TOWARDS DETH STAR */
    W8(SW_PHASE, R8(SW_PHASE) + 1);         /* GO AHEAD AND EXECUTE GROWING DETH STAR */
}

static void phes1g(sw_machine *m)
{
    vewhpb(m);                              /* VIEW THE GROWING DETH STAR */

    sw_donglw(m);
    sw_do1glw(m);
    sw_do1gas(m);

    sw_stwsp1(m);
    s1mvhp(m);

    uint16_t d = (uint16_t)((int8_t)(uint8_t)(0 - R8(SW_DT_STP)) + R16(SW_DT_SCL));
    d &= 0xff7f;
    W16(SW_DT_SCL, d);
    if (d <= SW_VGSCAL + 0x100 + 0x10) {    /* ?OBJECT BIG ENOUGH? */
        if (R8(SW_SP_WAV) == 0)             /* ?FIRST SPACE WAVE? */
            W8(SW_PHASE, SW_PHSS0B);        /* THEN SKIP AHEAD TO TRENCH */
        else                                /* ELSE ALLOW GROUND */
            W8(SW_PHASE, SW_PHSGD);         /* DO GROUND FLYING */
    }
    W16(SW_DT_STP, R16(SW_DT_STP) + 0x0060);    /* CHANGE STEP SIZE */

    uint8_t a = R8(SW_ST_BRT);              /* DIM OUT THE STARS */
    a = a <= 2 ? 0 : (uint8_t)(a - 2);
    W8(SW_ST_BRT, a);
}

/* --- the trench ----------------------------------------------------------------------- */

/* The trench itself (WSBASE.MAC), the proton torpedo and the laser's
 * collisions in the trench are not ported yet: ORIG. */

/* DOUBLE SIZE, then a message (the SCALE is RESET AUTOMATICALLY) */
static void big_message(sw_machine *m, uint8_t b)
{
    VG(SW_VGSCAL - 0x100);
    sw_domess(m, b);
}

/* VWMTWZ: MESSAGES STILL SEEN IN TRENCH */
static void vwmtwz(sw_machine *m)
{
    if ((int8_t)R8(SW_GD_WAV) > 0) {        /* NO MESSAGES IN FIRST WAVE TRENCH */
        sw_domess(m, SW_MS_TWR);            /* "TOWERS" */
        VG(SW_VGCOLR + (VGCGRN << 8));
        VG(SW_VGRW6 & 0x1fff);
        VG((SW_VGLIMR - 5 * SW_VGSCW) & 0x1fff);
        W8(SW_BT_SUP, 1);
        sw_vwbyte(m, R8(SW_GD_TWL));        /* TOWERS LEFT */
        VG(SW_VGCNTR);
    }
    if (R8(SW_Q_ATP) != 0)                  /* GOT THEM ALL, PROCLAIM SUCH */
        sw_domess(m, SW_MS_ATP);            /* "ALL TOWER TOPS" */
}

/* VEWBS: VIEW BASE TRENCH */
static void vewbs(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);

    vwmes(m);
    if (R8(SW_PH_TIM) <= 4) {
        if (R8(SW_BS_RPT) == 0)             /* ?FIRST TIME THRU THIS TRENCH? */
            vwmtwz(m);                      /* SOME TOWER PHASE MESSAGES SEEN STILL */
        else                                /* ELSE MUST HAVE MISSED AND AM REPEATING */
            big_message(m, SW_MS_MSD);      /* MISSED EXHAUST PORT */
    }
    if (R8(SW_SC_FWV) == 0 &&               /* ?FIRST WAVE? */
        R8(SW_PH_TIM) <= 8 &&
        R8(SW_BS_RPT) == 0 &&               /* ?FIRST TIME THRU THIS TRENCH? */
        R8(SW_Q_ATP) == 0) {                /* ?SPACE CLEAR SINCE NOT HIT ALL TOWER TOPS? */
        uint8_t b = SW_MS_SFB;              /* SHOOT FIREBALLS */
        /* ?FIRST WAVE WITHOUT CATWALKS? ELSE ALTERNATE WITH CATWALK ALSO */
        if (R8(SW_BS_WAV) != 0 && (R8(SW_FRAMEL) & 0x10) != 0)
            b = SW_MS_ACW;                  /* AVOID CATWALKS */
        sw_domess(m, b);
    }
    if (R8(SW_SC_FWV) == 0 && R8(SW_BS_PFL) != 0)   /* FIRST WAVE? YES, THEN MAY SHOW MESSAGE */
        big_message(m, SW_MS_EXH);          /* "EXHAUST PORT AHEAD" */
    uint8_t a = R8(SW_Q_FRC);
    if (!(a & 0x80)) {                      /* ?USING THE FORCE? */
        if (a == 0) sw_domess(m, SW_MS_USE);    /* ?BUT STILL WAITING? */
        else sw_vwfrc(m);                  /* ELSE WAS SUCCESSFUL: VIEW FORCE SCORING */
    }

    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwbase(m);                          /* VIEW STAR BASE */

    sw_tstlaz(m);                           /* PREPARE FOR LAZAR HITS */
    sw_vwgun(m);
    sw_clblz(m);                            /* PROCESS COLLISIONS */
    sw_vwlaz(m);                            /* AND SEE RESULT */

    sw_vwptgn(m);                           /* SEE THE PROTON TORPS IMMEDIATELY */

    sw_vwglw(m);
    vgdone(m);
}

/* VEWB0D: the picture while the player dies in the trench */
static void vewb0d(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);

    vwmes(m);
    if (R8(SW_Q_BSH) != 0)                  /* TEST IF WE BASHED OUR NOSES: ?HIT BACK WALL? */
        big_message(m, SW_MS_MSD);          /* MISSED EXHAUST PORT */

    sw_vwgas(m);
    sw_vwbase(m);                          /* VIEW STAR BASE */

    sw_vwgun(m);

    if (R8(SW_SC_FWV) == 0 && R8(SW_BS_PFL) != 0)   /* FIRST WAVE PLAYED? YES, THEN MAY SHOW MESSAGE */
        big_message(m, SW_MS_EXH);          /* "EXHAUST PORT AHEAD" */

    vwmovr(m);                              /* VIEW EXPANDING GAME OVER MESSAGE */

    sw_vwglw(m);
    vgdone(m);
}

/* One axis of S1MVBS: the speed forward times how far the yoke is over. */
static uint16_t slalom(sw_machine *m, uint16_t si, int twice)
{
    uint8_t a = (uint8_t)((R16(SW_M_S1 + M_VX) << 4) >> 8);     /* SCALE UP *16. */
    uint8_t b = R8(si);                     /* RESPOND IMMEDIATELY TO CONTROL POSITION--SLALOM */
    if (b & 0x80) b = (uint8_t)~b;          /* HANDLE 80 CORRECTLY,ABSB */
    if (twice) b = (uint8_t)(b << 1);       /* *2 FOR MAX SPEED SLALOM(45 DEGREE) */
    uint16_t d = (uint16_t)(a * b);
    if (R8(si) & 0x80) d = (uint16_t)(0 - d);   /* REMEMBER TO FLIP DIRECTIONS */
    return (uint16_t)((int16_t)d >> 4);     /* SCALE BACK DOWN/16. */
}

/* S1MVBS: WORMHOLE PLAYER MOVEMENT */
static void s1mvbs(sw_machine *m)
{
    uint16_t d = (uint16_t)(R16(SW_M_S1 + M_VX) + R16(SW_M_S1 + M_TX));    /* PLAYERS FORWARD MOVEMENT */
    W16(SW_M_S1 + M_TX, d);
    W16(SW_M_U1 + M_TX, d);

    d = slalom(m, SW_SI_RSX, 0);            /* FASTEST IS AT 45 DEGREES */
    W16(SW_M_S1 + M_VY, d);                 /* ALLOW OTHERS TO KNOW OUR SPEED */
    d = (uint16_t)(d + R16(SW_M_S1 + M_TY));
    if ((int16_t)d > 0x1ff) d = 0x1ff;
    if ((int16_t)d < -0x1ff) d = (uint16_t)-0x1ff;
    W16(SW_M_S1 + M_TY, d);
    W16(SW_M_U1 + M_TY, d);

    d = slalom(m, SW_SI_RSY, 1);
    W16(SW_M_S1 + M_VZ, d);
    d = (uint16_t)(d + R16(SW_M_S1 + M_TZ));
    if ((int16_t)d > -0x101) d = (uint16_t)-0x101;
    if ((int16_t)d < -0xdff) d = (uint16_t)-0xdff;
    W16(SW_M_S1 + M_TZ, d);
    W16(SW_M_U1 + M_TZ, d);
}

/* The difficulty of a wave on the Death Star: its number and the option. */
static void wave_hardness(sw_machine *m, uint8_t a, uint8_t add)
{
    a = (uint8_t)(a + add);
    if (a > 15) a = 15;
    W8(SW_WV_HRD, a);
}

/* PHIS0B, PHIG0B: SPACE TO BASE TRANSITION */
static void phis0b(sw_machine *m)
{
    W8(SW_BS_RPT, 0);                       /* FIRST TIME THIS WAVE: FIRST TIME THRU */
    W8(SW_Q_FRC, 0);                        /* ALLOWED TO USE THE FORCE */
    uint8_t a = R8(SW_GM_WAV);
    if (a > 31) a = 31;
    W8(SW_BS_WAV, a);
    wave_hardness(m, a, R8(SW_GM_DIF));
    sw_gnbase(m);                          /* GEN A NEW BASE TRENCH */
    sw_doxmt(m, SW_IDSUSE);                 /* USE THE FORCE LUKE */
    W16(SW_PH_TIM, 0);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phes0b(sw_machine *m)
{
    iparm(m);
    sw_nwbase(m);  
    W8(SW_PHASE, SW_PHSBS);
}

/* PHIB0B: BASE TO BASE TRANSITION */
static void phib0b(sw_machine *m)
{
    W8(SW_BS_RPT, 1);                       /* REPEATING THIS WAVE */
    iparm(m);
    sw_nwbase(m);                          /* DO NOT GENERATE A NEW BASE: JUST INIT THE CURRENT SELECTION */
    W8(SW_Q_FRC, 0xff);                     /* NO FORCE ALLOWED ON RERUNS */
    /* MAKE TRENCH HARDER ON REPEAT PASSES: BUMP UP THE DIFFICULTY */
    wave_hardness(m, R8(SW_WV_HRD), R8(SW_GM_BMP));
    W8(SW_PHASE, SW_PHSBS);                 /* PHEB0B */
}

/* PHIB0D: BASE TRENCH TO ENDGAME TRANSITION */
static void phib0d(sw_machine *m)
{
    sw_doxmt(m, SW_IDSSKP);                 /* SKIP ANY WAITING VOICES */
    W8(SW_PHASE, SW_PHSB0D + 1);            /* EXECUTE THIS PHASE */
    W16(SW_PH_TIM, 0);                      /* NEW PHASE TIMER JUST STARTED */
}

static void pheb0d(sw_machine *m)
{
    vewb0d(m);

    sw_mvgun(m);
    sw_kpglow(m);

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d >= 2 * 20) W8(SW_PHASE, SW_PHSEGM);   /* 2 SECONDS */
}

static void phebs(sw_machine *m)
{
    vewbs(m);                               /* VIEW BASE TRENCH */

    if (R8(SW_S_GAS) & 0x80) {              /* EXIT WHEN PLAYER DIES */
        phib0d(m);
        return;
    }

    sw_mvgun(m);
    sw_mvptgn(m);                           /* PLAYERS PROTON TORP GUN */
    sw_dobase(m);                          /* FIRES BASES SHOTS */
    sw_do1qglw(m);                          /* QUICKER GLOW IN TRENCH SEQUENCES */
    sw_do1gas(m);

    /* TWIRL THE SHIP THRU THE WORMHOLE (S1TWBS does nothing) */
    vgtosite(m);                            /* PULL IN SITE COORDS FROM IRQ RHEOSTATS */
    is1uv(m, SW_MSS1BC);
    s1mvbs(m);                              /* BASE TRENCH CONTROLS MOTION */

    if ((R8(SW_FRAME + 1) & 0x0f) == 0) {   /* ?PSUEDO SECOND PASSED? */
        uint8_t a = R8(SW_PH_TIM);
        if (a == 2)                         /* ?FINISHED WITH VOICE OR CURSING R2? */
            sw_doxmt(m, SW_IDPRRP);         /* THEN DO REBEL REPEAT THEME */
        if ((R8(SW_BS_WAV) & 0x01) == 0) {  /* BEGIN WITH WAVE 0 SAYING . . . */
            if (a == 16) sw_doxmt(m, SW_IDSTRU);        /* LUKE, TRUST ME */
            else if (a == 24) sw_doxmt(m, SW_IDSYAU);   /* YAHOO, YOURE ALL CLEAR KID */
        } else {                            /* ELSE ALTERNATE WAVE: OCCURS A BIT SOONER */
            if (a == 16) sw_doxmt(m, SW_IDSLET);        /* LET GO LUKE */
            else if (a == 22) sw_doxmt(m, SW_IDSSTR);   /* THE FORCE IS STRONG WITH THIS ONE */
        }
        W8(SW_PH_TIM, R8(SW_PH_TIM) + 1);
    }

    if (R8(SW_BS_EFL) == 0) return;         /* ?STARTING TO VIEW END OF TRENCH? */
    if ((uint16_t)(R16(SW_BS_ELC) - R16(SW_M_S1 + M_TX)) > 0x0800) return;
    /* ABOUT TO BASH OUR NOSE IN THE END WALL */
    if (R8(SW_PT_LIV) == 0) {               /* DID WE KILL IT?? NO, TOO BAD--NO EXPLOSION */
        W8(SW_Q_BSH, 1);                    /* FLAG FOR BASHED OUR NOSE */
        sw_doxmt(m, SW_IDACR);              /* CRASHED */
        sw_bg1glw(m);                       /* BASHED OUR NOSE -- LIMITS REPEATS */
        if ((int8_t)R8(SW_S_GAS) <= 0) {    /* ?WILL THIS KILL PLAYER? THEN EXIT */
            phib0d(m);
            return;
        }
        W8(SW_PHASE, SW_PHSB0B);            /* TRY TRENCH AGAIN UNTIL SUCCESSFUL */
        sw_doxmt(m, SW_IDSR2N);             /* R2 SWEARS AT PLAYER FOR MISSING EXHAUST PORT */
    } else {                                /* YES, LET'S WATCH IT EXPLODE! */
        W8(SW_PHASE, SW_PHSDX1);
        uint8_t a = R8(SW_GM_WAV);
        if ((int8_t)a >= 4 - 1 && (a & 1))  /* ?WAVE 4 OR HIGHER? ?ANDIF AN EVEN(ODD-1) WAVE? */
            sw_doxmt(m, SW_IDSGRE);         /* GREAT SHOT KID, THAT WAS 1 IN A MILLION */
    }
}

static void phibs(sw_machine *m)
{
    W8(SW_Q_BSH, 0);                        /* NOT YET BASHED OUR NOSES */
    W8(SW_PH_TIM, 0);                       /* TIMER FOR BASE STAR A */
    W16(SW_M_S1 + M_VX, 0x300);             /* INITIAL PLAYER SPEED */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
    phebs(m);                               /* DROP THRU -- DECREASE SCREEN FREEZE */
}

/* --- the ground: towers and bunkers --------------------------------------------------- */

/* The ground itself (WSGRND.MAC) is not ported yet: ORIG. */

#define GD_MNT 0x0200       /* GD$MNT, GD$MXT: how low and how high the player flies */
#define GD_MXT 0x1c00

/* ISTARG: the stars over the ground */
static void istarg(sw_machine *m)
{
    for (uint16_t x = SW_M_STAD; x < SW_M_STAZ; x += 8) {
        uint8_t a = R8(SW_P_RND2);          /* REVERSE OF INITING ORDER FOR RANDOMNESS */
        uint8_t b = R8(SW_P_RND1);          /* TRY TO POPULATE EVENLY */
        W16(x + 0, (uint16_t)((a << 8) | b));
        uint16_t d = (uint16_t)(a * b);
        W16(x + 2, (uint16_t)((R8(SW_P_RND2) << 8) | (d & 0xff)));
        W16(x + 4, 0);
    }
}

/* VWMTWR: VEIW MESSAGES RELATED TO TOWERS */
static void vwmtwr(sw_machine *m)
{
    if ((int8_t)R8(SW_GD_WAV) <= 0) {       /* LOWEST WAVE GETS NO MESSAGES ( NO TOWERS ) */
        if (R8(SW_Q_ATP) != 0) sw_domess(m, SW_MS_ATP);
        return;
    }
    if (R8(SW_GD_TWL) != 0) {               /* ?MORE TOWERS TO HIT? */
        if ((R8(SW_FRAMEL) & 0x30) != 0) {  /* ?SHOW NEXT TOWERS WORTH? */
            sw_domess(m, SW_MS_NXT);        /* "XXXXX POINTS NEXT TOWER" */
            VG(SW_VGCOLR + (VGCGRN << 8));
            VG(SW_VGRW7 & 0x1fff);          /* Y POS */
            VG(0x1ed0 & 0x1fff);            /* X POS */
            W8(SW_BT_SUP, 0x04);            /* SUPRESS 4 ZEROES */
            sw_vwbyte(m, R8(SW_TWRMUL));
            sw_vwbyte(m, R8(SW_TWRMUL + 1));
            sw_vwbyte(m, R8(SW_TWRMUL + 2));
            VG(SW_VGCNTR);
        } else {                            /* ELSE SHOW ALL TOWERS WORTH */
            sw_domess(m, SW_MS_RWD);        /* "50,000 FOR SHOOTING ALL TOWERS" */
        }
    }
    vwmtwz(m);
}

/* VEWGD: VIEW GROUND FLYING */
static void vewgd(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);
    vwmes(m);
    vwmtwr(m);                              /* TOWER RELATED MESSAGES */
    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwstrg(m);                           /* VIEW GROUND STARS */

    sw_tstlaz(m);                           /* PREPARE FOR LAZAR HITS */
    sw_vwgun(m);
    ORIG(SW_VWGRND);
    sw_clglz(m);                            /* PROCESS COLLISIONS */
    sw_vwlaz(m);                            /* AND SEE RESULT */

    sw_vwxpld(m);
    sw_vwglw(m);
    vgdone(m);
}

/* VEWG0D: the picture while the player dies over the ground */
static void vewg0d(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);

    vwmes(m);
    vwmtwr(m);                              /* TOWER RELATED MESSAGES */
    sw_vwgas(m);
    sw_vwstrg(m);                           /* VIEW GROUND STARS */

    sw_vwgun(m);
    ORIG(SW_VWGRND);
    vwmovr(m);                              /* VIEW EXPANDING GAME OVER MESSAGE */

    sw_vwglw(m);
    vgdone(m);
}

/* VEWG0B, VEWG1B: GROUND TO BASE TRANSITION, over the ground and then
 * over the trench */
static void vewgnb(sw_machine *m, int trench)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);
    vwmes(m);
    vwmtwz(m);                              /* TOWER DONE, END MESSAGES ONLY */
    sw_domess(m, SW_MS_USE);                /* USE THE FORCE */
    sw_vwgas(m);
    sw_vwsite(m);
    if (trench) sw_vwbase(m);
    else sw_vwstrg(m);                      /* VIEW GROUND STARS */

    sw_vwxpld(m);
    sw_vwglw(m);
    vgdone(m);
}

/* S1MVGD: GROUND FLYING PLAYER MOVEMENT */
static void s1mvgd(sw_machine *m)
{
    /* PLAYERS FORWARD MOVEMENT */
    int32_t t = (int16_t)R16(SW_M_S1 + M_VX) + (int16_t)R16(SW_M_S1 + M_TX);
    if (t > 32767 || t < -32768) {          /* ?WRAPPED AROUND? */
        uint8_t a = R8(SW_GD_SEQ);
        W8(SW_GD_SEQ, a + 1);               /* THEN START UP NEXT SEQUENCE */
        if (a == 0x7f) W8(SW_GD_SEQ, a);    /* PREVENT ITS OVERFLOW */
    }
    W16(SW_M_S1 + M_TX, (uint16_t)t);
    W16(SW_M_U1 + M_TX, (uint16_t)t);

    uint16_t d = slalom(m, SW_SI_RSX, 1);   /* FASTEST IS AT 45 DEGREES: FULL SCALE VALUE */
    W16(SW_M_S1 + M_VY, d);                 /* ALLOW OTHERS TO KNOW OUR SPEED */
    d = (uint16_t)(d + R16(SW_M_S1 + M_TY));
    W16(SW_M_S1 + M_TY, d);
    W16(SW_M_U1 + M_TY, d);

    d = slalom(m, SW_SI_RSY, 0);            /* SLOWER RANGE */
    W16(SW_M_S1 + M_VZ, d);
    d = (uint16_t)(d + R16(SW_M_S1 + M_TZ));
    if ((int16_t)d > GD_MXT) d = GD_MXT;
    else if ((int16_t)d < GD_MNT) d = GD_MNT;
    W16(SW_M_S1 + M_TZ, d);
    W16(SW_M_U1 + M_TZ, d);
}

/* SMVG9B: REST OF G*B TRANSITION: on, at an even speed, and once around */
static void smvg9b(sw_machine *m)
{
    W16(SW_M_S1 + M_TX, R16(SW_M_S1 + M_VX) + R16(SW_M_S1 + M_TX));    /* MOVE PLAYER FORWARD */
    uint16_t d = (uint16_t)((int16_t)(uint16_t)(0x300 - R16(SW_M_S1 + M_VX)) >> 3);
    W16(SW_M_S1 + M_VX, d + R16(SW_M_S1 + M_VX));
    /* VARY WHICH WAY WE ROTATE: 10.55 DEGREES *34. GIVES A 360 */
    W16(SW_M_SIN, (R8(SW_GM_WAV) & 1) ? 3000 : (uint16_t)-3000);
    W16(SW_M_COS, 16107);
    sw_s1rl(m);                             /* LETS ROLLEM */
    sw_untwgd(m, (uint16_t)(0 - R16(SW_GD_ACT)));       /* REMEMBER TO UNTWIST ANY GROUND MOVEMENT */
}

/* SMVG0B: the player comes down to the trench */
static void smvg0b(sw_machine *m)
{
    uint16_t d = R16(SW_M_S1 + M_TZ);
    if ((int16_t)d > 0x380)                 /* ?PLAYER TOO HI? */
        W16(SW_M_S1 + M_TZ, d - 0x180);     /* DROP HIM DOWN */
    smvg9b(m);
}

/* SMVG1B: and into it */
static void smvg1b(sw_machine *m)
{
    uint16_t d = R16(SW_M_S1 + M_TZ);
    if ((int16_t)d > -0x0e00 + 0x100)       /* JUST ABOVE BOTTOM OF TRENCH */
        W16(SW_M_S1 + M_TZ, d - 0x100);     /* DROP PLAYER INTO TRENCH */
    smvg9b(m);
}

/* PHIG0D: GROUND TO ENDGAME TRANSITION */
static void phig0d(sw_machine *m)
{
    sw_doxmt(m, SW_IDSSKP);                 /* SKIP ANY WAITING VOICES */
    W8(SW_PHASE, SW_PHSG0D + 1);            /* EXECUTE THIS PHASE */
    W16(SW_PH_TIM, 0);                      /* NEW PHASE TIMER JUST STARTED */
}

static void pheg0d(sw_machine *m)
{
    vewg0d(m);

    sw_mvgun(m);
    sw_kpglow(m);

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d >= 2 * 20) W8(SW_PHASE, SW_PHSEGM);   /* 2 SECONDS */
}

/* PHIGD: GROUND TERRAIN FOLLOWING */
static void phigd(sw_machine *m)
{
    uint8_t a = (uint8_t)(R8(SW_GM_WAV) - 1);   /* BUMP TO NEXT GROUND WAVE: ALWAYS SKIPPED ON FIRST GAME WAVE */
    if (a >= 31) a = 31;
    W8(SW_GD_WAV, a);
    a = (uint8_t)(a + R8(SW_GM_DIF));
    if (a > 15) a = 15;
    W8(SW_WV_HRD, a);
    iparm(m);                               /* INIT SHIP AND ALIEN PARMS */
    istarg(m);
    ORIG(SW_IGRND);
    sw_ixpld(m);
    W16(SW_M_S1 + M_VX, 0x100);             /* INITIAL PLAYER SPEED */
    W16(SW_M_S1 + M_TX, 0x100 >> 1);        /* OFFSET POSITION INTO MIDDLE FOR COLLISION DETECT */
    W16(SW_M_S1 + M_TZ, 0x2000);            /* INITIAL HITE */
    W8(SW_GD_SEQ, 0);
    W8(SW_Q_ATP, 0);                        /* NOT YET CLEARED ALL TOWER TOPS */
    W8(SW_Q_KTW, 0);                        /* NO KILL TOWERS */

    W8(SW_PH_TIM, 0);

    sw_doxmt(m, SW_IDP4TH);                 /* BATTLE MUSIC IN FOURTHS */

    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

/* PHEGD: DO GROUND FLYING */
static void phegd(sw_machine *m)
{
    vewgd(m);

    if (R8(SW_S_GAS) & 0x80) {              /* EXIT WHEN DEAD */
        phig0d(m);
        return;
    }

    sw_mvgun(m);
    sw_do1glw(m);
    sw_do1gas(m);
    sw_doxpld(m);

    /* TWIRL THE SHIP THRU GROUND FLYING */
    vgtosite(m);                            /* PULL IN SITE COORDS FROM IRQ RHEOSTATS */
    sw_s1twgd(m);
    is1uv(m, SW_MSS1BC);
    s1mvgd(m);

    uint16_t d = (uint16_t)(R16(SW_M_S1 + M_VX) + 1);
    if (d <= 0x400) W16(SW_M_S1 + M_VX, d);

    if ((R8(SW_FRAME + 1) & 0x0f) == 0) {
        if (R8(SW_PH_TIM) == 14)
            sw_doxmt(m, SW_IDPREB);         /* FINISH GROUND WITH REBEL */
        W8(SW_PH_TIM, R8(SW_PH_TIM) + 1);   /* ONE MORE PSUEDO-SECOND */
    }

    if (R8(SW_GD_SEQ) >= 5) {               /* ONLY GO SO FAR INTO GROUND SEQUENCES */
        W8(SW_Q_KTW, 0x01);                 /* CLEAR TOWERS FROM GROUND */
        if (R8(SW_M_S1 + M_TX) < 0x80)      /* SEQUENCE RUNS 80 TO 80: ?ONE FULL PASS INTO BLANKED SECTION 5? */
            W8(SW_PHASE, SW_PHSG0B);        /* BASE TRENCH FOLLOWS GROUND TOWERS */
    }
}

static void pheg0b(sw_machine *m)
{
    vewgnb(m, 0);

    sw_do1glw(m);
    sw_do1gas(m);
    sw_doxpld(m);
    smvg0b(m);

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d >= 17) W8(SW_PHASE, SW_PHSG1B);   /* 34. COUNTS FOR 360 TURNOVER, DO HALF HERE */
}

static void pheg1b(sw_machine *m)
{
    vewgnb(m, 1);

    sw_do1glw(m);
    sw_do1gas(m);
    sw_doxpld(m);
    smvg1b(m);

    uint16_t d = (uint16_t)(R16(SW_PH_TIM) + 1);
    W16(SW_PH_TIM, d);
    if (d < 17) return;                     /* ?TIME TO GOTO BASE? */

    uint16_t x = R16(SW_M_S1 + M_TX);       /* SAVE OUR LOCATION */
    uint16_t u = R16(SW_M_S1 + M_TY);
    d = R16(SW_M_S1 + M_TZ);
    iparm(m);                               /* INIT ALL PARMS */
    W16(SW_M_S1 + M_TX, x);                 /* RESTORE OUR LOCATION */
    W16(SW_M_S1 + M_TY, u);
    W16(SW_M_S1 + M_TZ, d);

    W8(SW_Q_FRC, 0);                        /* ALLOWED TO USE THE FORCE (MAKING SURE) */
    W8(SW_PHASE, SW_PHSBS);
}

static void phig1b(sw_machine *m)
{
    W16(SW_M_S1 + M_TX, 0);
    W16(SW_M_S1 + M_TY, 0);                 /* GET US AT START OF TRENCH */
    W16(SW_PH_TIM, 0);                      /* TIMER */

    smvg1b(m);                              /* ROLL AGAIN TO SKIP JERK */
    sw_nwbase(m);

    W8(SW_PHASE, R8(SW_PHASE) + 1);
    pheg1b(m);
}

/* --- the Death Star explodes, the next wave ------------------------------------------- */

/* VEWDX1: DETH STAR PARTIAL PHASE 1 */
static void vewdx1(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    vwmes(m);
    if ((int8_t)R8(SW_Q_FRC) > 0) sw_vwfrc(m);  /* ?SUCCESSFUL USE OF THE FORCE? */
    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwdthb(m);                           /* VIEW DETH STAR, ENLARGING */
    vgdone(m);
}

/* VEWDX2: DETH STAR PARTIAL PHASE 2 */
static void vewdx2(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    vwmes(m);
    sw_vwgas(m);
    sw_vwsite(m);
    /* VIEW DETH STAR MINIATURE */
    TAG(SW_TAG_DSTAR, 3);
    VG(SW_VGOFFY & 0x1fff);                 /* Y OFFSET */
    VG(0x0000);                             /* NO X OFFSET */
    VG(0x7200);                             /* SCALE FOR MINIATURE */
    VG(SW_VJBMIN);                          /* GET JSRL TO MINNY BASE OF DETH STAR */
    VG(SW_VGSCAL);                          /* RESTORE BASE */

    sw_vwxpln(m);                           /* DETH STAR PRETTY STARTING */
    vgdone(m);
}

/* VEWDX3: DETH STAR PARTIAL PHASE 3 */
static void vewdx3(sw_machine *m)
{
    ivgen1(m);
    vwmes(m);
    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwxpln(m);
    vgdone(m);
}

static void phidx1(sw_machine *m)
{
    W16(SW_DT_SCL, SW_VGSCAL + 0x100 + 0x04);   /* START DEATH STAR AT VERY LARGE */
    W16(SW_DT_STP, 0x0aff);                 /* STEP SIZE */
    istar(m);                               /* SETUP STARS */
    sw_unitv(m, SW_M_U1);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
    sw_doxmt(m, SW_IDPEND);                 /* START END OF DETH STAR MUSIC */
}

static void phedx1(sw_machine *m)
{
    vewdx1(m);

    sw_do1gas(m);
    sw_do1glw(m);

    uint16_t d = (uint16_t)((int8_t)R8(SW_DT_STP) + R16(SW_DT_SCL) + 0x80);    /* D7 IS WRAP */
    d &= 0xff7f;
    W16(SW_DT_SCL, d);
    if (d >= SW_VGSCAL + 0x400 + 0x80) W8(SW_PHASE, SW_PHSDX2);
    d = (uint16_t)(R16(SW_DT_STP) - 0x0010);    /* CHANGE STEP SIZE */
    if (d & 0x8000) d = 0x0000;
    W16(SW_DT_STP, d);
}

static void phidx2(sw_machine *m)
{
    sw_bgxpln(m);                           /* START EXPLOSION */
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phedx2(sw_machine *m)
{
    vewdx2(m);
    if (R8(SW_XP_PHS) >= 0x01) W8(SW_PHASE, SW_PHSDX3);    /* GOTO NEXT PHASE */
}

static void phedx3(sw_machine *m)
{
    vewdx3(m);
    if (R8(SW_XP_PHS) == 0) W8(SW_PHASE, SW_PHSNXT);    /* DEATH STAR FINISHED EXPLODING */
}

/* VEWNXT: the picture between two Death Stars: what was earned */
static void vewnxt(sw_machine *m)
{
    ivgen1(m);
    is1uv(m, SW_MSS1BC);                    /* MATH BOX, PRIMARY INIT */
    sw_residu(m);

    vwmes(m);
    sw_vwgas(m);
    sw_vwsite(m);
    sw_vwstar(m);

    sw_domess(m, SW_MS_DSD);                /* "DEATH STAR DESTROYED" */

    int8_t tim = (int8_t)R8(SW_PH_TIM);
    if (tim <= 2) {
        sw_domess(m, SW_MS_BRE);            /* "BONUS FOR REMAINING ENERGY" */
        sw_domess(m, SW_MS_BRE + 1);        /* "5,000 X " */
        VG(0x1fff & 144);                   /* Y POS */
        VG(0x1fff & 112);                   /* X POS */
        sw_vwnibl(m, R8(SW_GS_OLD));        /* SHOW ENERGY LEFT */
        VG(SW_VGCNTR);
    }

    if (tim <= 1 && R8(SW_PT_LIV) != 0) {   /* DID PLAYER GET KILL DETH STAR: THEN GIVE BONUS TYPE MESSAGE */
        uint8_t a = R8(SW_OPTS1) & 0x03;    /* HIGH BYTE OF 2ND OPTION BANK: BONUS FUEL UPON KILLING DETH STAR */
        if (a != 0) {
            uint8_t b = (uint8_t)((R8(SW_OPTS1 + 1) & 3) + 6);
            if (b <= R8(SW_S_GAS)) {        /* ?ARE WE FULL UP ON GAS? */
                b = SW_MS_BSE + 1;          /* SHIELD AT FULL STRENGTH */
            } else {                        /* ELSE TELL HOW MANY ADDED */
                VG(SW_VJMFL);               /* MAKE IT FLASHY LIKE THE REST OF THE MESSAGE */
                VG(0x1fff & 72);            /* SHOW THE AMOUNT */
                VG(0x1fff & (uint16_t)-320);    /* X POS */
                W8(SW_BT_SUP, 0x01);
                sw_vwnibl(m, a);
                VG(SW_VGCNTR);
                b = SW_MS_BSE;              /* "X ADDED TO DEFLECTOR SHIELD" */
            }
            sw_domess(m, b);
        }
    }

    if (tim <= 0 && R8(SW_SC_FWV) == 0 &&   /* FIRST WAVE? */
        R8(SW_GM_WAV) != 0)                 /* WASN'T WAVE 0, SO GIVE THEM MESSAGE */
        sw_domess(m, SW_MS_BON);            /* "DEATH STAR BONUS EARNED" */

    sw_vwglw(m);
    vgdone(m);
}

static void phinxt(sw_machine *m)
{
    imath(m);
    iparm(m);
    W8(SW_M_S1 + M_AX, 0xc0);               /* TURN PLAYER AWAY FROM NEW DETH STAR */
    W8(SW_M_S1 + M_BY, 0xc0);
    W8(SW_PH_TIM, 4);
    W8(SW_PHASE, R8(SW_PHASE) + 1);
}

static void phenxt(sw_machine *m)
{
    if ((R8(SW_FRAMEL) & 0x0f) == 0) {      /* ABOUT A SECOND */
        W8(SW_PH_TIM, R8(SW_PH_TIM) - 1);
        uint8_t a = R8(SW_PH_TIM);
        if (a == 3 && R8(SW_PT_LIV) != 0)   /* ?FIRED AT THE EXHAUST PORT? */
            sw_scrport(m);                  /* SCORE THE EXHAUST PORT */
        if (a == 2) sw_scrshld(m);          /* ADD SHIELD ENERGY BONUS */
        if (a == 1 && R8(SW_PT_LIV) != 0)   /* ADD BONUS ENERGY TO SHIELDS IF DESERVED: DID PLAYER KILL DETH STAR? */
            sw_adcgas(m);                   /* ADD CANNED GAS */
        if (a == 0) sw_scrwav(m);           /* ?ALMOST AT END OF NEXT PHASE? AND ALSO ADD WAV BONUS */
    }

    vewnxt(m);                              /* VIEW NXT PHASE STUFF */

    sw_do1gas(m);                           /* WE MIGHT INC GAS */
    sw_smvsp1(m);                           /* MOVE PLAYER (STARS) */

    if (R8(SW_PH_TIM) != (uint8_t)-2) return;
    /* WRAP TO NEXT DETH STAR */
    uint8_t a = (uint8_t)(R8(SW_GM_WAV) + 1);
    if (a > 98) a = 98;                     /* MAX OUT YET? YES, LIMIT IT */
    W8(SW_GM_WAV, a);
    if (R8(SW_GM_WAV) < 6 - 1) {            /* IGNORE BUMP WHEN ABOVE SELECTALEVEL: ?STARTING WAVE 0 TO 5? */
        a = (uint8_t)(R8(SW_GM_BMP) + 1);   /* USED TO KILL OFF RINGERS WHO START TO EASY */
        if (a > 4) a = 4;                   /* MAX TO BUMP */
        W8(SW_GM_BMP, a);
    }
    a = (uint8_t)(R8(SW_GM_DIF) + R8(SW_GM_BMP));   /* GAME DEFAULT DIFFICULTY GOES UP EACH WAVE */
    if (a > 15) a = 15;                     /* 0F MAX DIFFICULTY LEVELS (WORKS TO WAVE 10.) */
    W8(SW_GM_DIF, a);
    W8(SW_SC_FWV, 0xff);                    /* NO MORE FIRST WAVES */
    W8(SW_PHASE, SW_PHSBGN);                /* GO TO BEGINNING OF NEW WAVE */
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
    case SW_PHSS0G:     phis0g(m); break;
    case SW_PHSS0G + 1: if (!m->original_call) goto unported; phes0g(m); break;
    case SW_PHSS1G:     phis1g(m); break;
    case SW_PHSS1G + 1: if (!m->original_call) goto unported; phes1g(m); break;
    case SW_PHSS0B:
    case SW_PHSG0B:     if (!m->original_call) goto unported; phis0b(m); break;
    case SW_PHSS0B + 1: if (!m->original_call) goto unported; phes0b(m); break;
    case SW_PHSBS:      if (!m->original_call) goto unported; phibs(m); break;
    case SW_PHSBS + 1:  if (!m->original_call) goto unported; phebs(m); break;
    case SW_PHSB0B:     if (!m->original_call) goto unported; phib0b(m); break;
    case SW_PHSB0B + 1: W8(SW_PHASE, SW_PHSBS); break;  /* PHEB0B */
    case SW_PHSB0D:     phib0d(m); break;
    case SW_PHSB0D + 1: if (!m->original_call) goto unported; pheb0d(m); break;
    case SW_PHSGD:      if (!m->original_call) goto unported; phigd(m); break;
    case SW_PHSGD + 1:  if (!m->original_call) goto unported; phegd(m); break;
    case SW_PHSG0B + 1: pheg0b(m); break;
    case SW_PHSG1B:     phig1b(m); break;
    case SW_PHSG1B + 1: pheg1b(m); break;
    case SW_PHSG0D:     phig0d(m); break;
    case SW_PHSG0D + 1: if (!m->original_call) goto unported; pheg0d(m); break;
    case SW_PHSDX1:     phidx1(m); break;
    case SW_PHSDX1 + 1: if (!m->original_call) goto unported; phedx1(m); break;
    case SW_PHSDX2:     phidx2(m); break;
    case SW_PHSDX2 + 1: phedx2(m); break;
    case SW_PHSDX3:     W8(SW_PHASE, R8(SW_PHASE) + 1); break;  /* PHIDX3 */
    case SW_PHSDX3 + 1: phedx3(m); break;
    case SW_PHSNXT:     phinxt(m); break;
    case SW_PHSNXT + 1: if (!m->original_call) goto unported; phenxt(m); break;
    case SW_PHSS0D:     phis0d(m); break;
    case SW_PHSS0D + 1: if (!m->original_call) goto unported; phes0d(m); break;
    case SW_PHSEGM:     phiegm(m); break;
    case SW_PHSEGM + 1: if (!m->original_call) goto unported; pheegm(m); break;
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
