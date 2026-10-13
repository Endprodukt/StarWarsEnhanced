/* hscore.c - TCHSCR.MAC: the high score table and entering initials.
 *
 * Line by line after the 1983 source; its comments are kept in capitals.
 * The default initials and scores (INTINT, INTSCR) and the position tables
 * are read from the program ROM. */
#include "game/game.h"
#include "game/m6809.h"

static void doints(sw_machine *m);
static void puthi(sw_machine *m, uint16_t x);

/* One initial of a line: flashing while it is the one being entered, an
 * underline for a blank while entering. */
static uint16_t initial(sw_machine *m, uint16_t x, uint8_t which)
{
    if (!(R8(SW_UPDFLG) & 0x80)) {          /* ENTERING INITIALS? YES, HAVE TO FLASH CURRENT INITIAL */
        uint16_t d;
        if (R8(SW_UPDINT) == which) {       /* THIS INITIAL?? */
            if ((R8(SW_FRAME + 1) & 0x01) == 0) d = R16(SW_PH_COL);   /* OFF THIS FRAME */
            else d = R16(SW_L_TMPCOL);      /* WILL BE WHITE IF CURRENT INITIAL BEING ENTERED */
        } else {
            d = R16(SW_L_TMPCOL);
        }
        VG(d);
    }
    uint8_t a = R8(x++);
    if (a == 0) {                           /* BLANK */
        if (!(R8(SW_UPDFLG) & 0x80)) VG(R16(SW_VGMSGA + 82));   /* SHOW UNDERLINE INSTEAD OF BLANK */
        else VG(R16(SW_VGMSGA));
    } else {                                /* THEN A-Z */
        VG(R16(SW_VGMSGA + 0x14 + 2 * a));  /* JSRL TO LETTER */
    }
    return x;
}

