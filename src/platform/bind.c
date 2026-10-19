/* bind.c - see bind.h. The same way of laying out controls as every vector
 * game of this cabinet (the Tempest 2000 port's service menu); the menu's
 * own controls and the reading of everything come from vpbind.h. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "platform/bind.h"
#include "platform/vpbind.h"

#include <stdlib.h>
#include <string.h>

typedef enum { IN_NONE, IN_KEY, IN_BUTTON, IN_AXIS, IN_HAT, IN_MOUSE } in_type;

typedef struct {
    in_type type;
    int     joy;        /* joystick number, 0 = the first; -1 in AUTO: the yoke (by its name), else the first */
    int     code;       /* scancode, button, axis or hat number, mouse button (SDL_BUTTON_LEFT ...) */
    int     dir;        /* axis: -1 / +1; hat: SDL_HAT_* bit */
} input;

enum { M_AUTO, M_NONE, M_SET };         /* what an action has: AUTO, NONE, or its own inputs */

/* What AUTO is, read directly as everything else: the keys, the yoke's (or
 * the first joystick's) buttons and stick, and the mouse's buttons that
 * AdvanceMAME gives these by default (MAME's Star Wars mapping: fire left
 * p1_button1, fire right p1_button4, thumbs p1_button2 / 3, aux coin
 * p1_button5, test p1_button6). */
#define K(sc)        { IN_KEY, 0, SDL_SCANCODE_##sc, 0 }
#define JB(b)        { IN_BUTTON, -1, b, 0 }
#define JA(a, d)     { IN_AXIS, -1, a, d }
#define MB(b)        { IN_MOUSE, 0, SDL_BUTTON_##b, 0 }
static const input AUTO[ACT_COUNT][SW_BIND_SLOTS] = {
    [ACT_LEFT]     = { K(LEFT), JA(0, -1) },
    [ACT_RIGHT]    = { K(RIGHT), JA(0, 1) },
    [ACT_UP]       = { K(UP), JA(1, -1) },
    [ACT_DOWN]     = { K(DOWN), JA(1, 1) },
    [ACT_FIRE_L]   = { K(LCTRL), JB(0), MB(LEFT) },
    [ACT_FIRE_R]   = { K(LSHIFT), JB(3) },
    [ACT_THUMB_L]  = { K(LALT), JB(1), MB(RIGHT) },
    [ACT_THUMB_R]  = { K(SPACE), JB(2), MB(MIDDLE) },
    [ACT_START]    = { K(1) },
    [ACT_START2]   = { K(2) },
    [ACT_COIN1]    = { K(5) },
    [ACT_COIN2]    = { K(6) },
    [ACT_COIN_AUX] = { K(Z), JB(4) },
    [ACT_TEST]     = { K(X), JB(5) },
};

static const char *const NAMES[ACT_COUNT] = {
    "YOKE LEFT", "YOKE RIGHT", "YOKE UP", "YOKE DOWN",
    "FIRE LEFT", "FIRE RIGHT", "THUMB LEFT", "THUMB RIGHT",
    "START", "START 2", "COIN 1", "COIN 2", "AUX COIN", "TEST MENU",
};

static const char *const KEYS[ACT_COUNT] = {
    "bind_left", "bind_right", "bind_up", "bind_down",
    "bind_fire_left", "bind_fire_right", "bind_thumb_left", "bind_thumb_right",
    "bind_start", "bind_start2", "bind_coin1", "bind_coin2", "bind_coin_aux", "bind_test",
};

static input B[ACT_COUNT][SW_BIND_SLOTS];
static int mode[ACT_COUNT];             /* M_ */
static int held_now[ACT_COUNT], held_before[ACT_COUNT];
static vp_input *g_in;

#define MAX_JOYS 8
static SDL_Joystick *J[MAX_JOYS];
static int njoys = -1;

static struct {
    int active, replace;
    sw_action a;
    unsigned char keys[SDL_NUM_SCANCODES];
    unsigned char buttons[MAX_JOYS][32];
    signed char axes[MAX_JOYS][16];
    unsigned char hats[MAX_JOYS][4];
    Uint32 mouse;
} CAP;

static int f2_now, f2_before;

static void open_joysticks(void)
{
    int n = SDL_NumJoysticks();
    if (n == njoys) return;
    for (int i = 0; i < MAX_JOYS; i++) if (J[i]) { SDL_JoystickClose(J[i]); J[i] = NULL; }
    for (int i = 0; i < n && i < MAX_JOYS; i++) J[i] = SDL_JoystickOpen(i);
    njoys = n;
}

