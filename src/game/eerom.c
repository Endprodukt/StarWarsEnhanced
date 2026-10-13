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

enum { EE_INIT, EE_INI0 };

/* EEINIT and its second entry EEINI0 (which always sets up the option
 * switch areas from area A's defaults). */
static void eeinit_from(sw_machine *m, int entry, uint8_t a)
{
    uint16_t x, u;

    if (entry == EE_INI0) goto eeini0;

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
