/* swcompare - check the native game frame by frame against MAME.
 *
 *   swcompare CAPTURE.swcap [--roms PATH] [--syms sw_symbols.h]
 *             [--show N] [--max N] [--from F] [--to F] [--phase HH] [--csv FILE]
 *
 * The capture (tools/mame/swcapture.lua) holds the original's memory at
 * the start of every game frame (FRAM). For each pair of consecutive
 * frames the native machine is loaded with the first, fed the random
 * numbers the original read, runs one frame (IFRAME and the phase), and
 * must then hold what the second has: program RAM, math box RAM and the
 * display list just written. Every frame is checked on its own, so one
 * difference does not hide the next.
 *
 * Not compared, because the interrupt owns it and ran at other moments
 * than here: coins, switches, pots, timers, the sight. And, until they
 * are ported, what the routines listed below write.
 *
 * Exit status 0 when every frame of a ported phase matches.
 */
#include "capture/capture.h"
#include "core/emu.h"
#include "core/machine.h"
#include "game/game.h"
#include "rom/romset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- what is left out ------------------------------------------------------ */

typedef struct range { uint16_t lo, hi; const char *why; } range;

static const range SKIP[] = {
    { 0x4800, 0x4800, "IRQ: RAM trap" },
    { 0x4809, 0x481b, "IRQ: coins, interrupt count, game time" },
    { 0x481c, 0x4827, "IRQ: switches" },
    { 0x4828, 0x4830, "IRQ: pots, sight" },
    { 0x4832, 0x4840, "IRQ: fire edges, VG animation, frame and VG sync" },
    { 0x4851, 0x4852, "IRQ: rheostat direction" },
    { 0x4866, 0x4878, "IRQ: rheostats" },
    { 0x4b04, 0x4b07, "IRQ: time on" },
    { 0x4f00, 0x4fff, "6809 stack" },
    { 0x4831, 0x4831, "IRQ: lasers lit (VG.LON; the main line clears it)" },
    { 0x4af4, 0x4af4, "IRQ: a coin to account for (EECOIN)" },
    { 0x4b5f, 0x4b64, "IRQ: coin counts (EEACCU, not ported yet)" },
};
#define NSKIP ((int)(sizeof SKIP / sizeof SKIP[0]))

static int skipped(uint16_t a)
{
    for (int i = 0; i < NSKIP; i++)
        if (a >= SKIP[i].lo && a <= SKIP[i].hi) return 1;
    return 0;
}

/* Phases the native main line has (the others fall back to the banner). */
static int ported(uint8_t phase)
{
    static const uint8_t p[] = {
        SW_PHSRES, SW_PHSSTS, SW_PHSSTS + 1, SW_PHSOPT, SW_PHSOPT + 1, SW_PHSBNR, SW_PHSBNR + 1, SW_PHSCN1, SW_PHSCN1 + 1, SW_PHSINS, SW_PHSINS + 1,
        SW_PHSSCR, SW_PHSSCR + 1, SW_PHSHIS, SW_PHSHIS + 1, SW_PHSSDS, SW_PHSSDS + 1,
        SW_PHSENT, SW_PHSENT + 1, SW_PHSSG1, SW_PHSSG1 + 1, SW_PHSSG2, SW_PHSSG2 + 1,
        SW_PHSBGN, SW_PHSBGN + 1, SW_PHSSP1, SW_PHSSP1 + 1, SW_PHSSP2, SW_PHSSP2 + 1,
        SW_PHSS0G, SW_PHSS0G + 1, SW_PHSS1G, SW_PHSS1G + 1,
        SW_PHSS0B, SW_PHSS0B + 1, SW_PHSG0B, SW_PHSBS, SW_PHSBS + 1,
        SW_PHSB0B, SW_PHSB0B + 1, SW_PHSB0D, SW_PHSB0D + 1,
        SW_PHSGD, SW_PHSGD + 1, SW_PHSG0B + 1, SW_PHSG1B, SW_PHSG1B + 1, SW_PHSG0D, SW_PHSG0D + 1,
        SW_PHSDX1, SW_PHSDX1 + 1, SW_PHSDX2, SW_PHSDX2 + 1, SW_PHSDX3, SW_PHSDX3 + 1,
        SW_PHSNXT, SW_PHSNXT + 1,
        SW_PHSS0D, SW_PHSS0D + 1, SW_PHSEGM, SW_PHSEGM + 1,
    };
    for (unsigned i = 0; i < sizeof p; i++)
        if (p[i] == phase) return 1;
    return 0;
}

/* --- symbol names for the report --------------------------------------------- */

