/* mbox.h - the Star Wars math box (matrix processor) and divider.
 *
 * The hardware: 2K x 16 RAM (the 6809 sees it as 4K bytes at 0x5000,
 * high byte first), registers A, B, C and a 32-bit accumulator. Every
 * multiply step does
 *
 *     ACC = ACC + (A - B) * C          (Q2.14: 4000h * 4000h = 4000h)
 *
 * and leaves only their signs in A and B. The 6809 writes the block index
 * counter (BIC, 9 bits) and starts a program by writing its PC (address/4)
 * to MW0; the program runs until HALT. SWMP.DOC documents each program as
 * a formula, SWMP.MAC is the micro-code.
 *
 * Two implementations, held to each other by tests/test_mbox.c:
 *   mb_prog_*()        generated from SWMP.MAC by tools/mbasm.py, one C
 *                      function per program: what the game uses
 *   sw_mbox_run_prom() a micro-code interpreter (after MAME's
 *                      starwars_state::run_mproc, BSD-3-Clause, Steve Baines
 *                      and Frank Palazzolo): the reference
 *
 * Fixed point: unit vectors and sines are Q2.14 (4000h = 1.0); positions
 * are 16-bit and wrap.
 */
#ifndef SWE_MBOX_H
#define SWE_MBOX_H

#include <stdint.h>

#define SW_MBRAM_BASE 0x5000      /* where the 6809 sees the math box RAM */
#define SW_MBRAM_SIZE 0x1000

/* Directly addressed words (SWMP.DOC / TCODE2.MAC), as RAM word indexes. */
enum {
    MB_XP = 0x00, MB_YP, MB_ZP, MB_AX, MB_AY, MB_AZ, MB_BX, MB_BY, MB_BZ,
    MB_CX, MB_CY, MB_CZ, MB_XT, MB_YT, MB_ZT, MB_ZERO, MB_ONE, MB_SIN, MB_COS,
    MB_HALF, MB_AX1, MB_AY1, MB_AZ1, MB_VX, MB_BX1, MB_BY1, MB_BZ1, MB_VY,
    MB_CX1, MB_CY1, MB_CZ1, MB_VZ, MB_XT1, MB_YT1, MB_ZT1, MB_FX,
    MB_AX2, MB_AY2, MB_AZ2, MB_FY, MB_BX2, MB_BY2, MB_BZ2, MB_FZ,
    MB_CX2, MB_CY2, MB_CZ2, MB_THRUST, MB_XT2, MB_YT2, MB_ZT2, MB_GRAV,
    MB_C40, MB_C80, MB_SCALE, MB_XSCALE, MB_XPS, MB_YPS, MB_ZPS, MB_XCS,
    MB_QW, MB_T, MB_TAX, MB_TBX
};
/* Second names of shared words. */
#define MB_PX   MB_QW
#define MB_PZ1  MB_XPS
#define MB_PY1  MB_YPS
#define MB_PZ2  MB_ZPS
#define MB_PY2  MB_XCS
#define MB_ASPD MB_TBX

typedef struct sw_mbox {
    uint8_t  ram[SW_MBRAM_SIZE];  /* byte image, as at 0x5000 (big-endian words) */
    int16_t  a, b, c;
    int32_t  acc;
    uint16_t bic;                 /* 9 bits */
} sw_mbox;

