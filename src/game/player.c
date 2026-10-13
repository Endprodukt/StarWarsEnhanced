/* player.c - WSMAIN.MAC, "MOVE THE PLAYER": turning the player's ship.
 *
 * In space the player does not steer the ship: it turns by itself toward
 * the enemy being tracked (AIM), or toward the Death Star, and the yoke
 * only moves the sight. The turn is kept as a "rate" per axis (RH$RAT),
 * summed into a residue of small tics (RH$RSD); whole steps of 0.895 or
 * 4.99 degrees are taken out of the residue and applied to the ship's unit
 * vectors by the math box (RHRSDU), the rest only to the copy that is
 * shown (RESIDU): so the picture turns finer than the simulation.
 *
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

#define TRHTIC 0x71DA       /* TABLE OF TIC SIN AND COS (local: RESCNV's LDX), 3 bytes a tic */

static void aimdth(sw_machine *m);

/* RHTRIG: the rate of turn into the residue. X-->RAM BLOCK */
static void rhtrig(sw_machine *m, uint16_t x)
{
    uint8_t a = R8(x + RH_RAT);
    if (a & 0x80) a = (uint8_t)~a;          /* WIDER BALANCED ZERO, ALLOW -80 */
    a = (uint8_t)((a * 0x80) >> 8);         /* FROM +-128. TO ? */
    int r;
    if (!(R8(x + RH_RAT) & 0x80)) {
        r = (int8_t)a + (int8_t)R8(x + RH_RSD);
        if (r > 127 || r < -128) r = 127;
    } else {                                /* GO IN NEGATIVE DIRECTION */
        r = (int8_t)(uint8_t)-a + (int8_t)R8(x + RH_RSD);
        if (r > 127 || r < -128) r = -127;
    }
    W8(x + RH_RSD, (uint8_t)r);             /* SIGNED VALUE */
}

/* RHRSDU: a whole step out of the residue, as M.SIN and M.COS. Returns 0
 * if there is nothing to rotate. */
static int rhrsdu(sw_machine *m, uint16_t x)
{
    uint8_t a = R8(x + RH_RSD);
    if (a & 0x80) a = (uint8_t)-a;          /* ABSOLUTE VALUE */
    if (a >= 78) {                          /* ?PASSED THE LARGE MAGIC ANGLE? */
        W16(SW_M_COS, 0x3fc2);              /* ROTATE 4.99 LARGE MAGIC ANGLE DEGREES */
        a = R8(x + RH_RSD);
        if (a & 0x80) {
            W8(x + RH_RSD, a + 78);         /* NEGATIVE ROTATE */
            W16(SW_M_SIN, (uint16_t)-0x590);
        } else {
            W8(x + RH_RSD, a - 78);         /* POSITIVE ROTATE: CORRECT THE RESIDUE */
            W16(SW_M_SIN, 0x590);
        }
        return 1;
    }
    if (a >= 14) {                          /* ?BEYOND .895 MAGIC ANGLE? */
        W16(SW_M_COS, 0x3ffe);
        a = R8(x + RH_RSD);                 /* THEN STEP TOWARDS IT */
        if (a & 0x80) {
            W8(x + RH_RSD, a + 14);
            W16(SW_M_SIN, (uint16_t)-0x100);
        } else {
            W8(x + RH_RSD, a - 14);
            W16(SW_M_SIN, 0x100);
        }
        return 1;
    }
    return 0;                               /* NO ROTATE */
}

/* RESCNV: CONVERT A=RESIDUE TO SIN,COS */
static void rescnv(sw_machine *m, uint8_t a)
{
    if (a & 0x80) a = (uint8_t)-a;
    uint16_t x = (uint16_t)(TRHTIC + (uint8_t)(a * 3));
    W16(SW_M_SIN, R16(x));                  /* SIN AS WORD ENTRY */
    W16(SW_M_COS, (uint16_t)((int8_t)R8(x + 2) + 0x4000));  /* ONE BYTE COS OFFSET FROM 1 */
}

