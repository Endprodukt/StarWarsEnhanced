/* test_camera - the camera's turns (enh_camera, sw_machine.camera_ease):
 * in space the ship turns towards the enemy it tracks at a rate the game
 * sets anew every frame. With camera_ease the rate changes by at most EASE
 * a frame; without it, as on the arcade machine, it jumps. Needs the ROM
 * set (skipped without). */
#include "core/machine.h"
#include "core/sw_symbols.h"
#include "game/game.h"
#include "game/m6809.h"
#include "rom/romset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EASE 12

static sw_romset rom;
static sw_machine m;

static void irq(uint8_t in0)
{
    m.in.in0 = in0;
    m.in.in1 = 0xff;
    m.in.adc[0] = m.in.adc[1] = 0x80;
    sw_irq(&m);
    sw_main_poll(&m);
}

/* A game started: the biggest change of a rate of turn from one frame to
 * the next over the first 600 frames of the space battle, and how often
 * it was more than EASE. */
static int biggest_step(int ease, int *over)
{
    sw_machine_init(&m, &rom);
    m.yoke_exact = 1;
    m.steady = 1;
    m.camera_ease = (uint8_t)ease;
    sw_reset(&m);
    for (int i = 0; i < 500; i++) irq(0xff);
    for (int i = 0; i < 30; i++) irq(i < 25 ? (uint8_t)~SW_IN0_COINL : 0xff);
    for (int i = 0; i < 200; i++) irq(0xff);        /* (the credit counted, the attract mode asking for fire) */
    for (int i = 0; i < 60; i++) irq(i < 40 ? (uint8_t)~SW_IN0_LFIRE : 0xff);
    int n;
    for (n = 0; n < 4000 && m.ram[SW_PHASE - 0x4800] != SW_PHSSP1 + 1; n++) irq(0xff);
    if (n == 4000) {
        printf("phase %02X, credits %d\n", m.ram[SW_PHASE - 0x4800], m.ram[SW_SSCRDT - 0x4800]);
        return -1;
    }
    int biggest = 0, frames = 0;
    *over = 0;
    uint32_t seen = m.frames;
    int yaw = (int8_t)m.ram[SW_RHEOY + RH_RAT - 0x4800], pitch = (int8_t)m.ram[SW_RHEOP + RH_RAT - 0x4800];
    while (frames < 600 && m.ram[SW_PHASE - 0x4800] == SW_PHSSP1 + 1) {
        irq(0xff);
        if (m.frames == seen) continue;
        seen = m.frames;
        frames++;
        int y = (int8_t)m.ram[SW_RHEOY + RH_RAT - 0x4800], p = (int8_t)m.ram[SW_RHEOP + RH_RAT - 0x4800];
        int dy = abs(y - yaw), dp = abs(p - pitch);
        if (dy > biggest) biggest = dy;
        if (dp > biggest) biggest = dp;
        *over += dy > EASE || dp > EASE;
        yaw = y;
        pitch = p;
    }
    return biggest;
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_camera: no ROM set, skipped\n");
        return 0;
    }
    int fail = 0, over;
    int b = biggest_step(1, &over);
    if (b < 0) { printf("no space battle reached\n"); fail = 1; }
    else if (over) { printf("eased: the rate still jumped by up to %d, %d times\n", b, over); fail = 1; }
    b = biggest_step(0, &over);
    if (b < 0) { printf("no space battle reached (original)\n"); fail = 1; }
    else if (b <= EASE) { printf("original: no jump bigger than %d, so the test proves nothing (biggest %d)\n", EASE, b); fail = 1; }
    printf("test_camera: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
