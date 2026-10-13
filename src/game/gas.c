/* gas.c - WSGAS.MAC: the deflector shield ("gas") and its gauge.
 *
 * Ported so far: losing shield strength (DO1GAS) and the gauge with its
 * animation after a hit (VWGAS). The score routines of the same file
 * follow with the routines that call them. Line by line after the 1983
 * source; comments in capitals are the original's. */
#include "game/game.h"
#include "game/m6809.h"

/* Tables of the source that are local labels (read off VWGAS's code). */
#define TGCLR 0x96B6        /* COLOR BY S.GAS */
#define TGALL 0x96CA        /* DRAW WHOLE PIC, TOP,UPSTROKE,BASELINE */
#define TGUP  0x96DE        /* DRAW UPSTROKE FOLLOWED BY BASE LINE */
#define TGDRW 0x96F2        /* ANIMATE BY GOING THRU THESE BASE LINES */
#define TGDLM 0x9718        /* WHERE TO START UNDOING OLD OVERLAY ON GAUGE */

/* A word of a table indexed by a shield count (LSLB, LDx B(X)). */
static uint16_t entry(sw_machine *m, uint16_t table, uint8_t count)
{
    return R16(table + (int8_t)(uint8_t)(count << 1));
}

/* ADCGAS: ADD CANNED GAS */
void sw_adcgas(sw_machine *m)
{
    uint8_t a = R8(SW_OPTS1) & 0x03;        /* HOW MUCH BONUS ENERGY? */
    if (a == 0) return;
    W8(SW_S_GAS, a + R8(SW_S_GAS));         /* ADD IT IN */
    a = (uint8_t)((R8(SW_OPTS1 + 1) & 0x03) + 0x06);    /* MAX ALLOWABLE GAS IS STARTING AMOUNT */
    if (a < R8(SW_S_GAS)) W8(SW_S_GAS, a);  /* OVER THE MAX LIMIT: SO SET IT TO THE LIMIT */
    W8(SW_S_CAN, 20);                       /* SHOW GAS REFUEL FOR A SECOND */
}

/* DO1GAS */
void sw_do1gas(sw_machine *m)
{
    if ((int8_t)R8(SW_GS_GLW) > 0 && (int8_t)R8(SW_GS_HIT) <= 0) {  /* ?SHIELDS HIT? ?AND DONE WITH PREVIOUS GAS CHANGE? */
        W8(SW_GS_HIT, 1);                   /* GAS NOW HANDLING BEING HIT */
        W8(SW_GS_OLD, R8(SW_S_GAS));        /* SAVE OLD AMOUNT FOR FLICKER */
        W8(SW_GS_LEN, (uint8_t)(-10 + R8(SW_S_GAS)));   /* START DISPLAY ANIMATION FROM SCRATCH */
        uint8_t before = R8(SW_S_GAS);
        W8(SW_S_GAS, before - 1);           /* THEN DECREASE OUR FUEL */
        if ((int8_t)before <= 0) {          /* ?OUT OF FUEL? (DEC, then LT) */
            W8(SW_S_GAS, 0xff);             /* SET TO DEAD */
            W8(SW_GS_HIT, 0);               /* NO ANIMATION CUZ FINAL MESSAGE ALREADY ON */
        }
        uint8_t a = R8(SW_S_GAS);
        if (a == 0) {
            sw_doxmt(m, SW_IDSLOS);         /* I'VE LOST R2 */
            sw_doxmt(m, SW_IDADR);          /* DEAD R2, SWAN SONG */
        } else if (a == 1) {
            sw_doxmt(m, SW_IDARC);          /* GAS AT 1 COOKS R2 */
        } else if (a == 2) {
            sw_doxmt(m, SW_IDSR2T);         /* R2 TRY AND INCREASE THE POWER */
            sw_doxmt(m, SW_IDARD);          /* R2 DONE */
        }
        W8(SW_GS_VTP, 0);                   /* NO VIEW TOP OF GAUGE */
        W8(SW_GS_VUP, 0);                   /* NO VIEW UP PART OF GAUGE */
        W8(SW_GS_VBS, 0);                   /* NOT EVEN BASE OF GAUGE YET */
    }
    if (R8(SW_S_CAN) != 0) W8(SW_S_CAN, R8(SW_S_CAN) - 1);     /* DEC CAN ( FLAG FOR BRIGHT ) UNTIL 00 */
}

/* GSVHIT: VIEW A DAMAGED FUEL GAUGE */
static void gsvhit(sw_machine *m)
{
    VG(entry(m, TGCLR, R8(SW_S_GAS)));      /* GET CORRECT COLOR */
    VG(entry(m, TGALL, R8(SW_S_GAS)));      /* DRAW ALL OF THE GAUGE */
}

