/* avg.h - the Star Wars analog vector generator, as a display-list
 * interpreter.
 *
 * Instruction set (WSVGMC.MAC, the game's own VG macros; 16-bit words,
 * big-endian as the 6809 writes them):
 *
 *   000 VCTR  two words: DY (13-bit signed), then ZZ<<13 | DX (13-bit signed)
 *   001 HALT
 *   010 SVEC  one word: DY/2 in bits 12-8, ZZ in 7-5, DX/2 in 4-0 (5-bit signed)
 *   011 COLOR bit 12 clear: color in bits 10-8, FUZZ (defocus) bit 11,
 *             intensity in 7-0 (80 is normal)
 *       SCAL  bit 12 set: binary scale in bits 10-8 (n = 1/2^n),
 *             linear scale in 7-0 (0 full size, FF = 1/256)
 *   100 CNTR  beam to the center
 *   101 JSRL  call word address (bits 12-0), 4-deep stack that wraps
 *   110 RTSL
 *   111 JMPL
 *
 * ZZ (0..7) is the per-vector brightness; 0 draws nothing (a move). The
 * board turns ZZ into a current through a 12k/24k/47k resistor network and
 * scales it by the COLOR intensity (MAME avg_starwars_device::handler_7).
 *
 * Colors are 3-bit RGB: 4 red, 2 green, 1 blue (VGCRED, VGCGRN, VGCBLU;
 * 3 turquoise, 5 purple, 6 yellow, 7 white).
 *
 * Positions come out in AVG units with the center at 0,0, already
 * multiplied by the current scale. The game's screen edges in these units
 * are about +-510 in x and -612..+600 in y (VGEDGL/R/B/T) in its vector
 * words; it draws at a quarter scale (VGSCAL), so +-127.5 and -153..+150
 * here.
 */
#ifndef SWVP_AVG_H
#define SWVP_AVG_H

#include <stddef.h>
#include <stdint.h>

#define SW_AVG_SPACE_SIZE 0x4000   /* VG address space: RAM 0000-2FFF, ROM 3000-3FFF */

typedef struct sw_line {
    float   x0, y0, x1, y1;
    uint8_t color;        /* 3-bit RGB */
    uint8_t intensity;    /* 0..255, after the ZZ network */
    uint8_t zz;           /* the vector's own 0..7 */
    uint8_t defocus;      /* FUZZ: the beam was defocused */
    uint64_t id;          /* what the line is, for finding it again in the next list (0: unknown) */
} sw_line;

enum sw_avg_status {
    SW_AVG_OK = 0,          /* reached HALT */
    SW_AVG_LIMIT,           /* instruction limit: a runaway list */
    SW_AVG_BAD_ADDRESS,     /* jumped outside the VG space */
};

typedef struct sw_avg_list {
    sw_line *lines;
    int      count;
    int      cap;
    int      instructions;  /* executed by the last run */
    int      status;
} sw_avg_list;

void sw_avg_list_free(sw_avg_list *l);

/* Run the display list at byte address start of space (normally 0, where
 * the hardware starts: VGRAM[0] jumps to the current buffer) until HALT.
 * Lines are appended to out (cleared first). max_instructions <= 0 uses a
 * default. Returns out->status. */
int sw_avg_run(const uint8_t *space, uint16_t start, sw_avg_list *out, int max_instructions);

/* The same, with the tags of the words of VG RAM (core/tags.h; one per
 * word, 0x1800 of them): every line gets an id made of the tag of the
 * instruction of the list itself that drew it, that instruction's place
 * among those with the same tag, the vector ROM routine it called, and the
 * line's number within it. The same thing drawn in the next list has the
 * same id. starts (may be NULL) marks words that begin a new piece although
 * the tag stays: counting begins again there. Lines drawn by a vector ROM
 * routine do not count their place: all the stars, or all the letters E of
 * a text, share an id and are told apart by where they are
 * (render/smooth.h). */
int sw_avg_run_tagged(const uint8_t *space, uint16_t start, const uint16_t *tags, const uint8_t *starts,
                      sw_avg_list *out, int max_instructions);

/* How long the board takes to draw the list at byte address start, in
 * cycles of the 12.096 MHz master clock, until HALT: the time the game
 * waits before it can start the next picture, which is what slows the
 * original down when the screen is full. vgram is 0000-2FFF, vrom
 * 3000-3FFF (may be NULL). Follows MAME's avg state machine
 * (src/devices/video/avgdvg.cpp, BSD-3-Clause, Mathis Rosenhauer). */
uint32_t sw_avg_cycles(const uint8_t *vgram, const uint8_t *vrom, uint16_t start);

/* The intensity the board gives a vector of brightness zz under a COLOR
 * intensity lum (both as in the display list). */
uint8_t sw_avg_intensity(unsigned zz, unsigned lum);

#endif
