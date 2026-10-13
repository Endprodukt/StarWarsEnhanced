/* gmath.c - WSMATH.MAC, WSXMT.MAC, WSSITE.MAC: the small common routines.
 *
 * Math box calls, unit vector setup, the random number set, the request to
 * the sound board and the sight. After the 1983 source; comments in
 * capitals are the original's. */
#include "game/game.h"
#include "game/m6809.h"

/* MGOWT: START THE MATH BOX, AND WAIT (it is done at once here). */
void sw_mgowt(sw_machine *m, uint8_t a)
{
    W8(SW_M_PCGO, a);
    m->n_mbox++;
}

/* UNITV: SETUP UNIT VECTORS at U */
void sw_unitv(sw_machine *m, uint16_t u)
{
    W16(u + M_VX, 0);                       /* VELOCITY VECTORS */
    W16(u + M_VY, 0);
    W16(u + M_VZ, 0);
    W16(u + M_TX, 0);                       /* TRANSLATION COORDS */
    W16(u + M_TY, 0);
    W16(u + M_TZ, 0);
    W16(u + M_AY, 0);                       /* IDENTITY MATRIX */
    W16(u + M_AZ, 0);
    W16(u + M_BX, 0);
    W16(u + M_BZ, 0);
    W16(u + M_CX, 0);
    W16(u + M_CY, 0);
    W16(u + M_AX, 0x4000);                  /* IDENTITY VALUE FOR 1. */
    W16(u + M_BY, 0x4000);
    W16(u + M_CZ, 0x4000);
}

/* S1RL, S1PT, S1YW: ROLL, PITCH, YAW PLAYER SHIP */
void sw_s1rl(sw_machine *m) { W16(SW_M_BIC, SW_MSS1BC); sw_mgowt(m, SW_MSROLL); }
void sw_s1pt(sw_machine *m) { W16(SW_M_BIC, SW_MSS1BC); sw_mgowt(m, SW_MSPTCH); }
void sw_s1yw(sw_machine *m) { W16(SW_M_BIC, SW_MSS1BC); sw_mgowt(m, SW_MSYAW); }

/* RND8: GEN NON-VOLATILE SETS OF RANDOM NUMBERS */
void sw_rnd8(sw_machine *m)
{
    W16(SW_RND_S2, R16(SW_RND_S1));         /* MOVE RND1,2 TO RND2,3 */
    W8(SW_RND_S1, R8(SW_P_RND1));           /* GET NEW RANDOM BYTE */
}

/* DOXMT: A := ID OF REQUESTED SOUND, sent to the sound board. The native
 * sound takes the byte at once, so the port is never still full. */
void sw_doxmt(sw_machine *m, uint8_t a)
{
    /* ABOUT 50 CYCLE LATENCY FOR SOUND BOARD, USE SAFETY FACTOR OF 4: the
     * original waits up to 210 cycles for the board to take the byte before. */
    if ((R8(SW_R_STAS) & 0x80) && m->sound_wait) m->sound_wait(m->sound_user, 210);
    if (R8(SW_R_STAS) & 0x80) a = 0;        /* TOOK TOO LONG TO PROCESS, SO SOFTWARE RESET SOUND BOARD */
    W8(SW_W_XMTS, a);                       /* SHIP IT OUT */
    m->snd_q[m->snd_w++ % sizeof m->snd_q] = a;
    if (!m->sound_board) m->sound_out_full = 0;
}

/* VWCURS: VIEW CURSOR WITH ITS NORMAL COLOR */
void sw_vwcurs(sw_machine *m)
{
    TAG(SW_TAG_SITE, 1);
    VG((VGCTRQ << 8) + SW_VGCOPC);          /* SITE COLOR */
    sw_vwcurc(m);
}

/* VWCURC: VIEW CURSOR WITH YOUR OWN COLOR */
void sw_vwcurc(sw_machine *m)
{
    TAG(SW_TAG_SITE, 0);
    VG(SW_VJSITE);                          /* JOY MOVES CURSOR */
    VG(R16(SW_VGSITE));
}

/* One axis of VWPLAN: the sight's position (-128..127) times `scale`/256,
 * with the sign put back, as a 16-bit value. */
static uint16_t plan_shift(sw_machine *m, uint16_t rs, uint8_t scale)
{
    uint8_t a = R8(rs);
    if (a & 0x80) a = (uint8_t)-a;          /* ABSOLUTE VALUE */
    a = (uint8_t)((a * scale) >> 8);
    if (R8(rs) & 0x80) a = (uint8_t)-a;     /* RESTORE SIGN */
    return (uint16_t)(int16_t)(int8_t)a;    /* TFR A,B; SEX */
}

/* VWPLAN: VIEW PARTS OF X-WING VISIBLE TO PLAYER */
static void vwplan(sw_machine *m)
{
    uint16_t y = m->y;
    uint16_t d = plan_shift(m, SW_SI_RSX, 55 * 2);      /* ALLOW +-5.5 GRIDS MOVEMENT */
    W16(SW_LZ_GX, d);                       /* X DISTANCE OF SITE */
    d &= 0x1fff;                            /* NO BRITE */
    for (int i = 0; i < 5; i++) W16(y + 2 + 6 * i, d);  /* X VECTOR OFFSET */

    d = plan_shift(m, SW_SI_RSY, 40 * 2);   /* ALLOW +-4 GRIDS MOVEMENT */
    W16(SW_LZ_GY, d);                       /* Y DISTANCE OF SITE */
    d &= 0x1fff;                            /* VECTOR OPCODE */
    for (int i = 0; i < 5; i++) W16(y + 6 * i, d);      /* UP/DOWN Y VECTOR */

    W16(y + 4, SW_VJSTBM);                  /* PLAYERS HOOD, BOTTOM MIDDLE */
    W16(y + 10, SW_VJSTTR);                 /* PLAYERS GUN, TOP LEFT */
    W16(y + 16, SW_VJSTBL);                 /* PLAYERS GUN, BOTTOM LEFT */
    W16(y + 22, SW_VJSTTL);                 /* PLAYERS GUN, TOP RIGHT */
    W16(y + 28, SW_VJSTBR);                 /* PLAYERS GUN, BOTTOM RIGHT */
    for (int i = 0; i < 5; i++) {
        TAG(SW_TAG_PLANE, i);
        VGT(y + 6 * i); VGT(y + 6 * i + 2); VGT(y + 6 * i + 4);
    }
    m->y = (uint16_t)(y + 30);              /* BUMP PAST PREVIOUS CLUMP */
}

/* VWSITE: VIEW ALL SITE STUFF WITH NORMAL COLORS */
void sw_vwsite(sw_machine *m)
{
    sw_vwcurs(m);
    vwplan(m);
}

/* VWSITC: VIEW SITE STUFF WITH OWN CURSOR COLOR */
void sw_vwsitc(sw_machine *m)
{
    sw_vwcurc(m);
    vwplan(m);
}
