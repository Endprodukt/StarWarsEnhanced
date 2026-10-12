/* capture.h - game ticks recorded from MAME (tools/mame/swcapture.lua).
 *
 * One record per game tick, taken the moment the main line finishes its
 * display list (VGDONE writes FF to VGSYNC). Used two ways:
 *   - avgreplay draws the recorded display lists: the AVG interpreter and
 *     the monitor settings can be checked against the real game;
 *   - the reference for the native core: RAM and math box RAM per tick.
 *
 * File: "SWCAP1\0\0", then chunks: 4-byte tag, uint32 little-endian length,
 * payload. Tags:
 *   VROM  4096 bytes, the vector ROM (once, first)
 *   TICK  uint32 frame (FRAME, the game's tick counter), uint32 irq count,
 *         uint16 buffer (byte address of the list just finished), uint16 0,
 *         then VG RAM 0000-2FFF, RAM 4800-4FFF, math box RAM 5000-5FFF
 * Unknown tags are skipped, so the format can grow.
 */
#ifndef SWVP_CAPTURE_H
#define SWVP_CAPTURE_H

#include <stdint.h>
#include <stdio.h>

#define SWCAP_VGRAM_SIZE 0x3000
#define SWCAP_RAM_SIZE   0x0800
#define SWCAP_MBRAM_SIZE 0x1000
#define SWCAP_TICK_SIZE  (12 + SWCAP_VGRAM_SIZE + SWCAP_RAM_SIZE + SWCAP_MBRAM_SIZE)

typedef struct swcap_tick {
    uint32_t frame;
    uint32_t irq_count;
    uint16_t buffer;
    uint8_t  vgram[SWCAP_VGRAM_SIZE];
    uint8_t  ram[SWCAP_RAM_SIZE];
    uint8_t  mbram[SWCAP_MBRAM_SIZE];
} swcap_tick;

typedef struct swcap_reader {
    FILE   *f;
    uint8_t vrom[0x1000];
    int     have_vrom;
} swcap_reader;

int  swcap_open(swcap_reader *r, const char *path);   /* 0 or -1 */
void swcap_close(swcap_reader *r);
/* Next tick: 1, 0 at the end, -1 on a damaged file. */
int  swcap_next(swcap_reader *r, swcap_tick *t);
int  swcap_rewind(swcap_reader *r);

/* The VG address space of a tick: its VG RAM plus the vector ROM. */
void swcap_space(const swcap_reader *r, const swcap_tick *t, uint8_t space[0x4000]);

/* Writing, for tests and tools. */
int swcap_write_header(FILE *f, const uint8_t vrom[0x1000]);
int swcap_write_tick(FILE *f, const swcap_tick *t);

#endif