void sw_bind_init(void)
{
    open_joysticks();
}

static int count(sw_action a)
{
    int n = 0;
    for (int s = 0; s < SW_BIND_SLOTS; s++) n += B[a][s].type != IN_NONE;
    return n;
}

static int input_active(const input *in)
{
    int joy = in->joy < 0 ? (vpb_yoke() >= 0 ? vpb_yoke() : 0) : in->joy;
    switch (in->type) {
    case IN_KEY: {
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        return in->code > 0 && in->code < SDL_NUM_SCANCODES && k[in->code];
    }
    case IN_BUTTON:
        return joy < MAX_JOYS && J[joy] && SDL_JoystickGetButton(J[joy], in->code);
    case IN_AXIS: {
        if (joy >= MAX_JOYS || !J[joy]) return 0;
        int v = SDL_JoystickGetAxis(J[joy], in->code);
        return in->dir < 0 ? v < -16000 : v > 16000;
    }
    case IN_HAT:
        return joy < MAX_JOYS && J[joy] && (SDL_JoystickGetHat(J[joy], in->code) & in->dir);
    case IN_MOUSE:
        return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(in->code)) != 0;
    default:
        return 0;
    }
}

static int bound(sw_action a) { return mode[a] == M_SET; }

void sw_bind_update(vp_input *in)
{
    g_in = in;
    vpb_update();
    open_joysticks();
    for (int a = 0; a < ACT_COUNT; a++) {
        const input *set = mode[a] == M_SET ? B[a] : mode[a] == M_AUTO ? AUTO[a] : NULL;
        int h = 0;
        held_before[a] = held_now[a];
        for (int s = 0; set && s < SW_BIND_SLOTS; s++) h |= input_active(&set[s]);
        held_now[a] = h;
    }
    f2_before = f2_now;
    f2_now = vpb_key_held(SDL_SCANCODE_F2);
}

int sw_act_held(sw_action a) { return a < ACT_COUNT && held_now[a]; }
int sw_act_pressed(sw_action a) { return a < ACT_COUNT && held_now[a] && !held_before[a]; }
int sw_act_any_pressed(void)
{
    for (int a = 0; a < ACT_COUNT; a++) if (held_now[a] && !held_before[a]) return 1;
    return 0;
}

const char *sw_act_name(sw_action a) { return a < ACT_COUNT ? NAMES[a] : "?"; }
int sw_bind_is_bound(sw_action a) { return a < ACT_COUNT && bound(a); }

