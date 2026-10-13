/* romset.h - the original ROMs, read from the owner's MAME ROM set.
 *
 * The game's data (vector ROM shapes and font, object tables, Death Star
 * surface, sound program and speech) stays in the ROMs and is read from
 * the cabinet's own set at start-up; none of it is part of this program.
 *
 * The target set is MAME's "starwars1" (Star Wars, Atari, set 2): its
 * program ROMs are exactly the 22-June-1983 release (SW3.DOC: 114, 102,
 * 203, 104, 206) that the historical source and its load map
 * (WSROOT.MAP) describe. MAME keeps the shared ROMs in the parent
 * "starwars.zip" and only 136021-114 in "starwars1.zip": give both.
 * Files are matched by CRC32, so names and zip layout do not matter. Older
 * sets work too (starwar1.zip, math box PROMs dumped with other upper
 * bits, no AVG state PROM).
 */
#ifndef SWE_ROMSET_H
#define SWE_ROMSET_H

#include <stdint.h>

typedef struct sw_romset {
    uint8_t main[0x12000];   /* main CPU space; 0x10000-0x11FFF = ROM 0 page 1 */
    uint8_t vrom[0x1000];    /* vector ROM, at 0x3000 in the main space too */
    uint8_t sound[0x10000];  /* sound CPU space (ROMs mirrored as on the board) */
    uint8_t avg_prom[0x100];
    uint8_t mbox_prom[0x1000]; /* math box microcode: 4 PROMs of 1K x 4 bit */
    uint32_t found;          /* bit per entry of the ROM table */
} sw_romset;

/* Look in each path (a .zip or a directory) for the set's ROMs, in order,
 * until all are found. Returns 0 when complete; otherwise -1 and
 * sw_romset_missing() names what is missing. */
int sw_romset_load(sw_romset *rs, const char *const *paths, int npaths);

/* The ROMs not found, as a comma-separated list (empty when complete). */
const char *sw_romset_missing(const sw_romset *rs, char *buf, int size);

/* The default search list: $SWE_ROMS (a ':'-separated list), then
 * ./roms and the usual AdvanceMAME/MAME rom folders, each tried as
 * starwars1.zip, starwars.zip and a directory. Fills up to max entries
 * with pointers into an internal buffer; returns the count. */
int sw_romset_default_paths(const char **out, int max);

#endif