typedef struct sym { uint16_t addr; char name[64]; } sym;
static sym  *syms;
static int   nsyms;

static int sym_cmp(const void *a, const void *b)
{
    return (int)((const sym *)a)->addr - (int)((const sym *)b)->addr;
}

static void load_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256], name[64];
    unsigned addr;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "#define SW_%63s 0x%x", name, &addr) != 2) continue;
        if (addr < 0x4800 || addr > 0x5fff) continue;
        syms = (sym *)realloc(syms, (size_t)(nsyms + 1) * sizeof *syms);
        syms[nsyms].addr = (uint16_t)addr;
        snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", name);
        nsyms++;
    }
    fclose(f);
    qsort(syms, (size_t)nsyms, sizeof *syms, sym_cmp);
}

static const char *sym_at(uint16_t a, char *buf, int size)
{
    const sym *best = NULL;
    for (int i = 0; i < nsyms && syms[i].addr <= a; i++) best = &syms[i];
    if (!best) snprintf(buf, (size_t)size, "?");
    else if (best->addr == a) snprintf(buf, (size_t)size, "%.20s", best->name);
    else snprintf(buf, (size_t)size, "%.20s+%X", best->name, (unsigned)(a - best->addr) & 0xfff);
    return buf;
}

/* --- the random numbers the original read ------------------------------------ */

typedef struct feed { const uint8_t *v; uint32_t n, used, over; } feed;

static uint8_t feed_next(void *u)
{
    feed *fd = (feed *)u;
    if (fd->used < fd->n) return fd->v[fd->used++];
    fd->over++;
    return 0;
}

/* --- what the original read of the interrupt's memory -------------------------- */

typedef struct rdfeed { const uint8_t *r; uint32_t n, used, bad; } rdfeed;

/* A read of RAM by the C routines (or a subroutine of the original they
 * call): for the bytes the interrupt keeps, the value the original read at
 * this point of the frame. The original's polls of the frame and VG
 * handshakes, which the C routines do not make, are passed over. */
static uint8_t feed_ram(void *u, uint16_t a, uint8_t cur)
{
    rdfeed *fd = (rdfeed *)u;
    if (!((a >= 0x4809 && a <= 0x4840) || a == 0x4851 || a == 0x4852 || (a >= 0x4866 && a <= 0x4878) ||
          a == 0x4af4 || (a >= 0x4b04 && a <= 0x4b07)))
        return cur;
    while (fd->used < fd->n) {
        const uint8_t *rec = fd->r + 3 * fd->used;
        uint16_t ra = (uint16_t)(rec[0] | (rec[1] << 8));
        if (ra == a) {
            fd->used++;
            if (a == SW_VGSYNC && (rec[2] & 0x80)) continue;   /* still waiting for the buffer */
            return rec[2];
        }
        if (ra != SW_GMSYNC && ra != SW_VGSYNC) break;
        fd->used++;
    }
    /* A few reads the C routines leave out (WAITFRAME's look at the freeze
     * switch, a value the original fetches twice): look a little ahead. */
    for (uint32_t k = 1; k <= 6 && fd->used + k < fd->n; k++) {
        const uint8_t *rec = fd->r + 3 * (fd->used + k);
        if ((uint16_t)(rec[0] | (rec[1] << 8)) == a) {
            fd->used += k + 1;
            return rec[2];
        }
    }
    fd->bad++;
    return cur;
}

/* --- one frame ------------------------------------------------------------------ */

typedef struct snap {
    swcap_tick t;
    uint8_t    nv[0x100];
    int        have_nv;
} snap;

static void load(sw_machine *m, const sw_romset *rom, const snap *s)
{
    sw_machine_init(m, rom);
    memcpy(m->vgram, s->t.vgram, sizeof m->vgram);
    memcpy(m->ram, s->t.ram, sizeof m->ram);
    memcpy(m->mbox.ram, s->t.mbram, sizeof m->mbox.ram);
    if (s->have_nv) memcpy(m->nvram, s->nv, sizeof m->nvram);
    /* The divider's dividend register is not in the capture: it holds what
     * IMATH wrote (the main line writes nothing else there). */
    sw_wr16(m, SW_M_DVN, 0x0200);
}