/* An input by what it is: "LEFT CTRL", "JOY1 BUTTON 3", "MOUSE LEFT". */
static void describe(const input *in, char *buf, size_t size)
{
    switch (in->type) {
    case IN_KEY: {
        const char *n = SDL_GetScancodeName((SDL_Scancode)in->code);
        snprintf(buf, size, "%s", n && *n ? n : "?");
        for (char *p = buf; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
        break;
    }
    case IN_BUTTON: snprintf(buf, size, "JOY%d BUTTON %d", in->joy + 1, in->code + 1); break;
    case IN_AXIS:   snprintf(buf, size, "JOY%d AXIS %d %c", in->joy + 1, in->code + 1, in->dir < 0 ? '-' : '+'); break;
    case IN_HAT: {
        const char *d = in->dir == SDL_HAT_UP ? "UP" : in->dir == SDL_HAT_DOWN ? "DOWN"
                      : in->dir == SDL_HAT_LEFT ? "LEFT" : "RIGHT";
        snprintf(buf, size, "JOY%d HAT %s", in->joy + 1, d);
        break;
    }
    case IN_MOUSE:
        snprintf(buf, size, "MOUSE %s", in->code == SDL_BUTTON_LEFT ? "LEFT" : in->code == SDL_BUTTON_RIGHT ? "RIGHT" : "MIDDLE");
        break;
    default: snprintf(buf, size, "-"); break;
    }
}

void sw_bind_describe(sw_action a, char *buf, size_t size)
{
    size_t o = 0;
    if (a >= ACT_COUNT || mode[a] == M_AUTO) { snprintf(buf, size, "AUTO"); return; }
    if (mode[a] == M_NONE || !count(a)) { snprintf(buf, size, "NONE"); return; }
    buf[0] = '\0';
    for (int s = 0; s < SW_BIND_SLOTS; s++) {
        char one[40];
        if (B[a][s].type == IN_NONE) continue;
        describe(&B[a][s], one, sizeof one);
        o += (size_t)snprintf(buf + o, o < size ? size - o : 0, "%s%s", o ? " + " : "", one);
        if (o >= size) break;
    }
}

/* ---- laying out -------------------------------------------------------------- */

static void snapshot(void)
{
    int nk = 0;
    const Uint8 *k = SDL_GetKeyboardState(&nk);
    memset(CAP.keys, 0, sizeof CAP.keys);
    for (int i = 0; i < nk && i < SDL_NUM_SCANCODES; i++) CAP.keys[i] = k[i];
    memset(CAP.buttons, 0, sizeof CAP.buttons);
    memset(CAP.axes, 0, sizeof CAP.axes);
    memset(CAP.hats, 0, sizeof CAP.hats);
    for (int j = 0; j < MAX_JOYS; j++) {
        if (!J[j]) continue;
        for (int b = 0; b < SDL_JoystickNumButtons(J[j]) && b < 32; b++) CAP.buttons[j][b] = SDL_JoystickGetButton(J[j], b);
        for (int x = 0; x < SDL_JoystickNumAxes(J[j]) && x < 16; x++) {
            int v = SDL_JoystickGetAxis(J[j], x);
            CAP.axes[j][x] = (signed char)(v < -16000 ? -1 : v > 16000 ? 1 : 0);
        }
        for (int h = 0; h < SDL_JoystickNumHats(J[j]) && h < 4; h++) CAP.hats[j][h] = SDL_JoystickGetHat(J[j], h);
    }
    CAP.mouse = SDL_GetMouseState(NULL, NULL);
}

/* The cabinet's own lines: a start, a coin, the test button. */
static int cabinet_line(sw_action a) { return a >= ACT_START; }

void sw_bind_capture_begin(sw_action a, int replace)
{
    if (a >= ACT_COUNT) return;
    CAP.active = 1;
    CAP.a = a;
    CAP.replace = replace || mode[a] != M_SET;
    snapshot();                         /* what is held now counts once it is let go */
}

int sw_bind_capturing(void) { return CAP.active; }
int sw_bind_capture_adds(void) { return CAP.active && !CAP.replace; }

/* Never laid out: the menu's keys; on a line of the game's, also the
 * cabinet's starts and coins. */
static int kept_key(int sc)
{
    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F2 || sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER
        || sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_KP_PERIOD || sc == SDL_SCANCODE_BACKSPACE) return 1;
    if (cabinet_line(CAP.a)) return 0;
    for (int a = ACT_START; a <= ACT_COIN2; a++)
        for (int s = 0; s < SW_BIND_SLOTS; s++)
            if (AUTO[a][s].type == IN_KEY && AUTO[a][s].code == sc) return 1;
    return 0;
}

static void take(input got)
{
    sw_action a = CAP.a;
    if (CAP.replace) { memset(B[a], 0, sizeof B[a]); mode[a] = M_SET; }
    for (int s = 0; s < SW_BIND_SLOTS; s++)            /* it has it already */
        if (B[a][s].type == got.type && B[a][s].joy == got.joy && B[a][s].code == got.code && B[a][s].dir == got.dir) return;
    if (count(a) >= SW_BIND_SLOTS) {                    /* full: the oldest makes room */
        memmove(&B[a][0], &B[a][1], sizeof B[a][0] * (SW_BIND_SLOTS - 1));
        memset(&B[a][SW_BIND_SLOTS - 1], 0, sizeof B[a][0]);
    }
    for (int s = 0; s < SW_BIND_SLOTS; s++)
        if (B[a][s].type == IN_NONE) { B[a][s] = got; break; }
    mode[a] = M_SET;
}

