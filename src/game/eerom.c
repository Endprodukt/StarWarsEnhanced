/* eerom.c - TCEROM.MAC: options, accounting and high scores in the NOVRAM.
 *
 * The X2212 holds 256 nibbles: a RAM the program reads and writes, and an
 * EEPROM behind it that RECALL copies into the RAM and STORE writes from
 * it (core/machine.c). Four areas, each ending in a two-nibble checksum
 * (T.AREA): 0 the option switches' backup, 1 the top high scores, 2 pots,
 * coins and times, 3 the option switches.
 *
 * Ported so far: what RESET and the attract mode use. The accounting and
 * option screens (EEACC, EEOPT) and the game-end accounting follow with
 * the phases that call them. Comments in capitals are the original's. */
#include "game/game.h"
#include "game/m6809.h"

#define NAREA SW_NAREA

/* SUMMIT: the checksum of area a. Returns 0 if the stored checksum is
 * right (the original's Z flag); *sum is B and *xp is X (pointing at the
 * stored checksum) on return. */
int sw_summit(sw_machine *m, uint8_t a, uint8_t *sum, uint16_t *xp)
{
    uint8_t cnt = (uint8_t)(R8(SW_L_TAREA + 1 + a) - R8(SW_L_TAREA + a) - 2);   /* DON'T CHECKSUM THE CHECKSUM */
    uint16_t x = (uint16_t)(SW_EEROM + R8(SW_L_TAREA + a));
    unsigned newsum = 0;
    unsigned c = 1;                         /* SEC: PREVENT A ZERO CHECKSUM */
    do {
        unsigned t = (R8(x++) & 0x0fu) + newsum + c;    /* ONLY LOWER NIBBLE IS DATA */
        c = t >> 8;
        newsum = t & 0xff;
    } while (--cnt != 0);
    newsum = (newsum + c) & 0xff;           /* GET ALL THE CARRIES IN */
    uint8_t oldsum = (uint8_t)((uint8_t)(R8(x) << 4) + (R8(x + 1) & 0x0f));
    if (sum) *sum = (uint8_t)newsum;
    if (xp) *xp = x;
    return newsum != oldsum;
}

/* EECKSM: CALCULATE CHECKSUM FOR EEROM AREA and store it if it differs. */
int sw_eecksm(sw_machine *m, uint8_t a)
{
    uint8_t b;
    uint16_t x;
    if (sw_summit(m, a, &b, &x) == 0) return 0;
    W8(x + 1, b);                           /* LO NIBBLE */
    W8(x, b >> 4);                          /* HI NIBBLE */
    return 1;
}

/* EETMP3: MOVES (A)+1 BYTES FROM NIBBLES IN EEROM (X) TO (U). Returns X as
 * the original leaves it (restored only for 4 bytes). */
uint16_t sw_eetmp3(sw_machine *m, uint16_t x, uint16_t u, uint8_t a)
{
    W8(SW_TMPCNT, a);
    do {
        W8(u, R8(x++) << 4);                /* UPPER NIBBLE */
        W8(u, (R8(x++) & 0x0f) + R8(u));    /* LOWER NIBBLE */
        u++;
        W8(SW_TMPCNT, R8(SW_TMPCNT) - 1);
    } while (!(R8(SW_TMPCNT) & 0x80));
    return (uint16_t)(x - 8);
}

/* EETEMP: MOVES 8 NIBBLES FROM EEROM (X) TO 4 BYTES AT TEMP */
uint16_t sw_eetemp(sw_machine *m, uint16_t x)
{
    return sw_eetmp3(m, x, SW_TEMP, 3);
}

/* TMPEE3: (A)+1 BYTES FROM (U) TO NIBBLES IN EEROM (X) */
uint16_t sw_tmpee3(sw_machine *m, uint16_t x, uint16_t u, uint8_t a)
{
    W8(SW_TMPCNT, a);                       /* NUMBER OF BYTES TO MOVE */
    do {
        a = R8(u++);
        W8(x + 1, a);                       /* STORE LOWER NIBBLE FIRST */
        W8(x, a >> 4);                      /* STORE UPPER, UPDATE X */
        x += 2;
        W8(SW_TMPCNT, R8(SW_TMPCNT) - 1);
    } while (!(R8(SW_TMPCNT) & 0x80));
    return (uint16_t)(x - 6);
}

/* TEMPEE: 3 BYTES FROM TEMP TO 6 NIBBLES IN EEROM (X) */
static uint16_t tempee(sw_machine *m, uint16_t x)
{
    return sw_tmpee3(m, x, SW_TEMP, 2);
}

/* EECPY: COPY DEFAULT VALUES TO EEROM AREA(A), then checksum it. */
static void eecpy(sw_machine *m, uint8_t a)
{
    if (a < NAREA) {                        /* LEGAL? */
        if (a >= 0x02) {                    /* GET DEFAULTS FROM T.DFLT */
            W8(SW_TEMP + 1, R8(SW_L_TAREA + 1 + a) - 2);    /* LIMIT IS FIRST NIBBLE OF CHECKSUM */
            W8(SW_TEMP, SW_EEROM >> 8);
            uint8_t b = R8(SW_L_TAREA + a);                 /* GET START OF AREA */
            uint16_t u = (uint16_t)(SW_L_TDFLT - (SW_EECK1 - SW_EEROM + 2) + b);   /* DEFAULT TABLE STARTS WITH AREA 2 */
            uint16_t x = (uint16_t)(SW_EEROM + b);
            do {
                W8(x++, R8(u++));
            } while (x < R16(SW_TEMP));
        } else if (a == 0) {                /* OPTION SWITCHES----USE PHYSICAL SWITCHES FOR DEFAULT */
            W8(SW_TEMP, R8(SW_R_OPT1));     /* COINS */
            W8(SW_TEMP + 1, R8(SW_R_OPT2)); /* STARTING LIVES,DIFFICULTY,BONUS SHIELDS,ATTRACTIVE MUSIC */
            W8(SW_TEMP + 2, 0x00);          /* SET FOR MISC. OPTIONS */
            tempee(m, SW_DUPOPT);           /* START OF AREA 0 */
        } else {                            /* MUST BE HIGH SCORE INIT */
            sw_tmpee3(m, SW_SAVSCR, SW_INTSCR, 11);     /* INITIAL SCORE TABLE (SEE TCHSCR) */
            sw_tmpee3(m, SW_SAVINT, SW_INTINT, 0x08);   /* NOW FOR THE INITIALS */
        }
    }
    sw_eecksm(m, a);                        /* FALL THROUGH TO CHECKSUM IT */
}

