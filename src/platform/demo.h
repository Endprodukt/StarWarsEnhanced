/* demo.h - a demo flight in the attract mode (enhanced; the arcade machine
 * has none: its attract mode is pages of text).
 *
 * When the attract mode comes from the page of scores to the high scores
 * and nobody has paid, the machine as it is is put aside, and a game is
 * played by itself: a credit given, fire to start, the Death Star in the
 * middle chosen, then the yoke weaving and the trigger pulled, the shields
 * never running out. Each part of the flight (space, the surface, the
 * trench) is seen from another view (render/enhance.h), and the next demo
 * starts with the next one. After DEMO_SECONDS, or as soon as anybody
 * puts a coin in or presses a button, the machine is put back as it was:
 * the demo leaves nothing behind (no credit, no score, no count of plays
 * in the NOVRAM), and the coin or the button then acts as ever.
 */
#ifndef SWE_DEMO_H
#define SWE_DEMO_H

#include "core/machine.h"

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
} sw_demo;

/* Once per interrupt, after the controls have set the machine's inputs:
 * starts, flies or ends the demo. */
void sw_demo_tick(sw_demo *d, sw_machine *m);

/* Ends a demo at once (the menu is opened, the program quits). */
void sw_demo_stop(sw_demo *d, sw_machine *m);

#endif
