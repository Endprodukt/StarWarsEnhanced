/* test_subframe - the pictures between the frames (game/subframe.h).
 *
 * The C routines play a game by a script. For every frame of a phase that
 * has a view of its own:
 *   - the picture at t = 0 has to be the frame's own display list, word
 *     for word;
 *   - the pictures at t = 1/3 and 2/3 have to be display lists that halt,
 *     with about as many lines;
 *   - a line has to be nearer to where it is at t = 1/3 than the frames
 *     before and after are apart (the picture does move in between, and
 *     in steps smaller than the game's).
 *
 * Needs the ROM set: looks in $SWE_ROMS and the usual folders, and passes
 * without testing when there is none. */
#include "avg/avg.h"
#include "core/machine.h"
#include "game/game.h"
#include "game/subframe.h"
#include "rom/romset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sw_romset rom;
static sw_machine m, scratch;
static sw_subframe sub;
static uint8_t space[SW_AVG_SPACE_SIZE];

static long frames_seen, frames_viewed, exact, inexact, bad_lists, shown;
/* the picture computed a frame ahead, to be compared with the one the game then draws */
static uint8_t ahead[0x1400];
static uint16_t ahead_start, ahead_len;
static uint32_t ahead_frame;
static long ahead_n, ahead_same, ahead_words, ahead_words_same;
static long by_phase[256], bad_by_phase[256];
static double moved_sum, step_sum, seam_sum;
static long moved_n, seam_n;
static double last_x, last_y;      /* where the picture was at t = 1 of the frame before */
static uint32_t last_frame;
static int last_count;
static sw_avg_list l0, l1, l2;

static int run_list(const sw_machine *s, sw_avg_list *l)
{
    memcpy(space, s->vgram, sizeof s->vgram);
    memcpy(space + 0x3000, rom.vrom, sizeof rom.vrom);
    return sw_avg_run_tagged(space, 0, s->vgtag, s->vgstart, l, 0);
}

/* The mean of the lines' middles: a crude measure of where the picture is. */
static void middle(const sw_avg_list *l, double *x, double *y)
{
    *x = *y = 0;
    for (int i = 0; i < l->count; i++) {
        *x += (l->lines[i].x0 + l->lines[i].x1) * 0.5;
        *y += (l->lines[i].y0 + l->lines[i].y1) * 0.5;
    }
    if (l->count) { *x /= l->count; *y /= l->count; }
}

