#include "platform/audio.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

static SDL_AudioDeviceID g_dev;
static int16_t g_last;

static void SDLCALL fill(void *user, Uint8 *stream, int len)
{
    sw_sndboard *snd = (sw_sndboard *)user;
    int16_t *out = (int16_t *)stream;
    int want = len / 2;
    int got = sw_sndboard_read(snd, out, want);
    if (got > 0) g_last = out[got - 1];
    /* Run dry (the game stalled or is just starting): let the last level
     * die away instead of clicking. */
    for (int i = got; i < want; i++) {
        g_last = (int16_t)(g_last - g_last / 64);
        out[i] = g_last;
    }
}

int sw_audio_open(sw_sndboard *snd)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "starwarsvp: no sound: %s\n", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = SW_SOUND_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = fill;
    want.userdata = snd;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) {
        fprintf(stderr, "starwarsvp: no sound device: %s\n", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

void sw_audio_close(void)
{
    if (g_dev) SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
}

void sw_audio_balance(sw_sndboard *snd)
{
    /* More than a quarter of a second waiting: the device is behind. Drop
     * what is oldest, down to a tenth of a second. */
    uint32_t waiting = snd->ring_w - snd->ring_r;
    if (waiting > SW_SOUND_RATE / 4) {
        if (g_dev) SDL_LockAudioDevice(g_dev);
        snd->ring_r = snd->ring_w - SW_SOUND_RATE / 10;
        if (g_dev) SDL_UnlockAudioDevice(g_dev);
    }
}
