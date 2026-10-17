/* menu.h - Star Wars Enhanced's own test menu.
 *
 * The F2 key or the cabinet's self-test button (p1_button6) opens it;
 * the game stands still meanwhile. It holds what the port adds to the
 * arcade machine (the enhanced settings, each of which can be switched
 * off), the game's own options as the arcade program keeps them in its
 * NOVRAM (coins, shields, difficulty, music), a sound test, the screen
 * calibration, and the way on to the arcade program's own operator
 * screens (statistics, the resets, the NOVRAM test). The main page leads
 * to five pages: GAME OPTIONS, GRAPHICS, SOUND (the recordings, the engine,
 * the four volumes, the sound test), CONTROLS (the yoke's centring and the
 * owner's own bindings of every digital input, platform/bind.h), SCREEN
 * (the calibration).
 *
 *   yoke up / down      choose a line (also the arrow keys, the first joystick)
 *   yoke left / right   change its value; on a binding: back to the cabinet's
 *   fire / Return       change it, open a page, or do what the line says;
 *                       on a binding: wait for the input to bind
 *   thumb left / Backspace   the main page again
 *   F2 / test button    save the settings and go back to the game
 */
#ifndef SWE_MENU_H
#define SWE_MENU_H

#include "core/machine.h"
#include "render/config.h"

#include <vectorpie/vp.h>

enum {
    SW_MENU_NOTHING = 0,
    SW_MENU_CHANGED,        /* a setting changed: take it over */
    SW_MENU_OPTION,         /* a game option changed in the NOVRAM: keep it */
    SW_MENU_SOUND,          /* play sw_menu.sound */
    SW_MENU_CALIBRATE,      /* show the calibration screen */
    SW_MENU_OPERATOR,       /* leave, into the arcade program's operator screens */
    SW_MENU_CLOSE,          /* leave */
};

typedef struct sw_menu {
    int    open;
    int    page;            /* the page shown: 0 the main one, else one of its parts */
    int    item[8];         /* the line chosen on each page */
    int    sound;           /* the request of the sound test (docs/SOUNDS.md) */
    int    dir_x, dir_y;    /* where the yoke was held the last time */
    int    ok_was, back_was;    /* the fire and the back key the last time */
    double repeat_at;       /* when a held yoke counts again (seconds) */
    double now;             /* the time of the last update (the blinking) */
} sw_menu;

/* Once a frame while the menu is open, at the time now (seconds). Returns
 * what is to be done (SW_MENU_xxx). m: the game, for its options. */
int  sw_menu_update(sw_menu *u, sw_display_cfg *cfg, sw_machine *m, vp_input *in, double now);
void sw_menu_draw(const sw_menu *u, const sw_display_cfg *cfg, const sw_machine *m, vp_dvg *d);

/* 1 while the page of the picture's settings is shown (the program draws
 * the picture's frame behind it, to be seen as it changes). */
int  sw_menu_screen_page(const sw_menu *u);

/* What request n asks the sound board for ("SPKRED"), "" if nothing. */
const char *sw_menu_sound_name(int n);

#endif