static void hook(void *user, int after)
{
    (void)user;
    if (!after) {
        sw_subframe_before(&sub, &m);
        return;
    }
    sw_subframe_after(&sub, &m);
    frames_seen++;
    {
        /* the frame just drawn against what was computed for it a frame ago */
        uint16_t st = (uint16_t)((sub.a.ram[SW_VGSYNC - 0x4800] << 8) | SW_VGVARB);
        uint16_t len = (uint16_t)(m.y - st);
        if (ahead_frame + 1 == m.frames && ahead_start == st && len < sizeof ahead) {
            ahead_n++;
            if (len == ahead_len && memcmp(&m.vgram[st], ahead, len) == 0) ahead_same++;
            uint16_t common = len < ahead_len ? len : ahead_len;
            for (uint16_t i = 0; i + 1 < common; i += 2) {
                ahead_words++;
                ahead_words_same += m.vgram[st + i] == ahead[i] && m.vgram[st + i + 1] == ahead[i + 1];
            }
        }
        /* and the next one, ahead */
        sw_subframe_next(&sub, &scratch);
        ahead_start = (uint16_t)((scratch.vgram[0] & 0x1f) << 9 | scratch.vgram[1] << 1);
        ahead_len = (uint16_t)(scratch.y - ahead_start);
        if (ahead_len < sizeof ahead) memcpy(ahead, &scratch.vgram[ahead_start], ahead_len);
        ahead_frame = m.frames;
        if (run_list(&scratch, &l0) != SW_AVG_OK) bad_lists++;
    }
    uint8_t phase = sub.a.ram[SW_PHASE - 0x4800];

    /* t = 0: the frame's own list */
    if (!sw_subframe_render(&sub, &m, 0.0f, &scratch)) return;
    frames_viewed++;
    by_phase[phase]++;
    uint16_t start = (uint16_t)((sub.a.ram[SW_VGSYNC - 0x4800] << 8) | SW_VGVARB);
    uint16_t end = m.y, send = scratch.y;
    int same = end == send && end > start && memcmp(&m.vgram[start], &scratch.vgram[start], (size_t)(end - start)) == 0;
    if (same) {
        exact++;
    } else {
        inexact++;
        bad_by_phase[phase]++;
        if (shown++ < 5) {
            int at = 0;
            while (start + at < end && m.vgram[start + at] == scratch.vgram[start + at]) at++;
            printf("FAIL frame %u phase %02X: the picture at t=0 differs from the frame's at word %d (%d and %d words)\n",
                   m.frames, phase, at / 2, (end - start) / 2, (send - start) / 2);
        }
    }
    if (run_list(&scratch, &l0) != SW_AVG_OK) bad_lists++;

    /* in between */
    sw_subframe_render(&sub, &m, 1.0f / 3, &scratch);
    if (run_list(&scratch, &l1) != SW_AVG_OK) bad_lists++;
    sw_subframe_render(&sub, &m, 1.0f, &scratch);
    if (run_list(&scratch, &l2) != SW_AVG_OK) bad_lists++;
    /* (not while the lasers fire: the beams change sides every frame) */
    int quiet = (sub.a.ram[SW_LZ_ON - 0x4800] | sub.a.ram[SW_LZ_HIT - 0x4800] |
                 sub.b.ram[SW_LZ_ON - 0x4800] | sub.b.ram[SW_LZ_HIT - 0x4800]) == 0;
    if (!quiet) last_frame = 0;
    if (quiet && l0.count > 20 && l1.count > 20 && l2.count > 20) {
        double x0, y0, x1, y1, x2, y2;
        middle(&l0, &x0, &y0);
        middle(&l1, &x1, &y1);
        middle(&l2, &x2, &y2);
        moved_sum += hypot(x1 - x0, y1 - y0);
        step_sum += hypot(x2 - x0, y2 - y0);
        moved_n++;
        /* the seam: this frame's own picture against the last one's at t = 1 */
        if (last_frame + 1 == m.frames && abs(last_count - l0.count) <= l0.count / 10) {
            seam_sum += hypot(x0 - last_x, y0 - last_y);
            seam_n++;
        }
        last_x = x2;
        last_y = y2;
        last_count = l2.count;
        last_frame = m.frames;
    }
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_subframe: no ROM set found, nothing tested\n");
        return 0;
    }
    sw_machine_init(&m, &rom);
    m.yoke_exact = 1;
    sw_reset(&m);
    m.frame_hook = hook;
    const int irqs = 60000;
    for (int i = 0; i < irqs; i++) {
        /* two coins, start, then the yoke weaving and the trigger pulled */
        uint8_t in0 = 0xff;
        if ((i >= 3000 && i < 3025) || (i >= 4000 && i < 4025)) in0 &= (uint8_t)~SW_IN0_COINL;
        if (i >= 5000 && i < 5040) in0 &= (uint8_t)~SW_IN0_LFIRE;
        if (i > 12000) {
            m.in.adc[1] = (uint8_t)(128 + 70 * ((i / 300) % 2 ? 1 : -1) * ((i % 300) / 300.0));
            m.in.adc[0] = (uint8_t)(128 + 40 * ((i / 500) % 2 ? 1 : -1) * ((i % 500) / 500.0));
            if (i % 90 < 20) in0 &= (uint8_t)~SW_IN0_LFIRE;
        }
        m.in.in0 = in0;
        sw_irq(&m);
        sw_main_poll(&m);
    }
    int fails = 0;
    printf("%ld game frames, %ld with a view of their own:", frames_seen, frames_viewed);
    for (int p = 0; p < 256; p++)
        if (by_phase[p]) printf(" %02X: %ld", p, by_phase[p]);
    printf("\n  at t=0 the frame's own display list: %ld, not: %ld", exact, inexact);
    for (int p = 0; p < 256; p++)
        if (bad_by_phase[p]) printf(" (%02X: %ld)", p, bad_by_phase[p]);
    printf("\n  display lists in between that do not halt: %ld\n", bad_lists);
    if (moved_n)
        printf("  the picture moves %.1f units a frame, %.1f in the first third (%.0f %%)\n",
               step_sum / moved_n, moved_sum / moved_n, 100.0 * moved_sum / (step_sum + 1e-9));
    if (seam_n)
        printf("  from t=1 of a frame to the next frame's own picture: %.2f units (%ld frames)\n",
               seam_sum / seam_n, seam_n);
    if (seam_n && seam_sum / seam_n > 0.5 * step_sum / moved_n) {
        printf("FAIL: the picture jumps where one frame ends and the next begins\n");
        fails++;
    }
    if (ahead_n)
        printf("  the picture computed a frame ahead is the one then drawn: %ld of %ld frames, %.1f %% of the words\n",
               ahead_same, ahead_n, 100.0 * ahead_words_same / (ahead_words + 1e-9));
    if (ahead_n && ahead_words_same < 0.9 * ahead_words) {
        printf("FAIL: the picture computed ahead is not what the game draws\n");
        fails++;
    }
    if (frames_viewed < 500) { printf("FAIL: the game did not get far\n"); fails++; }
    if (inexact) { printf("FAIL: pictures at t=0 that are not the frame's\n"); fails++; }
    if (bad_lists) { printf("FAIL: display lists that do not halt\n"); fails++; }
    if (moved_n && (moved_sum > 0.6 * step_sum || moved_sum < 0.1 * step_sum)) {
        printf("FAIL: the picture in between is not in between\n");
        fails++;
    }
    sw_avg_list_free(&l0);
    sw_avg_list_free(&l1);
    sw_avg_list_free(&l2);
    return fails ? 1 : 0;
}
