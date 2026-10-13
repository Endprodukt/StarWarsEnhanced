/* test_attract - the native game running freely from reset: frames keep
 * coming, the vector generator keeps being restarted and every display
 * list halts, with the yoke at rest and held in each corner.
 *
 * Needs the ROM set (the game's tables are in it): looks in $SWE_ROMS and
 * the usual folders, and passes without testing when there is none. */
#include "avg/avg.h"
#include "core/emu.h"
#include "core/machine.h"
#include "game/game.h"
#include "render/smooth.h"
#include "rom/romset.h"
#include "sound/sndboard.h"

#include <stdlib.h>

#include <stdio.h>
#include <string.h>

static sw_romset rom;
static sw_machine m;
static uint8_t space[SW_AVG_SPACE_SIZE];

static int run(uint8_t adc_y, uint8_t adc_x, int irqs)
{
    int fails = 0;
    sw_avg_list list;
    memset(&list, 0, sizeof list);
    sw_machine_init(&m, &rom);
    m.in.adc[0] = adc_y;
    m.in.adc[1] = adc_x;
    sw_reset(&m);
    uint32_t frames_at = 0, starts_at = 0;
    static sw_smooth smooth;
    uint8_t buffer = 0;
    long keyed = 0, lines = 0, matched = 0;
    int last_frame = 0, last_start = 0, longest_frame = 0, longest_start = 0;
    for (int i = 0; i < irqs; i++) {
        sw_irq(&m);
        sw_main_poll(&m);
        if (m.frames != frames_at) {
            if (i - last_frame > longest_frame) longest_frame = i - last_frame;
            frames_at = m.frames;
            last_frame = i;
        }
        if (m.vg_starts != starts_at) {
            if (i - last_start > longest_start) longest_start = i - last_start;
            starts_at = m.vg_starts;
            last_start = i;
            memcpy(space, m.vgram, sizeof m.vgram);
            memcpy(space + 0x3000, rom.vrom, sizeof rom.vrom);
            int st = sw_avg_run_tagged(space, 0, m.vgtag, m.vgstart, &list, 0);
            int new_frame = m.vgram[0] != buffer;
            buffer = m.vgram[0];
            sw_smooth_key(&smooth, &list, i / 246.09, new_frame, 0, 0);
            if (new_frame && smooth.frames > 2 && smooth.match) {
                keyed++;
                lines += smooth.cur.count;
                for (int k = 0; k < smooth.cur.count; k++) matched += smooth.match[k] >= 0;
            }
            if (st != SW_AVG_OK) {
                if (!fails) printf("FAIL yoke %02X %02X: display list does not halt at interrupt %d (phase %02X)\n",
                                   adc_y, adc_x, i, m.ram[SW_PHASE - 0x4800]);
                fails++;
            }
        }
    }
    printf("yoke %02X %02X: %u frames, %u VG starts in %d interrupts; longest wait %d for a frame, %d for a VG start\n",
           adc_y, adc_x, m.frames, m.vg_starts, irqs, longest_frame, longest_start);
    if (irqs - last_frame > 60 || longest_frame > 60) { printf("FAIL: the main line stalls\n"); fails++; }
    if (irqs - last_start > 60 || longest_start > 60) { printf("FAIL: the vector generator stalls\n"); fails++; }
    printf("  lines found again in the next frame: %.1f %% of %ld in %ld frames\n", lines ? 100.0 * matched / lines : 0.0, lines, keyed);
    sw_smooth_free(&smooth);
    sw_avg_list_free(&list);
    return fails;
}

/* The original program on the 6809 core, played by a script: two coins,
 * start, then the yoke weaving and the trigger pulled. It has to get into
 * the game (the space wave and beyond) and keep drawing. */
static sw_emu emu;

