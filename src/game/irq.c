/* irq.c - WSINT.MAC: the hardware interrupt, 246 times a second, and RESET.
 *
 * Line by line after the 1983 source; its comments are kept in capitals.
 * What only guarded the hardware (PC and RAM traps, the watchdog, the sound
 * board check once a second, the VG timeout) has nothing to guard here and
 * is left out, as noted where it was. */
#include "game/game.h"

#include <string.h>

/* RHEOSTAT block offsets (RH$xx) */
#define RH_LO  0      /* LO VALUE-MINIMUM */
#define RH_LT  1      /* LO VALUE ON TRIAL */
#define RH_SP  2      /* SPREAD MULTIPLIER */
#define RH_ST  3      /* SPREAD CHANGE TIMER */
#define RH_NML 4      /* NORMALIZED POT VALUE */
#define RH_POS 5      /* SITE POSITION (16 bits) */

static void debnce(sw_machine *m, uint16_t x, uint8_t a);
static void dopots(sw_machine *m);
static void rhctrl(sw_machine *m);
static void gnsite(sw_machine *m);
static void dovg(sw_machine *m);

/* 6809 BCD add of one byte (ADDA/ADCA then DAA, for valid BCD). */
static uint8_t bcd_add(uint8_t a, uint8_t b, int *carry)
{
    unsigned lo = (a & 0x0f) + (b & 0x0f) + (unsigned)*carry;
    unsigned hi = (a >> 4) + (b >> 4);
    if (lo > 9) { lo -= 10; hi++; }
    *carry = hi > 9;
    if (hi > 9) hi -= 10;
    return (uint8_t)((hi << 4) | lo);
}

static void irq(sw_machine *m);

void sw_irq(sw_machine *m)
{
    m->in_irq = 1;
    m->irqs++;
    m->vg_left = m->vg_left > SW_IRQ_CYCLES ? m->vg_left - SW_IRQ_CYCLES : 0;   /* the VG drew meanwhile */
    irq(m);
    m->in_irq = 0;
}

static void irq(sw_machine *m)
{
    /* (PC validation, RAM trap $GARBG and the watchdog: hardware guards.) */

    /* ?TIME FOR NEXT GAME FRAME UPDATE? */
    uint8_t *gmtimr = sw_ram(m, SW_GMTIMR);
    if ((int8_t)--(*gmtimr) < 0) {
        *gmtimr = 11;                 /* 12.*4.2MS==>50. MS, 20 PER SECOND */
        RAM8(m, SW_GMSYNC)++;
    }

    RAM8(m, SW_SINTCT)++;             /* INCREMENT THE INTERRUPT COUNTER */

    sw_coins(m);
    /* (CREDITS >= BADCRD: "GAMING IS ILLEGAL", a software break. COINS
     * already caps them at MAXCRD.) */

    /* SWITCHES */
    debnce(m, SW_IN_NW0, sw_rd8(m, SW_R_BYT0));
    debnce(m, SW_IN_NW1, sw_rd8(m, SW_R_BYT1));
    debnce(m, SW_IN_NW2, sw_rd8(m, SW_R_BYT2));
    debnce(m, SW_IN_NW3, sw_rd8(m, SW_R_BYT3));

    /* Fire and thumb buttons in one byte; falling edges (went on) light the
     * lasers. */
    RAM8(m, SW_VG_LSO) = RAM8(m, SW_VG_LSN);           /* PREVIOUS NEW VALUE IS CURRENT OLD VALUE */
    uint8_t a = (uint8_t)((RAM8(m, SW_R_FIRE) & (uint8_t)~SW_THMBT) | (RAM8(m, SW_R_THMB) & SW_THMBT));
    RAM8(m, SW_VG_LSN) = a;
    RAM8(m, SW_VG_LSE) = (uint8_t)((a ^ RAM8(m, SW_VG_LSO)) & RAM8(m, SW_VG_LSO));   /* 1==>FALLING EDGE */
    a = RAM8(m, SW_VG_LSE) & (SW_FIREBT | SW_THMBT);
    if (a) RAM8(m, SW_VG_LON) |= a;                    /* THEN TURN ON LAZARS */

    dopots(m);

    /* (ONCE A SECOND: CHECK ON SOUND BOARD. The sound is native.) */

    if ((RAM8(m, SW_SINTCT) & 3) == 0) {               /* EVERY 16 MILS */
        uint8_t *gtime = sw_ram(m, SW_GTIME);
        uint8_t t = (uint8_t)(gtime[2] + 1);
        if (t >= 250) t = 0;                           /* 4 SECONDS? */
        gtime[2] = t;
        if (t == 0) {                                  /* 4 SECONDS, DO OTHER TIMERS */
            int c = 0;
            gtime[1] = bcd_add(gtime[1], 1, &c);
            uint8_t hi = bcd_add(gtime[0], 0, &c);
            if (!c) gtime[0] = hi;                     /* DON'T OVERFLOW TIME, MORE THAN 11 HOURS */
        }
        /* INC TOTAL TIME ON COUNT */
        uint8_t *timer = sw_ram(m, SW_TIMER);
        int c = 0;
        timer[3] = bcd_add(timer[3], 1, &c);
        timer[2] = bcd_add(timer[2], 0, &c);
        timer[1] = bcd_add(timer[1], 0, &c);
        timer[0] = bcd_add(timer[0], 0, &c);
    }

    dovg(m);
    /* STA IRCLR: ALLOW NEXT IRQ */
}

