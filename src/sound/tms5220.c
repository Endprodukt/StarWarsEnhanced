#include "sound/tms5220.h"

#include <string.h>

/* The chip's tables (see tms5220.h for their source). */
static const uint8_t ENERGY[16] = { 0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0 };
static const uint8_t PITCH[64] = {
    0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 46, 48,
    50, 52, 53, 56, 58, 60, 62, 65, 68, 70, 72, 76, 78, 80, 84, 86,
    91, 94, 98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159
};
static const int KBITS[10] = { 5, 5, 4, 4, 4, 4, 4, 3, 3, 3 };
static const int16_t KTABLE[10][32] = {
    { -501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
      -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436 },
    { -328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
      248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506 },
    { -441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368 },
    { -328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506 },
    { -328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368 },
    { -256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409 },
    { -308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409 },
    { -256, -161, -66, 29, 124, 219, 314, 409 },
    { -256, -176, -96, -15, 65, 146, 226, 307 },
    { -205, -132, -59, 14, 87, 160, 234, 307 },
};
static const int8_t CHIRP[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b, 0x13,
    0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
static const int INTERP_SHIFT[8] = { 0, 3, 3, 3, 2, 2, 1, 1 };

#define SAMPLES_PER_PERIOD 25

void tms5220_reset(tms5220 *t)
{
    memset(t, 0, sizeof *t);
    t->rng = 0x1fff;
    t->old_zero = t->new_zero = 1;
}

static void stop(tms5220 *t)
{
    t->talking = 0;
    t->speak_external = 0;
    t->fifo_head = t->fifo_count = t->fifo_bits = 0;
    t->cur_energy = t->new_energy = 0;
    t->old_zero = t->new_zero = 1;
}

void tms5220_write(tms5220 *t, uint8_t data)
{
    if (t->speak_external) {
        if (t->fifo_count < 16) {
            t->fifo[(t->fifo_head + t->fifo_count) & 15] = data;
            t->fifo_count++;
        }
        /* Speaking starts when the buffer is more than half full. */
        if (!t->talking && t->fifo_count > 8) {
            t->talking = 1;
            t->buffer_empty_stop = 0;
            t->period = 0;
            t->sample_in_period = 0;
            t->stop_after_frame = 0;
        }
        return;
    }
    switch (data & 0x70) {
    case 0x60:                              /* Speak External */
        stop(t);
        t->speak_external = 1;
        break;
    case 0x70:                              /* Reset */
        tms5220_reset(t);
        break;
    default:                                /* the speech ROM commands: no ROM here */
        break;
    }
}

uint8_t tms5220_status(const tms5220 *t)
{
    int ts = t->speak_external || t->talking;
    int bl = t->fifo_count <= 8;
    int be = t->fifo_count == 0;
    return (uint8_t)((ts ? 0x80 : 0) | (bl ? 0x40 : 0) | (be ? 0x20 : 0));
}

int tms5220_ready(const tms5220 *t)
{
    return !(t->speak_external && t->fifo_count >= 16);
}

/* Bits from the FIFO, each byte from its low end; -1 when it runs dry. */
static int bits(tms5220 *t, int count)
{
    int v = 0;
    while (count--) {
        if (t->fifo_count == 0) return -1;
        v = (v << 1) | ((t->fifo[t->fifo_head] >> t->fifo_bits) & 1);
        if (++t->fifo_bits == 8) {
            t->fifo_bits = 0;
            t->fifo_head = (t->fifo_head + 1) & 15;
            t->fifo_count--;
        }
    }
    return v;
}

/* The next frame becomes the target. Returns 0 if speech ends. */
static int next_frame(tms5220 *t)
{
    t->old_zero = t->new_zero;
    t->old_unvoiced = t->new_unvoiced;

    int e = bits(t, 4);
    if (e < 0) goto dry;
    if (e == 15) return 0;                  /* stop frame */
    if (e == 0) {                           /* silence */
        t->new_energy = 0;
        t->new_zero = 1;
        return 1;
    }
    t->new_zero = 0;
    t->new_energy = ENERGY[e];
    int rep = bits(t, 1);
    int p = bits(t, 6);
    if (rep < 0 || p < 0) goto dry;
    t->new_pitch = PITCH[p];
    t->new_unvoiced = p == 0;
    if (rep) return 1;                      /* repeat: the coefficients stay */
    for (int i = 0; i < 4; i++) {
        int k = bits(t, KBITS[i]);
        if (k < 0) goto dry;
        t->new_k[i] = KTABLE[i][k];
    }
    if (p == 0) {                           /* unvoiced: four coefficients only */
        for (int i = 4; i < 10; i++) t->new_k[i] = 0;
        return 1;
    }
    for (int i = 4; i < 10; i++) {
        int k = bits(t, KBITS[i]);
        if (k < 0) goto dry;
        t->new_k[i] = KTABLE[i][k];
    }
    return 1;
dry:
    t->buffer_empty_stop = 1;
    return 0;
}

static int32_t mul(int32_t a, int32_t b)    /* 10 bit coefficient times 14 bit value */
{
    int32_t r = (a * b) / 512;
    if (r > 16383) r = 16383;
    if (r < -16384) r = -16384;
    return r;
}

int tms5220_sample(tms5220 *t)
{
    if (!t->talking) return 0;

    if (t->sample_in_period == 0) {
        if (t->period == 0) {
            /* A new frame: what was the target is reached. */
            if (t->stop_after_frame || !next_frame(t)) {
                stop(t);
                return 0;
            }
            /* No gliding into or out of silence, nor between voiced and
             * unvoiced: the new frame's values at once. */
            if (t->old_zero || t->new_zero || t->old_unvoiced != t->new_unvoiced) {
                t->cur_energy = t->new_energy;
                t->cur_pitch = t->new_pitch;
                for (int i = 0; i < 10; i++) t->cur_k[i] = t->new_k[i];
            }
        } else {
            int s = INTERP_SHIFT[t->period];
            t->cur_energy += (t->new_energy - t->cur_energy) >> s;
            t->cur_pitch += (t->new_pitch - t->cur_pitch) >> s;
            for (int i = 0; i < 10; i++) t->cur_k[i] += (t->new_k[i] - t->cur_k[i]) >> s;
        }
    }

    /* Excitation: the chirp once per pitch period, or noise. */
    int32_t ex;
    if (t->cur_pitch == 0) {
        t->rng = (uint16_t)((t->rng >> 1) ^ ((t->rng & 1) ? 0xb800 : 0));
        ex = (t->rng & 1) ? 64 : -64;
        t->pitch_count = 0;
    } else {
        ex = t->pitch_count < 52 ? CHIRP[t->pitch_count] : 0;
        if (++t->pitch_count >= t->cur_pitch) t->pitch_count = 0;
    }

    /* The lattice filter. */
    int32_t *u = t->u, *x = t->x;
    u[10] = (t->cur_energy * ex * 64) / 512;
    for (int i = 9; i >= 0; i--) u[i] = u[i + 1] - mul(t->cur_k[i], x[i]);
    for (int i = 9; i >= 1; i--) x[i] = x[i - 1] + mul(t->cur_k[i - 1], u[i - 1]);
    x[0] = u[0];
    int32_t out = u[0];
    if (out > 8191) out = 8191;
    if (out < -8192) out = -8192;

    if (++t->sample_in_period == SAMPLES_PER_PERIOD) {
        t->sample_in_period = 0;
        t->period = (t->period + 1) & 7;
    }
    return (int)out;
}