/* GSVALL: VIEW ALL OF GAS GAUGE */
static void gsvall(sw_machine *m)
{
    uint16_t d = entry(m, TGCLR, R8(SW_S_GAS));
    if (R8(SW_S_CAN) != 0) d |= 0x00ff;     /* TEST BRIGHT FLAG: MAKE VERY BRIGHT */
    VG(d);
    VG(entry(m, TGALL, R8(SW_S_GAS)));
}

/* GSVNEW: ANIMATE OUR NEW GAS GAUGE */
static void gsvnew(sw_machine *m)
{
    if (R8(SW_GS_VTP) == 0) {               /* ?WORKING ON SMOOTH TOP LINE? */
        VG(SW_VJFLS);                       /* FLASH THE OLD VALUE */
        VG(entry(m, TGALL, R8(SW_GS_OLD))); /* COVER UP ALL OF THE GAUGE */
        (void)R8(SW_FRAMEL);                /* ?AND TIME INTERVAL HAS PASSED? (every frame) */
        uint8_t before = R8(SW_GS_LEN);
        W8(SW_GS_LEN, before + 1);
        /* ?FINISHED WITH SHIELD FLASH OF GAS GAUGE? (INC, then GE: N == V) */
        if (((int8_t)(uint8_t)(before + 1) >= 0) != (before == 0x7f))
            W8(SW_GS_VTP, R8(SW_GS_VTP) + 1);   /* THEN START REDRAW OF GAUGE, BEGIN WITH TOP */
        return;
    }
    /* ELSE DO ANIMATION OF REDRAW NEW GAUGE */
    gsvhit(m);                              /* DRAW THE HIT ON THE WHOLE NEW GAUGE */
    VG(SW_VJFLS);                           /* COVER PART OF NEW WITH FLASH THE OLD VALUE */
    if (R8(SW_GS_VUP) == 0) {               /* ?PREPARE FOR UPSTROKE? */
        VG(entry(m, TGUP, R8(SW_GS_OLD)));  /* COVER UP THE UPSTROKE ET AL. */
        (void)R8(SW_FRAMEL);
        W8(SW_GS_VUP, R8(SW_GS_VUP) + 1);   /* SET TO VIEW NORMAL UPSTROKE */
        W8(SW_GS_LEN, R8(TGDLM + (int8_t)R8(SW_S_GAS)));    /* WHERE TO START UNCOVERING NEW GAUGE */
    } else if (R8(SW_GS_VBS) == 0) {        /* ?STILL WORKING ON REVEALING BASE? */
        VG(entry(m, TGDRW, R8(SW_GS_LEN))); /* DRAW ANIMATION */
        (void)R8(SW_FRAMEL);
        uint8_t before = R8(SW_GS_LEN);
        W8(SW_GS_LEN, before - 1);          /* FLASHY COVERING GETS SHORTER AND SHORTER */
        /* ?ALL DONE UNCOVERING BASE? (DEC, then LE) */
        if ((uint8_t)(before - 1) == 0 || (((uint8_t)(before - 1) & 0x80) != 0) != (before == 0x80))
            W8(SW_GS_VBS, R8(SW_GS_VBS) + 1);   /* THEN SAY SO */
    } else {                                /* ELSE NEW BASE FULLY VISIBLE, NEW GAUGE DONE */
        (void)R8(SW_FRAMEL);
        W8(SW_GS_HIT, 0);                   /* THEN STOP ANIMATING THE HIT */
        W8(SW_GS_GLW, 0);                   /* AND TELL REST OF WORLD WE ARE READY FOR MORE */
        W8(SW_GS_OLD, R8(SW_S_GAS));        /* CURRENT IS NOW OLD, STABILIZE TO CORRECT VALUE */
    }
}

