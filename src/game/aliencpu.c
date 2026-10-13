/* aliencpu.c - WSCPU.MAC, "CPU-COMPUTER DRONES": what the enemy ships do
 * each frame.
 *
 * A ship's choreography (alien.c) gives it twirl and move flags for a
 * while; here they are carried out: roll, pitch and yaw by one small angle
 * through the math box, the velocity from the ship's own axes, the move.
 * Then the ship looks at the player (math box PRE2 with the ship as the
 * viewer): the status flags for the choreography, a shot when the odds of
 * the wave's difficulty say so, and, when told to chase (C$T0), turning
 * toward the player.
 *
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

#define C_AS 0x0004         /* ALIEN HAS PLAYER IN SITES */
#define C_AV 0x0008         /* ALIEN HAS PLAYER IN FRONT VIEW */
#define C_R  0x0030         /* C$R1 + C$R2: random bits */
#define C_AG 0x0040         /* ALIEN HAS FIRED A GUN */
#define C_PV 0x1000         /* PLAYER HAS ALIEN IN VIEW */
#define C_T9 0x40           /* twirl flag: AIM IN FRONT OF PLAYER */
#define C_T0 0x80           /* twirl flag: AIM AT PLAYER */

#define G_TYP 3
#define G_IZE 6

/* TGPROB: GUN SHOOTING PROB by the wave's difficulty: TIMER MASK (0F=1
 * SECOND), PROB COMPARE (C0=25%), HOW MANY GUNS ALLOWED TO SHOOT */
static const struct { uint8_t mask, prob, guns; } TGPROB[11] = {
    { 0x0f, 0x80, 1 }, { 0x0f, 0x80, 1 }, { 0x0f, 0x80, 2 }, { 0x0f, 0x40, 3 }, { 0x07, 0x80, 4 },
    { 0x07, 0x20, 5 }, { 0x07, 0x20, 6 }, { 0x03, 0x80, 6 }, { 0x03, 0x60, 6 }, { 0x03, 0x40, 6 },
    { 0x03, 0x30, 6 },
};

/* --- turning a ship: one step of 4.48 degrees (TSNGLE's entry 5) --------------- */

static void angle(sw_machine *m, int negative)
{
    W16(SW_M_SIN, negative ? (uint16_t)-0x04ff : 0x04ff);
    W16(SW_M_COS, 0x3fce);
}

static void turn(sw_machine *m, uint16_t x, uint8_t program)
{
    W16(SW_M_BIC, R8(x + A_BIC));
    sw_mgowt(m, program);
}

static void bump(sw_machine *m, uint16_t counter, int by) { W8(counter, R8(counter) + by); }

static void snptu(sw_machine *m, uint16_t x) { angle(m, 0); bump(m, SW_CP_PT, 1); turn(m, x, SW_MSPTCH); }   /* PITCH UP */
static void snptd(sw_machine *m, uint16_t x) { angle(m, 1); bump(m, SW_CP_PT, -1); turn(m, x, SW_MSPTCH); }  /* PITCH DOWN */
static void snrll(sw_machine *m, uint16_t x) { angle(m, 1); bump(m, SW_CP_RL, -1); turn(m, x, SW_MSROLL); }  /* ROLL LEFT */
static void snrlr(sw_machine *m, uint16_t x) { angle(m, 0); bump(m, SW_CP_RL, 1); turn(m, x, SW_MSROLL); }   /* ROLL RIGHT */
static void snywl(sw_machine *m, uint16_t x) { angle(m, 0); bump(m, SW_CP_YW, -1); turn(m, x, SW_MSYAW); }   /* YAW LEFT */
static void snywr(sw_machine *m, uint16_t x) { angle(m, 1); bump(m, SW_CP_YW, 1); turn(m, x, SW_MSYAW); }    /* YAW RIGHT */

/* --- moving a ship ---------------------------------------------------------------- */

/* SNMVx: one of the ship's axes (its x, y and z parts at ax, bx, cx),
 * scaled down, into the velocity. */