static const char *arg(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { i++; continue; }
        path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: swcompare CAPTURE.swcap [--roms PATH] [--syms sw_symbols.h]\n"
                        "                 [--show N] [--max N] [--from F] [--to F] [--phase HH] [--csv FILE]\n");
        return 2;
    }
    int maxd = arg(argc, argv, "--max") ? atoi(arg(argc, argv, "--max")) : 24;
    int show = arg(argc, argv, "--show") ? atoi(arg(argc, argv, "--show")) : 5;
    long from = arg(argc, argv, "--from") ? strtol(arg(argc, argv, "--from"), NULL, 0) : 0;
    long to = arg(argc, argv, "--to") ? strtol(arg(argc, argv, "--to"), NULL, 0) : 0x7fffffff;
    int only_phase = arg(argc, argv, "--phase") ? (int)strtol(arg(argc, argv, "--phase"), NULL, 16) : -1;
    load_syms(arg(argc, argv, "--syms") ? arg(argc, argv, "--syms") : "src/core/sw_symbols.h");

    static sw_romset rom;
    const char *paths[40];
    int np = 0;
    char zips[2][1024];
    if (arg(argc, argv, "--roms")) {
        snprintf(zips[0], sizeof zips[0], "%s/starwars1.zip", arg(argc, argv, "--roms"));
        snprintf(zips[1], sizeof zips[1], "%s/starwars.zip", arg(argc, argv, "--roms"));
        paths[np++] = zips[0];
        paths[np++] = zips[1];
        paths[np++] = arg(argc, argv, "--roms");
    }
    np += sw_romset_default_paths(paths + np, 40 - np);
    if (sw_romset_load(&rom, paths, np) != 0) {
        char missing[512];
        fprintf(stderr, "swcompare: ROMs missing: %s\n", sw_romset_missing(&rom, missing, sizeof missing));
        return 2;
    }

    swcap_reader rd;
    if (swcap_open(&rd, path) != 0) {
        fprintf(stderr, "swcompare: %s is not a capture file\n", path);
        return 2;
    }

    static snap prev, cur;
    static sw_machine m;
    static sw_emu emu;
    int use_emu = arg(argc, argv, "--emu") != NULL;
    static uint8_t payload[3 * 65536], rnd[65536], nv[0x100], irds[3 * 65536];
    uint32_t nirds = 0;
    uint32_t nrnd = 0, len;
    int have_prev = 0, have_nv = 0, tick_buffer = -1;
    char tag[4], name[32];
    long frames = 0, checked = 0, bad = 0, unported = 0, shown = 0;
    uint16_t pokes[8][2];
    int npokes = 0;
    long bad_by_phase[256] = { 0 }, n_by_phase[256] = { 0 }, replay_off = 0;
    double times[3] = { 0, 0, 0 };
    FILE *csv = arg(argc, argv, "--csv") ? fopen(arg(argc, argv, "--csv"), "w") : NULL;
    if (csv) {
        fprintf(csv, "frame,phase,cpu_us,wait_us,words,chars,digits,mess,mbox,shown,rnd,rd,wr");
        for (const sw_mbox_entry *e = sw_mbox_entries; e->name; e++) fprintf(csv, ",mb%02X", e->pc);
        fprintf(csv, "\n");
    }
    static long ref_irqs[256], ref_frames[256], nat_irqs[256], nat_frames[256];

    int got;
    while ((got = swcap_next_chunk(&rd, tag, payload, sizeof payload, &len)) == 1) {
        if (!memcmp(tag, "RNDM", 4)) {
            nrnd = len < sizeof rnd ? len : (uint32_t)sizeof rnd;
            memcpy(rnd, payload, nrnd < sizeof payload ? nrnd : sizeof payload);
        } else if (!memcmp(tag, "IRDS", 4)) {
            nirds = (len < sizeof irds ? len : (uint32_t)sizeof irds) / 3;
            memcpy(irds, payload, nirds * 3 < sizeof payload ? nirds * 3 : sizeof payload);
        } else if (!memcmp(tag, "NVRM", 4) && len == sizeof nv) {
            memcpy(nv, payload, sizeof nv);
            have_nv = 1;
        } else if (!memcmp(tag, "POKE", 4) && len == 3) {
            /* The recording's script wrote into the machine between the
             * frames: so does the frame checked against it. */
            if (npokes < 8) {
                pokes[npokes][0] = (uint16_t)(payload[0] | (payload[1] << 8));
                pokes[npokes][1] = payload[2];
                npokes++;
            }
        } else if (!memcmp(tag, "TIME", 4) && len == 24) {
            memcpy(times, payload, sizeof times);   /* little-endian doubles, as this machine's */
        } else if (!memcmp(tag, "TICK", 4) && len == SWCAP_TICK_SIZE) {
            tick_buffer = payload[8] | (payload[9] << 8);
        } else if (!memcmp(tag, "FRAM", 4) && len == SWCAP_TICK_SIZE) {
            swcap_parse_tick(payload, &cur.t);
            memcpy(cur.nv, nv, sizeof nv);
            cur.have_nv = have_nv;
            frames++;
            uint8_t phase = prev.t.ram[SW_PHASE - 0x4800];
            if (have_prev) {
                ref_irqs[phase] += cur.t.irq_count - prev.t.irq_count;
                ref_frames[phase]++;
            }
            if (have_prev && frames - 1 >= from && frames - 1 <= to &&
                (only_phase < 0 || phase == only_phase)) {
                if (!use_emu && !ported(phase)) {
                    unported++;
                } else {
                    load(&m, &rom, &prev);
                    feed fd = { rnd, nrnd, 0, 0 };
                    m.prng = feed_next;
                    m.prng_user = &fd;
                    /* The frame waited for the interrupt to hand it a VG
                     * buffer: the one the original then wrote. */
                    if ((m.ram[SW_VGSYNC - 0x4800] & 0x80) && tick_buffer >= 0)
                        m.ram[SW_VGSYNC - 0x4800] = (uint8_t)(tick_buffer >> 8);
                    /* The sight as the original's IFRAME found it (the
                     * interrupt may have moved it since the snapshot):
                     * what it copied to SI.xx is in the next snapshot. */
                    static const uint16_t sight[4][2] = {
                        { SW_VG_RSY, SW_SI_RSY }, { SW_VG_RSX, SW_SI_RSX }, { SW_VG_CY, SW_SI_CY }, { SW_VG_CX, SW_SI_CX }
                    };
                    for (int i = 0; i < 4; i++) {
                        m.ram[sight[i][0] - 0x4800] = cur.t.ram[sight[i][1] - 0x4800];
                        m.ram[sight[i][0] + 1 - 0x4800] = cur.t.ram[sight[i][1] + 1 - 0x4800];
                    }
                    uint16_t buf = (uint16_t)((m.ram[SW_VGSYNC - 0x4800] << 8) | SW_VGVARB);
                    m.y = buf;
                    int stuck = 0;
                    if (use_emu) {
                        sw_emu_init(&emu, &m);
                        emu.replay = nirds ? irds : NULL;
                        emu.replay_n = nirds;
                        stuck = sw_emu_frame(&emu, 600000) != 0;
                        if (emu.replay && emu.replay_bad)
                            replay_off++;
                    } else {
                        /* The C routines; the 6809 runs the subroutines
                         * they do not have yet. */
                        sw_emu_attach(&emu, &m);
                        m.prng = feed_next;
                        m.prng_user = &fd;
                        rdfeed rf = { irds, nirds, 0, 0 };
                        if (nirds) {
                            m.ram_read_hook = feed_ram;
                            m.ram_read_user = &rf;
                        }
                        sw_main_frame(&m);
                        m.ram_read_hook = NULL;
                        if (rf.bad) replay_off++;
                    }

                    for (int i = 0; i < npokes; i++)
                        if (pokes[i][0] >= 0x4800 && pokes[i][0] < 0x5000)
                            m.ram[pokes[i][0] - 0x4800] = (uint8_t)pokes[i][1];

                    int nd = 0;
                    int detail = shown < show;
                    #define DIFF(fmt, ...) do { \
                        if (detail) { \
                            if (nd == 0) printf("frame %ld (FRAME %04X, phase %02X -> %02X):\n", frames - 1, \
                                                prev.t.frame, phase, cur.t.ram[SW_PHASE - 0x4800]); \
                            if (nd < maxd) printf("  " fmt "\n", __VA_ARGS__); \
                        } \
                        nd++; } while (0)
                    for (uint16_t a = 0x4800; a < 0x5000; a++) {
                        if (skipped(a)) continue;
                        uint8_t nat = m.ram[a - 0x4800], ref = cur.t.ram[a - 0x4800];
                        if (nat != ref)
                            DIFF("RAM  %04X %-10s native %02X  original %02X  (before %02X)", a,
                                 sym_at(a, name, sizeof name), nat, ref, prev.t.ram[a - 0x4800]);
                    }
                    for (uint16_t a = 0x5000; a < 0x6000; a++) {
                        if (skipped(a)) continue;
                        uint8_t nat = m.mbox.ram[a - 0x5000], ref = cur.t.mbram[a - 0x5000];
                        if (nat != ref)
                            DIFF("MATH %04X %-10s native %02X  original %02X  (before %02X)", a,
                                 sym_at(a, name, sizeof name), nat, ref, prev.t.mbram[a - 0x5000]);
                    }
                    if (tick_buffer >= 0) {
                        /* The display list: from the buffer's start to the
                         * last byte the native frame wrote. */
                        for (uint16_t a = buf; a < buf - SW_VGVARB + SW_VGBFSZ && a < 0x3000; a++) {
                            if (m.vgram[a] != cur.t.vgram[a]) {
                                DIFF("VG   %04X (word %d of the list) native %02X%02X  original %02X%02X", a,
                                     (a - buf) / 2, m.vgram[a & ~1], m.vgram[a | 1],
                                     cur.t.vgram[a & ~1], cur.t.vgram[a | 1]);
                                a |= 1;
                            }
                        }
                    }
                    if (csv && tick_buffer >= 0) {
                        double start = times[0] > times[1] ? times[0] : times[1];
                        fprintf(csv, "%ld,%u,%.1f,%.1f,%d,%u,%u,%u,%u,%u,%u,%u,%u", frames - 1, phase,
                                (times[2] - start) * 1e6, (start - times[0]) * 1e6, (m.y - buf) / 2,
                                m.n_chars, m.n_digits, m.n_mess, m.n_mbox, m.n_shown, fd.used, m.n_rd, m.n_wr);
                        for (const sw_mbox_entry *e = sw_mbox_entries; e->name; e++)
                            fprintf(csv, ",%u", m.n_mb[e->pc]);
                        fprintf(csv, "\n");
                    }
                    if (stuck) DIFF("the frame does not end (%s)", "600000 cycles");
                    if (fd.used != fd.n || fd.over)
                        DIFF("random numbers: native read %u, original %u", fd.used + fd.over, fd.n);
                    #undef DIFF
                    if (detail && nd > maxd) printf("  ... %d differences in all\n", nd);
                    checked++;
                    n_by_phase[phase]++;
                    if (nd) { bad++; bad_by_phase[phase]++; shown++; }
                }
            }
            prev = cur;
            have_prev = 1;
            npokes = 0;
            tick_buffer = -1;
        }
    }
    swcap_close(&rd);
    if (csv) fclose(csv);
    if (got < 0) fprintf(stderr, "swcompare: damaged capture file\n");

    printf("%ld frames in the capture, %ld checked: %ld match, %ld differ; %ld in phases not ported\n",
           frames, checked, checked - bad, bad, unported);
    if (replay_off) printf("  %ld frames read the interrupt's memory in another order than the original\n", replay_off);
    for (int p = 0; p < 256; p++)
        if (n_by_phase[p]) printf("  phase %02X: %ld frames, %ld differ\n", p, n_by_phase[p], bad_by_phase[p]);

    /* The pace: interrupts per game frame, phase by phase. The native game
     * runs freely from reset for as many frames (with its own random
     * numbers, so the stars differ); the vector generator's drawing time
     * decides how long a frame takes. */
    sw_machine_init(&m, &rom);
    long irqs = 0, last = 0;
    if (use_emu) {
        /* The original program running freely on the native machine: a
         * frame is counted when FRAME moves on. */
        sw_emu_init(&emu, &m);
        uint8_t framel = 0;
        long n = 0;
        while (n < frames && irqs < frames * 64) {
            uint8_t phase = m.ram[SW_PHASE - 0x4800];
            sw_emu_irq(&emu);
            irqs++;
            if (m.ram[SW_FRAMEL - 0x4800] != framel) {
                framel = m.ram[SW_FRAMEL - 0x4800];
                nat_irqs[phase] += irqs - last;
                nat_frames[phase]++;
                last = irqs;
                n++;
            }
        }
    } else {
        sw_reset(&m);
    }
    uint32_t done = m.frames;
    while (!use_emu && (long)m.frames < frames && irqs < frames * 64) {
        uint8_t phase = m.ram[SW_PHASE - 0x4800];
        sw_irq(&m);
        sw_main_poll(&m);
        irqs++;
        if (m.frames != done) {
            done = m.frames;
            nat_irqs[phase] += irqs - last;
            nat_frames[phase]++;
            last = irqs;
        }
    }
    printf("interrupts per frame (12 = full speed, 20.5 frames a second):\n");
    for (int p = 0; p < 256; p++)
        if (ref_frames[p] > 4 || nat_frames[p] > 4)
            printf("  phase %02X: original %.2f over %ld frames, native %.2f over %ld frames\n", p,
                   ref_frames[p] ? (double)ref_irqs[p] / (double)ref_frames[p] : 0.0, ref_frames[p],
                   nat_frames[p] ? (double)nat_irqs[p] / (double)nat_frames[p] : 0.0, nat_frames[p]);
    return bad || got < 0 ? 1 : 0;
}
