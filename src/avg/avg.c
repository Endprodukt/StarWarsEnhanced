#include "avg/avg.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_MAX_INSTRUCTIONS 50000

void sw_avg_list_free(sw_avg_list *l)
{
    free(l->lines);
    memset(l, 0, sizeof *l);
}

static int push_line(sw_avg_list *l, const sw_line *ln)
{
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 1024;
        sw_line *p = (sw_line *)realloc(l->lines, (size_t)cap * sizeof *p);
        if (!p) return -1;
        l->lines = p;
        l->cap = cap;
    }
    l->lines[l->count++] = *ln;
    return 0;
}

uint8_t sw_avg_intensity(unsigned zz, unsigned lum)
{
    /* 12k / 24k / 47k current summing on ZZ bits 2, 1, 0. */
    static const float FULL = 1.0f + 0.5f + 12.0f / 47.0f;
    float dac = ((zz >> 2) & 1) * 1.0f + ((zz >> 1) & 1) * 0.5f + (zz & 1) * (12.0f / 47.0f);
    int v = (int)(dac / FULL * (float)(lum & 0xff) + 0.5f);
    return (uint8_t)(v > 255 ? 255 : v);
}

static int sext(unsigned v, int bits)
{
    unsigned m = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return (int)(v ^ m) - (int)m;
}

static unsigned word_at(const uint8_t *space, unsigned addr)
{
    return ((unsigned)space[addr] << 8) | space[addr + 1];
}

int sw_avg_run(const uint8_t *space, uint16_t start, sw_avg_list *out, int max_instructions)
{
    unsigned pc = start;
    unsigned stack[4] = { 0, 0, 0, 0 };
    unsigned sp = 0;
    float x = 0.0f, y = 0.0f;
    unsigned color = 0, lum = 0x80, defocus = 0;
    unsigned bin_scale = 0, lin_scale = 0;
    float scale = 255.0f / 256.0f;

    if (max_instructions <= 0) max_instructions = DEFAULT_MAX_INSTRUCTIONS;
    out->count = 0;
    out->instructions = 0;
    out->status = SW_AVG_OK;

    for (;;) {
        if (out->instructions >= max_instructions) return out->status = SW_AVG_LIMIT;
        if (pc + 1 >= SW_AVG_SPACE_SIZE) return out->status = SW_AVG_BAD_ADDRESS;
        out->instructions++;

        unsigned w = word_at(space, pc);
        pc += 2;
        int dx = 0, dy = 0;
        unsigned zz = 0;
        int draw = 0;

        switch (w >> 13) {
        case 0:  /* VCTR */
            if (pc + 1 >= SW_AVG_SPACE_SIZE) return out->status = SW_AVG_BAD_ADDRESS;
            dy = sext(w, 13);
            w = word_at(space, pc);
            pc += 2;
            dx = sext(w, 13);
            zz = w >> 13;
            draw = 1;
            break;
        case 1:  /* HALT */
            return out->status;
        case 2:  /* SVEC */
            dy = sext(w >> 8, 5) * 2;
            dx = sext(w, 5) * 2;
            zz = (w >> 5) & 7;
            draw = 1;
            break;
        case 3:
            if (w & 0x1000) {           /* SCAL */
                bin_scale = (w >> 8) & 7;
                lin_scale = w & 0xff;
                scale = (float)(lin_scale ^ 0xff) / 256.0f / (float)(1u << bin_scale);
            } else {                    /* COLOR / FUZZ */
                color = (w >> 8) & 7;
                defocus = (w >> 11) & 1;
                lum = w & 0xff;
            }
            break;
        case 4:  /* CNTR */
            x = y = 0.0f;
            break;
        case 5:  /* JSRL */
            stack[sp & 3] = pc;
            sp++;
            pc = (w & 0x1fff) << 1;
            break;
        case 6:  /* RTSL */
            sp--;
            pc = stack[sp & 3];
            break;
        case 7:  /* JMPL */
            pc = (w & 0x1fff) << 1;
            break;
        }

        if (draw) {
            float nx = x + (float)dx * scale;
            float ny = y + (float)dy * scale;
            if (zz) {
                sw_line ln;
                ln.x0 = x; ln.y0 = y; ln.x1 = nx; ln.y1 = ny;
                ln.color = (uint8_t)color;
                ln.zz = (uint8_t)zz;
                ln.intensity = sw_avg_intensity(zz, lum);
                ln.defocus = (uint8_t)defocus;
                if (ln.intensity && push_line(out, &ln) != 0) return out->status = SW_AVG_LIMIT;
            }
            x = nx;
            y = ny;
        }
    }
}
