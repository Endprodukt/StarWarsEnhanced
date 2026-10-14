/* dogfight.h - the dogfight: a mode of its own (enhanced; not in the
 * arcade machine), chosen as a fourth Death Star on the page where one is
 * chosen. The player flies freely in space, in a box he cannot leave (at
 * its walls the ship turns back by itself: "WHERE ARE YOU GOING"), the
 * Death Star far off; wave after wave of enemies comes, for as long as he
 * lives (docs/STATUS.md, "Dogfight").
 *
 * Controlled as Time Pilot 3D: the yoke turns and pitches the ship (the
 * sight stays in the middle: one aims with the nose); the thumb buttons
 * make it faster and slower, the throttle going back to the middle when
 * they are let go; both together and the yoke to a side roll it about its
 * own axis.
 *
 * Its picture is made as the game makes its own: a display list for the
 * vector generator, calling the game's own routines in the vector ROM
 * (its lettering, the X-wing's nose and guns, the sight, the stars, the
 * Death Star), run by render/avg into lines; so it looks as the game does,
 * and the views of render/enhance.h (cockpit, outside) work on it.
 *
 * Stage 1 (2026-10-14): flying in the box, the Death Star, the stars.
 */
#ifndef SWE_DOGFIGHT_H
#define SWE_DOGFIGHT_H

#include "avg/avg.h"
#include "core/machine.h"

/* What the player does, once a frame. */
typedef struct sw_dogfight_input {
    float yaw, pitch;           /* the yoke, -1..1: to the right, up (as the sight goes) */
    int   faster, slower;       /* the thumb buttons held */
    int   fire, torpedo;        /* the triggers held */
} sw_dogfight_input;

typedef struct sw_dogfight {
    int    active;
    double t;                   /* seconds since it began */
    /* the ship: where it is, and its own axes ahead (f), to the right (r)
     * and up (u), in the box's coordinates */
    double pos[3], f[3], r[3], u[3];
    double speed;               /* units a second */
    float  throttle;            /* -1 (slowest) .. 1 (fastest); rests at 0 */
    float  yaw_rate, pitch_rate, roll_rate;     /* radians a second, as they are now (they ease) */
    int    turning_back;        /* at a wall: the ship turns back by itself */
    double back_since;
    /* the display list and its lines */
    uint8_t  space[SW_AVG_SPACE_SIZE];
    uint16_t tags[SW_AVG_SPACE_SIZE / 2];
    uint16_t at;                /* the next word of the list */
    uint16_t tag;
    sw_avg_list list;
    float    dust[48][3];       /* specks near the ship, to see it moving */
} sw_dogfight;

/* Begins the dogfight (the vector ROM is the machine's). */
void sw_dogfight_start(sw_dogfight *d, const sw_machine *m);

/* A step of dt seconds. */
void sw_dogfight_step(sw_dogfight *d, const sw_dogfight_input *in, double dt);

/* The picture as it is: lines as render/avg gives them (tagged, see
 * core/tags.h). Valid until the next call. */
int sw_dogfight_lines(sw_dogfight *d, const sw_machine *m, const sw_line **out);

/* The fourth choice on the page where a Death Star is chosen: its picture
 * (appended to `list`), and whether the sight is on it. */
void sw_dogfight_choice_lines(sw_dogfight *d, const sw_machine *m, sw_avg_list *list);
int  sw_dogfight_chosen(const sw_machine *m);

/* How the ship banks and pitches as the player sees it from outside
 * (-1..1, as the sight would be): for render/enhance.h. */
void sw_dogfight_attitude(const sw_dogfight *d, float *bank, float *pitch);

#endif
