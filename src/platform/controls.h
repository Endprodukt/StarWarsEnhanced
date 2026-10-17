/* controls.h - the cabinet's controls as the Star Wars board's switches and
 * A/D inputs. The yoke's axes come from vp_input (advmame.rc), the digital
 * inputs from platform/bind.h (the cabinet's, or the owner's own bindings).
 *
 *   yoke        p1_stickx / p1_sticky   -> A/D channel 1 (yaw) / 0 (pitch)
 *               ACT_LEFT/RIGHT/UP/DOWN     a digital yoke besides it (keyboard, digital stick)
 *   left fire   ACT_FIRE_L (p1_button1), ACT_START (start1) -> IN0 D7 (also starts the game)
 *   right fire  ACT_FIRE_R (p1_button4), start2 -> IN0 D6
 *   left thumb  ACT_THUMB_L (p1_button2)  -> IN1 D5
 *   right thumb ACT_THUMB_R (p1_button3)  -> IN1 D4
 *   coins       ACT_COIN1 / ACT_COIN2     -> IN0 D1 / D0, as a 100 ms pulse
 *   aux coin    ACT_COIN_AUX (p1_button5) -> IN0 D2 (the operator's button: next page in self test)
 *   self test   ACT_TEST (p1_button6)     -> opens Star Wars Enhanced's own test menu (platform/menu.h);
 *                                            from there IN0 D4, the board's self-test switch
 *
 * The cabinet's defaults are MAME's Star Wars mapping. Coins are pulses
 * timed in IRQs, because MOOLAH accepts only 16..200 ms of switch closure.
 */
#ifndef SWE_CONTROLS_H
#define SWE_CONTROLS_H

#include "core/machine.h"
#include "platform/bind.h"

#include <vectorpie/vp_input.h>

typedef struct sw_controls {
    float   deadzone;          /* for the yoke; the default suits a real yoke */
    int     invert_x, invert_y;
    uint8_t in0, in1;          /* levels from the last read, active low */
    uint8_t adc_x, adc_y;
    int     coin_pulse[2];     /* IRQs left with the coin switch closed */
    /* The option yoke_autocenter: where the yoke rests is taken as its
     * middle. Learned from the first reading, and again whenever it lies
     * still for a while and the program says nobody is playing (learn). */
    int     autocenter, learn;
    int     centered, still_frames;
    float   center_x, center_y, still_x, still_y;
    float   digital_x, digital_y;  /* the digital yoke as it stands now (-1..1, eased) */
    int     self_test;         /* the self-test switch is on (set by the program, from its menu) */
    int     start_fires;       /* start is a fire button (to start a game); 0 in flight, where it changes the view */
} sw_controls;

void sw_controls_init(sw_controls *c, vp_input *in);

/* Once per frame: sample the cabinet. */
void sw_controls_read(sw_controls *c, vp_input *in);

/* Once per IRQ, before sw_irq: present the switches to the board. */
void sw_controls_tick(sw_controls *c, sw_machine *m);

#endif
