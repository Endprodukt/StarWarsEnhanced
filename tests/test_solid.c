/* test_solid - render/solid.c: a line behind a face of an object is cut,
 * one in front of it and the face's own lines are not. Needs the ROM set
 * for the objects (skipped without). */
#include "render/solid.h"
#include "rom/romset.h"

#include <stdio.h>
#include <string.h>

static sw_romset rom;
static sw_machine m;
static sw_solid solid;

static sw_line line(float x0, float y0, float x1, float y1, unsigned section, unsigned w)
{
    sw_line l;
    memset(&l, 0, sizeof l);
    l.x0 = x0; l.y0 = y0; l.x1 = x1; l.y1 = y1;
    l.color = 7; l.zz = 7; l.intensity = 255;
    l.id = (uint64_t)SW_TAG(section, 0) << 48;
    l.w0 = l.w1 = (uint16_t)w;
    return l;
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_solid: no ROM set, skipped\n");
        return 0;
    }
    sw_machine_init(&m, &rom);
    sw_solid_init(&solid, &m);
    int fail = 0;

    /* every object with lines has faces */
    for (int i = 0; i < SW_SOLID_MODELS; i++) {
        int want = i != SW_TDSXW && i != SW_TDSYW;
        if ((solid.model[i].n > 0) != want) {
            printf("object %02X: %d faces\n", i, solid.model[i].n);
            fail = 1;
        }
    }

    /* The wall panel (points 0 to 3 a rectangle) drawn as a square of 20
     * at 1/X = 1000, a star and two lines across it. */
    static const float sq[4][2] = { { -10, -10 }, { -10, 10 }, { 10, 10 }, { 10, -10 } };
    sw_line in[8];
    int k = 0;
    for (int i = 0; i < 4; i++) {
        sw_line l = line(sq[i][0], sq[i][1], sq[(i + 1) % 4][0], sq[(i + 1) % 4][1], SW_TAG_PANEL, 1000);
        l.inst = 1;
        l.model = SW_TDSWPN + 1;
        l.p0 = (uint8_t)(i + 1);
        l.p1 = (uint8_t)((i + 1) % 4 + 1);
        in[k++] = l;
    }
    in[k++] = line(0, 0, 0, 0, SW_TAG_STAR, 0);                 /* a star behind it */
    in[k++] = line(-20, 0, 20, 0, SW_TAG_TRENCH, 500);          /* a line behind it */
    in[k++] = line(-20, 5, 20, 5, SW_TAG_TRENCH, 2000);         /* a line before it */
    in[k++] = line(-20, -5, 20, -5, SW_TAG_MESS, 0);            /* a text */
    const sw_line *out;
    int no = sw_solid_run(&solid, in, k, &out);
    /* the square's 4, the far line in 2 pieces, the near line, the text */
    if (no != 8) { printf("%d lines left, 8 expected\n", no); fail = 1; }
    for (int i = 0; i < no; i++) {
        if ((out[i].id >> 58) == SW_TAG_STAR) { printf("the star shows\n"); fail = 1; }
        if (out[i].w0 == 500 && out[i].x0 > -10.5f && out[i].x1 < 10.5f) { printf("the far line shows inside\n"); fail = 1; }
    }
    printf("test_solid: %s\n", fail ? "FAILED" : "ok");
    return fail;
}
