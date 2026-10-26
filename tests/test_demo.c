/* test_demo - the attract mode's demo flight (platform/demo.h): short
 * pieces of one game between the attract mode's pages, each from another
 * view; the game goes on from piece to piece through space, the surface
 * and the trench, flown as a player would (what hits it costs shields, but
 * little hits it), and ends as the torpedoes go into the exhaust port: the
 * Death Star's end and GAME OVER are never seen. A ship lost ends its game
 * too, at once. After every piece the machine is as it was before it (RAM,
 * NOVRAM, credits). Needs the ROM set (skipped without). */
#include "core/machine.h"
#include "core/sw_symbols.h"
#include "game/game.h"
#include "platform/demo.h"
#include "render/enhance.h"
#include "rom/romset.h"

#include <stdio.h>
#include <string.h>

static sw_romset rom;
static sw_machine m;
static sw_demo demo;

static uint8_t phase(void) { return m.ram[SW_PHASE - 0x4800]; }

/* One interrupt of the program: the demo, then the game. */
static void irq(void)
{
    m.in.in0 = 0xff;
    m.in.in1 = 0xff;
    m.in.adc[0] = m.in.adc[1] = 0x80;
    sw_demo_tick(&demo, &m);
    sw_irq(&m);
    sw_main_poll(&m);
}

static int over_seen(void)
{
    uint8_t p = phase();
    return (p >= SW_PHSDX1 && p <= SW_PHSDX3 + 1) || p == SW_PHSEGM || p == SW_PHSEGM + 1;
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_demo: no ROM set, skipped\n");
        return 0;
    }
    sw_machine_init(&m, &rom);
    m.yoke_exact = 1;
    m.steady = 1;
    sw_reset(&m);
    demo.enabled = 1;
    int fail = 0;

    /* 600 s of the attract mode: its pages with pieces of a game between */
    static uint8_t ram[sizeof m.ram], nvram[sizeof m.nvram];
    int pieces = 0, games = 0, was = 0, last_view = -1, same_view = 0, shown = 0, port = 0, hits = 0, worst = 0;
    int space = 0, ground = 0, trench = 0, gas = -1;
    long began = 0, longest = 0;
    for (long i = 0; i < 600L * 246; i++) {
        /* (the piece's start and end looked at between the demo and the game) */
        m.in.in0 = 0xff;
        m.in.in1 = 0xff;
        m.in.adc[0] = m.in.adc[1] = 0x80;
        sw_demo_tick(&demo, &m);
        if (demo.active && !was) {
            pieces++;
            began = i;
            memcpy(ram, demo.saved->ram, sizeof ram);
            memcpy(nvram, demo.saved->nvram, sizeof nvram);
            if (demo.view == last_view) same_view++;
            last_view = demo.view;
        }
        if (demo.active) {
            uint8_t p = phase();
            space |= p == SW_PHSSP1 + 1;
            ground |= p == SW_PHSGD + 1;
            trench |= p == SW_PHSBS + 1;
            if (over_seen()) shown++;
            if (m.ram[SW_PT_LIV - 0x4800]) port = 1;
            int g = (int8_t)m.ram[SW_S_GAS - 0x4800];
            if (p >= SW_PHSSP1 && gas >= 0 && g < gas) hits++;
            if (p >= SW_PHSSP1) gas = g;
        }
        if (!demo.active && was) {
            if (i - began > longest) longest = i - began;
            if (memcmp(ram, m.ram, sizeof ram) != 0 || memcmp(nvram, m.nvram, sizeof nvram) != 0) {
                printf("the machine is not as it was after piece %d\n", pieces);
                fail = 1;
            }
            if (!demo.game) {
                games++;
                if (hits > worst) worst = hits;
                hits = 0;
                gas = -1;
            }
        }
        was = demo.active;
        sw_irq(&m);
        sw_main_poll(&m);
    }
    printf("  %d pieces (at most %.1f s), %d games ended, at most %d hits in one; space %d surface %d trench %d, port %d\n",
           pieces, longest / 246.0, games, worst, space, ground, trench, port);
    if (pieces < 12) { printf("too few pieces\n"); fail = 1; }
    if (longest > (long)(14.2 * 246)) { printf("a piece too long\n"); fail = 1; }
    if (same_view) { printf("%d pieces from the same view as the one before\n", same_view); fail = 1; }
    if (!space || !ground || !trench) { printf("the game did not go on through the flight\n"); fail = 1; }
    if (!games || !port) { printf("no game ended at the exhaust port\n"); fail = 1; }
    if (worst > 8) { printf("hit too often\n"); fail = 1; }
    if (shown) { printf("the Death Star's end or GAME OVER seen in %d interrupts\n", shown); fail = 1; }

    /* the ship lost in a piece: its end seen a moment, then the next page,
     * and the next piece begins a new game */
    while (!(demo.active && phase() >= SW_PHSSP1 && phase() <= SW_PHSBS + 1)) irq();
    for (int i = 0; i < 246; i++) irq();
    if (demo.active) {
        m.ram[SW_S_GAS - 0x4800] = 0;
        m.ram[SW_GS_GLW - 0x4800] = 1;              /* (a hit with no shield left) */
        long i;
        shown = 0;
        for (i = 0; i < 10 * 246 && demo.active; i++) {
            irq();
            if (demo.active && (phase() == SW_PHSEGM || phase() == SW_PHSEGM + 1)) shown++;
        }
        printf("  the ship lost: the piece ended %.1f s after\n", i / 246.0);
        if (demo.active || i > 3 * 246) { printf("the piece goes on after the ship is lost\n"); fail = 1; }
        if (shown) { printf("GAME OVER seen\n"); fail = 1; }
        if (demo.game) { printf("the lost game is kept\n"); fail = 1; }
    }
    sw_demo_stop(&demo, &m);
    printf("test_demo: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