/* EEINI2: MOVE AREA A FROM EEBUF TO EEROM */
static void eeini2(sw_machine *m, uint8_t a)
{
    W16(SW_TEMP, SW_EEROM + R8(SW_L_TAREA + 1 + a));    /* START OF NEXT AREA */
    uint8_t b = R8(SW_L_TAREA + a);
    uint16_t u = (uint16_t)(SW_EEBUF + b);
    uint16_t x = (uint16_t)(SW_EEROM + b);
    do {
        W8(x++, R8(u++));
    } while (x < R16(SW_TEMP));
}

enum { EE_INIT, EE_INI0, EE_INI1 };

/* EEINIT and its second entry EEINI0 (which always sets up the option
 * switch areas from area A's defaults). */
static void eeinit_from(sw_machine *m, int entry, uint8_t a)
{
    uint16_t x, u;

    if (entry == EE_INI0) goto eeini0;
    if (entry == EE_INI1) goto eeini1;

    /* COPY EEROM TO BUFFER */
    for (x = SW_EEROM, u = SW_EEBUF; x < SW_EEROMZ; x += 2, u += 2) W16(u, R16(x));

    W8(SW_NVRRCL, 0xff);                    /* PERFORM A RECALL OF THE NON-VOLATILE MEMORY */
    W8(SW_NVRRCL, 0x00);                    /* TURN OFF RECALL LINE (the waits for the cap: nothing to wait for) */

    /* SWAP EEROM AND BUFFER: EEBUF HAS RECALLED DATA AND EEROM HAS ITS ORIGINAL */
    for (x = SW_EEROM, u = SW_EEBUF; x < SW_EEROMZ; x += 2, u += 2) {
        uint16_t ee = R16(x), buf = R16(u);
        W16(u, ee);
        W16(x, buf);
    }

    /* START CHECKING VALIDITY: OPTION SWITCH AREA IS SPECIAL, 2 AREAS */
    if (sw_summit(m, NAREA, 0, 0) == 0) goto primary_good;
    a = 0x00;                               /* PRIMARY WAS BAD, CHECK BACKUP AREA */
    if (sw_summit(m, a, 0, 0) == 0) goto backup_good;
    /* BOTH RAM AREAS BAD, CHECK RECALLED DATA */
    eeini2(m, a);                           /* RESTORE PRIMARY AND BACKUP FROM EEBUFF COPY */
    a = NAREA;
    eeini2(m, a);
    if (sw_summit(m, a, 0, 0) == 0) goto primary_good;
    a = 0x00;                               /* NEW PRIMARY BAD, CHECK NEW BACKUP */
    if (sw_summit(m, a, 0, 0) == 0) goto backup_good;

eeini0:
    eecpy(m, a);                            /* RESTORE FROM ROM TABLE */
backup_good:                                /* PRIMARY BAD, BUT BACKUP OK: MOVE BACKUP TO PRIMARY */
    x = (uint16_t)(SW_EEROM + R8(SW_L_TAREA + NAREA));
    u = SW_EEROM;
    goto copy;
primary_good:
    if (sw_summit(m, 0x00, 0, 0) != 0) goto eeini1;     /* CHECK THE BACKUP AREA: FIX IT FROM PRIMARY */
    /* BOTH AREAS HAVE GOOD CHECKSUMS, MAKE AREAS THE SAME */
    W8(SW_TEMP, R8(SW_EECK3) & 0x0f);
    if ((R8(SW_EECK0) & 0x0f) == R8(SW_TEMP)) {
        W8(SW_TEMP, R8(SW_EECK3 + 1) & 0x0f);
        if ((R8(SW_EECK0 + 1) & 0x0f) == R8(SW_TEMP)) goto others;
    }
eeini1:                                     /* REPLACE BACKUP AREA WITH PRIMARY */
    u = (uint16_t)(SW_EEROM + R8(SW_L_TAREA + NAREA));
    x = SW_EEROM;
copy:
    W8(SW_TMPCNT, R8(SW_L_TAREA + 1));      /* LENGTH OF OPTION SWITCH AREAS */
    do {
        W8(x++, R8(u++));                   /* TRANSFER GOOD TO BAD */
        W8(SW_TMPCNT, R8(SW_TMPCNT) - 1);
    } while (R8(SW_TMPCNT) != 0);
others:
    a = NAREA - 1;                          /* NEXT AREA */
    do {
        if (sw_summit(m, a, 0, 0) != 0) {   /* NO GOOD */
            eeini2(m, a);                   /* REPLACE FROM BUFFER */
            if (sw_summit(m, a, 0, 0) != 0) eecpy(m, a);    /* REALLY BAD, USE DEFAULTS */
        }
        a--;
    } while (a != 0);

    sw_eetemp(m, SW_TIMEON);                /* SETUP RAM BASED TIME ON */
    W16(SW_TIMER, R16(SW_TEMP));
    W16(SW_TIMER + 2, R16(SW_TEMP + 2));
}

void sw_eeinit(sw_machine *m)
{
    eeinit_from(m, EE_INIT, 0);
}

void sw_eeini0(sw_machine *m, uint8_t a)
{
    eeinit_from(m, EE_INI0, a);
}

