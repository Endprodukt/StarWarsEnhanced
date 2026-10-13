/* menu.c - see menu.h. */
#include "platform/menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The requests of the sound board in their order (SNDPBX.MAC, docs/SOUNDS.md). */
static const char *const SOUNDS[] = {
    "", "AUDTST", "PMTST",
    "SPKALW", "SPKELE", "SPKFOA", "SPKFOR", "SPKGRE", "SPKHAV", "SPKHIT", "SPKI", "SPKIMO", "SPKLET",
    "SPKLOS", "SPKR2N", "SPKR2T", "SPKRED", "SPKREM", "SPKSHK", "SPKSIZ", "SPKSKP", "SPKSTA", "SPKSTR",
    "SPKTHI", "SPKTRU", "SPKUSE", "SPKYAU",
    "PMBEN", "PMCNT", "PMDAR", "PMDES", "PMEND", "PM4TH", "PMREB", "PMRRP", "PMSF2", "PMTH5", "PMTHB",
    "AUDCR", "AUDDF", "AUDDR", "AUDNAY", "AUDPB", "AUDPBZ", "AUDPF", "AUDPH", "AUDPS", "AUDRC", "AUDRD",
    "AUDRS", "AUDRY", "AUDSH", "AUDSS", "AUDSX", "AUDTC", "AUDTCZ", "AUDTH", "AUDTL", "AUDXL", "AUDYEA",
};
#define N_SOUNDS ((int)(sizeof SOUNDS / sizeof SOUNDS[0]))

const char *sw_menu_sound_name(int n)
{
    return n > 0 && n < N_SOUNDS ? SOUNDS[n] : "";
}

enum {
    I_OCCLUSION, I_SURFACE, I_STARS, I_TOWERS, I_DOGFIGHTS, I_LOGO, I_YOKE, I_SMOOTH, I_STEADY, I_SORT,
    I_SOUND, I_CALIBRATE, I_OPERATOR, I_EXIT,
    N_ITEMS
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

int sw_menu_update(sw_menu *u, sw_display_cfg *cfg, vp_input *in, double now)
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
    case I_SMOOTH:
        if (d) { cfg->smooth = step(cfg->smooth, d, 0, 3); return SW_MENU_CHANGED; }
        break;
    case I_STEADY:
        if (d) { cfg->steady_frames = step(cfg->steady_frames, d, 0, 2); return SW_MENU_CHANGED; }
        break;
    case I_LOGO:
        if (d) { cfg->logo_yellow = !cfg->logo_yellow; return SW_MENU_CHANGED; }
        break;
    case I_YOKE:
        if (fire) { cfg->yoke_autocenter = !cfg->yoke_autocenter; return SW_MENU_CHANGED; }    /* (not by the yoke itself) */
        break;
    case I_SORT:
        if (d) { cfg->sort_strokes = !cfg->sort_strokes; return SW_MENU_CHANGED; }
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

void sw_menu_draw(const sw_menu *u, const sw_display_cfg *cfg, vp_dvg *d)
{
    static const char *const SMOOTH[] = { "OFF", "LINES BETWEEN", "LINES AHEAD", "COMPUTED" };
    static const char *const STEADY[] = { "AS THE ARCADE", "FIXED PER PHASE", "ALWAYS 20" };
    char value[N_ITEMS][40];
    const char *label[N_ITEMS];
    memset(value, 0, sizeof value);
    label[I_OCCLUSION] = "OCCLUSION";       snprintf(value[I_OCCLUSION], 40, "%s", on_off(cfg->occlusion));
    label[I_SURFACE] = "SURFACE PLATING";   snprintf(value[I_SURFACE], 40, "%s", on_off(cfg->enh_surface));
    label[I_STARS] = "SURFACE STARS";       snprintf(value[I_STARS], 40, "%s", on_off(cfg->enh_stars));
    label[I_TOWERS] = "DETAILED TOWERS";    snprintf(value[I_TOWERS], 40, "%s", on_off(cfg->enh_towers));
    label[I_DOGFIGHTS] = "DISTANT DOGFIGHTS"; snprintf(value[I_DOGFIGHTS], 40, "%s", on_off(cfg->enh_dogfights));
    label[I_LOGO] = "YELLOW LOGO";          snprintf(value[I_LOGO], 40, "%s", on_off(cfg->logo_yellow));
    label[I_YOKE] = "YOKE AUTO CENTER";     snprintf(value[I_YOKE], 40, "%s", on_off(cfg->yoke_autocenter));
    label[I_SMOOTH] = "SMOOTH MOTION";      snprintf(value[I_SMOOTH], 40, "%s", SMOOTH[cfg->smooth & 3]);
    label[I_STEADY] = "FRAME PACE";         snprintf(value[I_STEADY], 40, "%s", STEADY[cfg->steady_frames % 3]);
    label[I_SORT] = "STROKE SORTING";       snprintf(value[I_SORT], 40, "%s", on_off(cfg->sort_strokes));
    label[I_SOUND] = "SOUND TEST";          snprintf(value[I_SOUND], 40, "%02X %s", u->sound, sw_menu_sound_name(u->sound));
    label[I_CALIBRATE] = "SCREEN CALIBRATION";
    label[I_OPERATOR] = "ARCADE OPERATOR SCREENS";
    label[I_EXIT] = "SAVE AND EXIT";

    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 200, 200, 60);
    vp_text(d, "STAR WARS VP  TEST MENU", 2048, 3750, 110, VP_TEXT_CENTER);
    vp_color(d, 60, 160, 200);
    vp_text(d, "ENHANCED SETTINGS", 600, 3450, 70, VP_TEXT_LEFT);
    vp_text(d, "TESTS", 600, 3450 - (I_SOUND + 1) * 165 - 50, 70, VP_TEXT_LEFT);

    for (int i = 0; i < N_ITEMS; i++) {
        float y = 3450.0f - 165.0f * (float)(i + 1) - (i >= I_SOUND ? 230.0f : 0.0f);
        if (i == u->item) vp_color(d, 255, 255, 255);
        else vp_color(d, 120, 120, 120);
        if (i == u->item) {             /* an arrow before the line chosen */
            vp_moveto(d, 600, (int)y + 72);
            vp_lineto(d, 690, (int)y + 40);
            vp_lineto(d, 600, (int)y + 8);
        }
        vp_text(d, label[i], 760, y, 80, VP_TEXT_LEFT);
        if (value[i][0]) vp_text(d, value[i], 3500, y, 80, VP_TEXT_RIGHT);
    }
    vp_color(d, 110, 110, 110);
    vp_text(d, "YOKE CHOOSE AND CHANGE   FIRE DO   TEST BUTTON EXIT", 2048, 330, 60, VP_TEXT_CENTER);
}
