/* m6809.h - a Motorola 6809, instruction by instruction, with its cycles.
 *
 * Runs the original program from the cabinet's ROMs: the main board's
 * (the game) and the sound board's. All documented instructions and
 * addressing modes, IRQ, FIRQ and NMI, SYNC and CWAI. Cycle counts are the
 * data sheet's: the game's pace depends on them.
 */
#ifndef SWE_CPU_M6809_H
#define SWE_CPU_M6809_H

#include <stdint.h>

typedef uint8_t (*m6809_read_fn)(void *user, uint16_t addr);
typedef void    (*m6809_write_fn)(void *user, uint16_t addr, uint8_t value);

typedef struct m6809 {
    uint8_t  a, b, dp, cc;
    uint16_t x, y, u, s, pc;
    uint8_t  irq, firq;         /* interrupt lines, level: 1 asserted */
    uint8_t  nmi_pending;       /* set by m6809_nmi (edge) */
    uint8_t  nmi_armed;         /* NMI is ignored until S has been loaded */
    uint8_t  waiting;           /* 1 in SYNC, 2 in CWAI (state already pushed) */
    uint64_t cycles;            /* total run */
    m6809_read_fn  read;
    m6809_write_fn write;
    void    *user;
} m6809;

#define M6809_CC_C 0x01
#define M6809_CC_V 0x02
#define M6809_CC_Z 0x04
#define M6809_CC_N 0x08
#define M6809_CC_I 0x10
#define M6809_CC_H 0x20
#define M6809_CC_F 0x40
#define M6809_CC_E 0x80

void m6809_init(m6809 *c, m6809_read_fn rd, m6809_write_fn wr, void *user);
void m6809_reset(m6809 *c);         /* PC from FFFE, I and F set */
void m6809_nmi(m6809 *c);

/* One instruction (or the taking of an interrupt, or one idle step while
 * waiting for one). Returns the cycles it took. */
int m6809_step(m6809 *c);

#endif