/* EEUTIM: UPDATE TOTAL TIME ON */
void sw_eeutim(sw_machine *m)
{
    sw_eetemp(m, SW_TIMEON);                /* MOVE TIMEON FROM EEROM */
    uint16_t u = R16(SW_TIMER + 2);         /* RAM BASED TIME */
    uint16_t d = R16(SW_TIMER);
    /* NEW TIME SHOULD BE LARGER THAN OLD */
    if (d > R16(SW_TEMP) || (d == R16(SW_TEMP) && u > R16(SW_TEMP + 2))) {
        W16(SW_TEMP, d);                    /* SAVE NEW TIME */
        W16(SW_TEMP + 2, u);
        sw_tmpee3(m, SW_TIMEON, SW_TEMP, 0x03);     /* PUT IT BACK INTO EEROM */
    } else {
        W16(SW_TIMER, R16(SW_TEMP));        /* RESTORE TIME FROM TIMEON */
        W16(SW_TIMER + 2, R16(SW_TEMP + 2));
    }
    sw_eecksm(m, 2);                        /* SET NEW CHECKSUM */
}

/* CHKEE: CHECK AND RESTORE IF NECESSARY THE VALIDITY OF AREA (A) */
void sw_chkee(sw_machine *m, uint8_t a)
{
    uint16_t x, u;
    if (sw_summit(m, a, 0, 0) == 0) return;
    /* ERROR */
    W8(SW_TEMP, a);                         /* SAVE AREA NUMBER */
    /* COPY EEROM TO BUFFER */
    for (x = SW_EEROM, u = SW_EEBUF; x < SW_EEROMZ; x += 2, u += 2) W16(u, R16(x));
    W8(SW_NVRRCL, 0xff);                    /* PERFORM A RECALL OF THE NON-VOLATILE RAM */
    W8(SW_NVRRCL, 0x00);                    /* (the waits for the cap: nothing to wait for) */
    /* SWAP EEROM AND BUFFER: EEBUF HAS RECALEED DATA AND EEROM HAS ITS ORIGINAL */
    for (x = SW_EEROM, u = SW_EEBUF; x < SW_EEROMZ; x += 2, u += 2) {
        uint16_t ee = R16(x), buf = R16(u);
        W16(u, ee);
        W16(x, buf);
    }
    a = R8(SW_TEMP);                        /* RESTORE AREA NUMBER */
    eeini2(m, a);
    if (sw_summit(m, a, 0, 0) != 0) eecpy(m, a);
}

/* ADDA or ADCA, then DAA, as the processor does it (for what is not a
 * decimal number too); *carry in and out. */
static uint8_t add_daa(uint8_t a, uint8_t b, int *carry)
{
    unsigned sum = a + b + (unsigned)*carry;
    int half = (a & 0x0f) + (b & 0x0f) + *carry > 0x0f;
    int c = sum > 0xff;
    uint8_t r = (uint8_t)sum, fix = 0;
    if (half || (r & 0x0f) > 0x09) fix |= 0x06;
    if (c || r > 0x99 || (r > 0x8f && (r & 0x0f) > 0x09)) fix |= 0x60;
    sum = r + fix;
    *carry = c || sum > 0xff;
    return (uint8_t)sum;
}

#define T_HIST 0xC7A4       /* the width of a slot of the histogram of game times (local: EEACCT's SUBB) */

/* EEACCT: at the end of a game UPDATE TOTAL GAME TIME, the NUMBER OF GAMES
 * PLAYED, the HIGHEST LEVEL EVER ATTAINED and the histogram of game
 * times; SAVE POT VALUES; and on into EEUTIM. */
