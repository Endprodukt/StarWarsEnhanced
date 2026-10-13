/* emu.h - the original program on the native machine.
 *
 * The 6809 (cpu/m6809.h) runs the game's own code from the ROMs against
 * sw_machine: its RAM, the native math box and divider, the NOVRAM, the
 * switches and the yoke, and the vector generator's timing. Everything the
 * game does is then the original's, every phase, option and test screen,
 * at the original's pace (the 6809's cycles are counted, the math box and
 * the vector generator take their time as in MAME).
 *
 * What stays native around it: the display (the lists the program writes
 * are interpreted, moved between frames and sent to the board), the
 * controls, the sound.
 *
 * The hand-ported routines of src/game compute the same, frame for frame
 * (tools/swcompare checks both against MAME); they are the way to the
 * remaster, where the picture is drawn from the game's state instead of
 * its display list.
 */
#ifndef SWE_EMU_H
#define SWE_EMU_H

#include "core/machine.h"
#include "cpu/m6809.h"

struct sw_sndboard;

typedef struct sw_emu {
    sw_machine *m;
    m6809     cpu;
    int32_t   budget;           /* CPU cycles to run (negative: run over into the next slice) */
    uint32_t  math_left;        /* master clock cycles until the math box is done */
    uint16_t  math_time[256];   /* how long each math box program takes, by its start */
    uint16_t  op_pc;            /* where the instruction being executed starts */
    /* For the renderer: which call of which routine is writing the display
     * list (the stack pointer and return address at the last word written,
     * and whether the stack has been above that level since). */
    uint16_t  vg_s, vg_ret;
    uint8_t   vg_returned;
    uint8_t   yoke_exact;       /* option yoke_autocenter (see game/mainline.c, pheres) */
    uint8_t   hybrid;           /* attached to the native game: the sound board is run per interrupt */
    struct sw_sndboard *snd;    /* the sound board, run in step with the main CPU; NULL: none */
    uint64_t  irqs;
    uint32_t  original_frames;  /* frames the 6809 ran for the native main line */

    /* Checking against a recording: what the original's main line read
     * of the interrupt's memory, 3 bytes a read (capture.h, IRDS). */
    const uint8_t *replay;
    uint32_t  replay_n, replay_used, replay_bad;
    int       replay_on;
} sw_emu;

#define SW_CPU_CYCLES_PER_IRQ 6144      /* 12.096 MHz / 8 = 1.512 MHz; 246.09 interrupts a second */

/* The machine must be initialized and have its ROM set. Resets the CPU:
 * the program starts at its RESET. */
void sw_emu_init(sw_emu *e, sw_machine *m);

/* One interrupt period: the interrupt line is raised, the CPU runs for
 * 6144 cycles. */
void sw_emu_irq(sw_emu *e);

/* The native game with the original standing in for what is not ported:
 * the interrupt (game/irq.c) and the main line (game/mainline.c) are the C
 * routines; a phase they do not have yet is run by the 6809, one frame at a
 * time, on the same memory. Every ported routine takes work from it.
 * Call instead of sw_emu_init's free run: sw_reset the machine, then
 * sw_emu_attach, then sw_emu_native_irq once per interrupt. */
void sw_emu_attach(sw_emu *e, sw_machine *m);
void sw_emu_native_irq(sw_emu *e);

/* Game frames run by the C routines and by the 6809 since attach. */
typedef struct sw_emu_share { uint32_t native_frames, original_frames; } sw_emu_share;
void sw_emu_get_share(const sw_emu *e, sw_emu_share *out);

/* One game frame without interrupts, for checking against a recording:
 * from WAITFRAME (the machine holding the memory of the frame's start)
 * until the main line is back there. Returns 0, or -1 if it does not come
 * back within max_cycles. */
int sw_emu_frame(sw_emu *e, uint32_t max_cycles);

#endif
