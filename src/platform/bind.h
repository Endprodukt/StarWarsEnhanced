/* bind.h - the game's digital inputs and what triggers them.
 *
 * Read directly, never by advmame.rc's names. Every input is AUTO (what
 * AdvanceMAME gives it by default: its keys, the yoke's or first
 * joystick's buttons and stick, the mouse's buttons), NONE, or up to
 * SW_BIND_SLOTS inputs of the owner's (the test menu, CONTROLS): any key,
 * joystick button, axis direction, hat direction or mouse button. One
 * input may do several things. The yoke's own axes stay analog
 * (controls.c); the four directions here are a digital yoke besides it,
 * for a keyboard or a digital stick, and count only when bound.
 *
 * Laying one out, as in every vector game of this cabinet: START 1 or
 * Enter on its line, then the input; the first since the cursor came to
 * the line replaces what it had, every further one is added. While it
 * waits: Enter (START 1 too on a line of the game's) leaves it as it was,
 * START 2 makes it NONE, Delete AUTO.
 */
#ifndef SWE_BIND_H
#define SWE_BIND_H

#include <stdio.h>
#include <vectorpie/vp_input.h>

typedef enum {
    ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,      /* the yoke, digital */
    ACT_FIRE_L, ACT_FIRE_R,                     /* the triggers (left starts a game too) */
    ACT_THUMB_L, ACT_THUMB_R,                   /* the thumb buttons */
    ACT_START, ACT_START2,
    ACT_COIN1, ACT_COIN2, ACT_COIN_AUX,         /* the coin switches and the operator's aux coin button */
    ACT_TEST,                                   /* the self-test button: the test menu */
    ACT_COUNT
} sw_action;

#define SW_BIND_SLOTS 4

void sw_bind_init(void);
/* Once per frame, after the SDK has read the controls. */
void sw_bind_update(vp_input *in);

int  sw_act_held(sw_action a);
int  sw_act_pressed(sw_action a);           /* down since the last frame */
int  sw_act_any_pressed(void);              /* any of the actions, down since the last frame */

const char *sw_act_name(sw_action a);       /* "FIRE LEFT" */
/* What an action is bound to, in one text: "AUTO", "NONE", "SPACE",
 * "SPACE + JOY1 BUTTON 3". */
void sw_bind_describe(sw_action a, char *buf, size_t size);
int  sw_bind_is_bound(sw_action a);         /* 0: AUTO or NONE */

/* Laying out from the menu: start waiting for an input for the action
 * (`replace`: it replaces what the action had, else it is added); then
 * call sw_bind_capture() each frame: 0 still waiting, 1 changed, -1 left
 * as it was. Inputs held when it starts are ignored until released. */
void sw_bind_capture_begin(sw_action a, int replace);
int  sw_bind_capture(void);
int  sw_bind_capturing(void);
int  sw_bind_capture_adds(void);            /* waiting, and what comes is added */
void sw_bind_clear(sw_action a);            /* AUTO */
void sw_bind_set_none(sw_action a);         /* NONE */
void sw_bind_toggle(sw_action a);           /* NONE, or AUTO again */
void sw_bind_clear_all(void);

/* The settings file: "bind_<action> = auto | off | <input>[, <input>...]". */
int  sw_bind_parse(const char *key, const char *value);
void sw_bind_save(FILE *f);

/* The menu's own keys, held, whatever is bound: the arrow keys, any stick,
 * hat or the yoke, START 1 / Enter / the keypad's Enter, Backspace, and F2
 * or the test button. */
typedef struct sw_menu_keys { int up, down, left, right, ok, back, test; } sw_menu_keys;
void sw_bind_menu_held(sw_menu_keys *k);
/* The test key (ACT_TEST, or F2) down since the last frame. */
int  sw_bind_test_pressed(void);

#endif
