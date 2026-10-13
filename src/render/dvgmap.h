/* dvgmap.h - AVG lines onto the VectorPie device layer. */
#ifndef SWE_DVGMAP_H
#define SWE_DVGMAP_H

#include "avg/avg.h"
#include "render/config.h"

#include <vectorpie/vp_dvg.h>

/* AVG units -> board units (0..4095, may fall outside: the SDK clips). */
void sw_map_point(const sw_display_cfg *c, float ax, float ay, int *bx, int *by);

/* The board color for an AVG color at an intensity (0..255). */
void sw_map_color(const sw_display_cfg *c, unsigned color, unsigned intensity,
                  uint8_t *r, uint8_t *g, uint8_t *b);

/* Draw lines in list order, chaining strokes where one line ends where the
 * next begins and setting the color only when it changes. */
void sw_draw_lines(vp_dvg *d, const sw_display_cfg *c, const sw_line *lines, int count);

#endif
