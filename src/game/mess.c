/* mess.c - TCMES.MAC: the message generator.
 *
 * Messages are numbers; their texts, positions and colors are tables in
 * the program ROM, read there. Line by line after the 1983 source; its
 * comments are kept in capitals. */
#include "game/game.h"
#include "game/m6809.h"

static void mcolr(sw_machine *m);
static void mout(sw_machine *m);
static void mout2(sw_machine *m);
static uint8_t spmoff(sw_machine *m, uint8_t a, uint8_t b);

/* MESSON: ADDS MESSAGE NUMBER IN MESSAGE DISPLAY LIST IF NOT ALREADY THERE */
void sw_messon(sw_machine *m, uint8_t a)
{
    uint16_t x = SW_MESBLK;
    if (x < R16(SW_MESLST)) {               /* LIST NOT EMPTY, SEARCH IT */
        do {
            if (a == R8(x++)) return;       /* ALREADY HERE */
        } while (x < R16(SW_MESLST));
    }
    W8(x++, a);                             /* STORE MESSAGE NUMBER */
    W16(SW_MESLST, x);                      /* UPDATE POINTER */
}

/* MESSOFF: DELETES MESSAGE NUMBER FROM MESSAGE LIST */
void sw_messoff(sw_machine *m, uint8_t a)
{
    uint16_t x = SW_MESBLK;
    do {
        if (a == R8(x)) {                   /* FOUND IT, DELETE IT */
            uint16_t u = R16(SW_MESLST);
            if (u > SW_MESBLK) {            /* LIST WASN'T EMPTY, OK TO DELETE IT */
                u--;                        /* POINTS TO LAST ENTRY NOW */
                a = R8(u);
                W8(x, a);                   /* INSERT AT ENTRY TO BE DELETED */
                W16(SW_MESLST, u);
                x = u;                      /* LEAVE LOOP */
            }
        }
        x++;
    } while (x < R16(SW_MESLST));
}

/* ALLOFF: SETS POINTER TO START OF LIST */
void sw_alloff(sw_machine *m)
{
    W8(SW_MESBLK, 0);
    W16(SW_MESLST, SW_MESBLK);
}

/* VWMESS: OUTPUT ALL MESSAGES IN THE MESSAGE DISPLAY LIST */
void sw_vwmess(sw_machine *m)
{
    uint16_t u = SW_MESBLK;
    if (u < R16(SW_MESLST)) {
        do {
            uint8_t b = R8(u++);
            if (b < SW_L_MESSZ) {           /* IS IT A VALID MESSAGE NUMBER? */
                W8(SW_MESNUM, b);
                TAG(SW_TAG_MESS, b);
                mcolr(m);
                mout2(m);
            }
        } while (u < R16(SW_MESLST));
    }
}

/* VWMES2 NEVER DOES A COLOR COMMAND */
void sw_vwmes2(sw_machine *m)
{
    uint16_t u = SW_MESBLK;
    if (u < R16(SW_MESLST)) {
        do {
            uint8_t b = R8(u++);
            if (b < SW_L_MESSZ) {
                W8(SW_MESNUM, b);
                TAG(SW_TAG_MESS, b);
                mout2(m);
            }
        } while (u < R16(SW_MESLST));
    }
}

/* SPMON: a line into the list of messages that recede into the distance.
 * An entry is the message number, its size (16 bits) and the size's
 * increment (16 bits). */
static void spmon(sw_machine *m, uint8_t a)
{
    uint16_t u = R16(SW_SPMLST);
    W8(u++, a);
    W16(u, 0x0000); u += 2;                 /* SIZE ALWAYS STARTS AT LINEAR SCALE OF 0 */
    W16(u, 0x0100); u += 2;                 /* START MOVING SLOW */
    W16(SW_SPMLST, u);
}

/* SPMOFF: the entry stays in the list with message number 0 (and B, as the
 * original leaves it, in the high byte of its size). Returns A (0 once an
 * entry was found). */
static uint8_t spmoff(sw_machine *m, uint8_t a, uint8_t b)
{
    uint16_t u = SW_SPMBLK;
    do {
        if (a == R8(u)) {                   /* YES, FOUND IT */
            a = 0;
            W16(u, (uint16_t)((a << 8) | b));
        }
        u += 5;                             /* NEXT ENTRY */
    } while (u < R16(SW_SPMLST));
    return a;
}

/* VWSPMS: OUTPUT MESSAGES IN SPECIAL MESSAGE LIST ACCORDING TO THEIR SIZE.
 * THIS GIVES THE "STAR WARS" EFFECT. */