/* DEBNCE: X --> NW, OL, SW. A switch is debounced on when two samples in a
 * row are on, off when two in a row are off. */
static void debnce(sw_machine *m, uint16_t x, uint8_t a)
{
    uint8_t *nw = sw_ram(m, x), *ol = nw + 1, *sw = nw + 2;
    uint8_t b = *nw;
    *ol = b;                         /* PREVIOUS NEW --> OLD */
    *nw = a;                         /* SAVE NEWEST */
    a &= *ol;                        /* SAME BITS ON */
    a |= *sw;
    *sw = a;                         /* SAVE DEBOUNCED ON BITS */
    b |= *nw;                        /* SAME BITS OFF */
    b &= *sw;
    *sw = b;                         /* SAVE DEBOUNCED OFF BITS */
}

/* DOPOTS: one pot per IRQ, alternating. "Pokey filter": the saved value
 * only moves when two samples in a row are both above (or both below) it,
 * and then to the closer of the two. */
static void dopots(sw_machine *m)
{
    uint16_t y = (RAM8(m, SW_SINTCT) & 1) ? SW_PT_VL : (uint16_t)(SW_PT_VL + SW_PTSIZ);
    uint8_t *vl = sw_ram(m, y), *vnew = vl + 1;
    uint8_t b = *vnew;               /* PREVIOUS NEW BECOMES CURRENT OLD */
    uint8_t a = sw_rd8(m, SW_R_CHN);
    *vnew = a;                       /* UPDATE CURRENT NEW */
    if (a >= *vl) {                  /* ?NEW VALUE HIGHER THAN STANDARD VALUE? */
        if (b > *vl) {               /* ?AND OLD VALUE HIGHER ALSO? */
            if (b > *vnew) b = *vnew;    /* ?VNEW IS CLOSER? */
            *vl = b;                 /* SAVE CLOSEST OF HIGHER VALUES */
        }
    } else {                         /* NEW VALUE IS LOWER THAN STANDARD VALUE */
        if (b < *vl) {               /* ?OLD VALUE ALSO LOWER? */
            if (b < *vnew) b = *vnew;
            *vl = b;                 /* SAVE CLOSEST OF LOWER VALUES */
        }
    }
    /* START NEXT A-D CONVERSION */
    if (y == SW_PT_VL) sw_wr8(m, SW_W_CHN0, a);
    else sw_wr8(m, SW_W_CHN1, a);
}

/* RHLIM: X = block, A = pot. Learns the pot's low end and its spread, so
 * any yoke reaches full scale; returns the value -7F..7F in RH$NML. */
