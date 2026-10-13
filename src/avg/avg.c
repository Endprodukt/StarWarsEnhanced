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
    return sw_avg_run_tagged(space, start, NULL, NULL, out, max_instructions);
}

int sw_avg_run_tagged(const uint8_t *space, uint16_t start, const uint16_t *tags, const uint8_t *starts,
                      sw_avg_list *out, int max_instructions)
{
    return sw_avg_run_noted(space, start, tags, starts, NULL, out, max_instructions);
}

int sw_avg_run_noted(const uint8_t *space, uint16_t start, const uint16_t *tags, const uint8_t *starts,
                     const sw_avg_note *notes, sw_avg_list *out, int max_instructions)
{
    static const sw_avg_note NO_NOTE = { 0, 0, 0, 0, 0, 0, { 0, 0, 0 } };
    sw_avg_note note = NO_NOTE;     /* of the instruction of the list itself being run */
    unsigned beam_w = 0, beam_pt = 0;   /* of the point the beam stands at */
    sw_avg_note beam = NO_NOTE;
    int depth = 0;                  /* calls deep: 0 is the list itself */
    unsigned run_tag = 0xffffffffu, unit = 0, routine = 0, nline = 0;
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

        if (depth == 0) {           /* an instruction of the list itself */
            unsigned tag = (tags && pc < 0x3000) ? tags[pc >> 1] : 0;
            if (tag != run_tag || (starts && pc < 0x3000 && starts[pc >> 1])) { run_tag = tag; unit = 0; }
            else unit++;
            routine = 0;
            nline = 0;
            note = (notes && pc < 0x3000) ? notes[pc >> 1] : NO_NOTE;
        }
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
            if (depth == 0) routine = w & 0x1fff;
            depth++;
            break;
        case 6:  /* RTSL */
            sp--;
            pc = stack[sp & 3];
            if (depth > 0) depth--;
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
                ln.w0 = (uint16_t)beam_w; ln.w1 = note.w;
                ln.inst = note.inst; ln.model = note.model;
                ln.p0 = (uint8_t)beam_pt; ln.p1 = depth ? 0 : note.pt;
                ln.d3 = (uint8_t)(!depth && note.d3 && beam.d3);
                for (int k = 0; k < 3; k++) { ln.v0[k] = beam.v[k]; ln.v1[k] = note.v[k]; }
                ln.id = tags ? ((uint64_t)(run_tag & 0xffff) << 48) | ((uint64_t)(routine ? 0 : unit & 0xffff) << 32) |
                               ((uint64_t)routine << 16) | (nline & 0xffff) : 0;
                nline++;
                if (ln.intensity && push_line(out, &ln) != 0) return out->status = SW_AVG_LIMIT;
            }
            x = nx;
            y = ny;
            beam_w = note.w;
            beam_pt = depth ? 0 : note.pt;
            beam = note;
            if (depth) beam.d3 = 0;
        }
    }
}

/* --- how long the board takes over a list ---------------------------------- */

/* The normalization the board does before a vector: both deltas are
 * shifted up until one of them uses its top bit; every shift halves the
 * time the vector timer runs. */
static int norm_shifts(unsigned dvy, unsigned dvx)
{
    int i = 0;
    while (((dvy ^ (dvy << 1)) & 0x1000) == 0 && ((dvx ^ (dvx << 1)) & 0x1000) == 0 && i < 16) {
        i++;
        dvy = (dvy & 0x1000) | ((dvy << 1) & 0x1fff);
        dvx = (dvx & 0x1000) | ((dvx << 1) & 0x1fff);
    }
    return i;
}

static unsigned vg_word(const uint8_t *vgram, const uint8_t *vrom, unsigned a)
{
    if (a + 1u < 0x3000u) return ((unsigned)vgram[a] << 8) | vgram[a + 1];
    if (vrom && a >= 0x3000u && a + 1u < 0x4000u) return ((unsigned)vrom[a - 0x3000] << 8) | vrom[a - 0x2fff];
    return 0x2020u;     /* outside: HALT */
}

uint32_t sw_avg_cycles(const uint8_t *vgram, const uint8_t *vrom, uint16_t start)
{
    /* States the machine goes through per instruction, 8 clock cycles each
     * (counted on the board's state PROM as MAME runs it; HALT's makes the
     * total come out). */
    static const uint8_t STATES[8] = { 8, 2, 6, 7, 5, 5, 4, 3 };
    unsigned pc = start, sp = 0, bin_scale = 0;
    unsigned stack[4] = { 0, 0, 0, 0 };
    uint32_t cycles = 0;

    for (int n = 0; n < DEFAULT_MAX_INSTRUCTIONS; n++) {
        unsigned w = vg_word(vgram, vrom, pc), w2;
        int k;
        pc += 2;
        cycles += 8u * STATES[w >> 13];
        switch (w >> 13) {
        case 0:  /* VCTR: the 15-bit timer, started further on per shift */
            w2 = vg_word(vgram, vrom, pc);
            pc += 2;
            k = norm_shifts(w & 0x1fff, w2 & 0x1fff) + (int)bin_scale;
            cycles += k < 15 ? 0x8000u >> k : 1u;
            break;
        case 1:  /* HALT */
            return cycles;
        case 2:  /* SVEC: the timer's low 8 bits only */
            k = norm_shifts(w & 0x1f00, (w & 0x1f) << 8) + (int)bin_scale;
            cycles += k < 8 ? 0x100u >> k : 1u;
            break;
        case 3:
            if (w & 0x1000) bin_scale = (w >> 8) & 7;
            break;
        case 4:  /* CNTR: the beam's way back takes a timer run too */
            k = norm_shifts(w & 0x1fff, 0);
            cycles += k < 15 ? 0x8000u >> k : 1u;
            break;
        case 5:
            stack[sp & 3] = pc;
            sp++;
            pc = (w & 0x1fff) << 1;
            break;
        case 6:
            sp--;
            pc = stack[sp & 3];
            break;
        case 7:
            pc = (w & 0x1fff) << 1;
            break;
        }
    }
    return cycles;      /* a list that never halts: the game's interrupt times it out */
}
