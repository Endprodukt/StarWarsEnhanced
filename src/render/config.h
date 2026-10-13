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
     * times brightness (up to 2: the game draws its normal lines at the
     * intensity 80h, so 1.7 brings them near full, its bright ones to it).
     * floor keeps the faintest lit vectors visible. */
    float gamma;
    float floor;
    float brightness;

    /* The board's RGB for each of the AVG's 8 colors (index = 3-bit RGB),
     * at full intensity: the WG6100's guns are not equally bright. */
    uint8_t palette[8][3];

    /* FUZZ (the cabinet defocuses the beam for glows): 0 ignore, 1 draw the
     * vector twice, slightly apart. */
    int defocus_mode;

    /* 1: the sight is centered from the first frame: the yoke is passed
     * through exactly, and where it rests is taken as its middle (learned
     * at the start and whenever it lies still while nobody plays). 0: as
     * the arcade machine, which learns the yoke's range anew after every
     * power-on. */
    int yoke_autocenter;
    /* Where the yoke rests (-1..1 on both axes, 0 0: exactly in the
     * middle), once it has been learned (yoke_center_known): kept here, so
     * that the sight is right from the first frame after every start. */
    int   yoke_center_known;
    float yoke_center_x, yoke_center_y;
    /* How the picture moves between the game's frames. 3 (on): it is
     * computed from the state of the game (game/subframe.h), lines moved
     * only where a phase has no view of its own. 0 (off): every game frame
     * is shown as it is. (1 and 2, the lines moved between or ahead of the
     * last frames (render/smooth.h), are still read but no longer offered:
     * one shows the world a frame late, the other overshoots.) */
    int smooth;
    /* How the game's frames come. 1: at one fixed pace in each phase, the
     * average of the arcade machine there (13 a second in the trench, 20.5
     * on the banner): as fast as the machine, without its ups and downs.
     * 2: a frame every 12th interrupt, 20.5 a second, everywhere: the rate
     * the program is written for, and faster than the machine ever was
     * in play. 0: a frame takes as long as the 6809 took over it. */
    int steady_frames;
    /* 1: things are solid: what is behind a ship, a tower, the logo or the
     * X-wing's own guns is not drawn (render/solid.h). 0: wire frames you
     * see through, as the arcade machine drew them. */
    int occlusion;
    /* Enhanced, over the surface of the Death Star (render/enhance.h), each
     * 1 or 0: its plating drawn on the ground; stars in the sky; the towers
     * in true perspective, with more to them, seen from farther and fading
     * in; far off, TIE fighters chasing an X-wing. */
    int enh_surface, enh_stars, enh_towers, enh_dogfights;
    /* How a far tower is sized: 0 as the arcade machine (a linear scale
     * with the distance, 1 - XP/4000h, so that it grows out of the ground
     * as it comes), 1 the same with half that scale, 2 true perspective. */
    int enh_tower_size;
    /* Enhanced, in the trench: 1: what stands in it keeps its shape as it
     * comes near, its lines cut at the edge of the screen. 0: as the arcade
     * machine, which pushes the points of a near object to the edge. */
    int enh_clip;
    /* Enhanced: 1: the STAR WARS logo of the attract mode is yellow, as in
     * the films. 0: blue, as the arcade machine has it. */
    int logo_yellow;
    /* Enhanced: 1: sounds for which a recording is in the folder sounds/
     * are played from it (sound/samples.h, docs/SOUNDS.md). 0: every sound
     * comes from the board's chips, as on the arcade machine. */
    int sound_replace;
} sw_display_cfg;

void sw_display_cfg_defaults(sw_display_cfg *c);

/* Overlay the settings found in path. Returns 0, or -1 if the file cannot
 * be read (the settings are then unchanged). Unknown keys are reported on
 * stderr and ignored. */
int sw_display_cfg_load(sw_display_cfg *c, const char *path);

/* Write every setting (a template to edit). */
int sw_display_cfg_save(const sw_display_cfg *c, const char *path);

#endif
