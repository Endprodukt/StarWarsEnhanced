#include "rom/romset.h"

#include "miniz.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum dest { D_MAIN0, D_MAIN, D_VROM, D_SOUND, D_AVG, D_MBOX };

typedef struct rom_entry {
    const char *name;     /* MAME's name, for messages only */
    uint32_t    crc;
    uint32_t    size;
    enum dest   dest;
    uint32_t    offset;
    uint32_t    crc_old;  /* the same ROM as older MAME sets have it, or 0 */
    int         optional; /* not needed by the native game */
} rom_entry;

/* MAME "starwars1" (src/mame/atari/starwars.cpp). Sets from older MAME
 * and AdvanceMAME versions (as on a VectorPie cabinet: starwar1.zip) have
 * the math box PROMs as dumps whose unused upper four bits are not zero,
 * so with other CRCs, and no AVG state PROM; the vector generator is
 * native here and does not use it. */
static const rom_entry ROMS[] = {
    { "136021-114.1f",  0xe75ff867, 0x4000, D_MAIN0, 0x6000, 0, 0 },
    { "136021.102.1hj", 0xf725e344, 0x2000, D_MAIN,  0x8000, 0, 0 },
    { "136021.203.1jk", 0xf6da0a00, 0x2000, D_MAIN,  0xa000, 0, 0 },
    { "136021.104.1kl", 0x7e406703, 0x2000, D_MAIN,  0xc000, 0, 0 },
    { "136021.206.1m",  0xc7e51237, 0x2000, D_MAIN,  0xe000, 0, 0 },
    { "136021-105.1l",  0x538e7d2f, 0x1000, D_VROM,  0x0000, 0, 0 },
    { "136021-107.1jk", 0xdbf3aea2, 0x2000, D_SOUND, 0x4000, 0, 0 },
    { "136021-208.1h",  0xe38070a8, 0x2000, D_SOUND, 0x6000, 0, 0 },
    { "136021-109.4b",  0x82fc3eb2, 0x0100, D_AVG,   0x0000, 0, 1 },
    { "136021-110.7h",  0x810e040e, 0x0400, D_MBOX,  0x0000, 0x01061762, 0 },
    { "136021-111.7j",  0xae69881c, 0x0400, D_MBOX,  0x0400, 0x2e619b70, 0 },
    { "136021-112.7k",  0xecf22628, 0x0400, D_MBOX,  0x0800, 0x6cfa3544, 0 },
    { "136021-113.7l",  0x83febfde, 0x0400, D_MBOX,  0x0c00, 0x03f6acb2, 0 },
};
#define NROMS ((int)(sizeof ROMS / sizeof ROMS[0]))
#define ALL_FOUND ((1u << NROMS) - 1u)

static int crc_is(const rom_entry *e, uint32_t crc)
{
    return e->crc == crc || (e->crc_old && e->crc_old == crc);
}

/* Every ROM the game needs is there. */
static int complete(const sw_romset *rs)
{
    for (int i = 0; i < NROMS; i++)
        if (!ROMS[i].optional && !(rs->found & (1u << i))) return 0;
    return 1;
}

static void place(sw_romset *rs, int i, const uint8_t *data)
{
    const rom_entry *e = &ROMS[i];
    switch (e->dest) {
    case D_MAIN0:   /* 16K: page 0 at 0x6000, page 1 banked at 0x10000 */
        memcpy(rs->main + 0x6000, data, 0x2000);
        memcpy(rs->main + 0x10000, data + 0x2000, 0x2000);
        break;
    case D_MAIN:
        memcpy(rs->main + e->offset, data, e->size);
        break;
    case D_VROM:
        memcpy(rs->vrom, data, e->size);
        memcpy(rs->main + 0x3000, data, e->size);
        break;
    case D_SOUND:   /* each sound ROM also answers 0x8000 higher */
        memcpy(rs->sound + e->offset, data, e->size);
        memcpy(rs->sound + e->offset + 0x8000, data, e->size);
        break;
    case D_AVG:
        memcpy(rs->avg_prom, data, e->size);
        break;
    case D_MBOX:
        memcpy(rs->mbox_prom + e->offset, data, e->size);
        break;
    }
    rs->found |= 1u << i;
}

