#include "render/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const COLOR_NAMES[8] = {
    "black", "blue", "green", "turquoise", "red", "purple", "yellow", "white"
};

void sw_display_cfg_defaults(sw_display_cfg *c)
{
    memset(c, 0, sizeof *c);
    /* The game's screen edges VGEDGL/R/B/T (-510..510, -612..600 in its
     * vector words) at the scale it draws at, VGSCAL: a quarter. */
    c->avg_left = -127.5f;
    c->avg_right = 127.5f;
    c->avg_bottom = -153.0f;
    c->avg_top = 150.0f;
    c->margin_left = c->margin_right = c->margin_bottom = c->margin_top = 64;
    c->gamma = 1.0f;
    c->floor = 0.12f;
    c->brightness = 1.7f;       /* the game's normal line (COLOR 80h) at 95 %, its bright ones at full */
    for (int i = 0; i < 8; i++) {
        c->palette[i][0] = (i & 4) ? 255 : 0;
        c->palette[i][1] = (i & 2) ? 255 : 0;
        c->palette[i][2] = (i & 1) ? 255 : 0;
    }
    c->defocus_mode = 1;
    c->yoke_autocenter = 1;
    c->smooth = 3;
    c->steady_frames = 1;
    c->occlusion = 1;
    c->enh_surface = c->enh_stars = c->enh_towers = c->enh_dogfights = 1;
    c->logo_yellow = 1;
    c->enh_clip = 1;
    c->enh_falcon = 1;
    c->enh_blast = 1;
    c->enh_camera = 1;
    c->enh_demo = 1;
    c->sound_replace = 1;
    c->enh_engine = 1;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static int set(sw_display_cfg *c, const char *key, const char *val)
{
    struct { const char *k; float *f; } floats[] = {
        { "yoke_center_x", &c->yoke_center_x }, { "yoke_center_y", &c->yoke_center_y },
        { "avg_left", &c->avg_left }, { "avg_right", &c->avg_right },
        { "avg_bottom", &c->avg_bottom }, { "avg_top", &c->avg_top },
        { "gamma", &c->gamma }, { "floor", &c->floor }, { "brightness", &c->brightness },
    };
    struct { const char *k; int *i; } ints[] = {
        { "margin_left", &c->margin_left }, { "margin_right", &c->margin_right },
        { "margin_bottom", &c->margin_bottom }, { "margin_top", &c->margin_top },
        { "flip_x", &c->flip_x }, { "flip_y", &c->flip_y },
        { "defocus_mode", &c->defocus_mode },
        { "yoke_autocenter", &c->yoke_autocenter }, { "yoke_center_known", &c->yoke_center_known }, { "smooth", &c->smooth },
        { "steady_frames", &c->steady_frames },
        { "occlusion", &c->occlusion },
        { "enh_surface", &c->enh_surface }, { "enh_stars", &c->enh_stars },
        { "enh_towers", &c->enh_towers }, { "enh_dogfights", &c->enh_dogfights },
        { "enh_clip", &c->enh_clip }, { "enh_falcon", &c->enh_falcon }, { "enh_blast", &c->enh_blast }, { "enh_camera", &c->enh_camera },
        { "enh_view", &c->enh_view }, { "enh_demo", &c->enh_demo },
        { "logo_yellow", &c->logo_yellow }, { "sound_replace", &c->sound_replace }, { "enh_engine", &c->enh_engine },
    };
    if (strcmp(key, "sort_strokes") == 0) return 0;     /* (gone: settings files from before may still have it) */
    for (size_t i = 0; i < sizeof floats / sizeof floats[0]; i++)
        if (strcmp(key, floats[i].k) == 0) { *floats[i].f = (float)atof(val); return 0; }
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++)
        if (strcmp(key, ints[i].k) == 0) { *ints[i].i = atoi(val); return 0; }
    if (strncmp(key, "color_", 6) == 0) {
        for (int i = 0; i < 8; i++) {
            if (strcmp(key + 6, COLOR_NAMES[i]) == 0) {
                int r, g, b;
                if (sscanf(val, "%d %d %d", &r, &g, &b) != 3) return -1;
                c->palette[i][0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
                c->palette[i][1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
                c->palette[i][2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
                return 0;
            }
        }
    }
    return -1;
}

int sw_display_cfg_load(sw_display_cfg *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strpbrk(line, ";#");
        if (hash) *hash = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line), *val = trim(eq + 1);
        if (*key && set(c, key, val) != 0)
            fprintf(stderr, "%s:%d: unknown or bad setting '%s'\n", path, lineno, key);
    }
    fclose(f);
    return 0;
}

int sw_display_cfg_save(const sw_display_cfg *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "; Star Wars Enhanced display settings (swenhanced --calibrate to tune)\n\n");
    fprintf(f, "; AVG window (AVG units) stretched to the board's 0..4095\n");
    fprintf(f, "avg_left = %g\navg_right = %g\navg_bottom = %g\navg_top = %g\n",
            c->avg_left, c->avg_right, c->avg_bottom, c->avg_top);
    fprintf(f, "\n; margins in board units\n");
    fprintf(f, "margin_left = %d\nmargin_right = %d\nmargin_bottom = %d\nmargin_top = %d\n",
            c->margin_left, c->margin_right, c->margin_bottom, c->margin_top);
    fprintf(f, "flip_x = %d\nflip_y = %d\n", c->flip_x, c->flip_y);
    fprintf(f, "\n; beam = floor + (1 - floor) * (intensity/255)^gamma, times brightness (up to 2;\n"
               "; the game's normal lines have intensity 80h, so 1.7 puts them near full)\n");
    fprintf(f, "gamma = %g\nfloor = %g\nbrightness = %g\n", c->gamma, c->floor, c->brightness);
    fprintf(f, "\n; board RGB per AVG color at full intensity\n");
    for (int i = 1; i < 8; i++)
        fprintf(f, "color_%s = %d %d %d\n", COLOR_NAMES[i],
                c->palette[i][0], c->palette[i][1], c->palette[i][2]);
    fprintf(f, "\n; FUZZ glow: 0 ignore, 1 double-draw\ndefocus_mode = %d\n", c->defocus_mode);
    fprintf(f, "\n; sight centered from the first frame, the yoke's rest taken as its middle\n"
               "; (0: learns the yoke anew, as the arcade machine)\n"
               "yoke_autocenter = %d\n", c->yoke_autocenter);
    fprintf(f, "; where the yoke rests, learned (-1..1; kept from start to start)\n"
               "yoke_center_known = %d\nyoke_center_x = %g\nyoke_center_y = %g\n",
            c->yoke_center_known, c->yoke_center_x, c->yoke_center_y);
    fprintf(f, "\n; motion between the game's frames: 3 computed from the state of the game, 0 off\n"
               "smooth = %d\n", c->smooth);
    fprintf(f, "\n; the game's frames: 1 at one fixed pace in each phase, as fast as the arcade\n"
               "; machine was there on average; 2 always 20.5 a second (faster than the\n"
               "; machine in play); 0 as long as its 6809 took over each\n"
               "steady_frames = %d\n", c->steady_frames);
    fprintf(f, "\n; enhanced: things are solid, what is behind them is not drawn (0: wire frames\n"
               "; you see through, as the arcade machine)\n"
               "occlusion = %d\n", c->occlusion);
    fprintf(f, "\n; enhanced, over the surface of the Death Star: the ground drawn, stars in the sky,\n"
               "; towers in true perspective with more detail and from farther, chases far off\n"
               "enh_surface = %d\nenh_stars = %d\nenh_towers = %d\nenh_dogfights = %d\n",
            c->enh_surface, c->enh_stars, c->enh_towers, c->enh_dogfights);
    fprintf(f, "\n; enhanced, in the trench: near objects keep their shape, cut at the edge of the screen\n"
               "enh_clip = %d\n", c->enh_clip);
    fprintf(f, "\n; enhanced, in the trench: shortly before the exhaust port the Millennium Falcon\n"
               "; flies over the player\n"
               "enh_falcon = %d\n", c->enh_falcon);
    fprintf(f, "\n; enhanced: the player's X-wing blows up when it is lost\n"
               "enh_blast = %d\n", c->enh_blast);
    fprintf(f, "\n; enhanced: the camera's turns towards the enemy it tracks gather speed and slow\n"
               "; down (0: at full rate from the first frame, as the arcade machine)\n"
               "enh_camera = %d\n", c->enh_camera);
    fprintf(f, "\n; enhanced: from where the player sees in flight: 0 as the arcade machine (the\n"
               "; X-wing's nose and guns), 1 from inside the cockpit, 2 from behind the X-wing\n"
               "; (the start button steps through them in play)\n"
               "enh_view = %d\n", c->enh_view);
    fprintf(f, "\n; enhanced: a demo flight in the attract mode, each part from another view\n"
               "enh_demo = %d\n", c->enh_demo);
    fprintf(f, "\n; enhanced: the STAR WARS logo yellow (0: blue, as the arcade machine)\n"
               "logo_yellow = %d\n", c->logo_yellow);
    fprintf(f, "\n; enhanced: play sounds/NAME.wav in place of the sound NAME where there is such a file\n"
               "sound_replace = %d\n", c->sound_replace);
    fprintf(f, "\n; enhanced: the X-wing's engine is heard in flight (inside or outside, as the view)\n"
               "enh_engine = %d\n", c->enh_engine);
    fclose(f);
    return 0;
}