void sw_eeacct(sw_machine *m)
{
    int c;
    uint8_t a;
    uint16_t x, u;

    sw_chkee(m, 2);                         /* MAKE SURE AREA 2 IS VALID */
    sw_eetemp(m, SW_TTIME);                 /* TOTAL GAME TIMES: GET TOTALS INTO TEMPS */
    c = 0;                                  /* ADD GAME TIME TO TOTAL */
    W8(SW_TEMP + 3, add_daa(R8(SW_TEMP + 3), R8(SW_GTIME + 1), &c));
    W8(SW_TEMP + 2, add_daa(R8(SW_TEMP + 2), R8(SW_GTIME + 0), &c));
    W8(SW_TEMP + 1, add_daa(R8(SW_TEMP + 1), 0x00, &c));
    a = add_daa(R8(SW_TEMP + 0), 0x00, &c);
    if (!c) W8(SW_TEMP + 0, a);             /* DO NOT OVERFLOW THE MSB */
    sw_tmpee3(m, SW_TTIME, SW_TEMP, 3);     /* DO FOUR BYTES: PUT DATA BACK TO EEROM */

    /* UPDATE NUMBER OF GAMES PLAYED */
    sw_eetemp(m, SW_GAMES);                 /* PUT IT IN TEMPS */
    c = 0;
    W8(SW_TEMP + 2, add_daa(R8(SW_TEMP + 2), 0x01, &c));
    W8(SW_TEMP + 1, add_daa(R8(SW_TEMP + 1), 0x00, &c));
    W8(SW_TEMP + 0, add_daa(R8(SW_TEMP + 0), 0x00, &c));
    tempee(m, SW_GAMES);                    /* PUT IT BACK IN EEROM */

    /* RECORD HIGHEST LEVEL EVER ATTAINED */
    sw_eetemp(m, SW_ENDGAM);                /* GET CURRENT HIGHEST TO TEMP */
    u = SW_TEMP + 4;                        /* (where EETEMP leaves U) */
    a = R8(SW_GM_DWA);                      /* END OF GAME LEVEL ( IN BCD OR DECIMAL ) */
    if (a > R8(SW_TEMP)) {                  /* THIS ONE IS HIGHER, USE IT INSTEAD */
        W8(SW_TEMP, a);
        (void)R8(SW_TEMP + 1);
        W8(SW_TEMP + 1, 0);                 /* CLEAR UNUSED NIBBLES */
        (void)R8(SW_TEMP + 2);
        W8(SW_TEMP + 2, 0);
        tempee(m, SW_ENDGAM);               /* PUT IT BACK INTO EEROM */
        u = SW_TEMP + 3;                    /* (and TEMPEE) */
    }

    /* UPDATE HISTOGRAM INFO */
    x = SW_ZHISTO - 2;                      /* LAST ENTRY POINTER */
    if (R8(SW_GTIME) == 0) {                /* MSB-IF NOT 0 THEN OVER 6:00, SO PUT IN LAST ENTRY */
        a = R8(SW_GTIME + 1);               /* FIND OUT WHERE TO PUT DATA */
        x = SW_HISTOG;                      /* BEGINNING OF HISTOGRAM DATA */
        W8(SW_TEMP, 0x9a - R8(T_HIST));     /* GET NEGATIVE OF T.HIST ( THIS WON'T WORK FOR ALL) */
        do {
            c = 0;
            a = add_daa(a, R8(SW_TEMP), &c);
            if (!c) break;                  /* FOUND ITS NICHE */
            x += 2;
        } while (x < SW_ZHISTO - 2);        /* IF NOT FOUND, PUT IN LAST ONE ANYWAY */
    }
    c = 0;
    a = add_daa(R8(x + 1) & 0x0f, 0x01, &c);    /* ADD 1 */
    W8(x + 1, a);
    if (a & 0xf0) {                         /* UPDATE HI NIBBLE */
        c = 0;
        a = add_daa(R8(x) & 0x0f, 0x01, &c);
        W8(x, a);
        if (a & 0xf0) {                     /* OVERFLOW-BAD NEWS DIVIDE ALL HISTOGRAM VALUES BY 2 */
            for (x = SW_HISTOG; x < SW_ZHISTO; x += 2) {
                W8(SW_TEMP, R8(x) << 4);
                a = (uint8_t)((R8(x + 1) & 0x0f) + R8(SW_TEMP));
                if (a & 0x10) a -= 0x06;    /* WILL DIVIDE BY 2 WORK? NO: THIS CORRECTS IT */
                a >>= 1;
                W8(x + 1, a);
                W8(x, a >> 4);
            }
            W8(u, 5);                       /* 1/2 OF 100 IS 50 (U is not the entry here: the original's slip) */
            W8(u + 1, 0);
        }
    }

    /* SAVE POT VALUES */
    W8(SW_TEMP + 0, R8(SW_RHEOP + RH_LO));
    W8(SW_TEMP + 1, R8(SW_RHEOP + RH_SP));
    W8(SW_TEMP + 2, R8(SW_RHEOY + RH_LO));
    W8(SW_TEMP + 3, R8(SW_RHEOY + RH_SP));
    sw_tmpee3(m, SW_POTSAV, SW_TEMP, 3);    /* MOVE THEM FROM TEMP TO POTSAV IN EEROM: 4 BYTES WORTH */

    sw_eeutim(m);
}

/* --- the operator's pages: statistics and options ------------------------------------- */

/* local tables (addresses from the program ROM) */
#define EEMASK 0xC737       /* the masks of fields of 1 to 4 bits */
#define T_OPT  0xC797       /* OPTION DESCRIPTOR TABLE: 20*POS+8*(LEN-1)+WRD */
#define N_OPT  12
#define T_ACCP 0xC7BB       /* Y POSITIONS FOR ACCOUNTING INFO */
#define T_HISP 0xC7CD       /* Y POSITIONS FOR HISTOGRAM */
#define T_MOPV 0xC7F1       /* TABLE OF MESSAGE #'S FOR OPTION SWITCH VALUES */

/* TIME: DISPLAY TIME IN M:S FORMAT. A: minutes in the high nibble, seconds
 * in fours in the low one */
static void time_ms(sw_machine *m, uint8_t a)
{
    W8(SW_BT_SUP, 0);
    uint8_t b = a & 0x0f;
    a >>= 4;
    if (a >= 0x0a) a = 0x09;                /* MAX OF 9 */
    sw_vwnibl(m, a);                        /* MINUTES */
    VG(SW_VJCOLN);                          /* CHANGE TO COLON IF WE MAKE ONE */
    a = 0;
    for (; b != 0; b--) {                   /* SECONDS: MULTIPLY BY 4 IN DECIMAL */
        int c = 0;
        a = add_daa(a, 0x04, &c);
    }
    sw_vwbyte(m, a);
}

/* TMPMUL: TEMP times 4 in decimal (GOT TO MULTIPLY BY 4 FOR SECONDS) */
static void tmpmul(sw_machine *m)
{
    for (int i = 0; i < 2; i++) {           /* DO IT TWICE: ADD TEMP TO TEMP IN DECIMAL */
        int c = 0;
        for (uint16_t x = SW_TEMP + 3; x >= SW_TEMP; x--)       /* START WITH LSD */
            W8(x, add_daa(R8(x), R8(x), &c));
    }
}

/* DIVIDE: TEMP2/TEMP, RESULT IN TEMP. USES ITERATIVE ADD OF -TEMP UNTIL
 * WRAP. TEMP = DENOMINATOR ( NON-ZERO ) */
static void divide(sw_machine *m)
{
    /* PERFORM DECIMAL NEGATE OF DENOMINATOR */
    W8(SW_TMPCNT, 1);                       /* SETUP FOR NEGATE */
    for (uint16_t x = SW_TEMP + 3; x >= SW_TEMP; x--) {     /* 4 BYTES WORTH */
        int c = 0;
        W8(x, add_daa((uint8_t)(0x99 - R8(x)), R8(SW_TMPCNT), &c));
        W8(SW_TMPCNT, c ? 1 : 0);           /* PROPAGATE THE CARRY */
    }
    uint8_t b = 0xff;
    for (;;) {                              /* START DIVIDE */
        b++;
        if (b == 0xef) break;               /* OVERFLOW---LEAVE AT MAX */
        int c = 0;
        /* START WITH LSB: SUBTRACT DENOMINATOR ( ADD NEGATIVE IN DECIMAL) */
        for (int i = 3; i >= 0; i--)
            W8(SW_TEMP2 + i, add_daa(R8(SW_TEMP2 + i), R8(SW_TEMP + i), &c));
        if (!c) break;                      /* DONE WHEN STAYS NEGATIVE ( DECIMAL ) NO CARRY */
    }
    W8(SW_TEMP, b);
}

