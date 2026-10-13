/* enhance.h - what Star Wars VP adds to the pictures of the game (the
 * enhanced settings; each can be switched off, and with all of them off
 * the picture is the arcade machine's).
 *
 * Over the surface of the Death Star:
 *   surface    the ground is drawn: the plating of the Death Star, lines
 *              in perspective out to the horizon
 *   stars      stars in the sky
 *   towers     the towers and bunkers in true perspective, with more to
 *              them, seen from twice as far and fading in. (The arcade
 *              machine shrinks a far tower towards its foot, so that it
 *              grows out of the ground as it comes nearer.) The game's own
 *              are taken out of the picture; with sw_machine.enh_towers
 *              the laser hits them as they are drawn here.
 *   dogfights  far off, TIE fighters chase an X-wing across the sky, two
 *              ships or three
 *
 * Nothing here is part of the game: it is drawn from the game's state
 * (where the player is and how he is turned, the map of the buildings and
 * what has become of them) with the game's own perspective, so it stands
 * where the game's things stand.
 */
#ifndef SWVP_ENHANCE_H
#define SWVP_ENHANCE_H

#include "avg/avg.h"
#include "core/machine.h"
#include "render/solid.h"

typedef struct sw_enhance {
    int surface, stars, towers, dogfights;      /* the settings */

    sw_line *out;
    int      n, cap;
    sw_solid_extra *faces;                      /* of the towers: for render/solid.h */
    int      n_faces, cap_faces;
    uint16_t inst;                              /* things drawn so far in this picture */
    uint32_t line_id;

    /* the view: where the player is, how he is turned */
    double   r[3][3], t[3];

    float    star[96][3];                       /* directions */
    uint8_t  star_lum[96];
    float    tie[64][3];                        /* the TIE fighter's points, from the ROM */
    uint8_t  tie_line[160][2];                  /* and its lines */
    int      tie_lines;
    int      tie_ready;
} sw_enhance;

/* Zero the struct first. */
void sw_enhance_init(sw_enhance *e, const sw_machine *m);
void sw_enhance_free(sw_enhance *e);

/* The picture `in` of the machine `m` (as it is shown: the game's state at
 * that moment) at the time now (seconds), with what is switched on added.
 * Returns the number of lines; *out stays valid until the next call. The
 * faces of what was added are in e->faces. */
int sw_enhance_run(sw_enhance *e, const sw_machine *m, double now, const sw_line *in, int n, const sw_line **out);

#endif
