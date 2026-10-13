/* config.h - display settings: where the AVG picture lands on the tube
 * and how its colors and intensities become the board's 8-bit RGB.
 *
 * Read from starwarsvp.ini (key = value, ';' or '#' comments). The defaults
 * are a starting point for a Wells-Gardner 6100 driven by a USB-DVG; tune
 * them on the monitor with `starwarsvp --calibrate`.
 */
#ifndef SWVP_CONFIG_H
#define SWVP_CONFIG_H

#include <stdint.h>

typedef struct sw_display_cfg {
    /* The AVG window, in AVG units as sw_avg_run gives them (the game draws
     * at a quarter scale: its +-510 are +-127.5 here), stretched to the
     * board's 0..4095. */
    float avg_left, avg_right, avg_bottom, avg_top;
    /* Then a margin per side in board units (overscan / tube edge). */
    int margin_left, margin_right, margin_bottom, margin_top;
    int flip_x, flip_y;

    /* Intensity 0..255 -> beam: out = floor + (1 - floor) * (i/255)^gamma,
     * times brightness. floor keeps the faintest lit vectors visible. */
    float gamma;
    float floor;
    float brightness;

    /* The board's RGB for each of the AVG's 8 colors (index = 3-bit RGB),
     * at full intensity: the WG6100's guns are not equally bright. */
    uint8_t palette[8][3];

    /* FUZZ (the cabinet defocuses the beam for glows): 0 ignore, 1 draw the
     * vector twice, slightly apart. */
    int defocus_mode;

    /* 1: the sight is centered from the first frame (the yoke is passed
     * through exactly; the cabinet's joystick calibration sets its rest
     * position). 0: as the arcade machine, which learns the yoke's range
     * anew after every power-on. */
    int yoke_autocenter;
    /* How the picture moves between the game's frames. 0: not at all, every
     * game frame is shown as it is. 1, 2: its lines are moved
     * (render/smooth.h). 3: it is computed from the state of the game
     * (game/subframe.h), lines moved only where a phase has no view of its
     * own. */
    int smooth;
    /* 1: the game runs a frame every 12th interrupt, 20.5 a second, always:
     * the rate its program is written for ("20 PER SECOND"). 0: it waits as
     * long as the 6809 took, and slows down where the arcade machine did
     * (to 13 a second in the trench). */
    int steady_frames;
    /* 1: the strokes of a picture are drawn in an order that keeps the
     * beam's dark travel short (the board then manages more pictures a
     * second). 0: in the game's order. */
    int sort_strokes;
} sw_display_cfg;

void sw_display_cfg_defaults(sw_display_cfg *c);

/* Overlay the settings found in path. Returns 0, or -1 if the file cannot
 * be read (the settings are then unchanged). Unknown keys are reported on
 * stderr and ignored. */
int sw_display_cfg_load(sw_display_cfg *c, const char *path);

/* Write every setting (a template to edit). */
int sw_display_cfg_save(const sw_display_cfg *c, const char *path);

#endif