int sw_bind_capture(void)
{
    if (!CAP.active) return -1;
    sw_action a = CAP.a;
    int nk = 0;
    const Uint8 *k = SDL_GetKeyboardState(&nk);

    /* Enter (START 1 too, on a line of the game's) leaves it as it was;
     * START 2 makes it NONE, Delete AUTO. */
    if (vpb_key_pressed(SDL_SCANCODE_RETURN) || vpb_key_pressed(SDL_SCANCODE_KP_ENTER) || vpb_key_pressed(SDL_SCANCODE_ESCAPE)
        || (!cabinet_line(a) && g_in && vp_input_pressed(g_in, "start1"))) {
        CAP.active = 0;
        return -1;
    }
    if (!cabinet_line(a) && g_in && vp_input_pressed(g_in, "start2")) { sw_bind_set_none(a); CAP.active = 0; return 1; }
    if (vpb_key_pressed(SDL_SCANCODE_DELETE) || vpb_key_pressed(SDL_SCANCODE_KP_PERIOD)) { sw_bind_clear(a); CAP.active = 0; return 1; }

    input got = { IN_NONE, 0, 0, 0 };
    for (int i = 1; i < nk && i < SDL_NUM_SCANCODES && got.type == IN_NONE; i++) {
        if (k[i] && !CAP.keys[i] && !kept_key(i)) { got.type = IN_KEY; got.code = i; }
        if (!k[i]) CAP.keys[i] = 0;                          /* released: may be taken next time */
    }
    for (int j = 0; j < MAX_JOYS && got.type == IN_NONE; j++) {
        if (!J[j]) continue;
        for (int b = 0; b < SDL_JoystickNumButtons(J[j]) && b < 32 && got.type == IN_NONE; b++) {
            int v = SDL_JoystickGetButton(J[j], b);
            if (v && !CAP.buttons[j][b]) { got.type = IN_BUTTON; got.joy = j; got.code = b; }
            if (!v) CAP.buttons[j][b] = 0;
        }
        for (int x = 0; x < SDL_JoystickNumAxes(J[j]) && x < 16 && got.type == IN_NONE; x++) {
            int v = SDL_JoystickGetAxis(J[j], x);
            int d = v < -20000 ? -1 : v > 20000 ? 1 : 0;
            if (d && d != CAP.axes[j][x]) { got.type = IN_AXIS; got.joy = j; got.code = x; got.dir = d; }
            if (v > -8000 && v < 8000) CAP.axes[j][x] = 0;
        }
        for (int h = 0; h < SDL_JoystickNumHats(J[j]) && h < 4 && got.type == IN_NONE; h++) {
            int v = SDL_JoystickGetHat(J[j], h);
            static const int DIRS[4] = { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT };
            for (int q = 0; q < 4 && got.type == IN_NONE; q++)
                if ((v & DIRS[q]) && !(CAP.hats[j][h] & DIRS[q])) { got.type = IN_HAT; got.joy = j; got.code = h; got.dir = DIRS[q]; }
            CAP.hats[j][h] = (unsigned char)(CAP.hats[j][h] & v);
        }
    }
    if (got.type == IN_NONE) {
        Uint32 m = SDL_GetMouseState(NULL, NULL);
        static const int MOUSE[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT };
        for (int q = 0; q < 3 && got.type == IN_NONE; q++)
            if ((m & SDL_BUTTON(MOUSE[q])) && !(CAP.mouse & SDL_BUTTON(MOUSE[q]))) { got.type = IN_MOUSE; got.code = MOUSE[q]; }
        CAP.mouse &= m;
    }
    if (got.type == IN_NONE) return 0;
    take(got);
    CAP.active = 0;
    /* the input just bound must not also count as a press this frame */
    held_now[a] = held_before[a] = 1;
    return 1;
}

void sw_bind_clear(sw_action a) { if (a < ACT_COUNT) { memset(B[a], 0, sizeof B[a]); mode[a] = M_AUTO; } }
void sw_bind_set_none(sw_action a) { if (a < ACT_COUNT) { memset(B[a], 0, sizeof B[a]); mode[a] = M_NONE; } }
void sw_bind_toggle(sw_action a)
{
    if (a >= ACT_COUNT) return;
    if (mode[a] == M_NONE) sw_bind_clear(a);
    else sw_bind_set_none(a);
}
void sw_bind_clear_all(void) { memset(B, 0, sizeof B); memset(mode, 0, sizeof mode); }

/* ---- the settings file ------------------------------------------------------ */