static void rhlim(sw_machine *m, uint16_t blk, uint8_t a)
{
    uint8_t *x = sw_ram(m, blk);
    if (a < x[RH_LO] && a >= x[RH_LT])  /* ?LESS THAN LO? ?HIGHER THAN TRIAL LO? */
        x[RH_LO]--;                     /* APPROACH NEW DEGLITCHED LO */
    x[RH_LT] = a;                       /* SAVE OUR NEW TRIAL LO */
    if (a < x[RH_LO]) a = x[RH_LO];     /* REPLACE WITH LOWER LIMIT */
    a = (uint8_t)(a - x[RH_LO]);        /* BASE AT ZERO */
    uint8_t dir = a;                    /* RH.DIR: TEMP */
    RAM8(m, SW_RH_DIR) = dir;
    unsigned prod = (unsigned)a * x[RH_SP];   /* SPREAD THE VALUES */
    unsigned sum = (prod >> 8) + dir;   /* MULTS BY 1XX SO VALUE GOES ABOVE 7F */
    a = (uint8_t)sum;
    int carry = sum > 0xff;
    if (a == 0) a = 1;                  /* RANGE IS -7F TO 7F (ALLOWS PROPER NEGATE) */
    if (!carry) {
        x[RH_ST] = 0;                   /* PREPARE TIMER FOR NEXT TOO-HIGH POT VALUE */
    } else {
        a = 0xff;                       /* THEN LIMIT VALUE */
        x[RH_ST]++;                     /* TIME THESE EXCESSES */
        if (x[RH_ST] >= 2) x[RH_SP]--;  /* TOO LARGE FOR TOO LONG: DECREASE SPREAD */
    }
    x[RH_NML] = (uint8_t)(a - 0x80);    /* CHANGE TO SIGNED VALUE */
}

/* RHPOS: slew the screen position toward the pot, faster when far. */
static void rhpos(sw_machine *m, uint16_t blk)
{
    uint8_t *x = sw_ram(m, blk);
    uint8_t dir = 0;                    /* START WITH POSITIVE DIRECTION */
    uint16_t pos = (uint16_t)((x[RH_POS] << 8) | x[RH_POS + 1]);
    uint16_t d0 = (uint16_t)((x[RH_NML] << 8) | 0x80);    /* HALF WAY INTO POTS STAMP */
    uint16_t d = (uint16_t)(d0 - pos);  /* SUBD RH$POS */
    /* SUBD flags */
    int n = (d & 0x8000) != 0;
    int v = ((d0 ^ pos) & (d0 ^ d) & 0x8000) != 0;
    int z = d == 0;
    uint8_t a = (uint8_t)(d >> 8), b = (uint8_t)d;
    if (n != v) {                       /* ?WANT TO GO IN NEGATIVE DIRECTION? */
        uint8_t old = a;
        a = (uint8_t)-a;                /* NEGA: ONLY THE HIGH BYTE */
        dir = 0xff;
        n = (a & 0x80) != 0;
        v = old == 0x80;
        z = a == 0;
    }
    uint16_t dd = (uint16_t)((a << 8) | b);
    if (!z && n == v)                   /* ?SLIGHTLY POSITIVE? */
        dd = (uint16_t)(dd + 0x00ff);   /* TRUNCATE UPWARD -- ALWAYS SLEW SOME */
    a = (uint8_t)(dd >> 8);
    if (a > 0xf8) a = 0xf8;             /* PREVENT OVERFLOWS DURING LATER TRUNCATIONS */
    uint8_t mul = 0x60;                 /* HOW QUICKLY TO SKEW TO DESIRED LOCATION */
    if (a < 0x40) mul = 0x30;           /* ONE-QUARTER SCREEN: SLOWER WHEN CLOSER */
    uint16_t step = (uint16_t)(a * mul);
    if (dir) step = (uint16_t)-step;    /* RESTORE ORIGINAL DIRECTION */
    pos = (uint16_t)(pos + step);       /* NOW UPDATE OUR CURSOR POSITION */
    x[RH_POS] = (uint8_t)(pos >> 8);
    x[RH_POS + 1] = (uint8_t)pos;
    RAM8(m, SW_RH_DIR) = dir;
}

