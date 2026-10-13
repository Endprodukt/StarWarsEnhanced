/* xpld.c - WSXPLD.MAC: the explosions of the others.
 *
 * A ship that is shot falls into three pieces (two wings and the globe), a
 * bunker or tower likewise. Each piece is an entry of a circular queue of
 * eight (XP$xx): a center, a speed ("munge") and a timer. The pieces are
 * drawn as objects through a matrix of their own (M.MUN) that keeps
 * rolling and pitching while any piece lives.
 *
 * Ported so far: all but the start of the bunker and tower explosions
 * (BGBKXP, BGTWXP), which follow with the ground. The pieces of bunkers and towers (MVBKn,
 * VWBKn, VWTWn) are ported but not yet checked against a recording.
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

#define XP_CX  0            /* EXPLOSION CENTER, X LOC */
#define XP_CY  2
#define XP_CZ  4
#define XP_MX  6            /* MUNGE FOR CENTER,X */
#define XP_MY  8
#define XP_MZ  10
#define XP_TYP 12           /* ANIMATION TYPE */
#define XP_TMR 13           /* ANIMATION TIMER */
#define XP_IZE 14
#define XP_EQ  8            /* MAX NUMBER OF EXPLOSION PIECES */

/* TMV, TVW: the types of pieces */
enum { TP_BK1, TP_BK2, TP_BK3, TP_TW1, TP_TW2, TP_TW3, TP_TI1, TP_TI2, TP_TI3, TP_COUNT };

#define TVWCLE 0xBB3B       /* VIEW EXPLODE: colors by the timer (local: VWTIN's LDU) */

/* ASRDn */
static uint16_t asrd(uint16_t d, int n)
{
    return (uint16_t)((int16_t)d >> n);
}

/* Would a shift left of this word overflow (ROL: V)? */
static int lsl_overflows(uint16_t d)
{
    return ((d ^ (d << 1)) & 0x8000) != 0;
}

/* NXTFRE: GET NEXT FREE EXPLOSION SLOT */
static uint16_t nxtfre(sw_machine *m)
{
    uint8_t b = (uint8_t)(R8(SW_XP_HDQ) + 1);   /* BUMP TO NEXT SLOT */
    if (b >= XP_EQ) b = 0;                  /* ?WRAPPED? THEN START AT FRONT OF CIRCULAR QUEUE AGAIN */
    W8(SW_XP_HDQ, b);
    return (uint16_t)(SW_XPQUE + XP_IZE * b);   /* X-->QUEUE ENTRY */
}

/* BGAM: HANDLE XP$M* */
static void bgam(sw_machine *m, uint16_t x, uint16_t u)
{
    W16(x + XP_MX, R16(u + M_VX) + R16(x + XP_MX));
    W16(x + XP_MY, R16(u + M_VY) + R16(x + XP_MY));
    W16(x + XP_MZ, R16(u + M_VZ) + R16(x + XP_MZ));
}

/* A wing: off the center by a part of the ship's sideways axis. */
static void wing(sw_machine *m, uint16_t u, uint8_t typ, int left)
{
    static const int8_t axis[3] = { M_AY, M_BY, M_CY };
    static const int8_t pos[3] = { M_TX, M_TY, M_TZ };
    uint16_t x = nxtfre(m);                 /* X-->EXPLOSION PARMS */
    W8(x + XP_TYP, typ);
    W8(x + XP_TMR, 0x18);
    for (int i = 0; i < 3; i++) {
        uint16_t d = R16(u + axis[i]);      /* OFFSET TO CENTER OF WING */
        if (left) d = (uint16_t)(0 - d);
        d = asrd(d, 6);
        W16(x + XP_MX + 2 * i, d);          /* INIT MUNGE VALUE */
        W16(x + XP_CX + 2 * i, d + R16(u + pos[i]));
    }
    bgam(m, x, u);
}

