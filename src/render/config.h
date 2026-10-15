/* config.h - display settings: where the AVG picture lands on the tube
 * and how its colors and intensities become the board's 8-bit RGB.
 *
 * Read from swenhanced.ini (key = value, ';' or '#' comments). The defaults
 * are a starting point for a Wells-Gardner 6100 driven by a USB-DVG; tune
 * them on the monitor with `swenhanced --calibrate`.
 */
#ifndef SWE_CONFIG_H
#define SWE_CONFIG_H

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
    /* Enhanced, in the trench: 1: what stands in it keeps its shape as it
     * comes near, its lines cut at the edge of the screen. 0: as the arcade
     * machine, which pushes the points of a near object to the edge. */
    int enh_clip;
    /* Enhanced, in the trench: 1: shortly before the exhaust port the
     * Millennium Falcon flies over the player and on down the trench. */
    int enh_falcon;
    /* Enhanced: 1: the player's X-wing blows up when it is lost (in the
     * dogfight, and on the game's own flights when the shields are gone). */
    int enh_blast;
    /* Enhanced: from where the player sees in flight (render/enhance.h,
     * SW_VIEW_...): 0 (the default) the arcade machine's view, the X-wing's
     * nose and guns; 1 from inside its cockpit; 2 from behind it. The
     * start button steps through them in play; every game (and every
     * dogfight) begins from the arcade view, whatever was last chosen. */
    int enh_view;
    /* Enhanced: 1 (the default): in the attract mode, before the high
     * scores, a demo flight, each part of it from another view
     * (platform/demo.h). 0: the attract mode as the arcade machine's. */
    int enh_demo;
    /* Enhanced: 1: the camera's turns (the ship turning towards the enemy
     * it tracks, or the Death Star) gather speed and slow down, in place of
     * the arcade machine's turns at full rate from the first frame. */
    int enh_camera;
    /* Enhanced: 1: the STAR WARS logo of the attract mode is yellow, as in
     * the films. 0: blue, as the arcade machine has it. */
    int logo_yellow;
    /* Enhanced: 1: sounds for which a recording is in the folder sounds/
     * are played from it (sound/samples.h, docs/SOUNDS.md). 0: every sound
     * comes from the board's chips, as on the arcade machine. */
    int sound_replace;
    /* Enhanced: 1: the X-wing's own engine is heard in flight (a recording,
     * one from inside the cockpit, one from outside, as the view is; in the
     * dogfight it runs up as the ship speeds up). Needs sound_replace. */
    int enh_engine;
} sw_display_cfg;

void sw_display_cfg_defaults(sw_display_cfg *c);

/* Overlay the settings found in path. Returns 0, or -1 if the file cannot
 * be read (the settings are then unchanged). Unknown keys are reported on
 * stderr and ignored. */
int sw_display_cfg_load(sw_display_cfg *c, const char *path);

/* Write every setting (a template to edit). */
int sw_display_cfg_save(const sw_display_cfg *c, const char *path);

#endif