/* A row of the statistics: its place, then what the caller draws. */
static void acc_row(sw_machine *m, int row)
{
    VG(R16(T_ACCP + 2 * row));
    VG(0x1fff & 48);
}

/* EEACC: DISPLAY STATS AND ACCOUNTING INFO */
void sw_eeacc(sw_machine *m)
{
    uint8_t a;
    int c;
    for (a = SW_MS_ACC; a < SW_MS_OPT; a++) sw_messon(m, a);
    sw_messon(m, SW_MS_XIT);                /* EXIT MESSAGE */

    VG(SW_VGCOLR + (VGCRED << 8));          /* SETUP COLOR FOR DATA MESSAGES */
    uint16_t x = SW_AMECH;                  /* START WITH AUX COIN DATA */
    uint16_t u = T_ACCP;                    /* TABLE OF POSITIONS */
    do {
        VG(R16(u)); u += 2;                 /* SET POSITION */
        VG(0x1fff & 48);
        W16(SW_TEMP, u);                    /* SAVE U--- VWNIBL ALTERS U */
        W8(SW_BT_SUP, 5);
        for (int i = 0; i < 6; i++) sw_vwnibl(m, R8(x++));     /* VIEW 6 NIBBLES */
        VG(SW_VGCNTR);
        u = R16(SW_TEMP);                   /* GET POINTER TO POS TABLE BACK */
    } while (x < SW_TTIME);

    /* DISPLAY HIGHEST LEVEL ATTAINED */
    VG(R16(T_ACCP + 6));
    VG(0x1fff & 316);
    W8(SW_BT_SUP, 1);                       /* SUPRESS 1 DIGIT */
    sw_vwnibl(m, R8(SW_ENDGAM));            /* BCD VALUE OF HIGHEST WAVE ATTAINED ( HI NIBL ) */
    sw_vwnibl(m, R8(SW_ENDGAM + 1));        /* LO NIBL */
    VG(SW_VGCNTR);

    /* GET TOTAL COINS PAID */
    sw_eetemp(m, SW_RMECH);                 /* GET RIGHT MECH DATA INTO TEMP */
    sw_eetmp3(m, SW_LMECH, SW_TEMP2, 3);    /* GET LEFT MECH DATA INTO TEMP2 */
    c = 0;                                  /* 3 BYTE ADD, RESULT IN TEMP */
    W8(SW_TEMP + 2, add_daa(R8(SW_TEMP + 2), R8(SW_TEMP2 + 2), &c));
    W8(SW_TEMP + 1, add_daa(R8(SW_TEMP + 1), R8(SW_TEMP2 + 1), &c));
    W8(SW_TEMP + 0, add_daa(R8(SW_TEMP + 0), R8(SW_TEMP2 + 0), &c));    /* NOW THE MSB */
    acc_row(m, 4);                          /* DISPLAY TOTAL COINS */
    W8(SW_BT_SUP, 5);
    for (int i = 0; i < 3; i++) sw_vwbyte(m, R8(SW_TEMP + i));
    VG(SW_VGCNTR);                          /* CENTER */

    sw_eetemp(m, SW_TTIME);                 /* DISPLAY TOTAL GAME TIME */
    tmpmul(m);                              /* GOT TO MULTIPLY BY 4 FOR SECONDS */
    acc_row(m, 7);
    W8(SW_BT_SUP, 7);
    for (int i = 0; i < 4; i++) sw_vwbyte(m, R8(SW_TEMP + i));
    VG(SW_VGCNTR);

    sw_eetemp(m, SW_TIMEON);                /* DISPLAY TOTAL TIME ON */
    tmpmul(m);
    acc_row(m, 8);
    W8(SW_BT_SUP, 7);
    for (int i = 0; i < 4; i++) sw_vwbyte(m, R8(SW_TEMP + i));
    VG(SW_VGCNTR);

    /* CALCULATE AND SHOW AVERAGE GAME TIME */
    sw_eetemp(m, SW_GAMES - 2);             /* MOVE NUMBER OF GAMES PLAYED TO TEMP+1 */
    (void)R8(SW_TEMP);
    W8(SW_TEMP, 0);                         /* LEADING 0 */
    if ((R8(SW_TEMP + 1) | R8(SW_TEMP + 2) | R8(SW_TEMP + 3)) != 0) {  /* I WON'T DIVIDE BY 0 */
        sw_eetmp3(m, SW_TTIME, SW_TEMP2, 3);    /* GET TOTAL GAME TIME TO TEMP2 */
        divide(m);                          /* ( GAME TIME/# OF GAMES) */
        /* ADJUST ANSWER FOR M:S FORMAT: E MEANS 56 SECONDS ( 14*4) F IS 60 SECONDS */
        a = R8(SW_TEMP);
        if ((a & 0x0f) >= 0x0f)             /* 60 SECONDS GETS ADDED IN AS A MINUTE (HI NIBBLE) */
            W8(SW_TEMP, R8(SW_TEMP) + 1);
        /* HI NIBBLE REALLY 64 SECONDS NOT 60 SECONDS, SO ADD 64 SECOND NIBBLE TO 4 SECOND NIBBLE */
        a = (uint8_t)((a >> 4) + R8(SW_TEMP));
        W8(SW_TEMP, a);                     /* ADJUSTED MINUTES+SECONDS/4 IE. M:S */
        if ((a & 0x0f) >= 0x0f)             /* IF 60 SECONDS ADD 1 TO MINUTES */
            W8(SW_TEMP, R8(SW_TEMP) + 1);   /* 0F+1=10 */
    }
    acc_row(m, 5);                          /* SHOW IT */
    time_ms(m, R8(SW_TEMP));
    VG(SW_VGCNTR);

    /* CALCULATE AND SHOW PERCENTAGE OF PLAY/TIMEON */
    sw_eetemp(m, SW_TIMEON);                /* GET TOTAL TIME ON TO TEMP */
    if ((R8(SW_TEMP) | R8(SW_TEMP + 1) | R8(SW_TEMP + 2) | R8(SW_TEMP + 3)) != 0) {    /* WON'T DIVIDE BY ZERO */
        sw_eetmp3(m, SW_TTIME + 2, SW_TEMP2, 3);    /* GET PLAY TIME ON TO TEMP2 ( 2 LSD IGNORED ) */
        (void)R8(SW_TEMP2 + 3);
        W8(SW_TEMP2 + 3, 0);                /* * 100. */
        divide(m);
    }
    acc_row(m, 6);                          /* SHOW IT */
    sw_binbcd(m, R8(SW_TEMP), 0x10);        /* CONVERT FROM HEX */
    W8(SW_BT_SUP, 1);
    sw_vwbyte(m, R8(SW_BCD + 2));
    VG(SW_VGCNTR);

    /* DISPLAY HISTOGRAM OF GAME TIMES */
    W8(SW_TEMP, (SW_ZHISTO - SW_HISTOG - 2) / 2);   /* START AT LARGEST TIME ( 6:00 ) */
    W16(SW_TEMP + 2, T_HISP);
    do {
        u = R16(SW_TEMP + 2);
        VG(R16(u)); u += 2;                 /* Y POSITION */
        /* PAST HALF WAY POINT + 2: PUT DATA ON LEFT HALF OF SCREEN, ELSE RIGHT SIDE */
        VG(u >= T_HISP + 20 ? (0x1fff & (uint16_t)-300) : (0x1fff & 100));
        W16(SW_TEMP + 2, u);
        time_ms(m, R8(T_HIST - 1 + R8(SW_TEMP)));   /* TIME SLOT TIME IN M:S FORMAT */
        uint16_t d = R16(SW_VGMSGA);        /* 2 BLANKS */
        VG(d);
        VG(d);
        W8(SW_BT_SUP, 0);
        uint8_t b = (uint8_t)(R8(SW_TEMP) << 1);
        sw_vwnibl(m, R8(SW_HISTOG + b));    /* HIGH NIBBLE: SHOW IT */
        sw_vwnibl(m, R8(SW_HISTOG + b + 1));
        VG(SW_VGCNTR);                      /* CENTER INSTRUCTION */
        a = (uint8_t)(R8(SW_TEMP) - 1);
        W8(SW_TEMP, a);
    } while (!(a & 0x80));
}

