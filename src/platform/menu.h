/* menu.h - Star Wars VP's own test menu.
 *
 * The cabinet's self-test button (p1_button6) opens it; the game stands
 * still meanwhile. It holds what the port adds to the arcade machine: the
 * enhanced settings (each can be switched off), a sound test, the screen
 * calibration, and the way on to the arcade program's own operator
 * screens (statistics and game options).
 *
 *   yoke up / down      choose a line
 *   yoke left / right   change its value
 *   fire                change it, or do what the line says
 *   self-test button    save the settings and go back to the game
 *
 * For cabinets without a self-test button: the start button held for two
 * seconds does the same, and so does the trigger held for three seconds
 * in the attract mode (to open the menu), on the calibration screen and
 * on the operator screens (to go back).
 */
#ifndef SWVP_MENU_H
#define SWVP_MENU_H

#include "render/config.h"

#include <vectorpie/vp.h>

enum {
    SW_MENU_NOTHING = 0,
    SW_MENU_CHANGED,        /* a setting changed: take it over */
    SW_MENU_SOUND,          /* play sw_menu.sound */
    SW_MENU_CALIBRATE,      /* show the calibration screen */
    SW_MENU_OPERATOR,       /* leave, into the arcade program's operator screens */
    SW_MENU_CLOSE,          /* leave */
};

typedef struct sw_menu {
    int    open;
    int    item;
    int    sound;           /* the request of the sound test (docs/SOUNDS.md) */
    int    dir_x, dir_y;    /* where the yoke was held the last time */
    double repeat_at;       /* when a held yoke counts again (seconds) */
} sw_menu;

/* Once a frame while the menu is open, at the time now (seconds). Returns
 * what is to be done (SW_MENU_xxx). */
int  sw_menu_update(sw_menu *u, sw_display_cfg *cfg, vp_input *in, double now);
void sw_menu_draw(const sw_menu *u, const sw_display_cfg *cfg, vp_dvg *d);

/* What request n asks the sound board for ("SPKRED"), "" if nothing. */
const char *sw_menu_sound_name(int n);

#endif