/* BGAXP: BEGIN ALIEN EXPLOSION. X-->ALIEN PARMS */
static void bgaxp(sw_machine *m, uint16_t x)
{
    uint16_t u = R16(x + A_UVP);            /* U-->ALIEN VECTORS */
    wing(m, u, TP_TI1, 1);                  /* TYPE 1, TIE FIGHTER */
    wing(m, u, TP_TI2, 0);                  /* TYPE 2, TIE FIGHTER RIGHT WING */

    x = nxtfre(m);
    W8(x + XP_TYP, TP_TI3);                 /* TYPE 3, TIE FIGHTER CENTER GLOBE */
    W8(x + XP_TMR, 0x10);
    W16(x + XP_CX, R16(u + M_TX));
    W16(x + XP_MX, R16(u + M_TX));
    W16(x + XP_CY, R16(u + M_TY));
    W16(x + XP_MY, R16(u + M_TY));
    W16(x + XP_CZ, R16(u + M_TZ));
    W16(x + XP_MZ, R16(u + M_TZ));

    uint16_t d = (uint16_t)((R8(x + XP_MX) << 8) | 0x80);   /* D:=X POSITION, LIMIT THE VECTORIZING */
    for (;;) {                              /* NORMALIZE VECTOR TO SHIP */
        if (lsl_overflows(d)) break;        /* ?OVER-NORMALIZED? */
        d = (uint16_t)(d << 1);
        uint16_t my = R16(x + XP_MY);
        if (lsl_overflows(my)) {            /* ?Y IS OVER? Y IS CORRECTED */
            d = asrd(d, 1);                 /* UNDO X */
            break;
        }
        W16(x + XP_MY, my << 1);
        uint16_t mz = R16(x + XP_MZ);
        if (lsl_overflows(mz)) {            /* ALSO END WHEN Z TOO LARGE: Z IS CORRECTED */
            /* UNDO ONE Y (ASR on both bytes, as the original has it) */
            W8(x + XP_MY, (uint8_t)((int8_t)R8(x + XP_MY) >> 1));
            W8(x + XP_MY + 1, (uint8_t)((int8_t)R8(x + XP_MY + 1) >> 1));
            d = asrd(d, 1);                 /* UNDO X ALSO */
            break;
        }
        W16(x + XP_MZ, mz << 1);
    }
    /* GET DISTANCE FROM CURRENT TO FRONTIER: GO ALL THE WAY TO EDGE IN 16. */
    d = asrd((uint16_t)(d - R16(x + XP_CX)), 4);
    W16(x + XP_MX, (d & 0xff00) | R8(SW_P_RND1));
    d = asrd((uint16_t)(R16(x + XP_MY) - R16(x + XP_CY)), 4);
    W16(x + XP_MY, (d & 0xff00) | R8(SW_P_RND1));
    d = asrd((uint16_t)(R16(x + XP_MZ) - R16(x + XP_CZ)), 4);
    W16(x + XP_MZ, (d & 0xff00) | R8(SW_P_RND1));
}

/* BGAXPLD: an alien explodes. X-->ALIEN PARMS */
void sw_bgaxpld(sw_machine *m, uint16_t x)
{
    W8(x + A_TYP, 0);                       /* THE ALIEN IS NOW DEAD */
    sw_cpugon(m);
    bgaxp(m, x);
}

#define TMPTP1 0x4801       /* TEMP TP1,1,TP2,1,TP3,1,VZ,1 */
#define TMPTP2 0x4802
#define TMPTP3 0x4803
#define TMPVZ  0x4804

/* A piece of a building: from the CENTER OF EXPLOSION (M.XT, YT, ZT; off
 * by DX, DY in the high bytes) forward past the player and to a side. */
static void building_piece(sw_machine *m, uint16_t typ, uint8_t dx, uint8_t dy, uint8_t side)
{
    uint16_t x = nxtfre(m);                 /* X-->EXPLOSION QUEUE ENTRY */
    W8(x + XP_TYP, R8(typ));
    W8(x + XP_TMR, 0x20);                   /* LIFE TIMER */
    W16(x + XP_CX, R16(SW_M_XT) + (uint16_t)(dx << 8));
    W16(x + XP_CY, R16(SW_M_YT) + (uint16_t)(dy << 8));
    W16(x + XP_CZ, R16(SW_M_ZT));
    uint16_t d = asrd((uint16_t)(R16(SW_M_S1 + M_TX) + 0x7f00 - R16(SW_M_XT)), 5);
    W16(x + XP_MX, (d & 0xff00) | R8(SW_P_RND1));       /* SPEED TO DESTINATION */
    d = asrd((uint16_t)(R16(SW_M_S1 + M_TY) + (uint16_t)(side << 8) - R16(SW_M_YT)), 5);
    W16(x + XP_MY, (d & 0xff00) | R8(SW_P_RND1));       /* LEFT/RIGHT SPEED TO DESTINATION */
    /* 728. TO 1024., VARY AROUND 54.*120.*2/16=810.: *4 */
    d = (uint16_t)((R8(TMPVZ) << 8) | R8(SW_P_RND1));
    W16(x + XP_MZ, d << 2);                 /* VERTICAL VELOCITY */
}

