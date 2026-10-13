/* menu.c - see menu.h. */
#include "platform/menu.h"

#include "game/game.h"
#include "sound/samples.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define N_SOUNDS SW_SOUND_REQUESTS

const char *sw_menu_sound_name(int n)
{
    return sw_sound_name(n);
}

/* The lines, in three parts. The game's options are those of the arcade
 * program's option page (TCEROM.MAC, T.OPT), in its order; their values
 * are named as its messages name them. */
enum {
    I_OCCLUSION, I_SURFACE, I_STARS, I_TOWERS, I_DOGFIGHTS, I_CLIP, I_LOGO, I_SAMPLES, I_YOKE, I_SMOOTH, I_STEADY,
    I_OPT0, I_OPT_END = I_OPT0 + SW_GAME_OPTIONS,
    I_SOUND = I_OPT_END, I_CALIBRATE, I_OPERATOR, I_EXIT,
    N_ITEMS
};

static const char *const OPTION_NAME[SW_GAME_OPTIONS] = {
    "COIN VALUE", "LEFT MECH", "RIGHT MECH", "BONUS ADDER",
    "STARTING SHIELDS", "DIFFICULTY", "BONUS SHIELDS", "MUSIC IN ATTRACT",
};
static const char *const OPTION_VALUE[SW_GAME_OPTIONS][8] = {
    { "FREE PLAY", "2 CREDITS", "1 CREDIT", "1/2 CREDIT" },
    { "1 COIN", "2 COINS" },
    { "1 COIN", "4 COINS", "5 COINS", "6 COINS" },
    { "NONE", "2 GIVES 1", "4 GIVES 1", "4 GIVES 2", "5 GIVES 1", "3 GIVES 1", "NONE", "NONE" },
    { "6", "7", "8", "9" },
    { "EASY", "MODERATE", "HARD", "HARDEST" },
    { "0", "1", "2", "3" },
    { "YES", "NO" },
};

/* Where the yoke is held, as a direction: -1, 0, 1. The cabinet's yoke is
 * analog; a keyboard gives the digital actions. */
static int direction(vp_input *in, const char *axis, const char *minus, const char *plus)
{
    float v = vp_input_axis(in, axis);
    if (v < -0.5f || vp_input_held(in, minus)) return -1;
    if (v > 0.5f || vp_input_held(in, plus)) return 1;
    return 0;
}

static int step(int v, int d, int lo, int hi)
{
    v += d;
    return v > hi ? lo : v < lo ? hi : v;
}

int sw_menu_update(sw_menu *u, sw_display_cfg *cfg, sw_machine *m, vp_input *in, double now)
{
    if (u->sound < 1 || u->sound >= N_SOUNDS) u->sound = 3;

    /* a push of the yoke counts once, and again and again while it is held */
    int x = direction(in, "p1_stickx", "p1_left", "p1_right");
    int y = direction(in, "p1_sticky", "p1_up", "p1_down");     /* up is the low end */
    int dx = 0, dy = 0;
    if (x != u->dir_x || y != u->dir_y) {
        dx = x != u->dir_x ? x : 0;
        dy = y != u->dir_y ? y : 0;
        u->repeat_at = now + 0.5;
    } else if ((x || y) && now >= u->repeat_at) {
        dx = x;
        dy = y;
        u->repeat_at = now + (u->item == I_SOUND && x ? 0.08 : 0.2);
    }
    u->dir_x = x;
    u->dir_y = y;
    int fire = vp_input_pressed(in, "p1_button1") || vp_input_pressed(in, "start1");

    if (dy) u->item = step(u->item, dy, 0, N_ITEMS - 1);
    int d = dx ? dx : (fire ? 1 : 0);
    if (u->item >= I_OPT0 && u->item < I_OPT_END) {
        if (!d) return SW_MENU_NOTHING;
        int i = u->item - I_OPT0, max;
        int v = sw_option_get(m, i, &max);
        sw_option_set(m, i, step(v, d, 0, max));
        return SW_MENU_OPTION;
    }
    switch (u->item) {
    case I_OCCLUSION:
        if (d) { cfg->occlusion = !cfg->occlusion; return SW_MENU_CHANGED; }
        break;
    case I_SURFACE:
        if (d) { cfg->enh_surface = !cfg->enh_surface; return SW_MENU_CHANGED; }
        break;
    case I_STARS:
        if (d) { cfg->enh_stars = !cfg->enh_stars; return SW_MENU_CHANGED; }
        break;
    case I_TOWERS:
        if (d) { cfg->enh_towers = !cfg->enh_towers; return SW_MENU_CHANGED; }
        break;
    case I_DOGFIGHTS:
        if (d) { cfg->enh_dogfights = !cfg->enh_dogfights; return SW_MENU_CHANGED; }
        break;
    case I_CLIP:
        if (d) { cfg->enh_clip = !cfg->enh_clip; return SW_MENU_CHANGED; }
        break;
    case I_LOGO:
        if (d) { cfg->logo_yellow = !cfg->logo_yellow; return SW_MENU_CHANGED; }
        break;
    case I_SAMPLES:
        if (d) { cfg->sound_replace = !cfg->sound_replace; return SW_MENU_CHANGED; }
        break;
    case I_YOKE:
        if (fire) { cfg->yoke_autocenter = !cfg->yoke_autocenter; return SW_MENU_CHANGED; }    /* (not by the yoke itself) */
        break;
    case I_SMOOTH:      /* on: the pictures in between computed from the game (3); off: none (0) */
        if (d) { cfg->smooth = cfg->smooth ? 0 : 3; return SW_MENU_CHANGED; }
        break;
    case I_STEADY:
        if (d) { cfg->steady_frames = step(cfg->steady_frames, d, 0, 2); return SW_MENU_CHANGED; }
        break;
    case I_SOUND:
        if (dx) u->sound = step(u->sound, dx, 1, N_SOUNDS - 1);
        if (fire) return SW_MENU_SOUND;
        break;
    case I_CALIBRATE:
        if (fire) return SW_MENU_CALIBRATE;
        break;
    case I_OPERATOR:
        if (fire) return SW_MENU_OPERATOR;
        break;
    case I_EXIT:
        if (fire) return SW_MENU_CLOSE;
        break;
    }
    return SW_MENU_NOTHING;
}

