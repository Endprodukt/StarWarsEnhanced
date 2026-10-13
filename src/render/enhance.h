/* enhance.h - what Star Wars VP adds to the pictures of the game (the
 * enhanced settings; each can be switched off, and with all of them off
 * the picture is the arcade machine's).
 *
 * Over the surface of the Death Star:
 *   surface    the ground is drawn: the plating of the Death Star, lines
 *              in perspective out to the horizon
 *   towers     the towers and bunkers in true perspective, with more to
 *              them, dark where they come into sight and brighter as they
 *              come nearer. (The arcade
 *              machine shrinks a far tower towards its foot, so that it
 *              grows out of the ground as it comes nearer.) The game's own
 *              are taken out of the picture; with sw_machine.enh_towers
 *              the laser hits them as they are drawn here.
 *   dogfights  far off, TIE fighters chase an X-wing across the sky, two
 *              ships or three
 *   stars      a few stars in the sky, twinkling
 * In the trench:
 *   stars      stars in the strip of sky between the tops of its walls
 *   clip       what stands on its walls and across it (panels, guns,
 *              catwalks, the exhaust port) keeps its shape as it comes
 *              near. The arcade machine cannot cut a line where it leaves
 *              the screen: it pushes the points of a near object to just
 *              beyond the edge, which bends the object. Here its lines are
 *              drawn anew from its points in space and cut.
 * And in the attract mode:
 *   logo       the STAR WARS logo is yellow, as in the films (the arcade
 *              machine has it blue)
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

/* Stars of the sky (many: over the trench only a strip of it shows). */
#define SW_ENHANCE_STARS 420

typedef struct sw_enhance {
    int surface, stars, towers, dogfights;      /* the settings */
    int logo;                                   /* 1: the STAR WARS logo yellow */
    int clip;                                   /* 1: what stands in the trench is cut at the edge of the screen */

    sw_line *out;
    int      n, cap;
    sw_solid_extra *faces;                      /* of the towers: for render/solid.h */
    int      n_faces, cap_faces;
    uint16_t inst;                              /* things drawn so far in this picture */
    uint32_t line_id;

    /* the view: where the player is, how he is turned */
    double   r[3][3], t[3];
    double   spin_c, spin_s;                    /* a block being drawn is turned so about its upright axis (cos, sin) */

    float    star[SW_ENHANCE_STARS][3];         /* directions */
    uint8_t  star_lum[SW_ENHANCE_STARS];
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