/* BGBKN: three pieces; M.XT,YT,ZT HAVE CENTER */
static void bgbkn(sw_machine *m)
{
    building_piece(m, TMPTP1, 0, (uint8_t)-2, (uint8_t)-0x3f);  /* PIECE 1: GO LEFT */
    building_piece(m, TMPTP2, 2, 0, 0);                         /* CENTER PIECE */
    building_piece(m, TMPTP3, 0, 2, 0x3f);                      /* RIGHT PIECE: GO RIGHT */
}

/* BGBKXP, BGWGXP: BEGIN BUNKER EXPLOSION */
void sw_bgbkxp(sw_machine *m)
{
    W16(TMPTP1, (TP_BK1 << 8) + TP_BK2);    /* TYPE 1 BUNKER, TYPE 2 BUNKER */
    W16(TMPTP3, (TP_BK3 << 8) + 0x03);      /* TYPE 3 BUNKER, Z VELOCITY OF 0C00(4*300) */
    bgbkn(m);
}

/* BGTWXP: a tower's top blows up */
void sw_bgtwxp(sw_machine *m)
{
    W16(TMPTP1, (TP_TW1 << 8) + TP_TW2);    /* TYPE 1 TOWER,TYPE 2 TOWER */
    W16(TMPTP3, (TP_TW3 << 8) + 2);         /* TYPE 3 TOWER, Z VELOCITY OF 800(4*200) */
    bgbkn(m);
}

/* IXPLD: ONCE PER PHASE TO CLEAR THINGS UP */
void sw_ixpld(sw_machine *m)
{
    for (uint16_t x = SW_XPQUE; x < SW_XPQUEZ; x += XP_IZE)
        W8(x + XP_TMR, 0);
}

/* MUNGE: CONTORT THE EXPLOSION MATRIX FOR VIEWING */
static void munge(sw_machine *m)
{
    W16(SW_M_SIN, 5309);                    /* 19.04 DEGREES ROLL */
    W16(SW_M_COS, 15500);
    W16(SW_M_BIC, SW_MSMUNB);
    sw_mgowt(m, SW_MSROLL);

    W16(SW_M_SIN, 1424);                    /* 4.99 DEGREES PITCH */
    W16(SW_M_COS, 16322);
    W16(SW_M_BIC, SW_MSMUNB);
    sw_mgowt(m, SW_MSPTCH);
}

/* MVBKn, MVTWn: a piece of a bunker or tower rises and falls */
static void mvbk(sw_machine *m, uint16_t x)
{
    for (int i = 0; i < 4; i += 2) {        /* X, Y */
        W16(x + XP_CX + i, R16(x + XP_MX + i) + R16(x + XP_CX + i));    /* MOVE CENTER OF EXPLOSION PIECE */
        uint16_t d = asrd((uint16_t)(0 - R16(x + XP_MX + i)), 5);
        W16(x + XP_MX + i, d + R16(x + XP_MX + i));     /* FRICTION LOSSES */
    }
    int32_t z = (int16_t)R16(x + XP_CZ) + (int16_t)R16(x + XP_MZ);
    if (z >= -32768 && z <= 32767) {        /* ?VALID RESULTS? */
        if (z < 0) z = 0;                   /* ?RAMMING INTO GROUND? THEN FREEZE IT AT GROUND LEVEL */
        W16(x + XP_CZ, (uint16_t)z);        /* UPDATE VERTICAL POSITION */
    }
    /* FORCE OF GRAVITY <54.*120.*2>/16.SQUARED FRAME */
    W16(x + XP_MZ, R16(x + XP_MZ) - 50 * 4);
}

/* MVTIn: LEFT WING, RIGHT WING KEEP ORIGINAL VELOCITY, CENTER GLOBE MOVE
 * AWAY FROM LAZAR BLAST */
static void mvti(sw_machine *m, uint16_t x)
{
    for (int i = 0; i < 6; i += 2) {
        int32_t d = (int16_t)R16(x + XP_MX + i) + (int16_t)R16(x + XP_CX + i);
        if (d >= -32768 && d <= 32767) W16(x + XP_CX + i, (uint16_t)d);
    }
}