/* One axis of RESIDU. */
static void residue_axis(sw_machine *m, uint16_t blk, uint8_t program)
{
    uint8_t a = R8(blk + RH_RSD);           /* TICS LEFT */
    if (a == 0) return;
    rescnv(m, a);                           /* CONVERT RESIDUE TO SIN/COS */
    if (R8(blk + RH_RSD) & 0x80)            /* ?TURNING BACKWARDS? THEN INVERT SINE */
        W16(SW_M_SIN, 0 - R16(SW_M_SIN));
    W16(SW_M_BIC, SW_MSU1BC);               /* THE DOWNLOADED UNIT VECTORS */
    sw_mgowt(m, program);
}

/* RESIDU: HANDLE RESIDUE ROTATIONS UPON DOWNLOADED UNIT VECTORS */
void sw_residu(sw_machine *m)
{
    residue_axis(m, SW_RHEOP, SW_MSPTCH);
    residue_axis(m, SW_RHEOY, SW_MSYAW);
    residue_axis(m, SW_RHEOR, SW_MSROLL);
}

/* S1RHPT, S1RHYW: SHIP 1 RHEOSTAT PITCH, YAW */
static void s1rhpt(sw_machine *m)
{
    rhtrig(m, SW_RHEOP);                    /* CONVERT TO TRIG VALUES */
    if (rhrsdu(m, SW_RHEOP)) sw_s1pt(m);    /* PITCH APPROPRIATELY */
}

static void s1rhyw(sw_machine *m)
{
    rhtrig(m, SW_RHEOY);
    if (rhrsdu(m, SW_RHEOY)) sw_s1yw(m);    /* YAW (N) APPROPRIATELY */
}

/* A 16-bit shift left as the 6809 does it in two steps: *v doubled;
 * returns the overflow flag (the sign changed), *carry the bit shifted out. */
static int shl16(uint16_t *v, int *carry)
{
    int overflow = ((*v >> 15) ^ (*v >> 14)) & 1;
    *carry = *v >> 15;
    *v = (uint16_t)(*v << 1);
    return overflow;
}

/* AIMA: AIM AT ALIEN. Where the alien is, seen from the ship, scaled up as
 * far as it goes, becomes the rates of turn. */
static void aima(sw_machine *m)
{
    W8(SW_M_BICL, SW_MSS1BC);               /* IS1UV: DOWNLOAD LATEST UNIT VECTOR */
    W8(SW_M_BICH, 0);
    sw_mgowt(m, SW_MSDLD1);
    sw_residu(m);                           /* AND HANDLE RESIDU */
    uint16_t x = R16(SW_S2_PRM);            /* GET THE ALIEN TO AIM AT */
    W16(SW_AM_PTR, x);                      /* SAVE OUR CURRENT ALIEN */
    W16(SW_M_BIC, (uint8_t)(R8(x + A_BIC) + 3));
    sw_mgowt(m, SW_MSPSB2);                 /* GET ALIENS LATEST LOC VIA PLAYER */

    uint16_t d = R16(SW_M_YP), v;
    int c;
    if ((int8_t)R8(SW_M_XP) > 0) {          /* ?ALIEN IN FRONT? THEN SCALE USING X */
        for (;;) {
            v = R16(SW_M_XP);
            int over = shl16(&v, &c);
            W16(SW_M_XP, v);
            if (over) goto done;            /* WHEN X OVERFLOWS */
            if (shl16(&d, &c)) {            /* NOW DO Y: ?Y AT MAX SCALE? */
                d = (uint16_t)((c << 15) | (d >> 1));   /* THEN RESTORE SIGN */
                goto done;
            }
            v = R16(SW_M_ZP);
            over = shl16(&v, &c);
            W16(SW_M_ZP, v);
            if (over) break;
        }
    } else {                                /* ELSE ALIEN IS BEHIND -- USE LARGEST SCALE */
        d |= 1;                             /* FORCE END TO LOOP */
        for (;;) {
            if (shl16(&d, &c)) {
                d = (uint16_t)((c << 15) | (d >> 1));
                goto done;
            }
            v = R16(SW_M_ZP);
            int over = shl16(&v, &c);
            W16(SW_M_ZP, v);
            if (over) break;
        }
    }
    W16(SW_M_ZP, (uint16_t)((c << 15) | (R16(SW_M_ZP) >> 1)));  /* RESTORE CORRECT SIGN */
done:
    W8(SW_RHEOY + RH_RAT, (uint8_t)~(d >> 8));      /* LEFT/RIGHT YAW */
    W8(SW_RHEOP + RH_RAT, R8(SW_M_ZP));             /* UP/DOWN PITCH */
}