/* EEOPT3: DISLAY THE OPTION SWITCH VALUE. A=OPTION NUMBER. The color MUST
 * BE ALREADY SET UP */
void sw_eeopt3(sw_machine *m, uint8_t a)
{
    uint8_t b = R8(T_OPT + a);              /* OPTION SWITCH DESCRIPTOR */
    W8(SW_TEMP, b);                         /* SAVE IT */
    uint16_t x = (uint16_t)(SW_OPTS0 + ((b & 0x07) << 1));  /* WHICH WORD? NIBBLES REMEMBER */
    b = R8(SW_TEMP) >> 3;
    W8(SW_TEMP, b);
    uint16_t u = (uint16_t)(EEMASK + (b & 0x03));   /* WHAT IS THE FIELD SIZE? POINTS TO FIELD MASK */
    b = R8(SW_TEMP) >> 2;
    W8(SW_TEMP, b);                         /* BIT POSITION OF FIELD */
    b = (uint8_t)(R8(x) << 4);              /* GET OPTION SWITCH DATA (BOTH NIBBLES) */
    W8(SW_TEMP + 1, b);
    b = (uint8_t)((R8(x + 1) & 0x0f) + R8(SW_TEMP + 1));
    for (;;) {                              /* MOVE FIELD TO LSB */
        uint8_t n = (uint8_t)(R8(SW_TEMP) - 1);
        W8(SW_TEMP, n);
        if (n & 0x80) break;
        b >>= 1;
    }
    b &= R8(u);                             /* MASK FOR FIELD */
    W8(SW_EEVAL, b);
    sw_domes2(m, (uint8_t)(b + R8(T_MOPV + a)));    /* MESSAGE NUMBER OF THIS VALUE */
}

/* EECMP: COMPARE EEROM DATA TO BUFFER DATA. Returns 0 if they are the same
 * (and says so in NVTSTR), else the address after the first that is not. */
static uint16_t eecmp(sw_machine *m)
{
    for (uint16_t x = SW_EEROM, u = SW_EEBUF; x < SW_EEROMZ; x++, u++)
        if (((R8(x) ^ R8(u)) & 0x0f) != 0) return (uint16_t)(x + 1);    /* LOW NIBBLE ONLY */
    W8(SW_NVTSTR, 0x01);                    /* SET FLAG TO SAY NOVRAM OK */
    return 0;
}

/* RESTOR: ERROR IN NOVRAM, SET FLAGS AND REWRITE DATA WITH BUFFER */
static void restor(sw_machine *m, uint16_t x)
{
    W16(SW_NVADDR, x - 1);                  /* ADDRESS OF ERROR, FOR REPORTING */
    W8(SW_NVTSTR, 0xff);                    /* FLAG SAYING NOVRAM BAD */
    for (uint16_t e = SW_EEROM, u = SW_EEBUF; e < SW_EEROMZ; e += 2, u += 2)
        W16(e, R16(u));                     /* COPY BUFFER TO EEROM RAM */
}

/* NOVRCL: RECALL NON-VOLATILE MEMORY. The original waits 33 game frames
 * for the line's capacitor; the frames are waited for afterwards
 * (sw_machine.hold_frames). */
static void novrcl(sw_machine *m)
{
    W8(SW_NVRRCL, 0xff);                    /* D7 = 1 PUTS NOVRAM IN RECALL MODE */
    W8(SW_NVRRCL, 0x00);                    /* D7 = 0 BRINGS US OUT OF RECALL MODE */
    m->hold_frames = (uint8_t)(m->hold_frames + 33);
}

