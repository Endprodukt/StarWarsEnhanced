/* demo.h - a demo flight in the attract mode (enhanced; the arcade machine
 * has none: its attract mode is pages of text).
 *
 * When the attract mode comes from the page of scores to the high scores
 * and nobody has paid, the machine as it is is put aside, and a game is
 * played by itself: a credit given, fire to start, the Death Star in the
 * middle chosen, then flown by a pilot who plays as a player would (demo.c):
 * he looks at the picture the game has drawn, puts the sight on the
 * fireball coming nearest and shoots it down, else on a TIE fighter, a
 * tower's top or a bunker; over the surface he steers clear of the towers,
 * in the trench of the force fields, and at its end he shoots for the
 * exhaust port. What hits him costs shields as in any game; only the last
 * one is never lost (the demo is not to end in the middle of its flight).
 * Each part of the flight (space, the surface, the trench) is seen from
 * another view (render/enhance.h), and the next demo starts with the next
 * one. After DEMO_SECONDS, or as soon as anybody puts a coin in or presses
 * a button, the machine is put back as it was: the demo leaves nothing
 * behind (no credit, no score, no count of plays in the NOVRAM), and the
 * coin or the button then acts as ever.
 */
#ifndef SWE_DEMO_H
#define SWE_DEMO_H

#include "avg/avg.h"
#include "core/machine.h"

#define SW_DEMO_SHOTS  6        /* the enemies' shots (GUN to GUNZ) */
#define SW_DEMO_THINGS 48       /* objects in the picture */

/* Something in the picture as the pilot sees it: where on the screen (in
 * the sight's own units, VG_CX and VG_CY: +-512), how big, how near (1/X,
 * larger is nearer; 0 not known), and what (its tag's key). */
typedef struct sw_demo_thing {
    float    x, y, r, top;
    uint16_t w;
    uint16_t key;
} sw_demo_thing;

typedef struct sw_demo {
    int         enabled;        /* the setting enh_demo */
    int         active;         /* a demo is being flown */
    int         interrupt;      /* set by the program: somebody wants to play (a coin, a button) */
    int         ended;          /* the demo has just ended: the program puts its own view back */
    int         view;           /* the view of the moment (SW_VIEW_...) */
    sw_machine *saved;          /* the machine as it was before the demo */
    long        irq;            /* interrupts since the demo began */
    uint8_t     last_phase;
    int         parts;          /* parts of the flight seen so far */
    int         begun;          /* the game has begun (it has left the attract mode's pages) */
    int         first_view;     /* the view the next demo starts with */
    /* the pilot: the picture he has looked at, what he saw in it */
    uint8_t    *space;          /* the vector generator's memory, for running the picture */
    sw_avg_list seen;
    uint32_t    vg_seen;        /* the picture looked at (sw_machine.vg_starts) */
    sw_demo_thing shot[SW_DEMO_SHOTS];      /* (r 0: not in the picture) */
    sw_demo_thing thing[SW_DEMO_THINGS];    /* the 3-D objects */
    int         things;
    int         fire;           /* interrupts the trigger is held yet (<0: let go) */
    int         dodge;         /* he steers clear of something: to plan_x, plan_y */
    uint32_t    plan_frame;     /* (the game's frame that was worked out for) */
    float       plan_x, plan_y;
} sw_demo;

/* Once per interrupt, after the controls have set the machine's inputs:
 * starts, flies or ends the demo. */
void sw_demo_tick(sw_demo *d, sw_machine *m);

/* Ends a demo at once (the menu is opened, the program quits). */
void sw_demo_stop(sw_demo *d, sw_machine *m);

#endif