/* VWGAS: the shield gauge, or SHIELD GONE */
void sw_vwgas(sw_machine *m)
{
    TAG(SW_TAG_SCORE, 3);
    if (R8(SW_GS_HIT) == 0 && (int8_t)R8(SW_S_GAS) <= 0) {     /* ?FINISHED HANDLING HIT? ?AND NO GAS LEFT? */
        /* VWGONE: JUST VIEW GAS GONE MESSAGE */
        VG(SW_VGSCAL - 0x100);              /* DOUBLE SIZE */
        sw_domess(m, SW_MS_SHG);            /* SHIELDS GONE WARNING */
        return;
    }
    VG(entry(m, TGCLR, R8(SW_S_GAS)));      /* SELECT CURRENT COLOR */
    VG(SW_VJFUEL);                          /* SHOW TITLE OF GAS GAUGE */
    VG(SW_VGRW0 & 0x1fff);                  /* GOTO BASE OF GAUGE */
    VG(0);
    if (R8(SW_GS_HIT) != 0) gsvnew(m);      /* ?ARE WE ANIMATING A HIT? THEN VIEW OUR NEW DISPLAY */
    else gsvall(m);                         /* ELSE LOOK AT OLD ONE */
    VG((-4 * SW_VGSC / 2) & 0x1fff);        /* DOWN 4/2 OF NUMERAL */
    VG((-1 * SW_VGSCW / 2) & 0x1fff);       /* LEFT ONE HALF NUMERAL */
    if ((int8_t)R8(SW_GS_HIT) > 0) VG(SW_VJFLS);    /* ?CURRENTLY REDRAWING? THEN FLASH NEW NUMBER */
    else VG(entry(m, TGCLR, R8(SW_S_GAS)));
    W8(SW_BT_SUP, 0);                       /* DONT SUPPRESS ANY NUMERALS */
    uint8_t a = R8(SW_S_GAS);
    if (a & 0x80) a = 0;                    /* LIMIT TO ZERO */
    sw_vwnibl(m, a);                        /* JUST VIEW ONE NUMERAL OF BYTE */
    VG(SW_VGCNTR);
}

/* --- the score --------------------------------------------------------------------- */

/* Six BCD digits each, local tables of the source (read off the LDUs of the
 * SCRxxx entries). */
#define TSCA1D 0x984A       /* TIE FIGHTER SHOT */
#define TSCA2D 0x984D       /* CHASED DARTH OFF */
#define TSCPNL 0x9850       /* DECORATIVE PANEL HIT */
#define TSCTRT 0x9853       /* TRENCH TURRET */
#define TSCBNK 0x9859       /* LASER BUNKER */
#define TSCSHT 0x985C       /* HIT FIREBALL */
#define TSCPRT 0x985F       /* HIT EXHAUST PORT */
#define TSCATP 0x9862       /* GOT ALL TOWER TOPS OR BUNKERS IN A WAVE */

/* ADUSCR: ADD FROM U PTR TO SCORE */
void sw_aduscr(sw_machine *m, uint16_t u)
{
    int c = 0;
    W8(SW_SC_ADD + 2, R8(u + 2));           /* DISPLAY OUR NEW ADDITION */
    W8(SW_S_SC21, sw_bcd_adc(R8(u + 2), R8(SW_S_SC21), &c));
    W8(SW_SC_ADD + 1, R8(u + 1));
    W8(SW_S_SC43, sw_bcd_adc(R8(u + 1), R8(SW_S_SC43), &c));
    W8(SW_SC_ADD + 0, R8(u + 0));
    W8(SW_S_SC65, sw_bcd_adc(R8(u + 0), R8(SW_S_SC65), &c));
    W8(SW_S_SC87, sw_bcd_adc(R8(SW_S_SC87), 0, &c));
    W8(SW_SC_ADT, 0xff);                    /* NEW TIMER FOR NEW ADDITION */
}

void sw_scrtie(sw_machine *m)   { sw_aduscr(m, TSCA1D); }   /* KILL A REGULAR TIE FIGHTER */
void sw_scrdarth(sw_machine *m) { sw_aduscr(m, TSCA2D); }   /* GOT DARTH */
void sw_scrpnl(sw_machine *m)   { sw_aduscr(m, TSCPNL); }   /* DECORATIVE PANEL */
void sw_scrbnk(sw_machine *m)   { sw_aduscr(m, TSCBNK); }   /* LASER BUNKERS */
void sw_scrtrt(sw_machine *m)   { sw_aduscr(m, TSCTRT); }   /* TRENCH GUN TURRET */
void sw_scrsht(sw_machine *m)   { sw_aduscr(m, TSCSHT); }   /* HIT FIREBALL */
void sw_scrport(sw_machine *m)  { sw_aduscr(m, TSCPRT); }   /* HIT EXHAUST PORT ( KILLED DEATH STAR ) */
void sw_scrall(sw_machine *m)   { sw_aduscr(m, TSCATP); }   /* GOT ALL TOWER TOPS OR BUNKERS IN A WAVE */

#define TSCFRC 0x983B       /* the points for using the force, by the wave */
#define TSCFRN 5

/* GETFRP: GET POINTER TO THIS WAVES POINTS */
static uint16_t getfrp(sw_machine *m)
{
    uint8_t b = R8(SW_GM_WAV);
    if (b >= TSCFRN) return TSCFRC + 3 * (TSCFRN - 1);  /* HIGHER LEVELS USE MAX SCORE */
    return (uint16_t)(TSCFRC + (uint8_t)((uint8_t)(b << 1) + R8(SW_GM_WAV)));   /* *3 */
}