static void slip(sw_machine *m, uint16_t u, int ax, int bx, int cx, int shift, int negate)
{
    static const int v[3] = { M_VX, M_VY, M_VZ };
    const int part[3] = { ax, bx, cx };
    for (int i = 0; i < 3; i++) {
        uint16_t d = R16(u + part[i]);
        if (negate) d = (uint16_t)(0 - d);
        d = (uint16_t)((int16_t)d >> shift);
        W16(u + v[i], d + R16(u + v[i]));
    }
}

/* DOAMOV: MOVE ALIEN. A position does not wrap: at the edge it stays. */
static void doamov(sw_machine *m, uint16_t u)
{
    static const int t[3] = { M_TX, M_TY, M_TZ }, v[3] = { M_VX, M_VY, M_VZ };
    for (int i = 0; i < 3; i++) {
        int32_t s = (int16_t)R16(u + v[i]) + (int16_t)R16(u + t[i]);
        if (s >= -32768 && s <= 32767) W16(u + t[i], (uint16_t)s);     /* NEW LOCAL POSITION */
    }
}

/* CPUMVL: MOVE LOCALLY */
static void cpumvl(sw_machine *m)
{
    uint16_t x = R16(SW_S1_PRM), u = R16(x + A_UVP);
    if (R8(x + A_GLW) == 0) {               /* NO NEW VELOCITY IF HIT */
        W8(SW_CP_SHF, R8(x + A_CHMV));      /* MOVE INFO */
        W16(u + M_VX, 0);                   /* SET VELOCITY TO ZERO */
        W16(u + M_VY, 0);
        W16(u + M_VZ, 0);                   /* NOW LET US SLIP */
        uint8_t f = R8(SW_CP_SHF);
        if (f & 0x01) slip(m, u, M_AZ, M_BZ, M_CZ, 6, 1);   /* DOWN */
        if (f & 0x02) slip(m, u, M_AZ, M_BZ, M_CZ, 5, 1);   /* DOWN TWICE */
        if (f & 0x04) slip(m, u, M_AZ, M_BZ, M_CZ, 6, 0);   /* UP */
        if (f & 0x08) slip(m, u, M_AZ, M_BZ, M_CZ, 5, 0);   /* UP TWICE */
        if (f & 0x10) slip(m, u, M_AX, M_BX, M_CX, 6, 0);   /* FORWARD */
        if (f & 0x20) slip(m, u, M_AX, M_BX, M_CX, 5, 0);   /* FORWARD TWICE */
        W8(SW_CP_SHF, f >> 6);              /* (six LSRs) */
    }
    doamov(m, u);                           /* AN ALIEN MOVE */
}

/* CPCHKL: KEEP DISTANCE IN RANGE */
static void cpchkl(sw_machine *m, uint16_t x)
{
    static const int t[3] = { M_TX, M_TY, M_TZ };
    uint16_t u = R16(x + A_UVP);            /* ALIEN UNIT VECTORS */
    for (int i = 0; i < 3; i++) {
        uint16_t d = R16(u + t[i]);         /* ALIEN LOC */
        if ((int8_t)(d >> 8) >= 0x7d) d = 0x7cff;           /* KEEP POSITIVE DISTANCE IN RANGE */
        if ((int8_t)(d >> 8) <= (int8_t)0x82) d = 0x8300;   /* KEEP NEGATIVE DISTANCE IN RANGE */
        W16(u + t[i], d);
    }
}

/* Roll to help a turn: only when the player is far enough off the axis
 * (AVOID JITTER); which way by the quadrant. */
static int off_axis(sw_machine *m, uint16_t coord, uint16_t other, int *negative)
{
    uint8_t b = R8(coord);
    uint8_t a = (b & 0x80) ? 0xff : 0x00;   /* SEX */
    if ((uint8_t)(b + 1) <= 1) return 0;
    *negative = ((a ^ R8(other)) & 0x80) != 0;      /* INVOLVE QUADRANT */
    return 1;
}

