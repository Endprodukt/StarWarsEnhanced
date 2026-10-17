/* bind.c - see bind.h. After Vectortron's bind.c (the same scheme, so that
 * the games of the cabinet feel alike). */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "platform/bind.h"

#include <stdlib.h>
#include <string.h>

typedef enum { IN_NONE, IN_KEY, IN_BUTTON, IN_AXIS, IN_HAT } in_type;

typedef struct {
    in_type type;
    int     joy;        /* joystick number, 0 = the first */
    int     code;       /* scancode, button, axis or hat number */
    int     dir;        /* axis: -1 / +1; hat: SDL_HAT_* bit */
} input;

/* the cabinet's advmame.rc names for each action, tried in turn (as
 * controls.h had them, MAME's Star Wars mapping) */
static const char *const CABINET[ACT_COUNT][3] = {
    [ACT_LEFT]     = { "p1_left", NULL, NULL },
    [ACT_RIGHT]    = { "p1_right", NULL, NULL },
    [ACT_UP]       = { "p1_up", NULL, NULL },
    [ACT_DOWN]     = { "p1_down", NULL, NULL },
    [ACT_FIRE_L]   = { "p1_button1", NULL, NULL },
    [ACT_FIRE_R]   = { "p1_button4", NULL, NULL },
    [ACT_THUMB_L]  = { "p1_button2", NULL, NULL },
    [ACT_THUMB_R]  = { "p1_button3", NULL, NULL },
    [ACT_START]    = { "start1", NULL, NULL },
    [ACT_START2]   = { "start2", NULL, NULL },
    [ACT_COIN1]    = { "coin1", NULL, NULL },
    [ACT_COIN2]    = { "coin2", NULL, NULL },
    [ACT_COIN_AUX] = { "p1_button5", NULL, NULL },
    [ACT_TEST]     = { "p1_button6", "service", "ui_configure" },
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
static int held_now[ACT_COUNT], held_before[ACT_COUNT];

#define MAX_JOYS 8
static SDL_Joystick *J[MAX_JOYS];
static int njoys = -1;

static struct {
    int active, slot;
    sw_action a;
    Uint32 start;
    unsigned char keys[SDL_NUM_SCANCODES];
    unsigned char buttons[MAX_JOYS][32];
    signed char axes[MAX_JOYS][16];
    unsigned char hats[MAX_JOYS][4];
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

static int input_active(const input *in)
{
    switch (in->type) {
    case IN_KEY: {
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        return in->code > 0 && in->code < SDL_NUM_SCANCODES && k[in->code];
    }
    case IN_BUTTON:
        return in->joy < MAX_JOYS && J[in->joy] && SDL_JoystickGetButton(J[in->joy], in->code);
    case IN_AXIS: {
        if (in->joy >= MAX_JOYS || !J[in->joy]) return 0;
        int v = SDL_JoystickGetAxis(J[in->joy], in->code);
        return in->dir < 0 ? v < -16000 : v > 16000;
    }
    case IN_HAT:
        return in->joy < MAX_JOYS && J[in->joy] && (SDL_JoystickGetHat(J[in->joy], in->code) & in->dir);
    default:
        return 0;
    }
}

static int bound(sw_action a) { return B[a][0].type != IN_NONE || B[a][1].type != IN_NONE; }

void sw_bind_update(vp_input *in)
{
    open_joysticks();
    for (int a = 0; a < ACT_COUNT; a++) {
        held_before[a] = held_now[a];
        int h = 0;
        if (bound((sw_action)a)) {
            for (int s = 0; s < SW_BIND_SLOTS; s++) h |= input_active(&B[a][s]);
        } else {
            for (int k = 0; k < 3 && CABINET[a][k]; k++) h |= vp_input_held(in, CABINET[a][k]);
        }
        held_now[a] = h;
    }
    f2_before = f2_now;
    f2_now = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F2] != 0;
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

static void describe(const input *in, char *buf, size_t size)
{
    switch (in->type) {
    case IN_KEY: {
        const char *n = SDL_GetScancodeName((SDL_Scancode)in->code);
        snprintf(buf, size, "KEY %s", n && *n ? n : "?");
        for (char *p = buf; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
        break;
    }
    case IN_BUTTON: snprintf(buf, size, "JOY %d BUTTON %d", in->joy + 1, in->code + 1); break;
    case IN_AXIS:   snprintf(buf, size, "JOY %d AXIS %d %c", in->joy + 1, in->code + 1, in->dir < 0 ? '-' : '+'); break;
    case IN_HAT: {
        const char *d = in->dir == SDL_HAT_UP ? "UP" : in->dir == SDL_HAT_DOWN ? "DOWN"
                      : in->dir == SDL_HAT_LEFT ? "LEFT" : "RIGHT";
        snprintf(buf, size, "JOY %d HAT %s", in->joy + 1, d);
        break;
    }
    default: snprintf(buf, size, "-"); break;
    }
}

void sw_bind_describe(sw_action a, char *buf, size_t size)
{
    if (a >= ACT_COUNT || !bound(a)) { snprintf(buf, size, "CABINET"); return; }
    char s0[40], s1[40];
    describe(&B[a][0], s0, sizeof s0);
    if (B[a][1].type == IN_NONE) { snprintf(buf, size, "%s", s0); return; }
    describe(&B[a][1], s1, sizeof s1);
    if (B[a][0].type == IN_NONE) { snprintf(buf, size, "%s", s1); return; }
    snprintf(buf, size, "%s + %s", s0, s1);
}

/* ---- capturing ----------------------------------------------------------- */

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
}

void sw_bind_capture_begin(sw_action a)
{
    if (a >= ACT_COUNT) return;
    CAP.active = 1;
    CAP.a = a;
    CAP.slot = !bound(a) || B[a][0].type == IN_NONE ? 0 : 1;   /* the first free slot, else the second */
    CAP.start = SDL_GetTicks();
    snapshot();
}

int sw_bind_capturing(void) { return CAP.active; }

int sw_bind_capture(void)
{
    if (!CAP.active) return -1;
    if (SDL_GetTicks() - CAP.start > 5000) { CAP.active = 0; return -1; }
    input got = { IN_NONE, 0, 0, 0 };
    int nk = 0;
    const Uint8 *k = SDL_GetKeyboardState(&nk);
    for (int i = 1; i < nk && i < SDL_NUM_SCANCODES && got.type == IN_NONE; i++) {
        if (i == SDL_SCANCODE_ESCAPE || i == SDL_SCANCODE_F2) continue;    /* the cabinet's exit and the menu's own key: never bound */
        if (k[i] && !CAP.keys[i]) { got.type = IN_KEY; got.code = i; }
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
    if (got.type == IN_NONE) return 0;
    if (!bound(CAP.a)) memset(B[CAP.a], 0, sizeof B[CAP.a]);
    B[CAP.a][CAP.slot] = got;
    CAP.active = 0;
    /* the input just bound must not also count as a press this frame */
    held_now[CAP.a] = held_before[CAP.a] = 1;
    return 1;
}

void sw_bind_clear(sw_action a) { if (a < ACT_COUNT) memset(B[a], 0, sizeof B[a]); }
void sw_bind_clear_all(void) { memset(B, 0, sizeof B); }

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
    default: snprintf(buf, size, "none"); break;
    }
}

int sw_bind_parse(const char *key, const char *value)
{
    for (int a = 0; a < ACT_COUNT; a++) {
        if (strcmp(key, KEYS[a])) continue;
        memset(B[a], 0, sizeof B[a]);
        char copy[256];
        snprintf(copy, sizeof copy, "%s", value);
        int slot = 0;
        for (char *tok = strtok(copy, ","); tok && slot < SW_BIND_SLOTS; tok = strtok(NULL, ","))
            if (parse_input(tok, &B[a][slot])) slot++;
        return 1;
    }
    return 0;
}

void sw_bind_save(FILE *f)
{
    fprintf(f, "\n; the owner's own bindings of the digital inputs (the test menu, CONTROLS): none = the\n"
               "; cabinet's advmame.rc; key:<scancode>, joy<n>:button<n>, joy<n>:axis<n>+/-, joy<n>:hat<n>:up\n");
    for (int a = 0; a < ACT_COUNT; a++) {
        char s0[48], s1[48];
        format_input(&B[a][0], s0, sizeof s0);
        format_input(&B[a][1], s1, sizeof s1);
        if (B[a][1].type != IN_NONE) fprintf(f, "%-16s = %s, %s\n", KEYS[a], s0, s1);
        else fprintf(f, "%-16s = %s\n", KEYS[a], s0);
    }
}

/* ---- the menu's keys ------------------------------------------------------------ */

static int raw_key(int sc) { const Uint8 *k = SDL_GetKeyboardState(NULL); return k[sc]; }

static int joy0_dir(int axis, int sign, int hatbit)
{
    if (!J[0]) return 0;
    int v = SDL_JoystickNumAxes(J[0]) > axis ? SDL_JoystickGetAxis(J[0], axis) : 0;
    if (sign < 0 ? v < -16000 : v > 16000) return 1;
    return SDL_JoystickNumHats(J[0]) > 0 && (SDL_JoystickGetHat(J[0], 0) & hatbit);
}

static int joy0_button(int b) { return J[0] && SDL_JoystickNumButtons(J[0]) > b && SDL_JoystickGetButton(J[0], b); }

void sw_bind_menu_held(sw_menu_keys *k)
{
    k->up    = sw_act_held(ACT_UP) || raw_key(SDL_SCANCODE_UP) || joy0_dir(1, -1, SDL_HAT_UP);
    k->down  = sw_act_held(ACT_DOWN) || raw_key(SDL_SCANCODE_DOWN) || joy0_dir(1, 1, SDL_HAT_DOWN);
    k->left  = sw_act_held(ACT_LEFT) || raw_key(SDL_SCANCODE_LEFT) || joy0_dir(0, -1, SDL_HAT_LEFT);
    k->right = sw_act_held(ACT_RIGHT) || raw_key(SDL_SCANCODE_RIGHT) || joy0_dir(0, 1, SDL_HAT_RIGHT);
    k->ok    = sw_act_held(ACT_FIRE_L) || sw_act_held(ACT_START) || raw_key(SDL_SCANCODE_RETURN) || raw_key(SDL_SCANCODE_KP_ENTER)
            || joy0_button(0);
    k->back  = sw_act_held(ACT_THUMB_L) || raw_key(SDL_SCANCODE_BACKSPACE) || joy0_button(1);
    k->test  = sw_act_held(ACT_TEST) || raw_key(SDL_SCANCODE_F2);
}

int sw_bind_test_pressed(void)
{
    return sw_act_pressed(ACT_TEST) || (f2_now && !f2_before);
}
