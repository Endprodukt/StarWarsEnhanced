/* The interrupt: clocks, switches, coins, pots and sight, VG restarts. */
#include "game/game.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static sw_romset rom;     /* zeros: the ROM tables are not needed here */
static sw_machine m;

static void boot(void)
{
    sw_machine_init(&m, &rom);
    sw_reset(&m);
    m.nvram[SW_OPTS0 + 1 - 0x4500] = 2;   /* $CMODE 2: 1 play per coin */
}

static void irqs(int n) { while (n--) sw_irq(&m); }

static void test_clocks(void)
{
    boot();
    uint8_t g0 = RAM8(&m, SW_GMSYNC);
    uint32_t v0 = m.vg_starts;
    irqs(120);
    CHECK((uint8_t)(RAM8(&m, SW_GMSYNC) - g0) == 10);    /* a game frame per 12 IRQs */
    CHECK(m.vg_starts - v0 == 20);                         /* a VG restart per 6 */
    CHECK(RAM8(&m, SW_SINTCT) == 120);

    /* 4 s of 16 ms ticks: GTIME+1 counts one, in BCD. */
    boot();
    irqs(250 * 4);
    CHECK(RAM8(&m, SW_GTIME + 1) == 0x01 && RAM8(&m, SW_GTIME + 2) == 0);
    irqs(250 * 4 * 9);
    CHECK(RAM8(&m, SW_GTIME + 1) == 0x10);                 /* 9 + 1 = 10 BCD */
    CHECK(RAM8(&m, SW_TIMER + 3) == 0x00 && RAM8(&m, SW_TIMER + 2) == 0x25);   /* 2500 = 25 00 */
}

static void test_buffers(void)
{
    boot();
    CHECK(RAM8(&m, SW_VGSYNC) == 0xff);
    irqs(6);
    /* The first restart gives the main line a buffer and shows the other. */
    uint8_t vs = RAM8(&m, SW_VGSYNC);
    CHECK(vs == 0x00 || vs == 0x14);
    uint16_t jmp = (uint16_t)((m.vgram[0] << 8) | m.vgram[1]);
    uint16_t shown = (uint16_t)((jmp & 0x1fff) << 1);
    CHECK((jmp & 0xe000) == 0xe000);
    CHECK(shown == (uint16_t)(((vs ? 0 : 0x14) << 8) + SW_VGVARB));
    /* Main line done: swaps again at the next restart. */
    RAM8(&m, SW_VGSYNC) = 0xff;
    irqs(6);
    CHECK(RAM8(&m, SW_VGSYNC) == (vs ? 0x00 : 0x14));
    /* Not done: no swap. */
    uint8_t keep = RAM8(&m, SW_VGSYNC);
    irqs(6);
    CHECK(RAM8(&m, SW_VGSYNC) == keep);
    /* The sight words and RTSL are in VG RAM. */
    CHECK(m.vgram[SW_VRSITE + 4] == 0xc0 && m.vgram[SW_VRSITE + 5] == 0x00);
}

static void test_fire(void)
{
    boot();
    irqs(4);
    CHECK(RAM8(&m, SW_VG_LON) == 0);
    m.in.in0 = (uint8_t)~SW_IN0_LFIRE;       /* left fire pressed */
    irqs(1);
    CHECK(RAM8(&m, SW_VG_LON) == 0);         /* one sample is not a press */
    irqs(1);
    CHECK(RAM8(&m, SW_VG_LON) & 0x80);       /* debounced: the lasers fire */
    RAM8(&m, SW_VG_LON) = 0;
    irqs(10);
    CHECK(RAM8(&m, SW_VG_LON) == 0);         /* held: no new edge */
    m.in.in0 = 0xff;
    irqs(3);
    m.in.in1 = (uint8_t)~SW_IN1_RTHUMB;      /* a thumb button fires too */
    irqs(2);
    CHECK(RAM8(&m, SW_VG_LON) & 0x10);
}

static void coin_pulse(int closed_irqs)
{
    m.in.in0 = (uint8_t)~SW_IN0_COINL;
    irqs(closed_irqs);
    m.in.in0 = 0xff;
    irqs(400);
}

/* After power-on $CNSTT is 0, which MOOLAH reads as "closed too long", and
 * MOOLAH only runs once a switch moves: so the first coin after power-on
 * re-arms the detector and is not counted. That is the source's logic;
 * to be confirmed against a MAME recording. The tests prime with one coin. */
static int insert_coin(int closed_irqs, int slam)
{
    boot();
    irqs(300);                               /* past start-up */
    coin_pulse(25);                          /* the first coin re-arms */
    uint8_t before = RAM8(&m, SW_SSCRDT);
    m.in.in0 = (uint8_t)~SW_IN0_COINL;
    irqs(closed_irqs);
    m.in.in0 = 0xff;
    if (slam) {
        irqs(20);
        m.in.in0 = (uint8_t)~SW_IN0_SLAM;
        irqs(3);
        m.in.in0 = 0xff;
    }
    irqs(400);
    return RAM8(&m, SW_SSCRDT) - before;
}

static void test_coins(void)
{
    CHECK(insert_coin(25, 0) == 1);          /* 100 ms: one credit */
    CHECK(insert_coin(2, 0) == 0);           /* 8 ms: too short */
    CHECK(insert_coin(300, 0) == 0);         /* 1.2 s: too long */
    CHECK(insert_coin(25, 1) == 0);          /* slammed during the post time */

    /* The first coin after power-on (see above). */
    boot();
    irqs(300);
    coin_pulse(25);
    CHECK(RAM8(&m, SW_SSCRDT) == 0);
    coin_pulse(25);
    CHECK(RAM8(&m, SW_SSCRDT) == 1);

    /* Two plays per coin. */
    boot();
    m.nvram[SW_OPTS0 + 1 - 0x4500] = 1;
    irqs(300);
    coin_pulse(25);
    coin_pulse(25);
    CHECK(RAM8(&m, SW_SSCRDT) == 2);
    /* The coin counter pulses (EMCNT1 is the left counter, latch 4680). */
    CHECK(RAM8(&m, SW_SCCTIM + 1) == 0);
}

static int16_t sight_x(void) { return (int16_t)(RAM16(&m, SW_VG_CX) << 3) >> 3; }   /* 13-bit */

static void test_sight(void)
{
    boot();
    m.in.adc[1] = 0x80;
    m.in.adc[0] = 0x80;
    irqs(6 * 80);                           /* past PT.DLY, settled */
    int16_t center = sight_x();
    m.in.adc[1] = 0xff;                      /* yoke hard right */
    int16_t last = center, moved = 0;
    for (int i = 0; i < 60; i++) {
        irqs(6);
        int16_t x = sight_x();
        CHECK(x >= last);                    /* slews one way, never back */
        if (x > last) moved++;
        last = x;
    }
    CHECK(moved > 5);
    CHECK(last > center + 200);              /* reached the right side */
    CHECK(last <= 0x70 * 4 + 8);             /* and stopped at VGCURR (+-128 -> +-512) */
    m.in.adc[1] = 0x00;                      /* hard left */
    irqs(6 * 120);
    CHECK(sight_x() < center - 200);
}

int main(void)
{
    test_clocks();
    test_buffers();
    test_fire();
    test_coins();
    test_sight();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("irq: all passed\n");
    return 0;
}
