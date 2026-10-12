/* The math box: the generated programs against the micro-code interpreter
 * on the assembled PROM image (identical to the board's PROMs, see
 * tools/mbasm.py --check), the documented formulas, and the divider. */
#include "mbox/mbox.h"
#include "mbox_prom.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void randomize(sw_mbox *m)
{
    for (int i = 0; i < SW_MBRAM_SIZE; i++) m->ram[i] = (uint8_t)rnd();
    m->a = (int16_t)rnd();
    m->b = (int16_t)rnd();
    m->c = (int16_t)rnd();
    m->acc = (int32_t)rnd();
    m->bic = (uint16_t)(rnd() & 0x1ff);
}

static int same(const sw_mbox *x, const sw_mbox *y)
{
    return memcmp(x->ram, y->ram, sizeof x->ram) == 0 && x->a == y->a && x->b == y->b &&
           x->c == y->c && x->acc == y->acc && x->bic == y->bic;
}

/* Every program, from random states, ends in the same state both ways. */
static void test_generated_vs_interpreter(void)
{
    static sw_mbox m1, m2;
    int programs = 0;
    for (const sw_mbox_entry *e = sw_mbox_entries; e->name; e++, programs++) {
        int bad = 0;
        for (int trial = 0; trial < 3000 && !bad; trial++) {
            randomize(&m1);
            m2 = m1;
            e->run(&m1);
            sw_mbox_run_prom(&m2, mbox_prom, e->pc);
            if (!same(&m1, &m2)) {
                printf("FAIL %s differs from the micro-code (trial %d)\n", e->name, trial);
                bad = 1;
                failures++;
            }
        }
        /* and the game's way in: by PC */
        randomize(&m1);
        m2 = m1;
        CHECK(sw_mbox_go(&m1, e->pc) == 0);
        sw_mbox_run_prom(&m2, mbox_prom, e->pc);
        CHECK(same(&m1, &m2));
    }
    CHECK(programs == 29);
    CHECK(sw_mbox_go(&m1, 0x01) == -1);
}

/* The constants the game sets up (IMATH, WSMAIN.MAC). */
static void init_constants(sw_mbox *m)
{
    memset(m, 0, sizeof *m);
    mb_wr(m, MB_ZERO, 0x0000);
    mb_wr(m, MB_ONE, 0x4000);
    mb_wr(m, MB_HALF, 0xE000);
    mb_wr(m, MB_C40, 0x0040);
    mb_wr(m, MB_C80, 0x0080);
}

static int16_t q14(double v) { return (int16_t)lround(v * 16384.0); }

/* ROLL as SWMP.DOC writes it: Y' = Y cos - Z sin, Z' = Y sin + Z cos, rounded. */
static void test_roll_formula(void)
{
    static sw_mbox m;
    init_constants(&m);
    double a = 0.3;
    mb_wr(&m, MB_SIN, (uint16_t)q14(sin(a)));
    mb_wr(&m, MB_COS, (uint16_t)q14(cos(a)));
    /* identity matrix in block 0x20 (rows of 4 words: X Y Z spare) */
    uint16_t blk = 0x20;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            mb_wr(&m, (uint16_t)(((blk + r) << 2) | c), r == c ? 0x4000 : 0);
    m.bic = blk;
    mb_prog_ROLL(&m);
    CHECK(m.bic == blk + 2);         /* IBC twice, the last word only halts */
    for (int r = 0; r < 3; r++) {
        double y = r == 1 ? 1.0 : 0.0, z = r == 2 ? 1.0 : 0.0;
        double ey = y * cos(a) - z * sin(a), ez = y * sin(a) + z * cos(a);
        int16_t gy = (int16_t)mb_rd(&m, (uint16_t)(((blk + r) << 2) | 1));
        int16_t gz = (int16_t)mb_rd(&m, (uint16_t)(((blk + r) << 2) | 2));
        CHECK(abs(gy - q14(ey)) <= 1);
        CHECK(abs(gz - q14(ez)) <= 1);
        CHECK(mb_rd(&m, (uint16_t)(((blk + r) << 2) | 0)) == (r == 0 ? 0x4000 : 0));
    }
}