#define TSCSHL 0x9865       /* SHIELD SCORE FOR REMAINING ENERGY UNITS; the wave bonuses follow */

/* SCRTWR: a tower's top is hit: the INCREMENTAL SCORE FOR TOWERS */
void sw_scrtwr(sw_machine *m)
{
    sw_aduscr(m, SW_TWRMUL);                /* ADD IT IN */
    /* ADD TO MULTIPLE SCORE: LSD FIRST */
    int c = 0;
    W8(SW_TWRMUL + 2, sw_bcd_adc(R8(SW_TWRMUL + 2), R8(SW_TSCTWR + 2), &c));
    W8(SW_TWRMUL + 1, sw_bcd_adc(R8(SW_TWRMUL + 1), R8(SW_TSCTWR + 1), &c));
    W8(SW_TWRMUL + 0, sw_bcd_adc(R8(SW_TWRMUL + 0), R8(SW_TSCTWR + 0), &c));   /* MSD */

    uint8_t a = R8(SW_GD_TWL);              /* HANDLE NUMBER OF TOWERS LEFT */
    if (a == 0) return;                     /* ?ANY LEFT? */
    c = 0;
    a = sw_bcd_adc(a, 0x99, &c);            /* DECIMAL DEC */
    W8(SW_GD_TWL, a);
    if (a == 0) {                           /* ?DONE ALL TOWERS? */
        sw_aduscr(m, TSCATP);               /* THEN GIVE BIG BONUS */
        W8(SW_Q_ATP, R8(SW_Q_ATP) + 1);     /* YEP, ALL TOPS GOT HIT */
    }
}

/* SCRSHLD: GIVE POINTS FOR SHIELD ENERGY REMAINING */
void sw_scrshld(sw_machine *m)
{
    uint8_t b = R8(SW_S_GAS);               /* SET COUNT */
    if (b == 0) return;                     /* PROBLEM IF ZERO */
    W8(SW_SC_ADD, 0);                       /* START WITH 000000 */
    W8(SW_SC_ADD + 1, 0);
    W8(SW_SC_ADD + 2, 0);
    do {                                    /* ADD THEM ALL IN */
        int c = 0;
        W8(SW_SC_ADD + 2, sw_bcd_adc(R8(TSCSHL + 2), R8(SW_SC_ADD + 2), &c));
        W8(SW_SC_ADD + 1, sw_bcd_adc(R8(TSCSHL + 1), R8(SW_SC_ADD + 1), &c));
        W8(SW_SC_ADD + 0, sw_bcd_adc(R8(TSCSHL + 0), R8(SW_SC_ADD + 0), &c));
    } while (--b != 0);                     /* DEC COUNT */
    sw_aduscr(m, SW_SC_ADD);                /* POINT TO SCORE WE JUST BUILT */
}

/* SCRWAV: ADD WAVE BONUS TO THE SCORE */
void sw_scrwav(sw_machine *m)
{
    if (R8(SW_SC_FWV) != 0) return;         /* FIRST WAVE? YES, THEN GIVE THE PLAYER HIS/HER JUST DESSERTS */
    uint8_t b = R8(SW_GM_WAV);
    if (b == 0) return;
    sw_aduscr(m, (uint16_t)(TSCSHL + (uint8_t)((uint8_t)(b << 1) + R8(SW_GM_WAV))));
}

/* SCRFRC: PLAYER USED THE FORCE IN THE TRENCH */
void sw_scrfrc(sw_machine *m) { sw_aduscr(m, getfrp(m)); }

/* VWFRC: VIEW THE SUCCESSFUL FORCE SCORE */
void sw_vwfrc(sw_machine *m)
{
    VG(SW_VJMFL);                           /* COLOR */
    VG(SW_VGRW7 & 0x1fff);                  /* Y DISTANCE UP */
    VG((uint16_t)-320 & 0x1fff);            /* X POSITION ON LEFT SCREEN */
    W8(SW_BT_SUP, 4);                       /* SUPPRESS UP TO 4 LEADING ZEROS */
    /* U-->6 DIGIT SCORE TO USE, X-->8 DIGIT FIELD, BUT DISPLAY LO 6 DIGITS ONLY */
    sw_vw6dig(m, (uint16_t)(getfrp(m) - 1));
    sw_domess(m, SW_MS_USE + 1);            /* FOLLOW WITH TEXT, FOR USING THE FORCE */
}