/* DOXPLD: PROCESS EXPLOSIONS IN PROGRESS */
void sw_doxpld(sw_machine *m)
{
    uint16_t x;
    for (x = SW_XPQUE; x < SW_XPQUEZ; x += XP_IZE) {
        if (R8(x + XP_TMR) == 0) continue;
        W8(x + XP_TMR, R8(x + XP_TMR) - 1); /* COUNT DOWN TO ZERO */
        uint8_t b = R8(x + XP_TYP);
        if (b >= TP_COUNT) continue;        /* (SWI) */
        if (b >= TP_TI1) mvti(m, x);
        else mvbk(m, x);
    }
    for (x = SW_XPQUE; x < SW_XPQUEZ; x += XP_IZE) {
        if (R8(x + XP_TMR) != 0) {
            munge(m);                       /* A LIVE EXPLOSION KEEPS THE MATRIX ROLLING */
            return;
        }
    }
    sw_unitv(m, SW_M_MUN);                  /* ?NONE ARE ACTIVE? THEN RESTART THE UNIT VECTORS */
}

/* VWBKN, VWTWN: a piece of a bunker (red) or tower (white) and its place
 * on the ground */
static void vwbkn(sw_machine *m, uint16_t x, uint8_t id, uint8_t color)
{
    W8(SW_BJ_ID, id);
    uint8_t b = R8(x + XP_TMR);             /* A:=COLOR SELECT AND OPCODE */
    if (b <= 7) b = (uint8_t)(b << 4);      /* ?FADE WHEN DONE? UP TO 0-70 */
    else b = 0x80;
    VG((((SW_VGCOLR >> 8) + color) << 8) | b);  /* COLOR SELECT */

    W16(SW_M_X0, R16(x + XP_CX));           /* GET SHADOW IN CORRECT POSITION */
    W16(SW_M_Y0, R16(x + XP_CY));
    W16(SW_M_Z0, 0);                        /* POSITION ON GROUND */
    W16(SW_M_BIC, SW_MSX0BC);
    /* ***DO YOU BELIEVE THIS SIDE EFFECT? */
    uint16_t zt = R16(SW_M_ZT);
    uint16_t yt = R16(SW_M_YT);
    uint16_t xt = R16(SW_M_XT);
    sw_mgowt(m, SW_MSPSB2);
    /* ***HERE WE GO BACK AGAIN */
    W16(SW_M_XT, xt);
    W16(SW_M_YT, yt);
    W16(SW_M_ZT, zt);

    W16(SW_M_DVD, R16(SW_M_XP));
    sw_objcen(m);                           /* POSITION THE SHADOW FIRST, THEN SCALE ALL ELSE */

    VG(0x7200 | (uint8_t)(R8(SW_M_XT) << 2));   /* SCALE DOWN TO MATCH GRND */
}

/* VWTIN: a piece of a TIE fighter */
static void vwtin(sw_machine *m, uint16_t x, uint8_t id)
{
    W8(SW_BJ_ID, id);
    uint8_t b = R8(x + XP_TMR);             /* SELECT A COLOR */
    if (b > 0x1f) VG(SW_VJFLS);             /* ?BEYOND THE TABLE? THEN REALLY FLASH */
    else VG(R16(TVWCLE + (b << 1)));        /* ELSE SHOW US BEATING IT */
    sw_objcen(m);
}

/* VWXPL2: one piece */
static void vwxpl2(sw_machine *m, uint16_t x)
{
    static const uint8_t bk[3] = { SW_TDSBK1, SW_TDSBK2, SW_TDSBK3 };
    static const uint8_t tw[3] = { SW_TDSTW1, SW_TDSTW2, SW_TDSTW3 };
    static const uint8_t ti[3] = { SW_TDSTI1, SW_TDSTI2, SW_TDSTI3 };

    W16(SW_M_X0, R16(x + XP_CX));           /* CENTER OF THIS PIECE */
    W16(SW_M_Y0, R16(x + XP_CY));
    W16(SW_M_Z0, R16(x + XP_CZ));

    W16(SW_M_BIC, SW_MSX0BC);
    sw_mgowt(m, SW_MSPSB2);

    int visible = 0;
    uint16_t d = R16(SW_M_XP);
    if (!(d & 0x8000)) {
        W16(SW_M_XT, d);
        W16(SW_M_DVD, d);
        d = R16(SW_M_YP);
        W16(SW_M_YT, d);
        if (d & 0x8000) d = (uint16_t)(0 - d);
        if ((int16_t)d < (int16_t)R16(SW_M_XT)) {
            d = R16(SW_M_ZP);
            W16(SW_M_ZT, d);
            if (d & 0x8000) d = (uint16_t)(0 - d);
            d >>= 1;                        /* ALLOW Z TO BE TWICE AS HI FOR LAZAR TOWERS */
            if ((int16_t)d < (int16_t)R16(SW_M_XT)) visible = 1;
        }
    }
    if (!visible) {
        (void)R8(x + XP_TMR);               /* (CLR reads first) */
        W8(x + XP_TMR, 0);                  /* IF PIECE NOT VISIBLE, THEN TURN IT OFF */
        return;
    }

    uint8_t b = R8(x + XP_TYP);
    if (b < TP_TW1) vwbkn(m, x, bk[b - TP_BK1], VGCRED);
    else if (b < TP_TI1) vwbkn(m, x, tw[b - TP_TW1], VGCWHT);
    else if (b < TP_COUNT) vwtin(m, x, ti[b - TP_TI1]);

    sw_adobj(m);                            /* MAKE SURE WE CAN USE IT */
    sw_objpnt(m);                           /* THEN GET THE POINTS */
    sw_objdrw(m);                           /* AND DRAW IT */
    VG(SW_VGSCAL);
    VG(SW_VGCNTR);
}