/* One RAM word by word index (0..7FF). */
static inline uint16_t mb_rd(const sw_mbox *m, uint16_t ma)
{
    const uint8_t *p = m->ram + ((ma & 0x7ff) << 1);
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline void mb_wr(sw_mbox *m, uint16_t ma, uint16_t v)
{
    uint8_t *p = m->ram + ((ma & 0x7ff) << 1);
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* The multiply step: ACC += (A - B) * C, then A and B keep only their sign.
 * The serial multiplier's pipeline puts the product two bits up (MAME); the
 * 32-bit accumulator wraps. */
static inline void mb_mac(sw_mbox *m, int16_t c)
{
    m->c = c;
    int64_t p = (int64_t)((int32_t)m->a - (int32_t)m->b) * 2 * (int64_t)c * 2;
    m->acc = (int32_t)(uint32_t)((uint64_t)(uint32_t)m->acc + (uint64_t)p);
    m->a = m->a < 0 ? -1 : 0;
    m->b = m->b < 0 ? -1 : 0;
}

/* Words by the 6809's address (0x5000-0x5FFF), as the game code uses them. */
static inline uint16_t mb_get(const sw_mbox *m, uint16_t addr)
{
    return (uint16_t)((m->ram[(addr - SW_MBRAM_BASE) & 0xfff] << 8) | m->ram[(addr - SW_MBRAM_BASE + 1) & 0xfff]);
}

static inline void mb_set(sw_mbox *m, uint16_t addr, uint16_t v)
{
    m->ram[(addr - SW_MBRAM_BASE) & 0xfff] = (uint8_t)(v >> 8);
    m->ram[(addr - SW_MBRAM_BASE + 1) & 0xfff] = (uint8_t)v;
}

/* --- the programs (mbox_programs.c, generated) ---------------------------- */

typedef void (*sw_mbox_prog)(sw_mbox *m);

typedef struct sw_mbox_entry {
    const char  *name;
    uint8_t      pc;       /* what the 6809 writes to MW0 */
    sw_mbox_prog run;
} sw_mbox_entry;

extern const sw_mbox_entry sw_mbox_entries[];   /* ends with a NULL name */

void mb_prog_ROLL(sw_mbox *m);      /* PC 00: roll 3 unit vectors at BIC */
void mb_prog_PITCH(sw_mbox *m);     /* PC 0E */
void mb_prog_YAW(sw_mbox *m);       /* PC 1C */
void mb_prog_PRESUB(sw_mbox *m);    /* PC 2A: ship 1 looks at a point, no wrap */
void mb_prog_CONCAT(sw_mbox *m);    /* PC 40: matrix for ship 1 looking at ship 2 */
void mb_prog_POSTADD(sw_mbox *m);   /* PC 50: ship 1 looks at ship 2's points */
void mb_prog_POST2(sw_mbox *m);     /* PC 60: ship 1 frame -> universe */
void mb_prog_PRE2(sw_mbox *m);      /* PC 67: ship 1 looks at the universe, wraps */
void mb_prog_DNLD1(sw_mbox *m);     /* PC 77: matrix + position at BIC -> ship 1 */
void mb_prog_DNLD2(sw_mbox *m);     /* PC 80: ... -> ship 2 */
void mb_prog_PERS(sw_mbox *m);      /* PC 86: YP, ZP times inverse X */
void mb_prog_PER2(sw_mbox *m);      /* PC AE: perspective for terrain objects */
void mb_prog_PER2I(sw_mbox *m);     /* PC B0 */

/* The PCs the game writes to MW0 (M$xxx in WSGLOB.MAC). */
enum {
    SW_MB_ROLL = 0x00, SW_MB_PTCH = 0x0E, SW_MB_YAW = 0x1C, SW_MB_PSUB = 0x2A,
    SW_MB_CNCT = 0x40, SW_MB_PAD = 0x50, SW_MB_S1UN = 0x60, SW_MB_PSB2 = 0x67,
    SW_MB_DLD1 = 0x77, SW_MB_DLD2 = 0x80, SW_MB_PERS = 0x86, SW_MB_PRS2 = 0xAE,
    SW_MB_PR2I = 0xB0
};

/* Start the program at pc as a write to MW0 would. Returns 0, or -1 if pc
 * is not the start of a program (the game never does that). */
int sw_mbox_go(sw_mbox *m, uint8_t pc);

/* The game's way: set BIC, start, wait (MGOWT). */
static inline void sw_mbox_call(sw_mbox *m, uint16_t bic, uint8_t pc)
{
    m->bic = bic & 0x1ff;
    sw_mbox_go(m, pc);
}

/* The reference: run the micro-code words (the four PROMs joined, 1024
 * entries) from pc*4 until HALT. Returns the micro-steps executed. */
int sw_mbox_run_prom(sw_mbox *m, const uint16_t prom[1024], uint8_t pc);

/* Join the four 1K x 4 PROMs (136021-110, -111, -112, -113, as in
 * sw_romset.mbox_prom) into 16-bit words. */
void sw_mbox_join_proms(const uint8_t proms[0x1000], uint16_t out[1024]);

/* Run every program both ways (generated code and the interpreter on the
 * given PROM words) from `trials` pseudo-random states each. Returns the
 * number of programs that disagree: 0 means the native math box is the
 * board's. swenhanced runs this against the cabinet's own PROMs. */
int sw_mbox_selftest(const uint16_t prom[1024], int trials);

/* --- the divider (4704-4707 write, 4700/4701 read) ------------------------ */

typedef struct sw_divider {
    uint16_t divisor, dividend;
    uint16_t dvd_shift, quotient;
} sw_divider;

/* Register writes as the 6809 makes them: offset 4 DVSRH (also loads the
 * shift register and clears the quotient), 5 DVSRL (starts), 6 DVDDH,
 * 7 DVDDL. */
void     sw_divider_write(sw_divider *d, int offset, uint8_t data);
uint16_t sw_divider_quotient(const sw_divider *d);   /* REH:REL */

/* The whole sequence the game uses: dividend high/low, divisor high/low. */
uint16_t sw_divide(sw_divider *d, uint16_t dividend, uint16_t divisor);

#endif
