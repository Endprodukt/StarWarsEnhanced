/* menu.c - see menu.h. */
#include "platform/menu.h"
#include "platform/bind.h"

#include "game/game.h"
#include "render/enhance.h"
#include "sound/samples.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define N_SOUNDS SW_SOUND_REQUESTS

const char *sw_menu_sound_name(int n)
{
    return sw_sound_name(n);
}

/* Every line the menu can show, on whichever page. */
enum {
    /* the pages, as lines of the main page */
    L_PAGE_OPTIONS, L_PAGE_GRAPHICS, L_PAGE_SOUND, L_PAGE_CONTROLS, L_PAGE_SCREEN,
    /* graphics */
    L_SMOOTH, L_CAMERA, L_STEADY, L_OCCLUSION, L_LOGO, L_SURFACE, L_TOWERS, L_DOGFIGHTS, L_STARS, L_CLIP, L_FALCON, L_COCKPIT, L_DEMO, L_BLAST,
    /* sound */
    L_SAMPLES, L_ENGINE, L_VOL_MUSIC, L_VOL_EFFECTS, L_VOL_SPEECH, L_VOL_ENGINE, L_SOUND_TEST,
    /* controls: the yoke, then one line per digital input (platform/bind.h) */
    L_YOKE, L_BIND0, L_BIND_END = L_BIND0 + ACT_COUNT, L_BIND_RESET = L_BIND_END,
    /* screen */
    L_CALIBRATE,
    /* the game's options, in the order of the arcade program's option page */
    L_OPT0, L_OPT_END = L_OPT0 + SW_GAME_OPTIONS,
    /* the rest of the main page */
    L_OPERATOR = L_OPT_END, L_EXIT,
    L_BACK,
    N_LINES
};

enum { P_MAIN, P_OPTIONS, P_GRAPHICS, P_SOUND, P_CONTROLS, P_SCREEN, N_PAGES };

#define PAGE_MAX 24

/* The lines of each page, in order; 0 is a gap before the line that
 * follows. */
