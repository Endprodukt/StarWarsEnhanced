/* solid.h - things you cannot see through (the setting `occlusion`).
 *
 * The game draws wire frames: every line of every thing shows, also the
 * far side of a ship and the stars behind the STAR WARS logo. Here the
 * lines of a picture are cut where something nearer covers them. Nothing
 * of the game changes; it is done to the picture on its way to the tube.
 *
 * What covers:
 *   - the faces of the 3-D objects (ships, towers, bunkers, what stands on
 *     the walls of the trench). The ROM has only their points and lines:
 *     the faces are found once, as the rings of lines that lie in a plane
 *     (sw_solid_init).
 *   - what is drawn as an outline: the letters of the logo, the X-wing's
 *     nose and guns, the Death Star.
 * How far away a line is the game notes while it draws (sw_line.w0, w1:
 * 1/X of the perspective, which is straight across a flat face on the
 * screen). The score, the texts, the sight and the laser beams are never
 * cut.
 */
#ifndef SWVP_SOLID_H
#define SWVP_SOLID_H

#include "avg/avg.h"
#include "core/machine.h"

#define SW_SOLID_MODELS   17        /* the objects of the ROM (TD$xxx) */
#define SW_SOLID_FACE_MAX 8         /* points of a face at most */

typedef struct sw_solid_face {
    uint8_t n;
    uint8_t pt[SW_SOLID_FACE_MAX];
} sw_solid_face;

typedef struct sw_solid_poly {
    int      first, n;              /* its edges (sw_solid.edges) */
    float    a, b, c;               /* 1/X across it: a x + b y + c */
    float    wmax;
    float    x0, y0, x1, y1;        /* the box around it */
    uint16_t inst;                  /* the object drawn it is a face of */
    uint64_t points;                /* its points, as bits */
} sw_solid_poly;

/* A face given as it is in the picture: its corners with their 1/X. */
typedef struct sw_solid_extra {
    uint16_t inst;                  /* the lines of the same thing carry it too */
    uint8_t  n;
    float    x[6], y[6], w[6];
} sw_solid_extra;

/* The shape of an outline, on a grid. */
typedef struct sw_solid_mask {
    uint64_t group;                 /* which instruction of the list drew it */
    int      count;                 /* its lines, and what they add up to */
    float    sum;
    float    ox, oy;                /* the grid's corner */
    int      w, h;
    uint8_t *cells;                 /* 2: outside */
    int      cap;
    float    c;                     /* 1/X of all of it */
    uint8_t  section;
    uint8_t  front;                 /* of the bright lines only (zz and up): the front of the logo */
    uint8_t  zz;
    uint8_t  active;                /* in the picture being cut */
    uint32_t used;
} sw_solid_mask;

#define SW_SOLID_MASKS 32

typedef struct sw_solid {
    struct {
        sw_solid_face *faces;
        int            n;
    } model[SW_SOLID_MODELS];
    /* the picture being cut */
    sw_solid_poly *polys;
    int            n_polys, cap_polys;
    float        (*edges)[4];
    int            n_edges, cap_edges;
    sw_line       *out;
    int            n_out, cap_out;
    /* Faces of things that are no object of the ROM (render/enhance.h), for
     * the next sw_solid_run: set by the caller, NULL/0 for none. */
    const sw_solid_extra *extra;
    int            n_extra;
    sw_solid_mask  mask[SW_SOLID_MASKS];
    uint32_t       run;
    int           *stack;
    int            cap_stack;
} sw_solid;

/* Zero the struct first. Finds the faces of the objects in the machine's ROM. */
void sw_solid_init(sw_solid *s, const sw_machine *m);
void sw_solid_free(sw_solid *s);

/* The lines of a picture (with the notes of sw_avg_run_noted), cut. Returns
 * how many there are now; *out stays valid until the next call. */
int sw_solid_run(sw_solid *s, const sw_line *in, int n, const sw_line **out);

#endif
