/* The capture format: written and read back, unknown chunks skipped, and a
 * recorded list runs through the AVG interpreter. */
#include "avg/avg.h"
#include "capture/capture.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static swcap_tick t1, t2, back;
static uint8_t vrom[0x1000];

int main(void)
{
    const char *path = "test_capture.swcap";
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return 1;

    for (int i = 0; i < 0x1000; i++) vrom[i] = (uint8_t)(i * 7);
    /* tick 1: a list at 0x0070: COLOR white, VCTR 100,0 ZZ 7, HALT */
    t1.frame = 41;
    t1.irq_count = 500;
    t1.buffer = 0x0070;
    const uint8_t list[] = { 0x67, 0xff, 0x00, 0x00, 0xe0, 0x64, 0x20, 0x00 };
    memcpy(t1.vgram + 0x70, list, sizeof list);
    t1.ram[0x3f] = 0xff;
    t1.mbram[0x90] = 0x40;
    t2 = t1;
    t2.frame = 42;

    CHECK(swcap_write_header(f, vrom) == 0);
    CHECK(swcap_write_tick(f, &t1) == 0);
    /* an unknown chunk a later version might add */
    fwrite("XTRA", 1, 4, f);
    const uint8_t len[4] = { 3, 0, 0, 0 };
    fwrite(len, 1, 4, f);
    fwrite("abc", 1, 3, f);
    CHECK(swcap_write_tick(f, &t2) == 0);
    fclose(f);

    swcap_reader r;
    CHECK(swcap_open(&r, path) == 0);
    CHECK(swcap_next(&r, &back) == 1);
    CHECK(r.have_vrom && memcmp(r.vrom, vrom, sizeof vrom) == 0);
    CHECK(back.frame == 41 && back.irq_count == 500 && back.buffer == 0x0070);
    CHECK(back.ram[0x3f] == 0xff && back.mbram[0x90] == 0x40);

    static uint8_t space[SW_AVG_SPACE_SIZE];
    swcap_space(&r, &back, space);
    sw_avg_list l = { 0 };
    CHECK(sw_avg_run(space, back.buffer, &l, 0) == SW_AVG_OK);
    CHECK(l.count == 1 && l.lines[0].color == 7 && l.lines[0].zz == 7);
    sw_avg_list_free(&l);

    CHECK(swcap_next(&r, &back) == 1);
    CHECK(back.frame == 42);
    CHECK(swcap_next(&r, &back) == 0);
    CHECK(swcap_rewind(&r) == 0 && swcap_next(&r, &back) == 1 && back.frame == 41);
    swcap_close(&r);
    remove(path);

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("capture: all passed\n");
    return 0;
}
