/* test_options - the game's options read and set from outside
 * (sw_option_get/set): what is set is what the game then uses, the NOVRAM
 * stays valid (both option areas with good checksums) and the arcade
 * program's option page would show the same. Needs the ROM set (skipped
 * without). */
#include "core/machine.h"
#include "core/sw_symbols.h"
#include "game/game.h"
#include "rom/romset.h"

#include <stdio.h>
#include <string.h>

static sw_romset rom;
static sw_machine m;

static void run(int irqs)
{
    for (int i = 0; i < irqs; i++) {
        m.in.in0 = 0xff;
        m.in.in1 = 0xff;
        m.in.adc[0] = m.in.adc[1] = 0x80;
        sw_irq(&m);
        sw_main_poll(&m);
    }
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_options: no ROM set, skipped\n");
        return 0;
    }
    sw_machine_init(&m, &rom);
    m.yoke_exact = 1;
    m.steady = 1;
    sw_reset(&m);
    run(500);                               /* the NOVRAM set up, the attract mode on */
    int fail = 0;

    int max;
    int v;
    static const int MAX[SW_GAME_OPTIONS] = { 3, 1, 3, 7, 3, 3, 3, 1 };
    for (int i = 0; i < SW_GAME_OPTIONS; i++) {
        v = sw_option_get(&m, i, &max);
        if (max != MAX[i] || v < 0 || v > max) { printf("option %d: value %d, max %d\n", i, v, max); fail = 1; }
    }

    /* difficulty (option 5, bits 2-3 of OPTS1's low nibble) to HARDEST, music off */
    sw_option_set(&m, 5, 3);
    sw_option_set(&m, 7, 1);
    if (sw_option_get(&m, 5, NULL) != 3) { printf("difficulty not set\n"); fail = 1; }
    if (sw_option_get(&m, 7, NULL) != 1) { printf("music not set\n"); fail = 1; }
    if (((sw_rd8(&m, SW_OPTS1 + 1) >> 2) & 3) != 3) { printf("the game's own word does not say HARDEST\n"); fail = 1; }
    if ((sw_rd8(&m, SW_OPTS1) & 0x04) == 0) { printf("the game's own word does not say music off\n"); fail = 1; }
    /* other fields of the same words untouched: starting shields */
    v = sw_option_get(&m, 4, NULL);
    sw_option_set(&m, 4, 3);                /* 9 shields */
    if (sw_option_get(&m, 5, NULL) != 3 || sw_option_get(&m, 4, NULL) != 3) { printf("fields overlap\n"); fail = 1; }
    if (((sw_rd8(&m, SW_OPTS1 + 1) & 3) + 6) != 9) { printf("the game's own word does not say 9 shields\n"); fail = 1; }
    sw_option_set(&m, 4, v);

    /* the NOVRAM valid: primary and backup option areas */
    if (sw_summit(&m, SW_NAREA, NULL, NULL) != 0) { printf("option area checksum bad\n"); fail = 1; }
    if (sw_summit(&m, 0, NULL, NULL) != 0) { printf("backup area checksum bad\n"); fail = 1; }
    /* and the game goes on running with them */
    run(2000);
    if (sw_option_get(&m, 5, NULL) != 3) { printf("difficulty lost while running\n"); fail = 1; }
    /* coin value: 1/2 credit; the coin logic takes its mode byte from the
     * NOVRAM whenever a coin comes (MOOLAH), so at the next coin */
    sw_option_set(&m, 0, 3);
    for (int i = 0; i < 30; i++) {
        m.in.in0 = i < 25 ? (uint8_t)~SW_IN0_COINL : 0xff;
        m.in.in1 = 0xff;
        sw_irq(&m);
        sw_main_poll(&m);
    }
    if ((sw_rd8(&m, SW_SCMODE) & 3) != 3) { printf("coin mode not taken over (%02X)\n", sw_rd8(&m, SW_SCMODE)); fail = 1; }

    /* free play (coin value 0): the attract mode keeps a credit, and the
     * fire button alone starts a game */
    sw_option_set(&m, 0, 0);
    run(600);
    if (sw_rd8(&m, SW_SSCRDT) != 1) { printf("free play: credits %d, 1 expected\n", sw_rd8(&m, SW_SSCRDT)); fail = 1; }
    for (int i = 0; i < 40; i++) {
        m.in.in0 = i < 30 ? (uint8_t)~SW_IN0_LFIRE : 0xff;
        m.in.in1 = 0xff;
        m.in.adc[0] = m.in.adc[1] = 0x80;
        sw_irq(&m);
        sw_main_poll(&m);
    }
    uint8_t phase = sw_rd8(&m, SW_PHASE);
    if (phase < SW_PHSSG1 || phase > SW_PHSBGN + 1) { printf("free play: no game started with fire (phase %02X)\n", phase); fail = 1; }

    printf("test_options: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