void sw_vwspms(sw_machine *m)
{
    uint16_t u = SW_SPMBLK;
    if (u >= R16(SW_SPMLST)) return;
    TAG(SW_TAG_SPMESS, 0x3ff);
    VG(SW_VGSCAL);                          /* SET UP SCALE */
    do {
        uint8_t a = R8(u++);                /* GET MESSAGE # */
        if (a) {
            W8(SW_MESNUM, a);
            TAG(SW_TAG_SPMESS, a);
            VG(SW_VGLIMT & 0x1fff);         /* VECTOR FROM CENTER TO VANISHING POINT */
            VG(0);
            uint8_t b = R8(u);              /* SIZE FROM LIST */
            VG((((SW_VGSCAL >> 8) & 0xf0) + 1) << 8 | b);    /* MAKES THIS A SCAL INSTRUCTION */
            b = (uint8_t)(~b + 0x10);       /* BRIGHTNESS RELATIVE TO INVERSE OF SCALE, PLUS AN OFFSET */
            VG(((VGCOLR >> 8) + VGCGRN) << 8 | b);
            uint16_t x = (uint16_t)(SW_L_TSPMX - SW_SPMS1 * 2 + 2 * R8(SW_MESNUM));
            VG(0x1dd0);                     /* DELTA Y (FROM VANISHING POINT) */
            VG(R16(x) & 0x1fff);            /* DELTA X FOR MESSAGE */
            sw_mesout(m);
            VG(SW_VGSCAL);                  /* RESET THE SCALE TO NORMAL */
            VG(SW_VGCNTR);                  /* CENTER AFTER MESSAGE */
        }
        u += 4;
    } while (u < R16(SW_SPMLST));
}

/* BGBNNR: BEGIN BANNER */
void sw_bgbnnr(sw_machine *m)
{
    W16(SW_BN_CNT, 0);
    W16(SW_BN_COL, (VGCOLR & 0xf000) + 0x18);   /* START WITH DIM Z */
    W16(SW_SPMLST, SW_SPMBLK);
    W16(SW_SPMALM, R16(SW_TSPMAL));         /* SETUP ALARM FOR 1ST MESSAGE */
    W8(SW_SPMNXT, SW_SPMS1);
}

/* SPMESS: SPECIAL MESSAGE UPDATE SIZES */
static void spmess(sw_machine *m)
{
    uint16_t x = SW_SPMBLK;
    uint16_t d;
    if (x < R16(SW_SPMLST)) {               /* THERE IS A MESSAGE TO PROCESS */
        do {
            d = R16(SW_BN_CNT);
            if (d < 0x00e0) {
                if (d >= 0x40) W16(x + 1, R16(x + 1) + R16(x + 3));
            } else if (d < 0x160) {
                W16(SW_SPMBLK + 3, 0x400);  /* SPEED IT UP */
            } else {
                d = (uint16_t)(R16(x + 1) + R16(x + 3));    /* UPDATE SCALE */
                W16(x + 1, d);
                if (d >= 0xf000) {          /* MAKE NEXT ONE FAST */
                    uint8_t a = (uint8_t)(R8(x) + 1);       /* NEXT MESSAGE */
                    uint8_t b = (uint8_t)d;
                    uint16_t u = SW_SPMBLK;
                    do {
                        if (a == R8(u)) {   /* FOUND NEXT HIGHER MESSAGE */
                            W16(u + 3, 0x400);
                            a = 0x04; b = 0x00;
                            u = R16(SW_SPMLST);             /* LEAVE LOOP */
                        }
                        u += 5;
                    } while (u < R16(SW_SPMLST));
                    spmoff(m, R8(x), b);    /* GET RID OF IT */
                    x -= 5;                 /* MAKE SURE WE DO THE ONE THAT REPLACED IT */
                }
            }
            x += 5;
        } while (x < R16(SW_SPMLST));
    }
    d = R16(SW_BN_CNT);                     /* INC TIMER */
    if (d >= 0x200) {                       /* DONE, GO BACK TO INSTRUCTION PHASE */
        W8(SW_PHASE, SW_PHSINS);
        d = (uint16_t)((SW_PHSINS << 8) | (d & 0xff));      /* LDA #PH$INS */
    }
    if (d >= R16(SW_SPMALM)) {              /* ALARM? ( NEXT LINE ?) */
        spmon(m, R8(SW_SPMNXT));
        uint8_t a = (uint8_t)(R8(SW_SPMNXT) + 1);
        if (a >= SW_SPMSZ) {                /* NO MORE MESSAGES */
            W16(SW_SPMALM, 0xffff);         /* TIMER WILL NEVER GET THIS FAR */
        } else {
            W8(SW_SPMNXT, a);               /* NEXT MESSAGE TO GO OUT */
            W16(SW_SPMALM, R16(SW_TSPMAL - SW_SPMS1 * 2 + 2 * a));
        }
    }
}