/* CPUAL: ALIEN CPU, LOCAL. X is the ship in S1.PRM. */
static void cpual(sw_machine *m, uint16_t x)
{
    sw_chopdo(m, x);
    W8(SW_CP_AC, 0);                        /* CLEAR ACTIONS REQUESTED */
    W8(SW_CP_RL, 0);
    W8(SW_CP_PT, 0);
    W8(SW_CP_YW, 0);
    /* SAVE IF IN VIEW FOR GUNS; PREPARE NEW CHOREG STATUS */
    W16(x + A_CHST, ((R8(x + A_CHST) & (C_PV >> 8)) << 8) | (R8(SW_P_RND1) & C_R));

    uint8_t a = (uint8_t)(R8(x + A_ROL) - 1);
    if (!(a & 0x80)) {
        W8(x + A_ROL, a);
        W16(SW_M_SIN, 5696);                /* 20.34 DEGREES */
        W16(SW_M_COS, 15362);
        turn(m, x, SW_MSROLL);              /* SKIP OVER AIM AND SHOOT (the twirls) */
    } else {
        uint8_t f = R8(x + A_CHTW);         /* TWIRL INFO */
        W8(SW_CP_SHF, f);
        if (f & 0x01) snrll(m, x);
        if (f & 0x02) snrlr(m, x);
        if (f & 0x04) snptu(m, x);
        if (f & 0x08) snptd(m, x);
        if (f & 0x10) snywr(m, x);
        if (f & 0x20) snywl(m, x);
        W8(SW_CP_SHF, f >> 6);
    }

    cpumvl(m);                              /* MOVE LOCALLY */
    x = R16(SW_S1_PRM);
    cpchkl(m, x);
    if (R8(x + A_TYP) != 1) goto done;      /* ?NOT LIVE SHIP? THEN NO SHOTS */

    W8(SW_M_BICL, R8(x + A_BIC));           /* IS1UV: DOWNLOAD VECTORS */
    W8(SW_M_BICH, 0);
    sw_mgowt(m, SW_MSDLD1);
    x = R16(SW_S1_PRM);
    W8(x + A_AIM, 0);
    W16(SW_M_BIC, SW_MSS1BC + 3);           /* CHASE PLAYER'S ONE SHIP */

    if (R8(x + A_CHTW) & C_T9) {            /* AIM IN FRONT OF PLAYER */
        W16(SW_M_S1 + M_TX, R16(SW_M_S1 + M_TX) + 0x1000);     /* GO IN FRONT OF PLAYER */
        sw_mgowt(m, SW_MSPSB2);             /* NOW VIEW PLAYER'S SHIP */
        W16(SW_M_S1 + M_TX, R16(SW_M_S1 + M_TX) - 0x1000);     /* RESTORE M$TX */
    } else {
        sw_mgowt(m, SW_MSPSB2);             /* VIEW PLAYERS SHIP */
    }

    uint16_t d = R16(SW_M_XP);              /* ?PLAYER IN FRONT? */
    if (!(d & 0x8000)) {                    /* IGNORE GUNS IF IN BACK */
        W8(x + A_AIM, R8(x + A_AIM) + 1);
        if ((int16_t)d < 0x4000) {          /* IGNORE GUN IF TOO FAR AWAY */
            W16(x + A_CHST, R16(x + A_CHST) | C_AV);        /* SET PLAYER IN ALIENS VIEW */
            if ((uint16_t)(R16(SW_M_YPS) + R16(SW_M_ZPS)) <= 0x20)     /* ?AIMING NEAR SHIP? */
                W16(x + A_CHST, R16(x + A_CHST) | C_AS);    /* PLAYER IN ALIENS SITES */
        }
    }

    if ((R8(x + A_CHST) & (C_PV >> 8)) &&   /* NO SHOOTING GUNS IF PLAYER CANT SEE US */
        !(R8(x + A_CHTW) & C_T9) &&         /* NO SHOOT IF AIMMING IN FRONT */
        (int16_t)R16(SW_M_XP) > 0x800 &&    /* TOO CLOSE */
        R8(x + A_GLW) == 0) {               /* IF ALIEN BEING HIT THEN DELAY SHOOTING */
        uint8_t b = R8(SW_WV_HRD);
        if (b >= 11) b = 10;                /* # OF WAVES DEFINED IN PROBABILITY TABLE */
        if ((R8(SW_FRAME + 1) & TGPROB[b].mask) == 0 &&     /* FIRE SHELL: SPACE THEM OUT */
            R8(SW_P_RND1) > TGPROB[b].prob) {               /* LOWER VAL IS MORE OFTEN */
            /* # OF GUNS ALLOWED: the last so many of the gun records */
            for (uint16_t u = (uint16_t)(SW_GUNZ - TGPROB[b].guns * G_IZE); u < SW_GUNZ; u += G_IZE) {
                if (R8(u + G_TYP) != 0) continue;           /* GUN FREE TO START? */
                x = R16(SW_S1_PRM);
                W16(x + A_CHST, R16(x + A_CHST) | C_AG);    /* TELL ALIEN HE FIRED A GUN */
                m->rx = x;
                m->ru = u;
                ORIG(SW_FRAGUN);
                break;
            }
        }
    }

    /* AIM AT PLAYER */
    x = R16(SW_S1_PRM);
    if (R8(x + A_CHTW) & C_T0) {
        int neg;
        if (R8(SW_CP_YW) == 0) {
            if (!(R8(SW_M_YP) & 0x80)) snywr(m, x);
            else snywl(m, x);
            if (R8(SW_CP_RL) == 0 && off_axis(m, SW_M_ZP, SW_M_YP, &neg)) {     /* ?HELP YAW BY ROLLING? */
                if (!neg) snrll(m, x);      /* +Z UP, +Y RIGHT QUAD */
                else snrlr(m, x);
            }
        }
        if (R8(SW_CP_PT) == 0) {
            if (!(R8(SW_M_ZP) & 0x80)) snptu(m, x);
            else snptd(m, x);
            if (R8(SW_CP_RL) == 0 && off_axis(m, SW_M_YP, SW_M_ZP, &neg)) {     /* ?ROLL AVAILABLE TO HELP PITCH? */
                if (!neg) snrlr(m, x);
                else snrll(m, x);
            }
        }
    }
done:
    x = R16(SW_S1_PRM);
    W16(x + A_CHST, R16(x + A_CHST) & (uint16_t)~C_PV);
}

