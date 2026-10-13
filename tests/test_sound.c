/* test_sound - the sound board's program in C (sound/sndprog.c) against
 * the original on the 6809 core.
 *
 * Two boards get the same requests at the same passes of their main line
 * (every request there is, one after the other). Everything they write to
 * the POKEYs and to the speech chip is noted and has to be the same, in
 * the same order: the effects' channels, the music's, the control
 * registers and the speech bytes are compared each for themselves, with
 * the pass they were written in (the speech chip takes its bytes when it
 * is ready, which may be a pass apart).
 *
 * Needs the ROM set: looks in $SWVP_ROMS and the usual folders, and passes
 * without testing when there is none. */
#include "core/machine.h"
#include "rom/romset.h"
#include "sound/sndboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NREQ      0x3b      /* requests 01 to 3B */
#define GAP       700       /* passes between two requests (2.8 s) */
#define PASSES    ((NREQ + 2) * GAP)

typedef struct { uint32_t pass; uint8_t reg, v; } wr;
typedef struct { wr *w; long n, cap; } wlog;

typedef struct side {
    sw_machine  m;
    sw_sndboard snd;
    uint32_t    pass;       /* passes of the main line so far */
    wlog        log[4];     /* effects, music, control, speech */
} side;

static sw_romset rom;
static side orig, port;

static void note(wlog *l, uint32_t pass, unsigned reg, uint8_t v)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4096;
        l->w = (wr *)realloc(l->w, (size_t)l->cap * sizeof *l->w);
    }
    l->w[l->n].pass = pass;
    l->w[l->n].reg = (uint8_t)reg;
    l->w[l->n].v = v;
    l->n++;
}

static void trace(void *user, int kind, unsigned reg, uint8_t v)
{
    side *s = (side *)user;
    if (kind == 2) {
        if (v != 0x5a) return;
        /* The main line begins a pass: the request due now, if any. */
        s->pass++;
        if (s->pass % GAP == 0 && s->pass / GAP <= NREQ) {
            s->m.sound_out = (uint8_t)(s->pass / GAP);
            s->m.sound_out_full = 1;
        }
        return;
    }
    if (kind == 1) note(&s->log[3], s->pass, 0, v);
    else if (reg < 0x10) note(&s->log[0], s->pass, reg, v);
    else if (reg < 0x20) note(&s->log[1], s->pass, reg, v);
    else note(&s->log[2], s->pass, reg, v);
}

static void start(side *s, int native)
{
    sw_machine_init(&s->m, &rom);
    s->m.sound_board = 1;
    sw_sndboard_init(&s->snd, &s->m);
    s->snd.native = native;
    s->snd.trace = trace;
    s->snd.trace_user = s;
    sw_sndboard_reset(&s->snd);
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_sound: no ROM set found, nothing tested\n");
        return 0;
    }
    start(&orig, 0);
    start(&port, 1);
    static int16_t pcm[4096];
    while (orig.pass < PASSES || port.pass < PASSES) {
        if (orig.pass < PASSES) sw_sndboard_run(&orig.snd, 2000);
        if (port.pass < PASSES) sw_sndboard_run(&port.snd, 2000);
        while (sw_sndboard_read(&orig.snd, pcm, 4096) > 0) { }
        while (sw_sndboard_read(&port.snd, pcm, 4096) > 0) { }
    }

    static const char *name[4] = { "effects", "music", "control", "speech" };
    int fails = 0;
    for (int k = 0; k < 4; k++) {
        const wlog *a = &orig.log[k], *b = &port.log[k];
        long same = 0, late = 0;
        while (same < a->n && same < b->n && a->w[same].reg == b->w[same].reg && a->w[same].v == b->w[same].v) {
            long d = (long)a->w[same].pass - (long)b->w[same].pass;
            if (d < 0) d = -d;
            if (d > late) late = d;
            if (k != 3 && d != 0) break;
            same++;
        }
        printf("%-8s original %6ld writes, in C %6ld, the same: %6ld", name[k], a->n, b->n, same);
        if (k == 3) printf(" (at most %ld passes apart)", late);
        printf("\n");
        if (same != a->n || same != b->n) {
            fails++;
            if (same < a->n && same < b->n)
                printf("  FAIL at write %ld: original %02X=%02X in pass %u (request %02X), in C %02X=%02X in pass %u\n", same,
                       a->w[same].reg, a->w[same].v, a->w[same].pass, a->w[same].pass / GAP,
                       b->w[same].reg, b->w[same].v, b->w[same].pass);
            else
                printf("  FAIL: one of them stops after %ld writes (pass %u, request %02X)\n", same,
                       same ? (same < a->n ? a : b)->w[same].pass : 0, same ? (same < a->n ? a : b)->w[same].pass / GAP : 0);
        } else if (k == 3 && late > 2) {
            printf("  FAIL: the speech bytes come at other times\n");
            fails++;
        }
    }
    return fails ? 1 : 0;
}
