/* yoke.c - see yoke.h. */
#include "platform/yoke.h"

#include <SDL.h>
#include <ctype.h>
#include <string.h>

#define MAX_BUTTONS 32

static SDL_Joystick *s_yoke;
static unsigned char s_now[MAX_BUTTONS], s_was[MAX_BUTTONS];
static int           s_buttons;
static unsigned      s_steps;

static int says_yoke(const char *name)
{
    char low[128];
    size_t i;
    if (!name) return 0;
    for (i = 0; name[i] && i + 1 < sizeof low; i++) low[i] = (char)tolower((unsigned char)name[i]);
    low[i] = 0;
    return strstr(low, "yoke") != NULL;
}

/* (the SDK's input has the joystick subsystem running and its events pumped) */
static void find_yoke(void)
{
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (!says_yoke(SDL_JoystickNameForIndex(i))) continue;
        s_yoke = SDL_JoystickOpen(i);
        if (s_yoke) {
            s_buttons = SDL_JoystickNumButtons(s_yoke);
            if (s_buttons > MAX_BUTTONS) s_buttons = MAX_BUTTONS;
            return;
        }
    }
}

void sw_yoke_update(void)
{
    if (s_yoke && !SDL_JoystickGetAttached(s_yoke)) {
        SDL_JoystickClose(s_yoke);
        s_yoke = NULL;
        s_buttons = 0;
    }
    if (!s_yoke && s_steps++ % 120 == 0) find_yoke();
    memcpy(s_was, s_now, sizeof s_was);
    memset(s_now, 0, sizeof s_now);
    if (!s_yoke) return;
    for (int i = 0; i < s_buttons; i++) s_now[i] = SDL_JoystickGetButton(s_yoke, i) ? 1 : 0;
}

int sw_yoke_present(void) { return s_yoke != NULL; }

int sw_yoke_held(int b) { return b >= 0 && b < MAX_BUTTONS && s_now[b]; }

int sw_yoke_pressed(int b) { return b >= 0 && b < MAX_BUTTONS && s_now[b] && !s_was[b]; }

void sw_yoke_axes(float *x, float *y)
{
    *x = *y = 0.0f;
    if (!s_yoke) return;
    *x = (float)SDL_JoystickGetAxis(s_yoke, 0) / 32767.0f;
    *y = (float)SDL_JoystickGetAxis(s_yoke, 1) / 32767.0f;
    if (*x < -1.0f) *x = -1.0f;
    if (*y < -1.0f) *y = -1.0f;
}
