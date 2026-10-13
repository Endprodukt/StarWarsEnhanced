#include "platform/controls.h"

#include <math.h>
#include <string.h>

#define COIN_PULSE_IRQS 25     /* about 100 ms closed: well inside 16..200 ms */

void sw_controls_init(sw_controls *c, vp_input *in)
{
    memset(c, 0, sizeof *c);
    c->deadzone = 0.02f;
    c->in0 = c->in1 = 0xff;
    c->adc_x = c->adc_y = 0x80;
    if (in) vp_input_set_deadzone(in, c->deadzone);
}

static uint8_t axis_to_adc(float v, int invert)
{
    if (invert) v = -v;
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;
    /* MAME's mapping: up and left are the low end, center 80. */
    return (uint8_t)lroundf(128.0f + v * 127.0f);
}

/* The yoke's own middle (yoke_autocenter): a yoke at rest further out than
 * REST_MAX is being held, not resting; it lies still if it moves less than
 * STILL_MAX over STILL_FRAMES readings (about two seconds). */
#define REST_MAX     0.35f
#define STILL_MAX    0.015f
#define STILL_FRAMES 100

/* A reading with the middle at `center` instead of 0, both ends where they were. */
static float recenter(float v, float center)
{
    return v >= center ? (v - center) / (1.0f - center) : (v - center) / (1.0f + center);
}

void sw_controls_read(sw_controls *c, vp_input *in)
{
    uint8_t in0 = 0xff, in1 = 0xff;
    if (vp_input_held(in, "p1_button1") || vp_input_held(in, "start1")) in0 &= (uint8_t)~SW_IN0_LFIRE;
    if (vp_input_held(in, "p1_button4") || vp_input_held(in, "start2")) in0 &= (uint8_t)~SW_IN0_RFIRE;
    if (vp_input_held(in, "p1_button2")) in1 &= (uint8_t)~SW_IN1_LTHUMB;
    if (vp_input_held(in, "p1_button3")) in1 &= (uint8_t)~SW_IN1_RTHUMB;
    if (vp_input_held(in, "p1_button5")) in0 &= (uint8_t)~SW_IN0_COINA;
    if (c->self_test) in0 &= (uint8_t)~SW_IN0_TEST;
    c->in0 = in0;
    c->in1 = in1;

    if (vp_input_pressed(in, "coin1") && c->coin_pulse[0] == 0) c->coin_pulse[0] = COIN_PULSE_IRQS;
    if (vp_input_pressed(in, "coin2") && c->coin_pulse[1] == 0) c->coin_pulse[1] = COIN_PULSE_IRQS;

    float x = vp_input_axis(in, "p1_stickx"), y = vp_input_axis(in, "p1_sticky");
    if (c->autocenter) {
        int near_middle = fabsf(x) < REST_MAX && fabsf(y) < REST_MAX;
        if (!c->centered && near_middle) {
            c->center_x = x;
            c->center_y = y;
            c->centered = 1;
        }
        if (fabsf(x - c->still_x) < STILL_MAX && fabsf(y - c->still_y) < STILL_MAX) {
            if (c->still_frames < STILL_FRAMES) c->still_frames++;
        } else {
            c->still_x = x;
            c->still_y = y;
            c->still_frames = 0;
        }
        if (c->learn && near_middle && c->still_frames >= STILL_FRAMES) {
            c->center_x += (c->still_x - c->center_x) * 0.1f;
            c->center_y += (c->still_y - c->center_y) * 0.1f;
        }
        if (c->centered) {
            x = recenter(x, c->center_x);
            y = recenter(y, c->center_y);
        }
    }
    c->adc_x = axis_to_adc(x, c->invert_x);
    c->adc_y = axis_to_adc(y, c->invert_y);
}

void sw_controls_tick(sw_controls *c, sw_machine *m)
{
    uint8_t in0 = c->in0;
    if (c->coin_pulse[0] > 0) { in0 &= (uint8_t)~SW_IN0_COINL; c->coin_pulse[0]--; }
    if (c->coin_pulse[1] > 0) { in0 &= (uint8_t)~SW_IN0_COINR; c->coin_pulse[1]--; }
    m->in.in0 = in0;
    m->in.in1 = c->in1;
    m->in.adc[0] = c->adc_y;
    m->in.adc[1] = c->adc_x;
}