/* VWHSCR: DISPLAY THE HIGH SCORE TABLE */
void sw_vwhscr(sw_machine *m)
{
    uint16_t d, x, u;

    if (!(R8(SW_UPDFLG) & 0x80)) {          /* ENTERING INITIALS? */
        sw_messon(m, SW_MS_RF1);            /* "REBEL FORCE" MIDDLE OF SCREEN */
        VG(SW_VGSCAL);
        d = SW_L_THSY1;                     /* TABLE OF HI SCORE Y POSITIONS FOR HALF SCREEN */
    } else {
        sw_messon(m, SW_MS_RF2);            /* "REBEL FORCE" TOP OF SCREEN */
        VG(SW_VGSCAL - 0x100 + 0x40);       /* MAKE LIST LARGER, BUT NOT TOO LARGE */
        d = SW_L_THSY2;                     /* ... FOR WHOLE SCREEN */
    }
    W16(SW_HSYPOS, d);
    W8(SW_HSCNT, 0);                        /* START AT THE FIRST ONE */

    do {
        TAG(SW_TAG_HISCORE, R8(SW_HSCNT) * 4 + 0);
        u = R16(SW_HSYPOS);                 /* DO THE INITIALS FIRST */
        VG(R16(u));                         /* GET Y POS */
        VG(0x1fff & -128);                  /* X POS */

        x = (uint16_t)(SW_INITLS + (uint8_t)(R8(SW_HSCNT) * 3));
        if (x == R16(SW_UPDFLG))            /* PLAYER'S INITIALS? ( ONLY FOR ENTERING INITIALS ) */
            d = VGCOLR + (VGCWHT << 8);     /* MAKE THEM STAND OUT */
        else
            d = R16(SW_PH_COL);             /* COLOR DEPENDING ON THE PHASE */
        VG(d);
        W16(SW_L_TMPCOL, d);                /* USED FOR FLASHING INITIAL IN ENTERING STATE */

        x = initial(m, x, 0);
        x = initial(m, x, 1);
        x = initial(m, x, 2);

        VG(R16(SW_L_TMPCOL));               /* RESTORE COLOR TO THIS LINE'S COLOR */
        VG(SW_VGCNTR);

        /* NOW THE NUMBER */
        TAG(SW_TAG_HISCORE, R8(SW_HSCNT) * 4 + 1);
        u = R16(SW_HSYPOS);
        VG(R16(u));
        VG(0x1fff & -200);
        uint8_t a = (uint8_t)(R8(SW_HSCNT) + 1);
        if (a >= 0x0a) a = 0x10;            /* MAKE IT 10 */
        W8(SW_BT_SUP, 1);                   /* SUPPRESS THIS MANY ZEROS */
        sw_vwbyte(m, a);
        VG(SW_VJPERD);                      /* PERIOD */
        VG(SW_VGCNTR);

        /* NOW FOR THE SCORE */
        TAG(SW_TAG_HISCORE, R8(SW_HSCNT) * 4 + 2);
        x = (uint16_t)(SW_HSCORS + (uint8_t)(R8(SW_HSCNT) << 2));
        u = R16(SW_HSYPOS);
        VG(R16(u));
        u += 2;
        VG(0x1fff & -16);
        W16(SW_HSYPOS, u);
        W8(SW_BT_SUP, 6);                   /* SUPPRESS THIS MANY LEADING ZEROS */
        sw_vw8dig(m, x);                    /* VIEW 8 DIGITS WITH COMMAS */

        VG(SW_VGCNTR);
        W8(SW_HSCNT, R8(SW_HSCNT) + 1);
    } while (R8(SW_HSCNT) < 0x0a);

    TAG(SW_TAG_HISCORE, 0x3fe);
    VG(SW_VGSCAL);                          /* RESTORE SCALE TO NORMAL */

    if (R16(SW_UPDFLG) & 0x8000) return;    /* ENTERING INITIALS? NOPE, THEN LEAVE */

    /* ELSE THEN SHOW ALPHABET */
    TAG(SW_TAG_ALPHABET, 0);
    VG(VGCOLR + (VGCRED << 8));
    x = SW_L_TINTXY + 4;                    /* TABLE OF XY POSITIONS FOR ALPHABET */
    u = SW_VGMSGA + 22;                     /* START WITH "A" */
    do {
        VG(R16(x + 2) & 0x1fff);            /* Y POS */
        VG(R16(x) & 0x1fff);                /* X POS */
        VG(R16(u)); u += 2;                 /* JSRL TO LETTER */
        VG(SW_VGCNTR);
        x += 4;
    } while (x < SW_L_TINTXY + 27 * 4);     /* STOP AFTER "Z" */

    /* THE UNDERLINE */
    VG(R16(SW_L_TINTXY + 2) & 0x1fff);
    VG(R16(SW_L_TINTXY + 0) & 0x1fff);
    VG(R16(SW_VGMSGA + 82));                /* "_" OR UNDERLINE */
    VG(SW_VGSCAL);
    VG(SW_VGCNTR);

    /* NEXT COMES "RUB" */
    if (R8(SW_INITL) == 27) d = VGCOLR + (VGCWHT << 8) - 0x30;    /* YES, THEN HIGHLIGHT IT */
    else d = VGCOLR + (VGCRED << 8) - 0x30;
    VG(d);
    VG(R16(x + 2) & 0x1fff);
    VG((R16(x) - 8) & 0x1fff);
    VG(SW_VGSCAL - 0x40);                   /* MAKE LETTERS SMALLER */
    VG(R16(SW_VGMSGA + 56));                /* "R" */
    VG(R16(SW_VGMSGA + 62));                /* "U" */
    VG(R16(SW_VGMSGA + 24));                /* "B" */
    VG(SW_VGSCAL);
    VG(SW_VGCNTR);

    /* NOW THE WORD "END" */
    if (R8(SW_INITL) == 28) d = VGCOLR + (VGCWHT << 8) - 0x30;
    else d = VGCOLR + (VGCRED << 8) - 0x30;
    VG(d);
    VG(R16(x + 6) & 0x1fff);
    VG((R16(x + 4) - 8) & 0x1fff);
    VG(SW_VGSCAL - 0x40);
    VG(R16(SW_VGMSGA + 30));                /* "E" */
    VG(R16(SW_VGMSGA + 48));                /* "N" */
    VG(R16(SW_VGMSGA + 28));                /* "D" */
    VG(SW_VGSCAL);
    VG(SW_VGCNTR);

    /* HILITE SELECTED LETTER */
    VG(VGCOLR + (VGCWHT << 8));
    uint8_t b = R8(SW_INITL);               /* CURRENT LETTER */
    if (b < 27) {                           /* NORMAL ALPHAS ( END, RUB DONE ELSEWHERE ) */
        x = (uint16_t)(SW_L_TINTXY + (uint8_t)(b << 2));
        VG(R16(x + 2) & 0x1fff);
        VG(R16(x) & 0x1fff);
        b = R8(SW_INITL);
        if (b == 0) d = R16(SW_VGMSGA + 82);    /* BLANK?? USE UNDERLINE INSTEAD */
        else d = R16(SW_VGMSGA + 20 + (uint8_t)(b << 1));
        VG(d);
        VG(SW_VGCNTR);
    }

    VG(VGCOLR + (VGCYLW << 8));             /* YELLOW FOR THE SITE */
    sw_vwcurc(m);                           /* DISPLAY THE CURSOR */
}

