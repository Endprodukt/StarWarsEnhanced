/* yoke.h - the yoke, read as it is (taken over from Time Pilot 3D's
 * controls.c).
 *
 * The cabinet's advmame.rc lays the yoke's buttons out for the arcade
 * games it runs: every one of them is "p1_button1". The dogfight needs
 * each on its own, and the yoke's whole travel. So when a joystick whose
 * name says "yoke" is connected, its axes and buttons are read directly.
 * Its buttons, as on the owner's yoke (Time Pilot 3D's yoke_layout 1 2 3
 * 4): 1 right trigger, 2 left trigger, 3 right thumb, 4 left thumb.
 */
#ifndef SWE_YOKE_H
#define SWE_YOKE_H

enum { SW_YOKE_RIGHT_TRIGGER, SW_YOKE_LEFT_TRIGGER, SW_YOKE_RIGHT_THUMB, SW_YOKE_LEFT_THUMB };

/* Once a frame: looks for the yoke now and then (it may be plugged in
 * later) and reads its buttons. */
void sw_yoke_update(void);

/* Is a yoke connected? */
int sw_yoke_present(void);

/* A button (SW_YOKE_..., or any number from 0) held, or down since the
 * last update. */
int sw_yoke_held(int button);
int sw_yoke_pressed(int button);

/* Its axes, -1..1: to the right, and pulled back (towards the player). */
void sw_yoke_axes(float *x, float *y);

#endif