/* RHCTRL: IRQ VG HANDLES CONTROLS */
static void rhctrl(sw_machine *m)
{
    uint8_t *p = sw_ram(m, SW_RHEOP);   /* DO PITCH POT: UP/DOWN VG Y */
    rhlim(m, SW_RHEOP, RAM8(m, SW_PT_VL2));
    int8_t a = (int8_t)p[RH_NML];       /* NOW LIMIT THE POT BY THE VG SCREEN */
    if (a > (int8_t)SW_VGCURT) a = (int8_t)SW_VGCURT;
    if (a < (int8_t)(uint8_t)SW_VGCURB) a = (int8_t)(uint8_t)SW_VGCURB;
    p[RH_NML] = (uint8_t)a;
    rhpos(m, SW_RHEOP);

    uint8_t *y = sw_ram(m, SW_RHEOY);   /* DO YAW POT: LEFT/RIGHT VG X */
    rhlim(m, SW_RHEOY, RAM8(m, SW_PT_VL1));
    a = (int8_t)y[RH_NML];              /* NOW LIMIT TO CURSOR SCREEN SIZE */
    if (a > (int8_t)SW_VGCURR) a = (int8_t)SW_VGCURR;
    if (a < (int8_t)(uint8_t)SW_VGCURL) a = (int8_t)(uint8_t)SW_VGCURL;
    y[RH_NML] = (uint8_t)a;
    rhpos(m, SW_RHEOY);
}

/* One of GNSITE's coordinates: the 16-bit position (+-128 in its high byte)
 * becomes +-512 in the form of a vector word, built with the rotates of the
 * original ("REVERSE LDD": A = low byte, B = high byte). */
static uint16_t site_coord(uint8_t hi, uint8_t lo)
{
    uint8_t b = hi, a = lo & 0xc0;      /* ONLY NEED TOP TWO BITS */
    int c = (a >> 7) & 1;               /* LSLA */
    a = (uint8_t)(a << 1);
    int c2 = (b >> 7) & 1;              /* ROLB: FROM +-128 TO +-256 */
    b = (uint8_t)((b << 1) | c);
    c = c2;
    if (c) a |= 0x3f;                   /* ?NEGATIVE? AFTER TWO SHIFTS IS SEX'D */
    c2 = (a >> 7) & 1;                  /* ROLA */
    a = (uint8_t)((a << 1) | c);
    c = c2;
    c2 = (b >> 7) & 1;                  /* ROLB: UP TO +-512 */
    b = (uint8_t)((b << 1) | c);
    c = c2;
    a = (uint8_t)((a << 1) | c);        /* ROLA: NOW OF FORM 1FFF, VECTOR OPCODE */
    return (uint16_t)((a << 8) | b);
}

/* GNSITE: IRQ,VG GENERATE SITE COORDS ON SCREEN */
static void gnsite(sw_machine *m)
{
    uint16_t d = site_coord(RAM8(m, SW_VG_RSY), RAM8(m, SW_VG_RSY + 1));
    SETRAM16(m, SW_VG_CY, d);           /* CURRENT SITE Y POS */
    d = (uint16_t)(d + SW_VGOFFY);      /* OFFSET TO CENTER */
    sw_vg_wr16(m, SW_VRSITE + 0, (uint16_t)(d & 0x1fff));   /* VECTOR OPCODE */

    d = site_coord(RAM8(m, SW_VG_RSX), RAM8(m, SW_VG_RSX + 1));   /* YAW CURSOR IN X POSITION */
    SETRAM16(m, SW_VG_CX, d);           /* CURRENT SITE LEFT/RIGHT */
    sw_vg_wr16(m, SW_VRSITE + 2, (uint16_t)(d & 0x1fff));   /* KEEP BRITENESS ZERO */

    sw_vg_wr16(m, SW_VRSITE + 4, 0xc000);   /* RTSL */
}

/* Step a 2-byte index through a ROM table of `size` bytes. */
static uint8_t step_index(uint8_t b, uint8_t size)
{
    b = (uint8_t)(b + 2);
    return b >= size ? 0 : b;
}

