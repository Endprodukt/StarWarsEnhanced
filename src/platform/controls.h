/* controls.h - the cabinet's controls (vp_input, bound in advmame.rc) as
 * the Star Wars board's switches and A/D inputs.
 *
 *   yoke        p1_stickx / p1_sticky   -> A/D channel 1 (yaw) / 0 (pitch)
 *   left fire   p1_button1, start1      -> IN0 D7 (also starts the game)
 *   right fire  p1_button4              -> IN0 D6
 *   left thumb  p1_button2              -> IN1 D5
 *   right thumb p1_button3              -> IN1 D4
 *   coins       coin1 / coin2           -> IN0 D1 / D0, as a 100 ms pulse
 *   aux coin    p1_button5              -> IN0 D2 (the operator's button: next page in self test)
 *   self test   p1_button6              -> opens Star Wars Enhanced's own test menu (platform/menu.h);
 *                                          from there IN0 D4, the board's self-test switch
 *
 * The same buttons as MAME's Star Wars mapping. Coins are pulses timed in
 * IRQs, because MOOLAH accepts only 16..200 ms of switch closure.
 */
#ifndef SWE_CONTROLS_H
#define SWE_CONTROLS_H

#include "core/machine.h"

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
    int     self_test;         /* the self-test switch is on (set by the program, from its menu) */
    int     start_fires;       /* start is a fire button (to start a game); 0 in flight, where it changes the view */
} sw_controls;

void sw_controls_init(sw_controls *c, vp_input *in);

/* Once per frame: sample the cabinet. */
void sw_controls_read(sw_controls *c, vp_input *in);

/* Once per IRQ, before sw_irq: present the switches to the board. */
void sw_controls_tick(sw_controls *c, sw_machine *m);

#endif