/* UPDATE: CHECK FOR HIGH SCORE. SET UPDFLG TO INDEX OR -1 IF NOT. */
void sw_update(sw_machine *m)
{
    uint16_t x = SW_HSCORS;
    do {
        uint16_t s = R16(SW_S_SC87), h = R16(x);    /* 4 MSDs */
        if (s > h) goto updat2;             /* MSD'S TELL US IT IS A HIGH SCORE */
        if (s == h && R16(SW_S_SC43) >= R16(x + 2)) goto updat2;   /* 4 LSDs */
        x += 4;                             /* NEXT HI SCORE */
    } while (x < SW_HSCORS + 4 * SW_NHSCRS);
    return;

updat2:
    puthi(m, x);                            /* MAKE ROOM */
    W8(SW_UPDINT, 0);                       /* WORK ON FIRST INITIAL */
    W16(SW_INITL, 0x0000);                  /* CURRENT VALUE OF INITIAL BEING WORKED ON */
}

/* PUTHI: MAKE ROOM IN HIGH SCORE TABLES FOR NEW HI SCORE. X=INDEX FOR NEW HI */
static void puthi(sw_machine *m, uint16_t x)
{
    W16(SW_UPDFLG, x);                      /* HERE'S WHERE TO STOP */
    uint16_t u = SW_INITLS + 3 * SW_NHSCRS - 3;     /* END OF INITIALS */
    x = SW_HSCORS + 4 * SW_NHSCRS - 4;      /* END OF HIGH SCORE TABLE */
    if (x != R16(SW_UPDFLG)) {              /* REPLACING LAST ENTRY?? NO, OK SHIFT TABLE */
        do {
            W16(x, R16(x - 4));             /* MOVE 4 BYTES OF SCORE DOWN */
            W16(x + 2, R16(x - 2));
            W16(u, R16(u - 3));             /* MOVE 3 BYTES OF INITIALS */
            W8(u + 2, R8(u - 1));
            u -= 3;
            x -= 4;
        } while (x > R16(SW_UPDFLG));
    }
    W16(SW_UPDFLG, u);                      /* U POINTS TO THE INITIALS */

    W8(u, 0x00);                            /* SETUP 1ST INITIAL AS A BLANK */
    W16(u + 1, 0);                          /* BLANKS FOR 2ND, AND 3RD */
    W16(x, R16(SW_S_SC87));                 /* MOVE PLAYER'S SCORE INTO TABLE */
    W16(x + 2, R16(SW_S_SC43));
}

