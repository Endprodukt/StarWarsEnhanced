/* m6809.h - how the ported routines write what the 6809 code wrote.
 *
 * The ports keep the original's memory accesses one for one, through the
 * bus (RAM, math box, VG RAM, ROM tables and I/O alike), so each line of C
 * stands next to its line of the 1983 source. Private to src/game. */
#ifndef SWVP_M6809_H
#define SWVP_M6809_H

#include "core/machine.h"

#include <string.h>

#define R8(a)     sw_rd8(m, (uint16_t)(a))
#define R16(a)    sw_rd16(m, (uint16_t)(a))
#define W8(a, v)  sw_wr8(m, (uint16_t)(a), (uint8_t)(v))
#define W16(a, v) sw_wr16(m, (uint16_t)(a), (uint16_t)(v))

/* STD (Y)++ : the next word of the display list (Y is m->y). */
#define VG(w)     do { sw_wr16(m, m->y, (uint16_t)(w)); VGT(m->y); m->y = (uint16_t)(m->y + 2); } while (0)

/* What the words written from here on belong to (core/tags.h), and the
 * note of it for a word written at address a. */
#define TAG(section, key) (m->tag = SW_TAG(section, key), memset(&m->note, 0, sizeof m->note))
#define VGT(a)    (m->vgtag[((uint16_t)(a) >> 1) % 0x1800] = m->tag, m->vgstart[((uint16_t)(a) >> 1) % 0x1800] = 0, \
                   m->vgnote[((uint16_t)(a) >> 1) % 0x1800] = m->note)

/* The overlay bank of the program ROM (6000-7FFF with W.BANK set), where
 * the "J" routines of the source and their tables live: read there
 * whatever the bank latch says. */
static inline uint8_t sw_ovl8(const sw_machine *m, uint16_t a)
{
    if (!m->rom) return 0;
    return (a >= 0x6000 && a < 0x8000) ? m->rom->main[0x10000 + (a - 0x6000)] : m->rom->main[a];
}
#define O8(a)     sw_ovl8(m, (uint16_t)(a))
#define O16(a)    ((uint16_t)((sw_ovl8(m, (uint16_t)(a)) << 8) | sw_ovl8(m, (uint16_t)((a) + 1))))

/* An alien's record (A$xx) */
#define A_UVP  0      /* UNIT VECTOR POINTER */
#define A_BIC  2      /* BLOCK INDEX COUNTER, LO BANK */
#define A_TYP  3      /* TYPE (0.DEAD; 3.EXPLODING) */
#define A_PIC  4      /* PICTURE # */
#define A_DIE  5      /* PROCESS OF DIEING */
#define A_GLW  6      /* PROCESS OF GLOWING */
#define A_HTA  7      /* HITS AVAILABLE BEFORE DIEING */
#define A_DMC  8      /* CURRENT DAMAGE COUNT */
#define A_ROL  9      /* FORCE ROLL OF SHIP */
#define A_AIM  10     /* AIMED AT PLAYER */
#define A_TRGT 11     /* =0.GO FOR HOME BASE; =-1.PLAYER */
#define A_CHPC 13     /* PC FOR CURRENT CHOREOG */
#define A_CHOP 15     /* OP CODE */
#define A_CHTM 16     /* TIMER */
#define A_CHTW 17     /* TWIRL FLAGS */
#define A_CHMV 18     /* TW+1, MOVE FLAGS */
#define A_CHCN 19     /* CONTROL MASK, UNTIL STRUCTURE */
#define A_CHST 21     /* STATUS TESTED BY CONTROL */
#define A_CHRT 23     /* RETURN PC, SUBROUTINE */
#define A_IZE  25     /* # OF BYTES PER RECORD */

/* ABSD (MOP69.MAC): TSTA, and NEGD when minus. */
static inline uint16_t sw_absd(uint16_t d) { return (d & 0x8000) ? (uint16_t)-d : d; }

/* ADCA then DAA, for valid BCD operands; *carry in and out. */
static inline uint8_t sw_bcd_adc(uint8_t a, uint8_t b, int *carry)
{
    unsigned lo = (a & 0x0fu) + (b & 0x0fu) + (unsigned)*carry;
    unsigned hi = (a >> 4) + (b >> 4);
    if (lo > 9) { lo -= 10; hi++; }
    *carry = hi > 9;
    if (hi > 9) hi -= 10;
    return (uint8_t)((hi << 4) | lo);
}

/* Addresses of tables that are local labels in the source, so not in the
 * load map: read from the operands of the instructions that use them in
 * the 22-June-1983 ROMs (the routine and offset are given). */