/* AIM: AIM AT ALIEN OR DEATH STAR */
static void aim(sw_machine *m)
{
    uint16_t x = R16(SW_AM_PTR);
    if (x == 0) x = SW_ALIEN;               /* CYCLE THRU STARTING AT CURRENT POSITION */
    do {                                    /* LOOP THRU ALL ALIENS */
        if (R8(x + A_TYP) == 1 && R8(x + A_GLW) == 0) {     /* ?FOUND OUR FIRST LIVE ALIEN? ?AND NOT GLOWING FROM A HIT? */
            W16(SW_S2_PRM, x);              /* SAVE ALIEN POINTER */
            aima(m);
            return;
        }
        if ((int8_t)R8(SW_Q_SHK) > 0)       /* ?STILL WAITING FOR GOOD OPPONENT? */
            W8(SW_Q_SHK, 9);                /* # OF SHOTS BEFORE 'I CANT SHAKE HIM' */
        x += A_IZE;                         /* GET NEW ALIEN TO AIM AT */
    } while (x < SW_ALIENZ);
    aimdth(m);
}

/* AIMDTH: WHEN NO ALIENS TO LOOK AT, USE DETH STAR */
static void aimdth(sw_machine *m)
{
    W16(SW_AM_PTR, 0);                      /* NO CURRENT POINTER TO ALIEN BEING TRACKED */
    uint8_t b;
    if (!(R8(SW_M_S1 + M_AX) & 0x80)) b = R8(SW_M_S1 + M_AZ);      /* ?DETH STAR IN FRONT? THEN SLEW NORMALLY(USE HITE OF DETHSTAR) */
    else b = (uint8_t)(0x7f - R8(SW_M_S1 + M_AZ));  /* STRAIGHT IN BACK GIVES LARGE VALUE */
    W8(SW_RHEOP + RH_RAT, b);

    if (!(R8(SW_M_S1 + M_AX) & 0x80)) b = R8(SW_M_S1 + M_AY);      /* (USE WIDTH OF DETHSTAR) */
    else b = (uint8_t)(0x7f - R8(SW_M_S1 + M_AY));
    W8(SW_RHEOY + RH_RAT, (uint8_t)~b);
}

/* ROLL IF HIT */
static void roll_if_hit(sw_machine *m)
{
    int8_t a = (int8_t)R8(SW_S_ROL);
    if (a == 0) return;                     /* ?ANY ROLLING TO DO? */
    if (a > 0) {
        W8(SW_S_ROL, a - 1);
        W16(SW_M_SIN, 0x04ff);
    } else {
        W8(SW_S_ROL, a + 1);
        W16(SW_M_SIN, (uint16_t)-0x04ff);
    }
    W16(SW_M_COS, 0x3fce);                  /* 4.48 DEGREES */
    sw_s1rl(m);
}

/* STWSP1: TWIRL PLAYER */
void sw_stwsp1(sw_machine *m)
{
    roll_if_hit(m);
    aim(m);                                 /* AIM AT ALIEN OR DETH STAR */
    s1rhpt(m);
    s1rhyw(m);
}

/* STWSP2: FOR THE LAST OF A GROUP OF THREE ALIENS */
void sw_stwsp2(sw_machine *m)
{
    roll_if_hit(m);
    aimdth(m);
    s1rhpt(m);
    s1rhyw(m);
}

/* SMVSP1, SMVSP2, SMVNXT, S1MV: PLAYER 1 MOVE */
void sw_smvsp1(sw_machine *m)
{
    W16(SW_ST_UX, R16(SW_FRAME) << 7);      /* STARS RELATIVE MOVEMENT */
}