/* EETST: TEST STORE-ABILITY OF EEROM (NOVRAM).
 * *** NOTE: USES UP 2 OF THE 1000 STORES QUOTED BY THE MANUFACTURER. *** */
static void eetst(sw_machine *m)
{
    uint16_t x, u;
    for (x = SW_EEROM, u = SW_EEBUF; x < SW_EEROMZ; x += 2, u += 2) W16(u, R16(x));    /* COPY CURRENT DATA TO BUFFER */
    for (x = SW_EEROM; x < SW_EEROMZ; x++) W8(x, ~R8(x));  /* COMPLIMENT THE DATA PRIOR TO STORE */
    W8(SW_NSTORE, 0);                       /* STORE IT OUT TO ROM PORTION OF EEROM */
    for (x = SW_EEROM; x < SW_EEROMZ; x++) W8(x, 0);       /* CLEAR IT OUT TO TEST THE RECALL */
    novrcl(m);                              /* RECALL THE DATA: SHOULD BE COMPLIMENT OF BUFFER DATA */
    for (x = SW_EEROM; x < SW_EEROMZ; x++) W8(x, ~R8(x));  /* COMPLIMENT IT AGAIN: THE ORIGINAL DATA AGAIN */
    W8(SW_NSTORE, 0);                       /* STORE IT OUT AGAIN */
    x = eecmp(m);                           /* COMPARE DATA TO BUFFER */
    if (x != 0) { restor(m, x); return; }   /* BAD DATA--SET FLAG AND RESTORE WITH BUFFER DATA */
    /* TWICE COMPLIMENTED DATA LOOKS OK, TRY RECALLING THE DATA AGAIN */
    for (x = SW_EEROM; x < SW_EEROMZ; x++) W8(x, 0);
    novrcl(m);
    x = eecmp(m);                           /* ONE LAST COMPARE */
    if (x != 0) restor(m, x);               /* PUT THE RAM PORTION BACK IF POSSIBLE */
}

/* EETSTR: REPORT EETST RESULTS */
static void eetstr(sw_machine *m)
{
    uint8_t a = R8(SW_NVTSTR);              /* 0 MEANS NO TEST PERFORMED */
    if (a == 0) return;
    if (a == 0x01) {                        /* NO ERRORS -- GOOD EEROM */
        sw_domess(m, SW_MS_GNV);            /* "NO ERRORS" ( GOOD NOVRAM ) */
        return;
    }
    VG(0x1fff & (uint16_t)-150);            /* THERE WERE ERRORS */
    VG(0x1fff & 420);
    sw_vwbyte(m, R8(SW_NVADDR + 1));        /* LOW BYTE OF ADDRESS OF ERROR */
    VG(SW_VGCNTR);
    sw_domess(m, SW_MS_BNV);                /* "ERROR AT" ( BAD NOVRAM ) */
}

/* EEOPT: DISPLAY ALL OPTIONS, and do what the operator asked to be reset */
void sw_eeopt(sw_machine *m)
{
    uint8_t a;
    for (a = SW_MS_OPT; a < SW_MS_VLC; a++) sw_messon(m, a);   /* OPTION SWITCH MESSAGES */
    sw_messon(m, SW_MS_TST);                /* "AUX FOR SELF TEST" MESSAGE */
    VG(SW_VGCOLR + (VGCGRN << 8));          /* OPTION SWITCH VALUE COLOR */
    W8(SW_TEMP2, N_OPT - 1);                /* DISPLAY ALL OPTION SWITCH VALUES */
    do {
        sw_eeopt3(m, R8(SW_TEMP2));
        a = (uint8_t)(R8(SW_TEMP2) - 1);
        W8(SW_TEMP2, a);
    } while (!(a & 0x80));

    eetstr(m);                              /* REPORT RESULTS OF EEROM ( NOVRAM ) TEST */

    if ((R8(SW_RESETS) & 0x0f) == 0) return;    /* SOMETHING NEEDS TO BE RESET */
    sw_domess(m, SW_MS_RES);                /* "USE RIGHT FIRE BUTTON TO PERFORM ANY RESET" */
    if ((R8(SW_GN_SWE) & SW_RFIRBT) == 0) return;   /* CHECK FOR RIGHT FIRE: IT WAS PUSHED, RESET INDICATED AREAS */
    if (R8(SW_RESETS) & 0x08) {             /* RESET HIGH SCORES? */
        eecpy(m, 0x01);                     /* PERFORM DEFAULT COPY ON AREA 1 */
        sw_setint(m);                       /* RESET OTHER INITIALS TOO */
    }
    if (R8(SW_RESETS) & 0x04) {             /* RESET ACCOUNTING INFO? */
        /* CLEAR AREA 2 STARTING AT GAMES ( LEAVE COINS ALONE ) */
        for (uint16_t x = SW_GAMES; x < SW_EECK2; x += 2) W16(x, 0x0000);
        sw_eecksm(m, 0x02);                 /* CHECKSUM AREA 2 */
    }
    if (R8(SW_RESETS) & 0x02) {             /* RESET OPTIONS TO DEFAULT? */
        eecpy(m, 0x00);                     /* SET AREA 0 TO DEFAULT */
        /* COPY BACKUP AREA TO PRIMARY AREA */
        uint16_t x = (uint16_t)(SW_EEROM + R8(SW_L_TAREA + NAREA));
        uint16_t u = SW_EEROM;
        W8(SW_TMPCNT, R8(SW_L_TAREA + 1));
        do {
            W8(x++, R8(u++));
            W8(SW_TMPCNT, R8(SW_TMPCNT) - 1);
        } while (R8(SW_TMPCNT) != 0);
    }
    if (R8(SW_RESETS) & 0x01) eetst(m);     /* TEST NOVRAM? TEST STORE-ABILITY OF NOVRAM */
    W16(SW_RESETS, 0x0000);                 /* CLEAR RESET BYTE */
}