static const char *on_off(int v) { return v ? "ON" : "OFF"; }

void sw_menu_draw(const sw_menu *u, const sw_display_cfg *cfg, const sw_machine *m, vp_dvg *d)
{
    static const char *const STEADY[] = { "AS THE ARCADE", "FIXED PER PHASE", "ALWAYS 20" };
    char value[N_ITEMS][40];
    const char *label[N_ITEMS];
    memset(value, 0, sizeof value);
    label[I_OCCLUSION] = "OCCLUSION";       snprintf(value[I_OCCLUSION], 40, "%s", on_off(cfg->occlusion));
    label[I_SURFACE] = "SURFACE PLATING";   snprintf(value[I_SURFACE], 40, "%s", on_off(cfg->enh_surface));
    label[I_STARS] = "TRENCH STARS";        snprintf(value[I_STARS], 40, "%s", on_off(cfg->enh_stars));
    label[I_TOWERS] = "DETAILED TOWERS";    snprintf(value[I_TOWERS], 40, "%s", on_off(cfg->enh_towers));
    label[I_DOGFIGHTS] = "DISTANT DOGFIGHTS"; snprintf(value[I_DOGFIGHTS], 40, "%s", on_off(cfg->enh_dogfights));
    label[I_CLIP] = "TRENCH CLIPPING";      snprintf(value[I_CLIP], 40, "%s", on_off(cfg->enh_clip));
    label[I_LOGO] = "YELLOW LOGO";          snprintf(value[I_LOGO], 40, "%s", on_off(cfg->logo_yellow));
    label[I_SAMPLES] = "REPLACED SOUNDS";   snprintf(value[I_SAMPLES], 40, "%s", on_off(cfg->sound_replace));
    label[I_YOKE] = "YOKE AUTO CENTER";     snprintf(value[I_YOKE], 40, "%s", on_off(cfg->yoke_autocenter));
    label[I_SMOOTH] = "SMOOTH MOTION";      snprintf(value[I_SMOOTH], 40, "%s", on_off(cfg->smooth));
    label[I_STEADY] = "FRAME PACE";         snprintf(value[I_STEADY], 40, "%s", STEADY[cfg->steady_frames % 3]);
    for (int i = 0; i < SW_GAME_OPTIONS; i++) {
        int max;
        int v = sw_option_get(m, i, &max);
        label[I_OPT0 + i] = OPTION_NAME[i];
        snprintf(value[I_OPT0 + i], 40, "%s", OPTION_VALUE[i][v & 7] ? OPTION_VALUE[i][v & 7] : "?");
    }
    label[I_SOUND] = "SOUND TEST";          snprintf(value[I_SOUND], 40, "%02X %s", u->sound, sw_menu_sound_name(u->sound));
    label[I_CALIBRATE] = "SCREEN CALIBRATION";
    label[I_OPERATOR] = "ARCADE OPERATOR SCREENS";
    label[I_EXIT] = "SAVE AND EXIT";

    /* the lines, 118 apart, a gap before each part's heading */
    const float TOP = 3560.0f, STEP = 118.0f, GAP = 150.0f;
    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 200, 200, 60);
    vp_text(d, "STAR WARS VP  TEST MENU", 2048, 3800, 100, VP_TEXT_CENTER);
    for (int i = 0; i < N_ITEMS; i++) {
        float y = TOP - STEP * (float)(i + 1) - (i >= I_OPT0 ? GAP : 0.0f) - (i >= I_SOUND ? GAP : 0.0f);
        const char *heading = i == 0 ? "ENHANCED SETTINGS" : i == I_OPT0 ? "GAME OPTIONS" : i == I_SOUND ? "TESTS" : NULL;
        if (heading) {
            vp_color(d, 60, 160, 200);
            vp_text(d, heading, 600, y + STEP + 10.0f, 62, VP_TEXT_LEFT);
        }
        if (i == u->item) vp_color(d, 255, 255, 255);
        else vp_color(d, 120, 120, 120);
        if (i == u->item) {             /* an arrow before the line chosen */
            vp_moveto(d, 600, (int)y + 60);
            vp_lineto(d, 680, (int)y + 32);
            vp_lineto(d, 600, (int)y + 4);
        }
        vp_text(d, label[i], 760, y, 66, VP_TEXT_LEFT);
        if (value[i][0]) vp_text(d, value[i], 3500, y, 66, VP_TEXT_RIGHT);
    }
    vp_color(d, 110, 110, 110);
    vp_text(d, "YOKE CHOOSE AND CHANGE   FIRE DO   TAB EXIT", 2048, 240, 56, VP_TEXT_CENTER);
}
