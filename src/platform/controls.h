/* controls.h - the cabinet's controls (vp_input, bound in advmame.rc) as
 * the Star Wars board's switches and A/D inputs.
 *
 *   yoke        p1_stickx / p1_sticky   -> A/D channel 1 (yaw) / 0 (pitch)
 *   left fire   p1_button1, start1      -> IN0 D7 (also starts the game)
 *   right fire  p1_button4              -> IN0 D6
 *   left thumb  p1_button2              -> IN1 D5
 *   right thumb p1_button3              -> IN1 D4
 *   coins       coin1 / coin2           -> IN0 D1 / D0, as a 100 ms pulse
 *
 * The same buttons as MAME's Star Wars mapping. Coins are pulses timed in
 * IRQs, because MOOLAH accepts only 16..200 ms of switch closure.
 */
#ifndef SWVP_CONTROLS_H
#define SWVP_CONTROLS_H

#include "core/machine.h"

#include <vectorpie/vp_input.h>

typedef struct sw_controls {
    float   deadzone;          /* for the yoke; the default suits a real yoke */
    int     invert_x, invert_y;
    uint8_t in0, in1;          /* levels from the last read, active low */
    uint8_t adc_x, adc_y;
    int     coin_pulse[2];     /* IRQs left with the coin switch closed */
} sw_controls;

void sw_controls_init(sw_controls *c, vp_input *in);

/* Once per frame: sample the cabinet. */
void sw_controls_read(sw_controls *c, vp_input *in);

/* Once per IRQ, before sw_irq: present the switches to the board. */
void sw_controls_tick(sw_controls *c, sw_machine *m);

#endif