static int parse_input(const char *s, input *in)
{
    char kind[16];
    int a = 0, b = 0;
    char d = 0;
    memset(in, 0, sizeof *in);
    if (sscanf(s, " key:%d", &a) == 1) { in->type = IN_KEY; in->code = a; return 1; }
    if (sscanf(s, " joy%d:button%d", &a, &b) == 2) { in->type = IN_BUTTON; in->joy = a; in->code = b; return 1; }
    if (sscanf(s, " joy%d:axis%d%c", &a, &b, &d) == 3) {
        in->type = IN_AXIS; in->joy = a; in->code = b; in->dir = d == '-' ? -1 : 1; return 1;
    }
    if (sscanf(s, " joy%d:hat%d:%15[a-z]", &a, &b, kind) == 3) {
        in->type = IN_HAT; in->joy = a; in->code = b;
        in->dir = !strcmp(kind, "up") ? SDL_HAT_UP : !strcmp(kind, "down") ? SDL_HAT_DOWN
                : !strcmp(kind, "left") ? SDL_HAT_LEFT : SDL_HAT_RIGHT;
        return 1;
    }
    if (sscanf(s, " mouse:%d", &a) == 1) { in->type = IN_MOUSE; in->code = a; return 1; }
    return 0;
}

static void format_input(const input *in, char *buf, size_t size)
{
    switch (in->type) {
    case IN_KEY:    snprintf(buf, size, "key:%d", in->code); break;
    case IN_BUTTON: snprintf(buf, size, "joy%d:button%d", in->joy, in->code); break;
    case IN_AXIS:   snprintf(buf, size, "joy%d:axis%d%c", in->joy, in->code, in->dir < 0 ? '-' : '+'); break;
    case IN_HAT:
        snprintf(buf, size, "joy%d:hat%d:%s", in->joy, in->code,
                 in->dir == SDL_HAT_UP ? "up" : in->dir == SDL_HAT_DOWN ? "down" : in->dir == SDL_HAT_LEFT ? "left" : "right");
        break;
    case IN_MOUSE:  snprintf(buf, size, "mouse:%d", in->code); break;
    default: snprintf(buf, size, "auto"); break;
    }
}

int sw_bind_parse(const char *key, const char *value)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        if (strcmp(key, KEYS[a])) continue;
        memset(B[a], 0, sizeof B[a]);
        mode[a] = M_AUTO;               /* ("none" in a file before 2026-10-19: the cabinet's, AUTO now) */
        if (!strncmp(value, "off", 3)) { mode[a] = M_NONE; return 1; }
        char copy[256];
        snprintf(copy, sizeof copy, "%s", value);
        int slot = 0;
        for (char *tok = strtok(copy, ","); tok && slot < SW_BIND_SLOTS; tok = strtok(NULL, ","))
            if (parse_input(tok, &B[a][slot])) slot++;
        if (slot) mode[a] = M_SET;
        return 1;
    }
    return 0;
}

void sw_bind_save(FILE *f)
{
    fprintf(f, "\n; the inputs (the test menu, CONTROLS), read directly: auto = what AdvanceMAME gives them\n"
               "; by default, off = nothing; else up to four of key:<scancode>, joy<n>:button<n>,\n"
               "; joy<n>:axis<n>+/-, joy<n>:hat<n>:up, mouse:<button>, ', ' between them\n");
    for (int a = 0; a < ACT_COUNT; a++) {
        char line[256] = "";
        size_t o = 0;
        if (mode[a] == M_NONE) snprintf(line, sizeof line, "off");
        else if (mode[a] == M_AUTO || !count(a)) snprintf(line, sizeof line, "auto");
        else
            for (int s = 0; s < SW_BIND_SLOTS; s++) {
                char one[48];
                if (B[a][s].type == IN_NONE) continue;
                format_input(&B[a][s], one, sizeof one);
                o += (size_t)snprintf(line + o, o < sizeof line ? sizeof line - o : 0, "%s%s", o ? ", " : "", one);
            }
        fprintf(f, "%-16s = %s\n", KEYS[a], line);
    }
}

/* ---- the menu's keys ------------------------------------------------------------ */

void sw_bind_menu_held(sw_menu_keys *k)
{
    /* The menu's own controls only, whatever is bound (vpbind.h): the arrow
     * keys, any stick, hat or the yoke; START 1, Enter and the keypad's
     * Enter; Backspace back; F2 or the test button out. */
    int wx = vpb_menu_way_x(), wy = vpb_menu_way_y();
    k->up    = wy < 0;
    k->down  = wy > 0;
    k->left  = wx < 0;
    k->right = wx > 0;
    k->ok    = g_in ? vpb_menu_confirm_held(g_in) : 0;
    k->back  = vpb_key_held(SDL_SCANCODE_BACKSPACE);
    k->test  = sw_act_held(ACT_TEST) || vpb_key_held(SDL_SCANCODE_F2);
}

int sw_bind_test_pressed(void)
{
    return sw_act_pressed(ACT_TEST) || (f2_now && !f2_before);
}
