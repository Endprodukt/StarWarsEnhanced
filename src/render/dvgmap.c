#include "render/dvgmap.h"

#include <math.h>

#define BOARD_MAX 4095

void sw_map_point(const sw_display_cfg *c, float ax, float ay, int *bx, int *by)
{
    float x0 = (float)c->margin_left, x1 = (float)(BOARD_MAX - c->margin_right);
    float y0 = (float)c->margin_bottom, y1 = (float)(BOARD_MAX - c->margin_top);
    float u = (ax - c->avg_left) / (c->avg_right - c->avg_left);
    float v = (ay - c->avg_bottom) / (c->avg_top - c->avg_bottom);
    if (c->flip_x) u = 1.0f - u;
    if (c->flip_y) v = 1.0f - v;
    *bx = (int)lroundf(x0 + u * (x1 - x0));
    *by = (int)lroundf(y0 + v * (y1 - y0));
}

void sw_map_color(const sw_display_cfg *c, unsigned color, unsigned intensity,
                  uint8_t *r, uint8_t *g, uint8_t *b)
{
    float k = 0.0f;
    if (intensity) {
        k = c->floor + (1.0f - c->floor) * powf((float)intensity / 255.0f, c->gamma);
        k *= c->brightness;
        if (k > 1.0f) k = 1.0f;
    }
    const uint8_t *p = c->palette[color & 7];
    *r = (uint8_t)lroundf(p[0] * k);
    *g = (uint8_t)lroundf(p[1] * k);
    *b = (uint8_t)lroundf(p[2] * k);
}

void sw_draw_lines(vp_dvg *d, const sw_display_cfg *c, const sw_line *lines, int count)
{
    int have_pos = 0, bx = 0, by = 0;
    int have_col = 0;
    uint8_t cr = 0, cg = 0, cb = 0;

    for (int i = 0; i < count; i++) {
        const sw_line *ln = &lines[i];
        uint8_t r, g, b;
        sw_map_color(c, ln->color, ln->intensity, &r, &g, &b);
        if (!(r | g | b)) continue;
        if (!have_col || r != cr || g != cg || b != cb) {
            vp_color(d, r, g, b);
            cr = r; cg = g; cb = b;
            have_col = 1;
        }
        int x0, y0, x1, y1;
        sw_map_point(c, ln->x0, ln->y0, &x0, &y0);
        sw_map_point(c, ln->x1, ln->y1, &x1, &y1);
        if (!have_pos || x0 != bx || y0 != by) vp_moveto(d, x0, y0);
        vp_lineto(d, x1, y1);
        if (ln->defocus && c->defocus_mode == 1) {
            /* A second pass a few units off thickens the line the way the
             * defocused beam did. */
            vp_moveto(d, x0 + 6, y0 + 6);
            vp_lineto(d, x1 + 6, y1 + 6);
            x1 += 6;
            y1 += 6;
        }
        bx = x1; by = y1;
        have_pos = 1;
    }
}