/* Offer one file's contents: placed if it is a wanted ROM not yet found. */
static void offer(sw_romset *rs, const uint8_t *data, size_t size)
{
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, data, size);
    for (int i = 0; i < NROMS; i++) {
        if (crc_is(&ROMS[i], crc) && ROMS[i].size == size && !(rs->found & (1u << i)))
            place(rs, i, data);
    }
}

static int wanted_size(size_t size)
{
    for (int i = 0; i < NROMS; i++)
        if (ROMS[i].size == size) return 1;
    return 0;
}

static void scan_zip(sw_romset *rs, const char *path)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, path, 0)) return;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n && rs->found != ALL_FOUND; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        if (!wanted_size((size_t)st.m_uncomp_size)) continue;
        /* The zip's own CRC tells whether the file is worth extracting. */
        int match = 0;
        for (int k = 0; k < NROMS; k++)
            if (crc_is(&ROMS[k], st.m_crc32) && !(rs->found & (1u << k))) match = 1;
        if (!match) continue;
        size_t size = 0;
        void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (data) {
            offer(rs, (const uint8_t *)data, size);
            mz_free(data);
        }
    }
    mz_zip_reader_end(&zip);
}

static void scan_file(sw_romset *rs, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t buf[0x4000 + 1];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < sizeof buf && wanted_size(n)) offer(rs, buf, n);
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int ends_with_zip(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && (strcmp(path + n - 4, ".zip") == 0 || strcmp(path + n - 4, ".ZIP") == 0);
}

static void scan_dir(sw_romset *rs, const char *path)
{
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *de;
    char full[2048];
    while ((de = readdir(d)) != NULL && rs->found != ALL_FOUND) {
        if (de->d_name[0] == '.') continue;
        snprintf(full, sizeof full, "%s/%s", path, de->d_name);
        if (!is_dir(full) && !ends_with_zip(full)) scan_file(rs, full);
    }
    closedir(d);
}

int sw_romset_load(sw_romset *rs, const char *const *paths, int npaths)
{
    memset(rs, 0, sizeof *rs);
    for (int i = 0; i < npaths && rs->found != ALL_FOUND; i++) {
        if (!paths[i] || !*paths[i]) continue;
        if (is_dir(paths[i])) scan_dir(rs, paths[i]);
        else if (ends_with_zip(paths[i])) scan_zip(rs, paths[i]);
        else scan_file(rs, paths[i]);
    }
    return complete(rs) ? 0 : -1;
}

const char *sw_romset_missing(const sw_romset *rs, char *buf, int size)
{
    int len = 0;
    buf[0] = '\0';
    for (int i = 0; i < NROMS; i++) {
        if ((rs->found & (1u << i)) || ROMS[i].optional) continue;
        len += snprintf(buf + len, len < size ? (size_t)(size - len) : 0, "%s%s",
                        len ? ", " : "", ROMS[i].name);
        if (len >= size) break;
    }
    return buf;
}

int sw_romset_default_paths(const char **out, int max)
{
    static char store[24][1024];
    int n = 0;
    const char *bases[8];
    int nb = 0;
    static char envcopy[2048];

    const char *env = getenv("SWE_ROMS");
    if (env && *env) {
        snprintf(envcopy, sizeof envcopy, "%s", env);
        for (char *p = strtok(envcopy, ":"); p && n < max && n < 16; p = strtok(NULL, ":"))
            out[n++] = p;
    }

    const char *home = getenv("HOME");
    static char h1[1024], h2[1024];
    bases[nb++] = "roms";
    if (home) {
        snprintf(h1, sizeof h1, "%s/.advance/rom", home);
        snprintf(h2, sizeof h2, "%s/mame/roms", home);
        bases[nb++] = h1;
        bases[nb++] = h2;
    }
    bases[nb++] = "/home/pi/.advance/rom";

    static const char *const forms[] = { "%s/starwars1.zip", "%s/starwar1.zip", "%s/starwars.zip", "%s/starwars1", "%s" };
    for (int b = 0; b < nb; b++) {
        for (int f = 0; f < 5 && n < max && n < 24; f++) {
            snprintf(store[n], sizeof store[n], forms[f], bases[b]);
            out[n] = store[n];
            n++;
        }
    }
    return n;
}