/* The options from outside the program (the port's test menu): the same
 * fields of the NOVRAM the option page shows, by the descriptors in T.OPT
 * (the first SW_GAME_OPTIONS of them are the options; the rest are the
 * resets and the NOVRAM test, which stay on the arcade program's page). */
int sw_option_get(const sw_machine *m, int i, int *max)
{
    uint8_t b = sw_rom16((sw_machine *)m, T_OPT + (uint16_t)i) >> 8;  /* OPTION DESCRIPTOR */
    uint16_t x = (uint16_t)(SW_OPTS0 + ((b & 0x07) << 1));
    uint8_t mask = sw_rom16((sw_machine *)m, EEMASK + ((b >> 3) & 0x03)) >> 8;
    uint8_t v = (uint8_t)((m->nvram[x - 0x4500] << 4) | (m->nvram[x + 1 - 0x4500] & 0x0f));
    if (max) *max = mask;
    return (v >> (b >> 5)) & mask;
}

void sw_option_set(sw_machine *m, int i, int value)
{
    uint8_t b = R8(T_OPT + i);              /* OPTION DESCRIPTOR */
    uint8_t mask = R8(EEMASK + ((b >> 3) & 0x03));
    unsigned pos = b >> 5;
    uint16_t x = (uint16_t)(SW_OPTS0 + ((b & 0x07) << 1));
    uint8_t v = (uint8_t)((R8(x) << 4) | (R8(x + 1) & 0x0f));
    v = (uint8_t)((v & ~(mask << pos)) | (((unsigned)value & mask) << pos));
    W8(x + 1, v);                           /* PUT IT BACK INTO EEROM */
    W8(x, v >> 4);
    sw_eecksm(m, NAREA);                    /* SET CHECKSUM */
    /* MOV EEROM TO EEBUF TO PREPARE FOR "EEINI1" (as EEOPT1 does) */
    for (uint16_t e = SW_EEROM, u = SW_EEBUF; e < SW_EEROM + 0xff; e += 2, u += 2) W16(u, R16(e));
    eeinit_from(m, EE_INI1, 0);             /* SETUP BACKUP AND CHECK OTHER AREAS */
}

/* EEOPT1: MAKE CHANGES TO OPTION SWITCH VALUES. The yoke up and down
 * chooses the option, the left trigger steps its value. */
void sw_eeopt1(sw_machine *m)
{
    uint8_t a, b;
    if ((R8(SW_FRAMEL) & 0x0f) == 0) {      /* NOT TOO QUICK */
        b = R8(SW_SI_RSY);                  /* UP/DOWN */
        if (b < 0xd0 && b > 0x30) {         /* MAY BE NEGATIVE ENOUGH; LARGE ENOUGH TO CHANGE */
            if (!(b & 0x80)) {              /* BACK AN OPTION */
                b = (uint8_t)(R8(SW_EEINDX) - 1);
                if (b & 0x80) b = N_OPT - 1;    /* WRAP WHEN BELOW 0 */
            } else {                        /* FORWARD AN OPTION */
                b = (uint8_t)(R8(SW_EEINDX) + 1);
                if (b > N_OPT - 1) b = 0;   /* WRAP */
            }
            W8(SW_EEINDX, b);
        }
    }
    b = R8(T_OPT + R8(SW_EEINDX));          /* GET OPTION DESCRIPTOR */
    W8(SW_TEMP + 2, b);
    a = R8(EEMASK + ((b >> 3) & 0x03));     /* FIELD SIZE: MASK FOR THIS FIELD SIZE */
    W8(SW_TEMP, a);                         /* ALSO MAX VALUE FOR CURRENT OPTION */

    /* LEFT FIRE BUTTON CHANGES THE OPTION */
    if ((R8(SW_GN_SWE) & SW_LFIRBT) == 0) return;   /* LOOK FOR EDGE OF LEFT FIRE BUTTON */
    b = (uint8_t)(R8(SW_EEVAL) + 1);        /* CURRENT VALUE: NEXT VALUE, MAYBE */
    if (b > R8(SW_TEMP)) b = 0x00;          /* PAST MAX? YES, WRAP BACK TO LOWEST VALUE */
    W8(SW_EEVAL, a);                        /* (STA: the mask, as the original has it) */

    a = (R8(SW_TEMP + 2) >> 5) & 0x07;      /* OPTION DESCRIPTOR: GET BIT POSITION */
    for (; a != 0; a--) {                   /* MOVE DATA AND MASK INTO POSITION */
        b = (uint8_t)(b << 1);              /* SHIFT DATA */
        W8(SW_TEMP, R8(SW_TEMP) << 1);      /* SHIFT MASK */
    }
    /* INDEX INTO OPTION SWITCH AREA: NIBBLES, NIBBLES, NIBBLES. I'VE GOT TO REMEMBER NIBBLES */
    uint16_t x = (uint16_t)(SW_OPTS0 + ((R8(SW_TEMP + 2) & 0x07) << 1));
    W8(SW_TEMP + 1, R8(x) << 4);            /* GET THE DATA INTO TEMP STORAGE: HIGH NIBBLE */
    W8(SW_TEMP + 1, (R8(x + 1) & 0x0f) + R8(SW_TEMP + 1));     /* LO NIBBLE */
    b ^= R8(SW_TEMP + 1);                   /* PUT NEW DATA INTO TEMP */
    b &= R8(SW_TEMP);
    b ^= R8(SW_TEMP + 1);
    W8(x + 1, b);                           /* PUT IT BACK INTO EEROM */
    W8(x, b >> 4);
    sw_eecksm(m, NAREA);                    /* SET CHECKSUM */
    /* MOV EEROM TO EEBUF TO PREPARE FOR "EEINI1" */
    for (uint16_t e = SW_EEROM, u = SW_EEBUF; e < SW_EEROM + 0xff; e += 2, u += 2) W16(u, R16(e));
    eeinit_from(m, EE_INI1, 0);             /* SETUP BACKUP AND CHECK OTHER AREAS */
    sw_eeopt3(m, R8(SW_EEINDX));            /* DISPLAY THE NEW VALUE */
}