/* GETINT: GET PLAYER'S INITIALS FOR HI SCORE */
void sw_getint(sw_machine *m)
{
    uint16_t u = (uint16_t)(R16(SW_UPDFLG) + R8(SW_UPDINT));   /* WHICH INITIAL ARE WE GETTING */
    uint16_t x;
    if (R8(SW_UPDINT) >= 0x03) x = SW_L_TINTXY + 4 * 27;    /* THEN ONLY CHECK FOR RUB OR END */
    else x = SW_L_TINTXY;                   /* TABLE OF POSITIONS FOR INITIALS ( CENTER OF LETTER ) */
    do {
        uint16_t d = sw_absd((uint16_t)(R16(SW_SI_CX) - 8 - R16(x)));   /* CHECK X POS OF CURSOR */
        W16(SW_L_TMPABS, d);
        if (d < 24) {                       /* WITHIN X LIMIT */
            d = sw_absd((uint16_t)(R16(SW_SI_CY) + (uint16_t)(SW_VGOFFY - 12) - R16(x + 2)));
            if (d < 24) {                   /* ALSO WITHIN Y LIMIT */
                d = (uint16_t)(d + R16(SW_L_TMPABS));       /* OCTAGON CHECK */
                if (d < 32)                 /* HIT */
                    W8(SW_INITL, (uint8_t)(x - SW_L_TINTXY) >> 2);
            }
        }
        x += 4;
    } while (x < SW_L_TINTZ);

    uint8_t a = R8(SW_INITL);               /* PUT IT IN TABLE */
    if (a < 27) W8(u, a);                   /* NORMAL ALPHA? */

    if (R8(SW_GN_SWE) & (SW_FIREBT | SW_THMBT)) {   /* TRANSITION FROM OFF TO ON */
        a = R8(SW_INITL);
        if (a == 27) {                      /* "RUB"? BACK UP ONE INITIAL */
            if (R8(SW_UPDINT) <= 0x02) W8(u, 0x00);     /* ON AN INITIAL? YES, RUB IT OUT */
            if (R8(SW_UPDINT)) {            /* DON'T WRAP */
                W8(SW_UPDINT, R8(SW_UPDINT) - 1);
                W8(u - 1, 0x00);            /* BLANK FOR THE INITIAL WE JUST MOVED BACK TO */
            }
            sw_doxmt(m, SW_IDASS);          /* SHOOTING ENEMY SHOT SOUND, RUBBING IT OUT */
        } else if (a == 28) {               /* "END"? ALL DONE, LEAVE */
            W16(SW_UPDFLG, 0xffff);         /* NO MORE HI SCORE */
            sw_doxmt(m, SW_IDAPH);          /* PORTHOLE SHOT WHEN ENDING INITIALS */
        } else {                            /* NORMAL ALPHA CHARACTER */
            W8(SW_UPDINT, R8(SW_UPDINT) + 1);
            if (R8(SW_UPDINT) >= 0x03) W8(SW_INITL, 28);    /* ALL INITIALS SELECTED? INITL TO "END" */
            sw_doxmt(m, SW_IDAXL);          /* LAZAR SOUND WHEN SHOOTING INITIALS */
        }
    }
}

/* SETINT: PUT OUR INITIALS IN FROM RESET */
void sw_setint(sw_machine *m)
{
    doints(m);                              /* COPY DEFAULTS TO HIGH SCORE TABLE */
    if (sw_summit(m, 0x01, 0, 0) == 0) {    /* GOOD, USE EEROM FOR THE TOP 3 SCORES */
        sw_eetmp3(m, SW_SAVINT, SW_INITLS, 0x08);
        sw_eetmp3(m, SW_SAVSCR, SW_HSCORS, 11);
    }
    sw_chkhis(m);
}

/* CHKHIS: MAKE SURE EACH ENTRY IS IN VALID RANGE */
void sw_chkhis(sw_machine *m)
{
    uint16_t x = SW_INITLS;                 /* INITIALS CAN RANGE FROM 0 TO 26. */
    do {
        if (R8(x++) >= 27) { doints(m); return; }   /* ERROR SOMEWHERE, PUT IN DEFAULTS */
    } while (x < SW_INITLS + 30);

    x = SW_HSCORS;                          /* MAKE SURE EACH NUMBER IS DECIMAL ( BCD ) */
    do {
        uint8_t a = R8(x++);
        if (a >= 0xa0 || (a & 0x0f) >= 0x0a) { doints(m); return; }
    } while (x < SW_HSCORS + 40);
}

static void doints(sw_machine *m)
{
    uint16_t x = SW_INITLS, u = SW_INTINT;  /* INITIAL INITIALS */
    do {
        W16(x, R16(u));
        u += 2; x += 2;
    } while (x < SW_INITLS + 30);
    x = SW_HSCORS; u = SW_INTSCR;           /* INITIAL HIGH SCORES */
    do {
        W16(x, R16(u));
        u += 2; x += 2;
    } while (x < SW_HSCORS + 40);
}