/* VWBNNR: VIEW BANNER, STAR WARS TITLE. Falls through to SPMESS. */
void sw_vwbnnr(sw_machine *m)
{
    uint16_t d = (uint16_t)(R16(SW_BN_CNT) + 1);
    W16(SW_BN_CNT, d);
    if (d < 0xf8) {                         /* MAX OF LINEAR SCALE; ELSE TOO SMALL */
        if (d < 0x40) {                     /* BRIGHTENING, NOT MOVING */
            d = R16(SW_BN_COL);
            d = (uint16_t)((d & 0xff00) | (uint8_t)(d + 3));
            W16(SW_BN_COL, d);
            d = 0x40;                       /* ASSUME A SIZE */
        } else {                            /* NOW MOVE AND DIM BRIGHTNESS RELATIVE TO SCALE */
            d = R16(SW_BN_CNT);
            d = (uint16_t)((d & 0xff00) | (uint8_t)(~d + 0x18));    /* ADD AN OFFSET SO BRIGHT LONGER */
            W16(SW_BN_COL, d);
            d = R16(SW_BN_CNT);
        }
        d |= ((SW_VGSCAL >> 8) + 1) << 8;
        W16(SW_BN_SCL, d);

        TAG(SW_TAG_BANNER, 0);
        VG(R16(SW_BN_COL) | (((VGCOLR & 0xf000) >> 8) + VGCBLU) << 8);   /* SETUP COLOR JUST ONCE */

        static const uint16_t letters[6] = {
            SW_VJSWST, SW_VJSWSA, SW_VJSWSR, SW_VJSWWW, SW_VJSWWA, SW_VJSWRS   /* 'ST' 'A' 'R' 'W' 'A' 'RS' */
        };
        uint16_t y = m->y;
        for (int i = 0; i < 6; i++) {
            W16(y + i * 8 + 0, SW_VGLIMT & 0x1fff);     /* HORIZON POINT FOR BANNERS */
            W16(y + i * 8 + 2, 0);
            W16(y + i * 8 + 4, R16(SW_BN_SCL));
            W16(y + i * 8 + 6, letters[i]);
            TAG(SW_TAG_BANNER, 1 + i);
            VGT(y + i * 8 + 0); VGT(y + i * 8 + 2); VGT(y + i * 8 + 4); VGT(y + i * 8 + 6);
        }
        m->y = (uint16_t)(y + 6 * 8);
    }
    spmess(m);
}

/* VW8DIG: VIEW 87,654,321. X --> START OF 8 BCD VALUES. */
void sw_vw8dig(sw_machine *m, uint16_t x)
{
    sw_vwbyte(m, R8(x));                    /* GET 8 AND 7 */
    if ((int8_t)R8(SW_BT_SUP) <= 0)         /* ?ENCOUNTERED A DIGIT? */
        VG(SW_VJNUMS);                      /* THEN SEPARATE NUMERALS WITH COMMAS */
    sw_vw6dig(m, x);
}

/* VW6DIG: VIEW 654,321 */
void sw_vw6dig(sw_machine *m, uint16_t x)
{
    sw_vwbyte(m, R8(x + 1));                /* SHOW 65 */
    sw_vwnibl(m, R8(x + 2) >> 4);           /* SHOW 4, */
    if ((int8_t)R8(SW_BT_SUP) <= 0) VG(SW_VJNUMS);
    sw_vwnibl(m, R8(x + 2));                /* VIEW 3 */
    sw_vwbyte(m, R8(x + 3));                /* VIEW 21 */
}

/* One numeral: the index is twice the digit. A zero is a blank while zero
 * suppression (BT.SUP) is still positive. */
static void numeral(sw_machine *m, uint8_t twice)
{
    uint16_t u;
    m->n_digits++;
    if (twice == 0 && (int8_t)R8(SW_BT_SUP) > 0) {
        u = R16(SW_VGMSGA);                 /* THEN GET A BLANK */
    } else {
        W8(SW_BT_SUP, 0);                   /* IS SIGNIFICANT DIGIT--STOP ZERO SUPPRESSION */
        u = R16(SW_VGMSGA + 2 + twice);
    }
    W8(SW_BT_SUP, R8(SW_BT_SUP) - 1);       /* DECREASE NUMBER OF SUPPRESSED DIGITS */
    VG(u);
}