/* CPU: COMPUTER DRONES */
void sw_cpu(sw_machine *m)
{
    uint16_t x = SW_ALIEN;
    do {
        W16(SW_S1_PRM, x);
        if (R8(x + A_TYP) != 0) cpual(m, x);        /* IGNORE DEAD ONES */
        x = (uint16_t)(R16(SW_S1_PRM) + A_IZE);
    } while (x < SW_ALIENZ);
}

static uint8_t absa(uint8_t a) { return (a & 0x80) ? (uint8_t)-a : a; }

/* One coordinate's high byte toward the middle. */
static void toward_center(sw_machine *m, uint16_t addr, int limit, int step)
{
    int8_t a = (int8_t)R8(addr);
    if (a >= limit + step) a = (int8_t)(a - step);
    if (a <= -limit - step) a = (int8_t)(a + step);
    W8(addr, (uint8_t)a);
}

/* CPURET: COMPUTER DRONES RUN AWAY, back to the Death Star */
void sw_cpuret(sw_machine *m)
{
    uint16_t x = SW_ALIEN;
    do {
        W16(SW_S1_PRM, x);
        uint16_t u = R16(x + A_UVP);
        if (R8(x + A_TYP) == 1) {           /* ?ALIVE? MOVE Y AND Z TO CENTER */
            toward_center(m, u + M_TY, 7, 2);
            toward_center(m, u + M_TZ, 6, 3);
            int32_t s = (int16_t)R16(u + M_TX) + 0x400;     /* RACE BACK TO DETH STAR */
            if (s <= 32767) {
                W16(u + M_TX, (uint16_t)s);
            } else if ((int8_t)absa(R8(u + M_TY)) <= 0x08 && (int8_t)absa(R8(u + M_TZ)) <= 0x08) {
                W8(x + A_TYP, 0);           /* OVERFLOWED TO DETH STAR, Y AND Z CLOSE ALSO: TURN OFF ALIEN */
            }
        }
        x = (uint16_t)(R16(SW_S1_PRM) + A_IZE);     /* LOOP WITH NEXT ALIEN */
    } while (x < SW_ALIENZ);
}

/* --- a laser hits a ship ------------------------------------------------------------ */

