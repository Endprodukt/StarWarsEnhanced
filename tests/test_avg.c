/* The AVG interpreter against display lists built with the encodings of
 * WSVGMC.MAC (the game's own VG macros). */
#include "avg/avg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 0.01f)

static uint8_t space[SW_AVG_SPACE_SIZE];
static unsigned at;

static void w(unsigned word) { space[at] = (uint8_t)(word >> 8); space[at + 1] = (uint8_t)word; at += 2; }

/* The macros, as WSVGMC.MAC writes them. */
static void VCTR(int dx, int dy, int zz) { w((unsigned)dy & 0x1fff); w(((unsigned)zz << 13) | ((unsigned)dx & 0x1fff)); }
static void SVEC(int dx, int dy, int zz) { w(0x4000 | ((unsigned)zz << 5) | (((unsigned)(dx / 2)) & 0x1f) | (((unsigned)(dy * 0x80)) & 0x1f00)); }
static void COLOR(int col, int lum) { w(0x6000 | (((unsigned)col & 7) << 8) | ((unsigned)lum & 0xff)); }
static void FUZZ(int col, int lum) { w(0x6800 | (((unsigned)col & 7) << 8) | ((unsigned)lum & 0xff)); }
static void SCAL(int s, int ls) { w(0x7000 | (unsigned)ls | ((unsigned)s << 8)); }
static void CNTR(void) { w(0x8040); }
static void HALT(void) { w(0x2000); }
static void JSRL(unsigned byte_addr) { w(((byte_addr / 2) & 0x1fff) | 0xa000); }
static void RTSL(void) { w(0xc000); }
static void JMPL(unsigned byte_addr) { w(((byte_addr / 2) & 0x1fff) | 0xe000); }

static const float FULL = 255.0f / 256.0f;   /* linear scale 0 */

static void test_long_and_short(void)
{
    sw_avg_list l = { 0 };
    memset(space, 0, sizeof space);
    at = 0;
    CNTR();
    SCAL(0, 0);
    COLOR(4, 0x80);
    VCTR(100, -50, 7);
    VCTR(0, 200, 0);        /* a move */
    SVEC(-10, 6, 5);
    HALT();

    CHECK(sw_avg_run(space, 0, &l, 0) == SW_AVG_OK);
    CHECK(l.count == 2);
    if (l.count == 2) {
        CHECK(NEAR(l.lines[0].x0, 0) && NEAR(l.lines[0].y0, 0));
        CHECK(NEAR(l.lines[0].x1, 100 * FULL) && NEAR(l.lines[0].y1, -50 * FULL));
        CHECK(l.lines[0].color == 4 && l.lines[0].zz == 7);
        CHECK(l.lines[0].intensity == sw_avg_intensity(7, 0x80));
        CHECK(NEAR(l.lines[1].x0, 100 * FULL) && NEAR(l.lines[1].y0, 150 * FULL));
        CHECK(NEAR(l.lines[1].x1, 90 * FULL) && NEAR(l.lines[1].y1, 156 * FULL));
        CHECK(l.lines[1].zz == 5);
    }
    sw_avg_list_free(&l);
}

static void test_scale_and_fuzz(void)
{
    sw_avg_list l = { 0 };
    memset(space, 0, sizeof space);
    at = 0;
    SCAL(2, 0x80);          /* 1/4 binary, about 1/2 linear */
    FUZZ(7, 0xff);
    VCTR(400, 0, 7);
    HALT();
    CHECK(sw_avg_run(space, 0, &l, 0) == SW_AVG_OK);
    CHECK(l.count == 1);
    if (l.count == 1) {
        CHECK(NEAR(l.lines[0].x1, 400.0f * (127.0f / 256.0f) / 4.0f));
        CHECK(l.lines[0].defocus == 1);
        CHECK(l.lines[0].intensity == 255);
    }
    sw_avg_list_free(&l);
}

static void test_subroutines(void)
{
    sw_avg_list l = { 0 };
    memset(space, 0, sizeof space);
    /* A "character" in ROM: a short stroke, then return. */
    at = 0x3100;
    VCTR(10, 0, 7);
    RTSL();
    /* The buffer at 0x0070 calls it three times; 0 jumps to the buffer. */
    at = 0x0000;
    JMPL(0x0070);
    at = 0x0070;
    CNTR();
    SCAL(0, 0);
    COLOR(2, 0x80);
    JSRL(0x3100);
    JSRL(0x3100);
    JSRL(0x3100);
    HALT();
    CHECK(sw_avg_run(space, 0, &l, 0) == SW_AVG_OK);
    CHECK(l.count == 3);
    if (l.count == 3) CHECK(NEAR(l.lines[2].x1, 30 * FULL));
    sw_avg_list_free(&l);
}

static void test_runaway(void)
{
    sw_avg_list l = { 0 };
    memset(space, 0, sizeof space);
    at = 0;
    JMPL(0);                /* never halts */
    CHECK(sw_avg_run(space, 0, &l, 1000) == SW_AVG_LIMIT);
    sw_avg_list_free(&l);
}

static void test_intensity_network(void)
{
    /* ZZ 7 at full COLOR intensity is full scale; ZZ 4 (the 12k resistor)
     * is 1/1.755 of it; ZZ 0 is dark. */
    CHECK(sw_avg_intensity(7, 0xff) == 255);
    CHECK(sw_avg_intensity(4, 0xff) == 145);
    CHECK(sw_avg_intensity(0, 0xff) == 0);
    CHECK(sw_avg_intensity(7, 0x80) == 128);
}

int main(void)
{
    test_long_and_short();
    test_scale_and_fuzz();
    test_subroutines();
    test_runaway();
    test_intensity_network();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("avg: all passed\n");
    return 0;
}
