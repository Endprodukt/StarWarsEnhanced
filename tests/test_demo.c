/* test_demo - the attract mode's demo flight (platform/demo.h): it starts
 * as the attract mode goes on to the high scores, flies a game by itself
 * through space, the surface and the trench, each from another view, and
 * when it ends the machine is as it was before it (RAM, NOVRAM, credits).
 * Needs the ROM set (skipped without). */
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

static void irq(void)
{
    m.in.in0 = 0xff;
    m.in.in1 = 0xff;
    m.in.adc[0] = m.in.adc[1] = 0x80;
    sw_demo_tick(&demo, &m);
    if (!demo.active) return;
    sw_irq(&m);
    sw_main_poll(&m);
}

static uint8_t phase(void) { return m.ram[SW_PHASE - 0x4800]; }

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

    /* the attract mode until the demo begins */
    long i;
    for (i = 0; i < 200000 && !demo.active; i++) {
        m.in.in0 = 0xff;
        m.in.in1 = 0xff;
        m.in.adc[0] = m.in.adc[1] = 0x80;
        sw_demo_tick(&demo, &m);
        sw_irq(&m);
        sw_main_poll(&m);
    }
    if (!demo.active) { printf("no demo began\n"); return 1; }
    static uint8_t ram[sizeof m.ram], nvram[sizeof m.nvram];
    memcpy(ram, demo.saved->ram, sizeof ram);
    memcpy(nvram, demo.saved->nvram, sizeof nvram);

    /* it flies through space, over the surface, down the trench, each part
     * from the next view */
    long space = -1, ground = -1, trench = -1;
    int views[3] = { -1, -1, -1 };
    for (i = 0; i < 40000 && demo.active; i++) {
        irq();
        if (!demo.active) break;
        if (space < 0 && phase() == SW_PHSSP1 + 1) space = i;
        if (ground < 0 && phase() == SW_PHSGD + 1) ground = i;
        if (trench < 0 && phase() == SW_PHSBS + 1) trench = i;
        /* (the view a little into each part) */
        if (space >= 0 && i == space + 50) views[0] = demo.view;
        if (ground >= 0 && i == ground + 50) views[1] = demo.view;
        if (trench >= 0 && i == trench + 50) views[2] = demo.view;
    }
    printf("  space %.1f s, surface %.1f s, trench %.1f s; views %d %d %d\n",
           space / 246.0, ground / 246.0, trench / 246.0, views[0], views[1], views[2]);
    if (space < 0 || ground < 0 || trench < 0) { printf("the demo did not get through the flight\n"); fail = 1; }
    if (views[0] == views[1] || views[1] == views[2] || views[0] == views[2]) { printf("not each part from another view\n"); fail = 1; }
    /* when it has ended (on its own, or now), the machine is as it was */
    sw_demo_stop(&demo, &m);
    if (demo.active || memcmp(ram, m.ram, sizeof ram) != 0 || memcmp(nvram, m.nvram, sizeof nvram) != 0) {
        printf("the machine is not as it was\n");
        fail = 1;
    }
    printf("test_demo: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