/* VWXPLD: VIEW OTHERS EXPLODING */
void sw_vwxpld(sw_machine *m)
{
    W8(SW_M_BICL, SW_MSMUNB);               /* IS2UV: DOWNLOAD DESTINATION MATRIX */
    W8(SW_M_BICH, 0);
    sw_mgowt(m, SW_MSDLD2);
    sw_mgowt(m, SW_MSCNCT);                 /* CONCATENATE WITH VIEWERS MATRIX */

    uint16_t x = SW_XPQUE;
    do {                                    /* CYCLE THRU EXPLOSION LIST, VIEWING IT */
        W16(SW_S2_PRM, x);
        if (R8(x + XP_TMR) != 0) {          /* ?GOT A LIVE EXPLOSION? */
            TAG(SW_TAG_XPLD, (x - SW_XPQUE) / XP_IZE);
            vwxpl2(m, x);
        }
        x = (uint16_t)(R16(SW_S2_PRM) + XP_IZE);
    } while (x < SW_XPQUEZ);
}

/* --- DETH STAR VISUALS --------------------------------------------------------------- */

/* The Death Star blows up in four parts (XP.PHS 1 to 4, 0: over), each
 * counting XP.CNT: circles that fill from the center, then rings that
 * run outwards. */

/* DCIRCL: DRAW A SERIES OF CONCENTRIC CIRCLES. D=VG COLOR COMMAND, U=VG
 * SCALE COMMAND FOR STARTING VALUE, X=# OF CIRCLES TO BE DRAWN.
 * OUT: U=NEXT SCALE VALUE */
static uint16_t dcircl(sw_machine *m, uint16_t d, uint16_t u, uint16_t x)
{
    VG(d);
    do {
        VG(SW_VGOFFY & 0x1fff);             /* Y OFFSET FOR GAME'S SCREEN CENTER */
        VG(0x0000);                         /* NO OFFSET FOR X */
        VG(u);                              /* SCALE */
        VG(SW_VJBCR2);                      /* BASE CIRCLE WITHOUT COLOR */
        u -= 2;
    } while (--x != 0);
    return u;
}

/* DRING: DRAW RINGS. SIMILAR TO CIRCLES BUT SCALE SMOOTHLY. */
static void dring(sw_machine *m, uint16_t d, uint16_t u, uint16_t x)
{
    VG(d);
    do {
        VG(SW_VGOFFY & 0x1fff);
        VG(0x0000);
        VG(u);
        VG(SW_VJBCR2);                      /* BASE STAR CIRCLE WITHOUT COLOR COMMAND */
        u = (uint16_t)(u - 4) & 0xff7f;
    } while (--x != 0);
}

/* The scale the EXPANSION RINGS start at, from the count. */
static uint16_t ring_scale(uint16_t cnt)
{
    uint16_t d = (uint16_t)~(uint16_t)(cnt << 3);
    if (!(d & 0x0080)) d = (uint16_t)((d - 0x100) | 0x80);
    d = (uint16_t)(d + SW_VGSCAL + 0x400 + 0x70);
    if (!(d & 0x0080)) d = (uint16_t)(d - 0x100);
    return d;
}

#define XCOLOR(c) ((SW_VGCOLR + ((c) << 8)) | 0xff)

/* The count has run out: the next part. */
static void next_part(sw_machine *m, uint16_t cnt)
{
    W16(SW_XP_CNT, cnt);
    W8(SW_XP_PHS, R8(SW_XP_PHS) + 1);
    sw_doxmt(m, SW_IDADF);                  /* FINAL DETH STAR EXPLOSION */
}

