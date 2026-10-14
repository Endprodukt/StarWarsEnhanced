/* enhance.h - what Star Wars Enhanced adds to the pictures of the game (the
 * enhanced settings; each can be switched off, and with all of them off
 * the picture is the arcade machine's).
 *
 * Over the surface of the Death Star:
 *   surface    the ground is drawn: the plating of the Death Star, lines
 *              in perspective out to the horizon
 *   towers     the towers and bunkers with more to them, sized as the
 *              arcade machine sizes them (a linear scale with the distance
 *              on top of the perspective, about the foot, so that a far
 *              tower is small and grows as it comes), dark where they come
 *              into sight and brighter as they come nearer. The game's own
 *              are taken out of the picture; the laser hits them by the
 *              game's own rule.
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
 *   falcon     shortly before the exhaust port the Millennium Falcon comes
 *              over the player from behind and flies on down the trench
 * Wherever the X-wing's nose and guns are drawn:
 *   view       from where the player sees: the arcade machine's view; from
 *              inside the cockpit (the canopy's frame and the instrument
 *              panel, what is behind them hidden, the lasers coming out
 *              from behind the panel); or from behind the X-wing, which
 *              banks and turns with the yoke, its lasers fired from the
 *              tips of its wings
 * And in the attract mode:
 *   logo       the STAR WARS logo is yellow, as in the films (the arcade
 *              machine has it blue)
 *
 * Nothing here is part of the game: it is drawn from the game's state
 * (where the player is and how he is turned, the map of the buildings and
 * what has become of them) with the game's own perspective, so it stands
 * where the game's things stand.
 */
#ifndef SWE_ENHANCE_H
#define SWE_ENHANCE_H

#include "avg/avg.h"
#include "core/machine.h"
#include "render/solid.h"

/* From where the player sees in flight: as the arcade machine (the
 * X-wing's nose and guns), from inside its cockpit, or from behind it. */
enum { SW_VIEW_ARCADE, SW_VIEW_COCKPIT, SW_VIEW_OUTSIDE, SW_VIEWS };

/* Shapes on the screen that may hide what is behind them (the cockpit's,
 * the faces of the X-wing seen from outside). */
#define SW_ENHANCE_HIDE 128

/* Stars of the sky (many: over the trench only a strip of it shows). */
#define SW_ENHANCE_STARS 420

typedef struct sw_enhance {
    int surface, stars, towers, dogfights;      /* the settings */
    int logo;                                   /* 1: the STAR WARS logo yellow */
    int clip;                                   /* 1: what stands in the trench is cut at the edge of the screen */
    int falcon;                                 /* 1: the Millennium Falcon flies over before the exhaust port */
    int view;                                   /* SW_VIEW_...: from where the player sees */
    int plane_seen;                             /* the last picture had the X-wing's nose and guns (in flight) */

    sw_line *out;
    int      n, cap;
    sw_line *cock;                              /* (the lines with what the cockpit hides cut out) */
    int      cap_cock;
    float    hide_p[SW_ENHANCE_HIDE][6][2];     /* shapes on the screen that hide what is behind them */
    int      hide_n[SW_ENHANCE_HIDE], n_hide;
    float    ck_sx, ck_sy;                      /* the cockpit: where the sight is (smoothed), */
    float    ck_ox, ck_oy, ck_c, ck_s;          /* and how it is moved and turned on the screen */
    sw_solid_extra *faces;                      /* of the towers: for render/solid.h */
    int      n_faces, cap_faces;
    uint16_t inst;                              /* things drawn so far in this picture */
    uint32_t line_id;

    /* the view: where the player is, how he is turned */
    double   r[3][3], t[3];
    double   spin_c, spin_s;
    /* what is drawn is shrunk on the screen about the point (foot_x, foot_y)
     * by `shrink` (1: not at all): the arcade machine's sizing of a building */
    float    shrink, foot_x, foot_y;
    float    plate;                             /* how bright the plating is (1; fading as the ground ends) */
    double   plate_at;
    double   falcon_at;                         /* when the Falcon's run began (0: not on its way) */
    int      falcon_done;                       /* it has flown past this port */

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
