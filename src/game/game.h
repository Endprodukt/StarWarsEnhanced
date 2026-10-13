/* game.h - the native port of the Star Wars program, routine by routine.
 * Each function names the original routine and file it follows. */
#ifndef SWVP_GAME_H
#define SWVP_GAME_H

#include "core/machine.h"

/* WSINT.MAC: the 246 Hz interrupt, including the VG restart every 6th. */
void sw_irq(sw_machine *m);

/* WSINT.MAC RESET: the state the main line starts from (RAM cleared, VG RAM
 * filled with HALTs and the double buffer set up, pot filter delay). */
void sw_reset(sw_machine *m);

/* WSCOIN.MAC COINS + COIN69.MAC MOOLAH, called from the IRQ. */
void sw_coins(sw_machine *m);

/* TCEROM.MAC EEACCU: coin accounting into the EEROM. Not ported yet. */
void sw_eeaccu(sw_machine *m);

/* TCSPLS.MAC SPLASH: four random laser splash pictures into VG RAM. */
void sw_splash(sw_machine *m);

/* The splash vector table (TSPLSH) as the source defines it; 0 if the
 * same 96 bytes are found in the program ROM, as they should be. */
int sw_splash_verify(const sw_machine *m);

#endif