#define SW_L_MESSZ   0xD6      /* TCMES: number of messages (DOMESS: CMPB #MESSZ) */
#define SW_L_TMESS   0xE894    /* TCMES TMESS, message text pointers (MESOUT+1) */
#define SW_L_TMESXY  0xEA50    /* TCMES TMESXY, x,y of each message (MOUT) */
#define SW_L_TMESCL  0xEDA8    /* TCMES TMESCL, color command of each (MCOLR) */
#define SW_L_TSPMX   0xEA40    /* TCMES TSPMX, x of the receding lines (VWSPMS: E99E + 2*SPMS1) */
#define SW_L_THSY1   0xCA64    /* TCHSCR THSY1, high score rows, half screen */
#define SW_L_THSY2   0xCA78    /* TCHSCR THSY2, whole screen */
#define SW_L_TINTXY  0xCBA4    /* TCHSCR TINTXY, the alphabet for initials */
#define SW_L_TINTZ   (SW_L_TINTXY + 29 * 4)
#define SW_L_TDFLT   0xC73B    /* TCEROM T.DFLT, EEROM defaults from area 2 on */
#define SW_L_TAREA   0xC7B6    /* TCEROM T.AREA, start of each EEROM area */

/* Routines of WSMAIN that are local labels: read off the JSRs of the
 * phases that call them (tools/dis6809.py; the phase table TPHASE is at
 * 6044). */
#define SW_L_VIEW    0x72C7    /* VIEW: the picture of the space, ground and trench phases */
#define SW_L_STWSP1  0x6DD2    /* STWSP1: TWIRL PLAYER */
#define SW_L_STWSP2  0x6DFA    /* STWSP2 */
#define SW_L_SMVSP1  0x6F5F    /* SMVSP1, SMVSP2: MOVE PLAYER */
#define SW_L_PHIS0D  0x6C76    /* PHIS0D: SPACE TO DIE, TRANSITION */
#define SW_L_VWDTHA  0x7765    /* VWDTHA: VIEW DETH STAR, VERSION A (MINIATURE) */
#define SW_L_SNVW    0x786A    /* SNVW: the ships, seen from the player's */
#define SW_L_TSTLAZ  0xAE60    /* TSTLAZ: PREPARE FOR LAZAR HITS (WSLAZR) */
#define SW_L_VWLAZ   0xAEBD    /* VWLAZ */
#define SW_L_CLSLZ   0xB32B    /* CLSLZ: PROCESS COLLISIONS in space */
#define SW_L_VWGUN   0xAAE4    /* VWGUN (WSGUNS) */

/* A subroutine of the original that is not ported yet (core/machine.h,
 * original_call): the game then needs the ROM's code at hand. */
#define ORIG(addr) do { if (m->original_call) m->original_call(m->original_user, (uint16_t)(addr)); } while (0)

/* D-page temporaries (TEMP macro, WSCOMN.MAC): DPRAM+1 on. */
#define SW_L_TMPCOL  0x4801    /* TCHSCR: TEMP COL,2 */
#define SW_L_TMPABS  0x4801    /* TCHSCR: TEMP ABS,2 */
#define SW_L_TMPCNT  0x4801    /* WSOBJ: TEMP CNT,1 */

/* Offsets into a set of unit vectors (M$xx). */
#define M_AX (-0x10)
#define M_AY (-0x0e)
#define M_AZ (-0x0c)
#define M_BX (-0x08)
#define M_BY (-0x06)
#define M_BZ (-0x04)
#define M_CX 0x00
#define M_CY 0x02
#define M_CZ 0x04
#define M_TX 0x08
#define M_TY 0x0a
#define M_TZ 0x0c
#define M_VX (-0x0a)
#define M_VY (-0x02)
#define M_VZ 0x06

/* GUN block (G$xx): a shot */
#define G_UVP  0      /* POINTER TO ITS POSITION IN THE MATH BOX */
#define G_BIC  2
#define G_TYP  3
#define G_MOV  4
#define G_TMR  5
#define G_IZE  6

/* RHEOSTAT block (RH$xx) */
#define RH_LO  0
#define RH_LT  1
#define RH_SP  2
#define RH_ST  3
#define RH_NML 4
#define RH_POS 5
#define RH_RAT 7
#define RH_RSD 8

/* Colors and the commands built from them (WSVGMC.MAC, TCMES.MAC) */
#define VGCBLU 1
#define VGCGRN 2
#define VGCTRQ 3
#define VGCRED 4
#define VGCPRP 5
#define VGCYLW 6
#define VGCWHT 7
#define VGCOLR 0x6080

#endif
