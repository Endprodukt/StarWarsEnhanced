/* bind.h - the game's digital inputs and what triggers them.
 *
 * Every input follows the cabinet's advmame.rc (vp_input, the VectorPie way)
 * until the owner binds it otherwise in the test menu (CONTROLS): up to two
 * inputs each, any key, joystick button, joystick axis direction or hat
 * direction, read straight from SDL. The yoke's own axes stay analog
 * (controls.c); the four directions here are a digital yoke besides it, for
 * a keyboard or a digital stick. As in the other VectorPie games (Vectortron).
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

#define SW_BIND_SLOTS 2

void sw_bind_init(void);
/* Once per frame, after the SDK has read the controls. */
void sw_bind_update(vp_input *in);

int  sw_act_held(sw_action a);
int  sw_act_pressed(sw_action a);           /* down since the last frame */
int  sw_act_any_pressed(void);              /* any of the actions, down since the last frame */

const char *sw_act_name(sw_action a);       /* "FIRE LEFT" */
/* What an action is bound to, both slots in one text: "CABINET", "KEY SPACE",
 * "KEY SPACE + JOY 1 BUTTON 3". */
void sw_bind_describe(sw_action a, char *buf, size_t size);
int  sw_bind_is_bound(sw_action a);         /* 0: the cabinet's own */

/* Binding from the menu: start waiting for an input for the action (into
 * its first free slot; both taken: the second is replaced); then call
 * sw_bind_capture() each frame: 0 still waiting, 1 bound, -1 gave up
 * (5 seconds). Inputs held when it starts are ignored until released. */
void sw_bind_capture_begin(sw_action a);
int  sw_bind_capture(void);
int  sw_bind_capturing(void);
void sw_bind_clear(sw_action a);            /* back to the cabinet's */
void sw_bind_clear_all(void);

/* The settings file: "bind_<action> = <input>[, <input>]". */
int  sw_bind_parse(const char *key, const char *value);
void sw_bind_save(FILE *f);

/* The menu's own keys, held: the bound actions, and always the arrow keys,
 * Return, Backspace, F2 and the first joystick's stick, hat and buttons 1 /
 * 2, so that no binding can lock the owner out. */
typedef struct sw_menu_keys { int up, down, left, right, ok, back, test; } sw_menu_keys;
void sw_bind_menu_held(sw_menu_keys *k);
/* The test key (ACT_TEST, or F2) down since the last frame. */
int  sw_bind_test_pressed(void);

#endif
