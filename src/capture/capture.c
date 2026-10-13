#include "capture/capture.h"

#include <string.h>

static const char MAGIC[8] = { 'S', 'W', 'C', 'A', 'P', '1', 0, 0 };

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int swcap_open(swcap_reader *r, const char *path)
{
    memset(r, 0, sizeof *r);
    r->f = fopen(path, "rb");
    if (!r->f) return -1;
    char magic[8];
    if (fread(magic, 1, 8, r->f) != 8 || memcmp(magic, MAGIC, 8) != 0) {
        fclose(r->f);
        r->f = NULL;
        return -1;
    }
    return 0;
}

void swcap_close(swcap_reader *r)
{
    if (r->f) fclose(r->f);
    r->f = NULL;
}

int swcap_rewind(swcap_reader *r)
{
    return fseek(r->f, 8, SEEK_SET) == 0 ? 0 : -1;
}

int swcap_next(swcap_reader *r, swcap_tick *t)
{
    for (;;) {
        uint8_t hdr[8];
        size_t n = fread(hdr, 1, 8, r->f);
        if (n == 0) return 0;
        if (n != 8) return -1;
        uint32_t len = rd32(hdr + 4);
        if (memcmp(hdr, "VROM", 4) == 0 && len == sizeof r->vrom) {
            if (fread(r->vrom, 1, len, r->f) != len) return -1;
            r->have_vrom = 1;
        } else if (memcmp(hdr, "TICK", 4) == 0 && len == SWCAP_TICK_SIZE) {
            uint8_t head[12];
            if (fread(head, 1, 12, r->f) != 12) return -1;
            t->frame = rd32(head);
            t->irq_count = rd32(head + 4);
            t->buffer = (uint16_t)(head[8] | (head[9] << 8));
            if (fread(t->vgram, 1, SWCAP_VGRAM_SIZE, r->f) != SWCAP_VGRAM_SIZE) return -1;
            if (fread(t->ram, 1, SWCAP_RAM_SIZE, r->f) != SWCAP_RAM_SIZE) return -1;
            if (fread(t->mbram, 1, SWCAP_MBRAM_SIZE, r->f) != SWCAP_MBRAM_SIZE) return -1;
            return 1;
        } else if (fseek(r->f, (long)len, SEEK_CUR) != 0) {
            return -1;
        }
    }
}

int swcap_next_chunk(swcap_reader *r, char tag[4], uint8_t *buf, uint32_t cap, uint32_t *len)
{
    uint8_t hdr[8];
    size_t n = fread(hdr, 1, 8, r->f);
    if (n == 0) return 0;
    if (n != 8) return -1;
    memcpy(tag, hdr, 4);
    *len = rd32(hdr + 4);
    uint32_t take = *len < cap ? *len : cap;
    if (fread(buf, 1, take, r->f) != take) return -1;
    if (*len > take && fseek(r->f, (long)(*len - take), SEEK_CUR) != 0) return -1;
    return 1;
}

void swcap_parse_tick(const uint8_t *p, swcap_tick *t)
{
    t->frame = rd32(p);
    t->irq_count = rd32(p + 4);
    t->buffer = (uint16_t)(p[8] | (p[9] << 8));
    memcpy(t->vgram, p + 12, SWCAP_VGRAM_SIZE);
    memcpy(t->ram, p + 12 + SWCAP_VGRAM_SIZE, SWCAP_RAM_SIZE);
    memcpy(t->mbram, p + 12 + SWCAP_VGRAM_SIZE + SWCAP_RAM_SIZE, SWCAP_MBRAM_SIZE);
}

void swcap_space(const swcap_reader *r, const swcap_tick *t, uint8_t space[0x4000])
{
    memcpy(space, t->vgram, SWCAP_VGRAM_SIZE);
    memcpy(space + 0x3000, r->vrom, sizeof r->vrom);
}

int swcap_write_header(FILE *f, const uint8_t vrom[0x1000])
{
    uint8_t hdr[8];
    memcpy(hdr, "VROM", 4);
    wr32(hdr + 4, 0x1000);
    if (fwrite(MAGIC, 1, 8, f) != 8 || fwrite(hdr, 1, 8, f) != 8) return -1;
    return fwrite(vrom, 1, 0x1000, f) == 0x1000 ? 0 : -1;
}

int swcap_write_tick(FILE *f, const swcap_tick *t)
{
    uint8_t hdr[8], head[12] = { 0 };
    memcpy(hdr, "TICK", 4);
    wr32(hdr + 4, SWCAP_TICK_SIZE);
    wr32(head, t->frame);
    wr32(head + 4, t->irq_count);
    head[8] = (uint8_t)t->buffer;
    head[9] = (uint8_t)(t->buffer >> 8);
    if (fwrite(hdr, 1, 8, f) != 8 || fwrite(head, 1, 12, f) != 12) return -1;
    if (fwrite(t->vgram, 1, SWCAP_VGRAM_SIZE, f) != SWCAP_VGRAM_SIZE) return -1;
    if (fwrite(t->ram, 1, SWCAP_RAM_SIZE, f) != SWCAP_RAM_SIZE) return -1;
    return fwrite(t->mbram, 1, SWCAP_MBRAM_SIZE, f) == SWCAP_MBRAM_SIZE ? 0 : -1;
}