/* XP.PH0: FROM DIM TO BRIGHT */
static void xp_ph0(sw_machine *m)
{
    dcircl(m, SW_VGCOLR + (VGCRED << 8), SW_VGSCAL + 0x400 + 0xf0, R16(SW_XP_CNT));
    uint16_t d = (uint16_t)(R16(SW_XP_CNT) + 2);
    W16(SW_XP_CNT, d);
    if (d < 0x003f) next_part(m, 0x0001);
}

/* XP.PH1: BRIGHT BLUE FROM CENTER,RED EXPANSION */
static void xp_ph1(sw_machine *m)
{
    uint16_t u = dcircl(m, XCOLOR(VGCBLU), SW_VGSCAL + 0x400 + 0xf0, R16(SW_XP_CNT));   /* DRAW BLUE CIRCLES */
    uint16_t d = (uint16_t)(R16(SW_XP_CNT) + 2);
    W16(SW_XP_CNT, d);
    if (d < 0x3f) {                         /* DRAW SOME RED */
        dcircl(m, XCOLOR(VGCRED), u, (uint8_t)(0x3f - R8(SW_XP_CNT + 1)));
        /* RED EXPANSION RINGS */
        d = R16(SW_XP_CNT);
        dring(m, XCOLOR(VGCRED), ring_scale(d),
              (d & 0xff00) | (uint8_t)(((((uint8_t)d >> 2) & 0x07) ^ 0x07) + 1));
    }
    if (R16(SW_XP_CNT) >= 0x3f) next_part(m, 0x0001);   /* SETUP FOR NEXT PHASE */
}

/* XP.PH2: BRIGHT WHITE FROM CENTER,BLUE EXPANSION */
static void xp_ph2(sw_machine *m)
{
    uint16_t u = dcircl(m, XCOLOR(VGCWHT), SW_VGSCAL + 0x400 + 0x70, R16(SW_XP_CNT));   /* DRAW WHITE CIRCLES */
    uint16_t d = (uint16_t)(R16(SW_XP_CNT) + 3);
    W16(SW_XP_CNT, d);
    if (d < 0x3f)                           /* DRAW SOME BLUE */
        dcircl(m, XCOLOR(VGCBLU), u, (uint8_t)(0x3f - R8(SW_XP_CNT + 1)));
    d = R16(SW_XP_CNT);                     /* BLUE EXPANSION RINGS */
    if (d < 0x3f)                           /* DRAW EM */
        dring(m, XCOLOR(VGCBLU), ring_scale(d),
              (d & 0xff00) | (uint8_t)((((uint8_t)d & 0x0f) ^ 0x0f) + 1));
    if (R16(SW_XP_CNT) >= 0x50) next_part(m, 0x0080);   /* SETUP FOR NEXT PHASE */
}

/* XP.PH3: CONTINUALLY EXPANDING WHITE RINGS */
static void xp_ph3(sw_machine *m)
{
    uint16_t d = (uint16_t)(R16(SW_XP_CNT) - 4);
    W16(SW_XP_CNT, d);
    if (d >= 0x0008) {                      /* CONTINUE THIS EXPLOSION PHASE */
        uint16_t u = (uint16_t)(d + SW_VGSCAL + 0x300);
        d = R16(SW_XP_CNT) >> 1;
        dring(m, XCOLOR(VGCWHT), u, (d & 0xff00) | (((uint8_t)d & 0x3f) ^ 0x3f));  /* FINAL EXPLOSION */
    } else {
        W8(SW_XP_PHS, 0);                   /* NULL PHASE, ALL DONE */
    }
}

/* BGXPLN: the Death Star begins to explode */
void sw_bgxpln(sw_machine *m)
{
    W8(SW_XP_PHS, 1);                       /* STARTING PHASE, 00 IS FLAG MEANING END */
    W16(SW_XP_CNT, 0x0001);                 /* STARTING COUNT */
}

/* VWXPLN: the explosion in view, and on by one step */
void sw_vwxpln(sw_machine *m)
{
    TAG(SW_TAG_DSTAR, 4);
    switch (R8(SW_XP_PHS)) {
    case 1: xp_ph0(m); break;
    case 2: xp_ph1(m); break;
    case 3: xp_ph2(m); break;
    case 4: xp_ph3(m); break;
    default: break;                         /* XP.NUL */
    }
}