/* VWBYTE: VIEW BOTH NUMERALS IN BYTE */
void sw_vwbyte(sw_machine *m, uint8_t a)
{
    numeral(m, (a >> 3) & 0x1e);
    sw_vwnibl(m, a);
}

/* VWNIBL: VIEW ONLY LO NUMERAL */
void sw_vwnibl(sw_machine *m, uint8_t a)
{
    numeral(m, (uint8_t)(a << 1) & 0x1e);
}

/* DOMESS: PUT THE MESSAGE ON THE SCREEN. THIS ROUTINE MUST BE CALLED EVERY
 * FRAME FOR EVERY MESSAGE OUTPUT IN THIS WAY. */
void sw_domess(sw_machine *m, uint8_t b)
{
    if (b < SW_L_MESSZ) {
        W8(SW_MESNUM, b);
        TAG(SW_TAG_MESS, b);
        mcolr(m);                           /* LOOKUP AND OUTPUT COLOR INSTRUCTION TO VG */
        mout(m);                            /* POSITION AND OUTPUT CHARACTERS */
    }
}

void sw_domes2(sw_machine *m, uint8_t b)
{
    if (b < SW_L_MESSZ) {
        W8(SW_MESNUM, b);
        TAG(SW_TAG_MESS, b);
        mout(m);
    }
}

static void mcolr(sw_machine *m)
{
    VG(R16(SW_L_TMESCL + 2 * R8(SW_MESNUM)));
}

static void moutxt(sw_machine *m, uint16_t d)
{
    VG(d);
    m->n_mess++;
    sw_mesout(m);                           /* NOW PUT OUT THE CHARACTERS */
    VG(SW_VGSCAL);                          /* SET TO NORMAL SCALE */
    VG(SW_VGCNTR);                          /* CENTER AFTER MESSAGE */
}

static void mout(sw_machine *m)
{
    uint16_t x = (uint16_t)(SW_L_TMESXY + 4 * R8(SW_MESNUM));
    VG(R16(x + 2));                         /* DELTA Y FROM CENTER */
    moutxt(m, R16(x));                      /* DELTA X FROM CENTER */
}

/* MOUT2: USES Y OFFSET FOR SCROLLING */
static void mout2(sw_machine *m)
{
    uint16_t x = (uint16_t)(SW_L_TMESXY + 4 * R8(SW_MESNUM));
    VG((R16(x + 2) - R16(SW_MSYOFF)) & 0x1fff);
    moutxt(m, R16(x));
}

/* MESOUT: OUTPUT MESSAGE TO VGRAM (VIA Y). The text ends with the
 * character that has bit 7 set; each character becomes a JSRL to its
 * shape in the vector ROM. */
void sw_mesout(sw_machine *m)
{
    uint16_t x = R16(SW_L_TMESS + 2 * R8(SW_MESNUM));
    uint8_t ch;
    do {
        ch = R8(x++);
        m->n_chars++;
        W16(SW_MESPNT, x);                  /* SAVE UPDATED POINTER */
        uint8_t b = (uint8_t)(ch << 1);     /* *2 BECAUSE VGMSGA IS WORD TABLE */
        uint16_t t;
        if (b >= 0x82) {                    /* ALPHA */
            t = SW_VGMSGA + 0x16 - 2;
            b &= 0x7f;
        } else if (b == 0x74) {             /* ":" */
            t = SW_VGMSGA + 0x50 - 0x74;
        } else if (b == 0x80) {             /* "@" FOR CIRCLE C */
            t = SW_VGMSGA + 0x56;
            b = 0;
        } else if (b >= 0x60) {             /* MUST BE NUMERIC */
            t = SW_VGMSGA + 2 - 0x60;
        } else if (b == 0x40) {             /* SPACE */
            t = SW_VGMSGA - 0x40;
        } else if (b == 0x4e) {             /* APOSTROPHE */
            t = SW_VGMSGA + 0x4a - 0x4e;
        } else if (b == 0x58) {             /* COMMA */
            t = SW_VGMSGA + 0x4c - 0x58;
        } else if (b == 0x5a) {             /* HYPHEN */
            t = SW_VGMSGA + 0x58 - 0x5a;
        } else if (b == 0x4a) {             /* "%" FOR 1/2 */
            t = SW_VGMSGA + 0x54 - 0x4a;
        } else {                            /* MUST BE PERIOD */
            t = SW_VGMSGA + 0x4e - 0x5c;
        }
        VG(R16(t + b));                     /* JSRL TO APPROPRIATE LETTER ROUTINE */
    } while (!(ch & 0x80));
}