static const struct { const char *title; uint8_t lines[PAGE_MAX + 4]; } PAGES[N_PAGES] = {
    { "TEST MENU", { L_PAGE_OPTIONS, L_PAGE_GRAPHICS, L_PAGE_SOUND, L_PAGE_CONTROLS, L_PAGE_SCREEN, 0, L_OPERATOR, L_EXIT, N_LINES } },
    { "GAME OPTIONS", { L_OPT0, L_OPT0 + 1, L_OPT0 + 2, L_OPT0 + 3, 0, L_OPT0 + 4, L_OPT0 + 5, L_OPT0 + 6, L_OPT0 + 7, 0, L_BACK, N_LINES } },
    { "GRAPHICS", { L_SMOOTH, L_CAMERA, L_STEADY, L_OCCLUSION, L_LOGO, L_COCKPIT, L_DEMO, L_BLAST, L_SURFACE, L_TOWERS, L_DOGFIGHTS, 0, L_STARS, L_CLIP, L_FALCON, 0, L_BACK, N_LINES } },
    { "SOUND", { L_SAMPLES, L_ENGINE, 0, L_VOL_MUSIC, L_VOL_EFFECTS, L_VOL_SPEECH, L_VOL_ENGINE, 0, L_SOUND_TEST, 0, L_BACK, N_LINES } },
    { "CONTROLS", { L_YOKE, 0,
                    L_BIND0 + ACT_LEFT, L_BIND0 + ACT_RIGHT, L_BIND0 + ACT_UP, L_BIND0 + ACT_DOWN,
                    L_BIND0 + ACT_FIRE_L, L_BIND0 + ACT_FIRE_R, L_BIND0 + ACT_THUMB_L, L_BIND0 + ACT_THUMB_R,
                    L_BIND0 + ACT_START, L_BIND0 + ACT_START2, L_BIND0 + ACT_COIN1, L_BIND0 + ACT_COIN2,
                    L_BIND0 + ACT_COIN_AUX, L_BIND0 + ACT_TEST, 0, L_BIND_RESET, L_BACK, N_LINES } },
    { "SCREEN", { L_CALIBRATE, 0, L_BACK, N_LINES } },
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

/* The lines of page p: how many, and which is the nth. */
static int page_lines(int p, uint8_t *out)
{
    int n = 0;
    for (int i = 0; PAGES[p].lines[i] != N_LINES && i < PAGE_MAX + 4; i++)
        if (PAGES[p].lines[i]) out[n++] = PAGES[p].lines[i];
    return n;
}

/* Where the yoke is held, as a direction: -1, 0, 1. The cabinet's yoke is
 * analog; the digital inputs (the arrow keys, the bound ones) come through
 * sw_bind_menu_held. */
static int direction(vp_input *in, const char *axis, int minus, int plus)
{
    float v = vp_input_axis(in, axis);
    if (v < -0.5f || minus) return -1;
    if (v > 0.5f || plus) return 1;
    return 0;
}

static int step(int v, int d, int lo, int hi)
{
    v += d;
    return v > hi ? lo : v < lo ? hi : v;
}

static int clamp_step(int v, int d, int lo, int hi)
{
    v += d;
    return v > hi ? hi : v < lo ? lo : v;
}

static int toggle(int *v) { *v = !*v; return SW_MENU_CHANGED; }

int sw_menu_update(sw_menu *u, sw_display_cfg *cfg, sw_machine *m, vp_input *in, double now)
{
    u->now = now;
    if (u->sound < 1 || u->sound >= N_SOUNDS) u->sound = 3;
    if (u->page < 0 || u->page >= N_PAGES) u->page = P_MAIN;
    uint8_t lines[PAGE_MAX];
    int n = page_lines(u->page, lines);
    if (u->item[u->page] >= n) u->item[u->page] = 0;
    int line = lines[u->item[u->page]];

    /* binding an input: nothing else counts until it is done */
    if (sw_bind_capturing()) {
        int r = sw_bind_capture();
        return r == 1 ? SW_MENU_CHANGED : SW_MENU_NOTHING;
    }

    /* a push of the yoke counts once, and again and again while it is held */
    sw_menu_keys k;
    sw_bind_menu_held(&k);
    int x = direction(in, "p1_stickx", k.left, k.right);
    int y = direction(in, "p1_sticky", k.up, k.down);     /* up is the low end */
    int dx = 0, dy = 0;
    if (x != u->dir_x || y != u->dir_y) {
        dx = x != u->dir_x ? x : 0;
        dy = y != u->dir_y ? y : 0;
        u->repeat_at = now + 0.5;
    } else if ((x || y) && now >= u->repeat_at) {
        dx = x;
        dy = y;
        u->repeat_at = now + (line == L_SOUND_TEST && x ? 0.08 : 0.2);
    }
    u->dir_x = x;
    u->dir_y = y;
    int fire = k.ok && !u->ok_was, back = k.back && !u->back_was;
    u->ok_was = k.ok;
    u->back_was = k.back;

    if (back && u->page != P_MAIN) { u->page = P_MAIN; return SW_MENU_NOTHING; }
    if (dy) { u->item[u->page] = step(u->item[u->page], dy, 0, n - 1); return SW_MENU_NOTHING; }
    int d = dx ? dx : (fire ? 1 : 0);
    if (!d) return SW_MENU_NOTHING;

    if (line >= L_OPT0 && line < L_OPT_END) {
        int i = line - L_OPT0, max;
        sw_option_set(m, i, step(sw_option_get(m, i, &max), d, 0, max));
        return SW_MENU_OPTION;
    }
    if (line >= L_BIND0 && line < L_BIND_END) {
        /* fire: wait for the input to bind; left or right: the cabinet's again */
        if (fire) sw_bind_capture_begin((sw_action)(line - L_BIND0));
        else { sw_bind_clear((sw_action)(line - L_BIND0)); return SW_MENU_CHANGED; }
        return SW_MENU_NOTHING;
    }
    switch (line) {
    case L_PAGE_OPTIONS:  if (fire) u->page = P_OPTIONS; break;
    case L_PAGE_GRAPHICS: if (fire) u->page = P_GRAPHICS; break;
    case L_PAGE_SOUND:    if (fire) u->page = P_SOUND; break;
    case L_PAGE_CONTROLS: if (fire) u->page = P_CONTROLS; break;
    case L_PAGE_SCREEN:   if (fire) u->page = P_SCREEN; break;
    case L_BACK:          if (fire) u->page = P_MAIN; break;
    case L_SMOOTH:        cfg->smooth = cfg->smooth ? 0 : 3; return SW_MENU_CHANGED;   /* 3: computed from the game */
    case L_STEADY:        cfg->steady_frames = step(cfg->steady_frames, d, 0, 2); return SW_MENU_CHANGED;
    case L_OCCLUSION:     return toggle(&cfg->occlusion);
    case L_LOGO:          return toggle(&cfg->logo_yellow);
    case L_SURFACE:       return toggle(&cfg->enh_surface);
    case L_TOWERS:        return toggle(&cfg->enh_towers);
    case L_DOGFIGHTS:     return toggle(&cfg->enh_dogfights);
    case L_STARS:         return toggle(&cfg->enh_stars);
    case L_CLIP:          return toggle(&cfg->enh_clip);
    case L_FALCON:        return toggle(&cfg->enh_falcon);
    case L_BLAST:         return toggle(&cfg->enh_blast);
    case L_DEMO:          return toggle(&cfg->enh_demo);
    case L_COCKPIT:       cfg->enh_view = step(cfg->enh_view % SW_VIEWS, d, 0, SW_VIEWS - 1); return SW_MENU_CHANGED;
    case L_CAMERA:        return toggle(&cfg->enh_camera);
    case L_SAMPLES:       return toggle(&cfg->sound_replace);
    case L_ENGINE:        return toggle(&cfg->enh_engine);
    case L_VOL_MUSIC:     cfg->vol_music = clamp_step(cfg->vol_music, d * 10, 0, 200); return SW_MENU_CHANGED;
    case L_VOL_EFFECTS:   cfg->vol_effects = clamp_step(cfg->vol_effects, d * 10, 0, 200); return SW_MENU_CHANGED;
    case L_VOL_SPEECH:    cfg->vol_speech = clamp_step(cfg->vol_speech, d * 10, 0, 200); return SW_MENU_CHANGED;
    case L_VOL_ENGINE:    cfg->vol_engine = clamp_step(cfg->vol_engine, d * 10, 0, 200); return SW_MENU_CHANGED;
    case L_SOUND_TEST:
        if (dx) u->sound = step(u->sound, dx, 1, N_SOUNDS - 1);
        if (fire) return SW_MENU_SOUND;
        break;
    case L_YOKE:          if (fire) return toggle(&cfg->yoke_autocenter); break;   /* (not by the yoke itself) */
    case L_BIND_RESET:    if (fire) { sw_bind_clear_all(); return SW_MENU_CHANGED; } break;
    case L_CALIBRATE:     if (fire) return SW_MENU_CALIBRATE; break;
    case L_OPERATOR:      if (fire) return SW_MENU_OPERATOR; break;
    case L_EXIT:          if (fire) return SW_MENU_CLOSE; break;
    }
    return SW_MENU_NOTHING;
}

static const char *on_off(int v) { return v ? "ON" : "OFF"; }

/* A line's label and its value ("" for none). */
static void describe(int line, const sw_menu *u, const sw_display_cfg *cfg, const sw_machine *m,
                     const char **label, char *value, size_t size)
{
    static const char *const STEADY[] = { "AS THE ARCADE", "FIXED PER PHASE", "ALWAYS 20" };
    static const char *const VIEW[SW_VIEWS] = { "ARCADE", "COCKPIT", "OUTSIDE" };
    value[0] = '\0';
    if (line >= L_OPT0 && line < L_OPT_END) {
        int i = line - L_OPT0, max;
        int v = sw_option_get(m, i, &max);
        *label = OPTION_NAME[i];
        snprintf(value, size, "%s", OPTION_VALUE[i][v & 7] ? OPTION_VALUE[i][v & 7] : "?");
        return;
    }
    if (line >= L_BIND0 && line < L_BIND_END) {
        sw_action a = (sw_action)(line - L_BIND0);
        *label = sw_act_name(a);
        if (sw_bind_capturing() && u->item[P_CONTROLS] < PAGE_MAX && PAGES[P_CONTROLS].lines[0] != N_LINES) {
            /* (the line being bound: the prompt, blinking) */
            uint8_t lines[PAGE_MAX];
            page_lines(P_CONTROLS, lines);
            if (lines[u->item[P_CONTROLS]] == line) {
                snprintf(value, size, "%s", ((int)(u->now * 3) & 1) ? "PRESS AN INPUT" : "");
                return;
            }
        }
        sw_bind_describe(a, value, size);
        return;
    }
    switch (line) {
    case L_PAGE_OPTIONS:  *label = "GAME OPTIONS"; break;
    case L_PAGE_GRAPHICS: *label = "GRAPHICS"; break;
    case L_PAGE_SOUND:    *label = "SOUND"; break;
    case L_PAGE_CONTROLS: *label = "CONTROLS"; break;
    case L_PAGE_SCREEN:   *label = "SCREEN"; break;
    case L_SMOOTH:        *label = "SMOOTH MOTION";     snprintf(value, size, "%s", on_off(cfg->smooth)); break;
    case L_STEADY:        *label = "FRAME PACE";        snprintf(value, size, "%s", STEADY[cfg->steady_frames % 3]); break;
    case L_OCCLUSION:     *label = "OCCLUSION";         snprintf(value, size, "%s", on_off(cfg->occlusion)); break;
    case L_LOGO:          *label = "YELLOW LOGO";       snprintf(value, size, "%s", on_off(cfg->logo_yellow)); break;
    case L_SURFACE:       *label = "SURFACE PLATING";   snprintf(value, size, "%s", on_off(cfg->enh_surface)); break;
    case L_TOWERS:        *label = "DETAILED TOWERS";   snprintf(value, size, "%s", on_off(cfg->enh_towers)); break;
    case L_DOGFIGHTS:     *label = "DISTANT DOGFIGHTS"; snprintf(value, size, "%s", on_off(cfg->enh_dogfights)); break;
    case L_STARS:         *label = "STARS";             snprintf(value, size, "%s", on_off(cfg->enh_stars)); break;
    case L_CLIP:          *label = "TRENCH CLIPPING";   snprintf(value, size, "%s", on_off(cfg->enh_clip)); break;
    case L_COCKPIT:       *label = "VIEW";              snprintf(value, size, "%s", VIEW[cfg->enh_view % SW_VIEWS]); break;
    case L_DEMO:          *label = "ATTRACT DEMO";      snprintf(value, size, "%s", on_off(cfg->enh_demo)); break;
    case L_FALCON:        *label = "FALCON FLY-OVER";   snprintf(value, size, "%s", on_off(cfg->enh_falcon)); break;
    case L_BLAST:         *label = "SHIP EXPLOSION";    snprintf(value, size, "%s", on_off(cfg->enh_blast)); break;
    case L_CAMERA:        *label = "SMOOTH CAMERA";     snprintf(value, size, "%s", on_off(cfg->enh_camera)); break;
    case L_SAMPLES:       *label = "REPLACED SOUNDS";   snprintf(value, size, "%s", on_off(cfg->sound_replace)); break;
    case L_ENGINE:        *label = "ENGINE SOUND";      snprintf(value, size, "%s", on_off(cfg->enh_engine)); break;
    case L_VOL_MUSIC:     *label = "MUSIC VOLUME";      snprintf(value, size, "%d", cfg->vol_music); break;
    case L_VOL_EFFECTS:   *label = "EFFECTS VOLUME";    snprintf(value, size, "%d", cfg->vol_effects); break;
    case L_VOL_SPEECH:    *label = "SPEECH VOLUME";     snprintf(value, size, "%d", cfg->vol_speech); break;
    case L_VOL_ENGINE:    *label = "ENGINE VOLUME";     snprintf(value, size, "%d", cfg->vol_engine); break;
    case L_SOUND_TEST:    *label = "SOUND TEST";        snprintf(value, size, "%02X %s", u->sound, sw_menu_sound_name(u->sound)); break;
    case L_YOKE:          *label = "YOKE AUTO CENTER";  snprintf(value, size, "%s", on_off(cfg->yoke_autocenter)); break;
    case L_BIND_RESET:    *label = "ALL INPUTS AS THE CABINET'S"; break;
    case L_CALIBRATE:     *label = "SCREEN CALIBRATION"; break;
    case L_OPERATOR:      *label = "ARCADE OPERATOR SCREENS"; break;
    case L_EXIT:          *label = "SAVE AND EXIT"; break;
    case L_BACK:          *label = "BACK"; break;
    default:              *label = "?"; break;
    }
}

void sw_menu_draw(const sw_menu *u, const sw_display_cfg *cfg, const sw_machine *m, vp_dvg *d)
{
    int page = u->page >= 0 && u->page < N_PAGES ? u->page : P_MAIN;
    /* the controls page has the most lines: closer together, a little smaller */
    const int dense = page == P_CONTROLS;
    const float STEP = dense ? 145.0f : 190.0f, GAP = dense ? 70.0f : 120.0f, TEXT = dense ? 66.0f : 90.0f;
    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 200, 200, 60);
    vp_text(d, page == P_MAIN ? "STAR WARS ENHANCED  TEST MENU" : PAGES[page].title, 2048, 3650, 90, VP_TEXT_CENTER);

    /* the lines, from the top, the gaps where the page has them; the
     * block centred on the screen */
    int n = 0;
    float height = 0.0f;
    for (int i = 0; PAGES[page].lines[i] != N_LINES && i < PAGE_MAX + 4; i++)
        height += PAGES[page].lines[i] ? STEP : GAP, n += PAGES[page].lines[i] != 0;
    float y = 2048.0f + height * 0.5f - STEP * 0.5f + (dense ? 60.0f : 150.0f);
    int k = 0;
    for (int i = 0; PAGES[page].lines[i] != N_LINES && i < PAGE_MAX + 4; i++) {
        int line = PAGES[page].lines[i];
        if (!line) { y -= GAP; continue; }
        const char *label;
        char value[48];
        describe(line, u, cfg, m, &label, value, sizeof value);
        int chosen = k == u->item[page];
        /* a bound input: lit while it is held (so the binding can be tried) */
        int lit = line >= L_BIND0 && line < L_BIND_END && sw_act_held((sw_action)(line - L_BIND0));
        if (chosen) vp_color(d, 255, 255, 255);
        else if (lit) vp_color(d, 80, 230, 80);
        else vp_color(d, 120, 120, 120);
        if (chosen) {                   /* an arrow before the line chosen */
            vp_moveto(d, 600, (int)(y + TEXT * 0.9f));
            vp_lineto(d, 690, (int)(y + TEXT * 0.5f));
            vp_lineto(d, 600, (int)(y + TEXT * 0.1f));
        }
        vp_text(d, label, 760, y, TEXT, VP_TEXT_LEFT);
        if (value[0]) vp_text(d, value, 3500, y, TEXT, VP_TEXT_RIGHT);
        y -= STEP;
        k++;
    }
    vp_color(d, 110, 110, 110);
    const char *hint = page == P_MAIN ? "YOKE CHOOSE   FIRE OPEN   F2 SAVE AND EXIT"
                     : page == P_CONTROLS ? "FIRE BIND   LEFT RIGHT CABINET'S   F2 SAVE AND EXIT"
                     : "YOKE CHOOSE AND CHANGE   FIRE DO   F2 SAVE AND EXIT";
    vp_text(d, hint, 2048, 330, 60, VP_TEXT_CENTER);
}