static int run_game(int irqs, int stand_in)
{
    int fails = 0;
    sw_avg_list list;
    static sw_smooth smooth;
    memset(&list, 0, sizeof list);
    sw_machine_init(&m, &rom);
    memset(&emu, 0, sizeof emu);
    /* The sound board with it; $SWE_WAV names a file for what it plays. */
    static sw_sndboard snd;
    sw_sndboard_init(&snd, &m);
    snd.native = stand_in >= 2;     /* the sound board's program in C with the steady frames */
    sw_sndboard_reset(&snd);
    emu.snd = &snd;
    emu.yoke_exact = 1;
    m.yoke_exact = 1;
    m.steady = stand_in >= 2 ? (uint8_t)(stand_in - 1) : 0;
    if (stand_in) {
        /* the C routines, the 6809 only for the phases not ported */
        sw_reset(&m);
        sw_emu_attach(&emu, &m);
    } else {
        sw_emu_init(&emu, &m);
        emu.snd = &snd;
        emu.yoke_exact = 1;
    }
    FILE *wav = getenv("SWE_WAV") ? fopen(getenv("SWE_WAV"), "wb") : NULL;
    static int16_t pcm[4096];
    long samples = 0, loud = 0, requests = 0;
    uint32_t snd_seen = 0;
    if (wav) fseek(wav, 44, SEEK_SET);
    uint8_t seen[256] = { 0 }, buffer = 0;
    uint32_t starts_at = 0;
    long keyed = 0, lines = 0, matched = 0, bad_lists = 0;
    int last_start = 0, longest_start = 0, sight_ok = 0;
    /* the pace: interrupts and game frames by the phase */
    static long irqs_in[256], frames_in[256];
    uint8_t last_frame = 0;
    memset(irqs_in, 0, sizeof irqs_in);
    memset(frames_in, 0, sizeof frames_in);
    for (int i = 0; i < irqs; i++) {
        uint8_t in0 = 0xff;
        if ((i >= 3000 && i < 3025) || (i >= 4000 && i < 4025)) in0 &= (uint8_t)~SW_IN0_COINL;
        if (i >= 5000 && i < 5040) in0 &= (uint8_t)~SW_IN0_LFIRE;
        if (i > 12000) {
            m.in.adc[1] = (uint8_t)(128 + 70 * ((i / 300) % 2 ? 1 : -1) * ((i % 300) / 300.0));
            m.in.adc[0] = (uint8_t)(128 + 40 * ((i / 500) % 2 ? 1 : -1) * ((i % 500) / 500.0));
            if (i % 90 < 20) in0 &= (uint8_t)~SW_IN0_LFIRE;
        }
        m.in.in0 = in0;
        if (stand_in) sw_emu_native_irq(&emu);
        else sw_emu_irq(&emu);
        for (int got; (got = sw_sndboard_read(&snd, pcm, 4096)) > 0; ) {
            for (int k = 0; k < got; k++) loud += pcm[k] > 500 || pcm[k] < -500;
            samples += got;
            if (wav) fwrite(pcm, 2, (size_t)got, wav);
        }
        requests += m.snd_w - snd_seen;
        snd_seen = m.snd_w;
        seen[m.ram[SW_PHASE - 0x4800]] = 1;
        irqs_in[m.ram[SW_PHASE - 0x4800]]++;
        if (m.ram[SW_FRAME + 1 - 0x4800] != last_frame) {
            last_frame = m.ram[SW_FRAME + 1 - 0x4800];
            frames_in[m.ram[SW_PHASE - 0x4800]]++;
        }
        if (i == 2000) sight_ok = m.ram[SW_VG_CX - 0x4800] == 0 && m.ram[SW_VG_CY - 0x4800] == 0;
        if (m.vg_starts != starts_at) {
            /* (the program's reset waits a second for the NOVRAM before anything is drawn) */
            if (i > 2000 && i - last_start > longest_start) longest_start = i - last_start;
            starts_at = m.vg_starts;
            last_start = i;
            memcpy(space, m.vgram, sizeof m.vgram);
            memcpy(space + 0x3000, rom.vrom, sizeof rom.vrom);
            if (sw_avg_run_tagged(space, 0, m.vgtag, m.vgstart, &list, 0) != SW_AVG_OK) bad_lists++;
            int new_frame = m.vgram[0] != buffer;
            buffer = m.vgram[0];
            sw_smooth_key(&smooth, &list, i / 246.09, new_frame, 0, 0);
            if (new_frame && smooth.frames > 2 && smooth.match) {
                keyed++;
                lines += smooth.cur.count;
                for (int k = 0; k < smooth.cur.count; k++) matched += smooth.match[k] >= 0;
            }
        }
    }
    printf("%s: phases seen:", stand_in ? "C routines with the 6809 standing in" : "original program");
    for (int p = 0; p < 256; p++) if (seen[p]) printf(" %02X", p);
    printf("\n  %u VG starts in %d interrupts, longest wait %d; %ld lists not halting\n", m.vg_starts, irqs,
           longest_start, bad_lists);
    printf("  interrupts per frame:");
    for (int p = 0; p < 256; p++)
        if (frames_in[p] >= 30) printf(" %02X: %.1f", p, (double)irqs_in[p] / frames_in[p]);
    printf("\n");
    printf("  lines found again in the next frame: %.1f %% of %ld in %ld frames\n",
           lines ? 100.0 * matched / lines : 0.0, lines, keyed);
    printf("  sound: %ld requests to the sound board, %ld samples, %.1f %% of them loud\n", requests, samples,
           samples ? 100.0 * loud / samples : 0.0);
    if (wav) {
        /* the header of a 16 bit mono WAV */
        uint32_t bytes = (uint32_t)samples * 2, rate = SW_SOUND_RATE, v32;
        uint16_t v16;
        fseek(wav, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav); v32 = 36 + bytes; fwrite(&v32, 4, 1, wav);
        fwrite("WAVEfmt ", 1, 8, wav); v32 = 16; fwrite(&v32, 4, 1, wav);
        v16 = 1; fwrite(&v16, 2, 1, wav); fwrite(&v16, 2, 1, wav);
        fwrite(&rate, 4, 1, wav); v32 = rate * 2; fwrite(&v32, 4, 1, wav);
        v16 = 2; fwrite(&v16, 2, 1, wav); v16 = 16; fwrite(&v16, 2, 1, wav);
        fwrite("data", 1, 4, wav); fwrite(&bytes, 4, 1, wav);
        fclose(wav);
    }
    if (samples < (long)irqs * SW_SOUND_RATE / 250 || loud == 0) { printf("FAIL: the sound board is silent\n"); fails++; }
    if (!seen[SW_PHSSP1 + 1]) { printf("FAIL: the game did not reach the space wave\n"); fails++; }
    if (longest_start > 200 || bad_lists) { printf("FAIL: the picture stalls or a list does not halt\n"); fails++; }
    if (!sight_ok) { printf("FAIL: the sight is not centered with the yoke at rest (yoke_autocenter)\n"); fails++; }
    if (stand_in) {
        sw_emu_share sh;
        sw_emu_get_share(&emu, &sh);
        printf("  game frames: %u by the C routines, %u by the 6809 (%.0f %% native)\n", sh.native_frames,
               sh.original_frames, 100.0 * sh.native_frames / (sh.native_frames + sh.original_frames + 1e-9));
    }
    sw_smooth_free(&smooth);
    sw_avg_list_free(&list);
    return fails;
}

int main(void)
{
    const char *paths[32];
    int n = sw_romset_default_paths(paths, 32);
    if (sw_romset_load(&rom, paths, n) != 0) {
        printf("test_attract: no ROM set found, nothing tested\n");
        return 0;
    }
    int fails = 0;
    fails += run(0x80, 0x80, 30000);
    fails += run(0x00, 0x00, 12000);
    fails += run(0xff, 0xff, 12000);
    fails += run(0x00, 0xff, 12000);
    fails += run(0xff, 0x00, 12000);
    fails += run_game(60000, 0);
    fails += run_game(60000, 1);
    fails += run_game(60000, 2);     /* the same with steady frames at the machine's pace */
    fails += run_game(60000, 3);     /* and with a frame every 12th interrupt */
    return fails ? 1 : 0;
}