#define C_AH 0x0001         /* NEARBY ALIEN HAS BEEN HIT */
#define TMPLP 0x4801        /* TEMP LP,1 */

/* A random part of the hit along one of the player's axes: the size from
 * the axis, the direction from a random bit. */
static uint16_t random_kick(sw_machine *m, uint16_t rnd, uint16_t axis)
{
    uint16_t d = (uint16_t)((R8(rnd) | 0x80) * R8(axis));   /* AT LEAST SOME ANGLE */
    uint8_t a = (uint8_t)(d >> 8);
    if (!(d & 0x80)) a = (uint8_t)-a;       /* RANDOM BIT */
    return (uint16_t)(int16_t)(int8_t)a;
}

/* XPSA: EXPLODE SPACE ALIEN */
static void xpsa(sw_machine *m)
{
    uint16_t x = R16(SW_CL_AP);             /* X-->HIT ALIEN */
    sw_doxmt(m, SW_IDASX);                  /* SMALL EXPLOSION */
    if (R8(x + A_BIC) == R8(SW_Q_PBB))      /* ?SAME GUY DOING CURRENT PASSBY? */
        sw_doxmt(m, SW_IDAPBZ);             /* THEN KILL ANY PASSBY SOUND */
    m->rx = x;
    ORIG(SW_BGAXPL);
    sw_scrtie(m);                           /* ADD SCORE FOR KILLING TIE FIGHTER */
}

/* CPHTSA: SPACE LAZAR HIT ALIEN SHIP (the one in CL.AP) */
void sw_cphtsa(sw_machine *m)
{
    uint16_t x = R16(SW_CL_AP);             /* X-->ALIEN PARMS */
    if (R8(x + A_TYP) != 1) return;         /* NO DOUBLE JEOPARDY IF NOT ALIVE */
    if (R8(x + A_GLW) != 0) return;         /* ?OR IS IT GLOWING FROM A HIT? LEAVE ALONE FOR A WHILE */
    sw_rnd8(m);

    if (R8(x + A_TYP) == 1) W16(x + A_CHST, R16(x + A_CHST) | C_AH);   /* STATUS: THIS ALIEN DAMAGED */
    x = R16(SW_CL_AP);
    W8(x + A_DMC, R8(x + A_DMC) + 1);       /* RECORD SOME MORE DAMAGE */
    uint8_t before = R8(x + A_HTA);
    W8(x + A_HTA, before - 1);              /* DECREASE HITS LEFT */
    if ((int8_t)before <= 1) {              /* NO HITS LEFT - EXPLODE SPACE ALIEN (DEC, then LE) */
        xpsa(m);
        return;
    }

    W8(x + A_HTA, 0x05);                    /* KEEP DARTH ALIVE */
    sw_scrdarth(m);                         /* ADD SCORE FOR HITTING DARTH */
    W8(x + A_ROL, 0x1f);                    /* FORCE GLOW: TWO OR SO SECONDS */
    W8(x + A_GLW, 0x1f);

    /* THROW SHIP FOR A LOOP, INDEPENDENT OF PREVIOUS MOTION */
    x = SW_M_S1;
    uint16_t u = R16(R16(SW_CL_AP) + A_UVP);        /* ALIEN UVP */
    W16(u + M_VX, 0);
    W16(u + M_VY, 0);
    W16(u + M_VZ, 0);
    W8(TMPLP, 2);
    do {
        /* PORTION OF FORWARD VELOCITY: the nearer, the more */
        uint16_t d = (uint16_t)(((uint16_t)(0x4000 - R16(SW_CL_ADS)) >> 8) * R8(x + M_AX));
        W16(u + M_VX, (uint16_t)(int16_t)(int8_t)(uint8_t)((d >> 8) << 1) + R16(u + M_VX));
        W16(u + M_VY, random_kick(m, SW_RND_S1, x + M_BX) + R16(u + M_VY));
        W16(u + M_VZ, random_kick(m, SW_RND_S2, x + M_CX) + R16(u + M_VZ));
        x += 2;
        W8(TMPLP, R8(TMPLP) - 1);
    } while (!(R8(TMPLP) & 0x80));
    sw_doxmt(m, SW_IDASX);
}