/* PERS: YP = YP * XP, ZP = ZP * XP in Q2.14. */
static void test_pers_formula(void)
{
    static sw_mbox m;
    init_constants(&m);
    mb_wr(&m, MB_XP, (uint16_t)q14(0.5));
    mb_wr(&m, MB_YP, (uint16_t)q14(0.75));
    mb_wr(&m, MB_ZP, (uint16_t)q14(-1.0));
    mb_prog_PERS(&m);
    CHECK((int16_t)mb_rd(&m, MB_YP) == q14(0.375));
    CHECK((int16_t)mb_rd(&m, MB_ZP) == q14(-0.5));
}

/* PRE2: positions wrap, the result is halved, then turned by ship 1. */
static void test_pre2_formula(void)
{
    static sw_mbox m;
    init_constants(&m);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            mb_wr(&m, (uint16_t)(MB_AX1 + i * 4 + j), i == j ? 0x4000 : 0);
    mb_wr(&m, MB_SCALE, 0x4000);
    mb_wr(&m, MB_XT1, 0x7000);       /* ship at x = 7000h */
    uint16_t blk = 0x40;
    mb_wr(&m, (uint16_t)(blk << 2 | 0), 0x9000);   /* point at x = 9000h: 2000h ahead, wrapped */
    mb_wr(&m, (uint16_t)(blk << 2 | 1), 0x0100);
    mb_wr(&m, (uint16_t)(blk << 2 | 2), 0xff00);
    m.bic = blk;
    mb_prog_PRE2(&m);
    CHECK((int16_t)mb_rd(&m, MB_XP) == 0x1000);
    CHECK((int16_t)mb_rd(&m, MB_YP) == 0x0080);
    CHECK((int16_t)mb_rd(&m, MB_ZP) == -0x0080);
    CHECK(m.bic == blk + 1);
}

static void test_divider(void)
{
    sw_divider d = { 0 };
    /* The quotient is dividend/divisor in Q2.14 (4000h = 1). */
    CHECK(sw_divide(&d, 0x2000, 0x4000) == 0x2000);
    CHECK(sw_divide(&d, 0x1000, 0x4000) == 0x1000);
    CHECK(sw_divide(&d, 0x0400, 0x1000) == 0x1000);
    CHECK(sw_divide(&d, 0x0000, 0x1234) == 0x0000);
    /* Register by register, as the 6809 writes 16-bit values (high first). */
    sw_divider e = { 0 };
    sw_divider_write(&e, 6, 0x10);
    sw_divider_write(&e, 7, 0x00);
    sw_divider_write(&e, 4, 0x40);
    sw_divider_write(&e, 5, 0x00);
    CHECK(sw_divider_quotient(&e) == 0x1000);
}

static void test_join_proms(void)
{
    static uint8_t proms[0x1000];
    uint16_t words[1024];
    for (int i = 0; i < 1024; i++) {
        proms[0x000 + i] = (uint8_t)(mbox_prom[i] >> 12);
        proms[0x400 + i] = (uint8_t)((mbox_prom[i] >> 8) & 0xf);
        proms[0x800 + i] = (uint8_t)((mbox_prom[i] >> 4) & 0xf);
        proms[0xc00 + i] = (uint8_t)(mbox_prom[i] & 0xf);
    }
    sw_mbox_join_proms(proms, words);
    CHECK(memcmp(words, mbox_prom, sizeof words) == 0);
}

int main(void)
{
    test_generated_vs_interpreter();
    test_roll_formula();
    test_pers_formula();
    test_pre2_formula();
    test_divider();
    test_join_proms();
    CHECK(sw_mbox_selftest(mbox_prom, 20) == 0);
    {
        static uint16_t broken[1024];
        memcpy(broken, mbox_prom, sizeof broken);
        broken[0x002] ^= 0x0001;              /* ROLL reads HALF instead of COS */
        CHECK(sw_mbox_selftest(broken, 20) == 1);
    }
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("mbox: all passed\n");
    return 0;
}
