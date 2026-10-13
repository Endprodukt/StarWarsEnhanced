/* view.h - the game's perspective, for what the renderer draws itself.
 *
 * Measured against the math box, which agrees to the last digit: a point
 * P of the world is seen at
 *     V = R (P - T) / 2          R: the player's unit vectors, T: his place
 * (math box PRE2: V.x ahead, V.y across, V.z up), a point of an object's
 * shape adds R times itself, and on the screen that is 512 V.y / V.x
 * across and 512 V.z / V.x up, in the units of the vector words; 1/X as
 * the divider gives it is 800000h / V.x. The list is drawn at VGSCAL and
 * VGOFFY above the middle.
 */
#ifndef SWVP_VIEW_H
#define SWVP_VIEW_H

#define SW_VIEW_PERSPECTIVE 512.0
#define SW_VIEW_W_UNIT      8388608.0
#define SW_VIEW_SCALE       (255.0f / 256.0f / 4.0f)
#define SW_VIEW_OFFY        (-104.0f)       /* VGOFFY */
#define SW_VIEW_NEAR        48.0            /* nothing nearer is drawn */
/* the edges of the screen (VGEDGL/R/B/T at the scale the game draws at) */
#define SW_VIEW_X0          -127.5f
#define SW_VIEW_X1          127.5f
#define SW_VIEW_Y0          -153.0f
#define SW_VIEW_Y1          150.0f

/* Where the point V is on the screen (AVG units), and its 1/X. */
static inline void sw_view_project(double vx, double vy, double vz, float *x, float *y, float *w)
{
    *x = (float)(SW_VIEW_PERSPECTIVE * vy / vx) * SW_VIEW_SCALE;
    *y = ((float)(SW_VIEW_PERSPECTIVE * vz / vx) + SW_VIEW_OFFY) * SW_VIEW_SCALE;
    double q = SW_VIEW_W_UNIT / vx;
    *w = (float)(q > 65535.0 ? 65535.0 : q < 1.0 ? 1.0 : q);
}

/* The line from a to b (V), cut at the nearest the player sees and at the
 * edges of the screen. Returns 0 if nothing of it is left. */
static inline int sw_view_line(const double *a, const double *b, float *x0, float *y0, float *w0,
                               float *x1, float *y1, float *w1)
{
    double p[3] = { a[0], a[1], a[2] }, q[3] = { b[0], b[1], b[2] };
    if (p[0] < SW_VIEW_NEAR && q[0] < SW_VIEW_NEAR) return 0;
    if (p[0] < SW_VIEW_NEAR) {
        double k = (SW_VIEW_NEAR - p[0]) / (q[0] - p[0]);
        p[1] += (q[1] - p[1]) * k; p[2] += (q[2] - p[2]) * k; p[0] = SW_VIEW_NEAR;
    }
    if (q[0] < SW_VIEW_NEAR) {
        double k = (SW_VIEW_NEAR - q[0]) / (p[0] - q[0]);
        q[1] += (p[1] - q[1]) * k; q[2] += (p[2] - q[2]) * k; q[0] = SW_VIEW_NEAR;
    }
    float ax, ay, aw, bx, by, bw;
    sw_view_project(p[0], p[1], p[2], &ax, &ay, &aw);
    sw_view_project(q[0], q[1], q[2], &bx, &by, &bw);
    /* Liang-Barsky against the screen */
    float t0 = 0.0f, t1 = 1.0f, dx = bx - ax, dy = by - ay;
    float pp[4] = { -dx, dx, -dy, dy };
    float qq[4] = { ax - SW_VIEW_X0, SW_VIEW_X1 - ax, ay - SW_VIEW_Y0, SW_VIEW_Y1 - ay };
    for (int i = 0; i < 4; i++) {
        if (pp[i] == 0.0f) { if (qq[i] < 0.0f) return 0; continue; }
        float r = qq[i] / pp[i];
        if (pp[i] < 0.0f) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    *x0 = ax + dx * t0; *y0 = ay + dy * t0; *w0 = aw + (bw - aw) * t0;
    *x1 = ax + dx * t1; *y1 = ay + dy * t1; *w1 = aw + (bw - aw) * t1;
    return 1;
}

#endif
