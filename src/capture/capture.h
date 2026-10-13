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
 *   FRAM  as TICK (buffer is 0), but taken when the main line starts a
 *         frame (WAITFRAME shifts a 1 out of GMSYNC): the state a frame of
 *         the native game is started from to be compared with the next FRAM
 *   NVRM  256 bytes, the NOVRAM (4500-45FF), before each FRAM
 *   RNDM  the random numbers (4703) the main line read during the frame
 *         before, in order; before each FRAM
 *   IRDS  what the main line read during the frame before of the memory
 *         the interrupt keeps (4809-4840, 4851-4852, 4866-4878, 4AF4,
 *         4B04-4B07): 3 bytes per read, address (little-endian) and
 *         value, in order; before each FRAM
 *   POKE  uint16 address, uint8 value: the recording's script wrote this
 *         into the machine just before the FRAM that follows
 *   TIME  three doubles (seconds of emulated time), before each TICK: when
 *         the frame started, when the interrupt handed it its VG buffer,
 *         when it finished the list: how long the 6809 took over it
 * Unknown tags are skipped, so the format can grow.
 */
#ifndef SWE_CAPTURE_H
#define SWE_CAPTURE_H

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

/* Any next chunk: its tag and length in *len, its first min(len, cap) bytes
 * in buf. 1, 0 at the end, -1 on a damaged file. */
int  swcap_next_chunk(swcap_reader *r, char tag[4], uint8_t *buf, uint32_t cap, uint32_t *len);

/* A TICK or FRAM payload (SWCAP_TICK_SIZE bytes) as a tick. */
void swcap_parse_tick(const uint8_t *payload, swcap_tick *t);

/* The VG address space of a tick: its VG RAM plus the vector ROM. */
void swcap_space(const swcap_reader *r, const swcap_tick *t, uint8_t space[0x4000]);

/* Writing, for tests and tools. */
int swcap_write_header(FILE *f, const uint8_t vrom[0x1000]);
int swcap_write_tick(FILE *f, const swcap_tick *t);

#endif