/* DOVG: HANDLE VG STUFF. The next picture starts on the 6th interrupt
 * after the last start, or as soon after that as the VG has halted. */
static void dovg(sw_machine *m)
{
    uint8_t *vgtimr = sw_ram(m, SW_VGTIMR);
    uint8_t before = (*vgtimr)--;
    if (before == 0x80) {               /* ?VGEN TIMED OUT? */
        sw_wr8(m, SW_VGSTOP, 0);        /* THEN KILL IT */
        sw_downvg(m);                   /* RELOAD PSUEDO ROM ( IE PROGRAM ROM TO VG RAM ) */
        return;
    }
    if (!(*vgtimr & 0x80)) return;      /* ?START NEXT VGEN FIELD? */
    if (!(sw_rd8(m, SW_VGHALT) & 0x40)) return;    /* ?VGEN HALTED (READY TO START)? */

    uint8_t *vgsync = sw_ram(m, SW_VGSYNC);
    if (*vgsync & 0x80) {               /* ?SWITCH FRAME BUFFERS? (MAIN LINE IS DONE) */
        /* Hand the main line the buffer on display, show the other one. */
        uint8_t a = m->vgram[0] & (SW_VGBFSZ / 0x200);    /* HIGH BYTE IN VG WORDS */
        if (a) a = SW_VGBFSZ / 0x100;                     /* HIGH BYTE ADDRESS IN BYTES */
        a |= (SW_VGRAM / 0x100) & 0xff;
        *vgsync = a;
        a = (uint8_t)(a >> 1);                            /* ALSO SWITCH FIELD BUFFERS */
        a &= SW_VGBFSZ / 0x200;
        a ^= SW_VGBFSZ / 0x200;
        a |= 0xe0;                                        /* INCLUDE THE JMP */
        sw_vg_wr16(m, 0x0000, (uint16_t)((a << 8) | (SW_VGVARB / 2)));
    }

    uint8_t *ptdly = sw_ram(m, SW_PT_DLY);
    if ((int8_t)--(*ptdly) < 0) {       /* ON STARTUP, DONT USE UNSEASONED POT FILTER */
        *ptdly = 0;
        rhctrl(m);                      /* DO CONTROLS AT IRQ VG TIME */
    }
    gnsite(m);                          /* AND DISPLAY ITS COORDS */

    /* LAZAR FIRE: rotate the 16-word laser table into VG RAM, a new start
     * each restart, so the bolts travel. */
    uint8_t b = step_index(RAM8(m, SW_VG_LZF), (uint8_t)(SW_VRLZFZ - SW_VRLZF));
    RAM8(m, SW_VG_LZF) = b;
    uint16_t x = (uint16_t)(SW_VRLZF + 2 + b);
    uint16_t u = SW_VTLZF;
    do {                                /* LOOP TO FRONT OF RAM */
        x -= 2;
        sw_vg_wr16(m, x, sw_rom16(m, u));
        u += 2;
    } while ((int16_t)x > (int16_t)SW_VRLZF);
    x = SW_VRLZFZ;                      /* PUT REST OF ROM AT END */
    while (u < SW_VTLZFZ) {
        x -= 2;
        sw_vg_wr16(m, x, sw_rom16(m, u));   /* VIEW RAM HAS HEAD FAR AWAY, TAIL NEAR */
        u += 2;
    }

    /* LAZAR SPLASH: ONE PICTURE ONLY */
    b = step_index(RAM8(m, SW_VG_LZS), (uint8_t)(SW_VTLZSZ - SW_VTLZS));
    RAM8(m, SW_VG_LZS) = b;
    sw_vg_wr16(m, SW_VRLZS, sw_rom16(m, (uint16_t)(SW_VTLZS + b)));

    /* ALIEN GUN SHOT, BASE SPARKLE: EVERY FOURTH SCREEN, CHANGE PICTURE TYPE */
    uint8_t *vf = sw_ram(m, SW_VF_GNB);
    if ((int8_t)--(*vf) <= 0) {
        *vf = 4;
        b = step_index(RAM8(m, SW_VG_GNB), (uint8_t)(SW_VTGNBZ - SW_VTGNB));
        RAM8(m, SW_VG_GNB) = b;
        sw_vg_wr16(m, SW_VRGNB, sw_rom16(m, (uint16_t)(SW_VTGNB + b)));
        sw_vg_wr16(m, SW_VRGNX, sw_rom16(m, (uint16_t)(SW_VTGNX + b)));   /* GUN EXPLODE SAME TIME */
    }

    /* ALIEN GUN SHOT, TIP FUSE: EVERY SCREEN */
    vf = sw_ram(m, SW_VF_GNT);
    if ((int8_t)--(*vf) <= 0) {
        *vf = 1;
        b = step_index(RAM8(m, SW_VG_GNT), (uint8_t)(SW_VTGNTZ - SW_VTGNT));
        RAM8(m, SW_VG_GNT) = b;
        sw_vg_wr16(m, SW_VRGNT, sw_rom16(m, (uint16_t)(SW_VTGNT + b)));
    }

    /* BRITE FLASH */
    b = step_index(RAM8(m, SW_VG_FLS), (uint8_t)(SW_VTFLSZ - SW_VTFLS));
    RAM8(m, SW_VG_FLS) = b;
    sw_vg_wr16(m, SW_VRFLS, sw_rom16(m, (uint16_t)(SW_VTFLS + b)));
    sw_vg_wr16(m, SW_VRFLS + 2, SW_VGRTSL);   /* AND RETURN VG */

    /* MEDIUM FLASH */
    b = step_index(RAM8(m, SW_VG_MFL), (uint8_t)(SW_VTMFLZ - SW_VTMFL));
    RAM8(m, SW_VG_MFL) = b;
    sw_vg_wr16(m, SW_VRMFL, sw_rom16(m, (uint16_t)(SW_VTMFL + b)));
    sw_vg_wr16(m, SW_VRMFL + 2, SW_VGRTSL);

    sw_splash(m);                       /* OSCILLATE LAZAR SPLASH PICS */

    sw_wr8(m, SW_VGGO, 0);              /* START VGEN AGAIN */
    *vgtimr = 5;
}

void sw_reset(sw_machine *m)
{
    /* CLEAR RAM, MATH RAM, VECTOR RAM */
    memset(m->ram, 0, sizeof m->ram);
    memset(m->mbox.ram, 0, sizeof m->mbox.ram);
    memset(m->vgram, 0, sizeof m->vgram);
    memset(m->latch, 0, sizeof m->latch);      /* W.BANK: NORMAL ROOT ROM */

    /* (Self test when the switch is on: not ported yet.) */

    /* SETUP FOR THE FIRST DEBOUNCE OF THE OPTION SWITCHES */
    RAM8(m, SW_R_OPT2) = sw_rd8(m, SW_R_BYT2);
    RAM8(m, SW_R_OPT1) = sw_rd8(m, SW_R_BYT3);

    /* SETUP VG WITH ALL HALT INSTRUCTIONS (JUST IN CASE ...) */
    for (uint16_t a = 0; a < SW_VGROM; a += 2) sw_vg_wr16(m, a, 0x2020);
    sw_vg_wr16(m, SW_VGRAM, (uint16_t)((SW_VGVARB / 2) + 0xe000));   /* JUMP TO DOUBLE BUFFER */
    RAM8(m, SW_VGSYNC) = 0xff;                 /* SWITCH BUFFERS IMMEDIATELY */
    RAM8(m, SW_SGARBG) = 0x3f;                 /* TRAP OUT FIRST BYTE RAM */
    RAM8(m, SW_PT_DLY) = 0x40;                 /* DELAY RHEOSTATS UNTIL FILTER IS STABLE */
    sw_eeinit(m);                              /* INIT EEROM ( VERIFY/SET NOVRAM AREAS TO VALID STATES ) */
    m->main_state = SW_MAIN_RUN;               /* JMP MAIN: START THE MAINLINE */
    m->unported_phase = 0;
}

void sw_eeaccu(sw_machine *m)
{
    (void)m;   /* TCEROM.MAC: coin accounting in the EEROM, a later step */
}
