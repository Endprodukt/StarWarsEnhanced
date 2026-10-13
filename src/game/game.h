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

/* WSMAIN.MAC (game/mainline.c). sw_main_poll is WAITFRAME and MAIN: call it
 * after every interrupt, it runs a game frame whenever one is due (every
 * 12th interrupt, and the VG buffer handed over). sw_main_frame runs one
 * frame at once: IFRAME and the current phase. */
void sw_main_poll(sw_machine *m);
void sw_main_frame(sw_machine *m);
/* game/pace.c: the time the 6809 took over the frame just run, in master
 * clock cycles, from the work counted; clears the counts. */
uint32_t sw_pace_frame_cycles(sw_machine *m);
void sw_downvg(sw_machine *m);      /* DOWNVG: the VG pseudo-ROM from the program ROM into VG RAM */

/* TCMES.MAC (game/mess.c): messages by number, into the display list at Y. */
void sw_messon(sw_machine *m, uint8_t a);
void sw_messoff(sw_machine *m, uint8_t a);
void sw_alloff(sw_machine *m);
void sw_vwmess(sw_machine *m);
void sw_vwmes2(sw_machine *m);
void sw_vwspms(sw_machine *m);
void sw_bgbnnr(sw_machine *m);
void sw_vwbnnr(sw_machine *m);
void sw_vw8dig(sw_machine *m, uint16_t x);
void sw_vw6dig(sw_machine *m, uint16_t x);
void sw_vwbyte(sw_machine *m, uint8_t a);
void sw_vwnibl(sw_machine *m, uint8_t a);
void sw_domess(sw_machine *m, uint8_t b);
void sw_domes2(sw_machine *m, uint8_t b);
void sw_mesout(sw_machine *m);

/* TCHSCR.MAC (game/hscore.c): the high score table. */
void sw_vwhscr(sw_machine *m);
void sw_update(sw_machine *m);
void sw_getint(sw_machine *m);
void sw_setint(sw_machine *m);
void sw_chkhis(sw_machine *m);

/* TCEROM.MAC (game/eerom.c): the NOVRAM. sw_summit and sw_eecksm return 0
 * when the area's checksum was right. */
void     sw_eeinit(sw_machine *m);
void     sw_eeini0(sw_machine *m, uint8_t a);
int      sw_summit(sw_machine *m, uint8_t a, uint8_t *sum, uint16_t *xp);
int      sw_eecksm(sw_machine *m, uint8_t a);
uint16_t sw_eetemp(sw_machine *m, uint16_t x);
uint16_t sw_eetmp3(sw_machine *m, uint16_t x, uint16_t u, uint8_t a);
uint16_t sw_tmpee3(sw_machine *m, uint16_t x, uint16_t u, uint8_t a);
void     sw_eeutim(sw_machine *m);

/* WSSTAR.MAC (game/star.c): the stars in space and on the ground. */
void sw_vwstar(sw_machine *m);
void sw_vwstrg(sw_machine *m);

/* WSOBJ.MAC (game/obj.c): 3-D objects through the math box. */
void sw_inobj(sw_machine *m);
void sw_adbobj(sw_machine *m, uint8_t b);
void sw_adobj(sw_machine *m);
void sw_objcen(sw_machine *m);
void sw_bjlnv(sw_machine *m);
void sw_bjlnb(sw_machine *m);
void sw_objbpnt(sw_machine *m, uint8_t b);
void sw_objpnt(sw_machine *m);
void sw_objdrw(sw_machine *m);
void sw_bjgrot(sw_machine *m);
void sw_bjfrot(sw_machine *m);
void sw_bjgpnt(sw_machine *m);
void sw_bjbclp(sw_machine *m);
void sw_bjgclp(sw_machine *m);
void sw_bjgdrw(sw_machine *m);

/* WSCPU.MAC (game/alien.c): the enemy ships. X is an alien's record. */
void sw_nwnshp(sw_machine *m);
void sw_adashp(sw_machine *m);
void sw_cpu(sw_machine *m);         /* game/aliencpu.c */
void sw_cpuret(sw_machine *m);
void sw_nwchor(sw_machine *m, uint16_t x);
void sw_chopdo(sw_machine *m, uint16_t x);
void sw_cpugon(sw_machine *m);

/* WSLAZR.MAC (game/laser.c) */
void sw_tstlaz(sw_machine *m);
void sw_vwlaz(sw_machine *m);
void sw_clslz(sw_machine *m);
void sw_clglz(sw_machine *m);
void sw_clblz(sw_machine *m);
void sw_cphtsa(sw_machine *m);      /* game/aliencpu.c */
/* WSGUNS.MAC (game/guns.c): the enemies shots. */
void sw_fragun(sw_machine *m, uint16_t x, uint16_t u);
void sw_mvgun(sw_machine *m);
void sw_vwgun(sw_machine *m);
void sw_gnhtsg(sw_machine *m);
void sw_frplgn(sw_machine *m);
void sw_frprgn(sw_machine *m);
void sw_frbgn(sw_machine *m);
void sw_frwgn(sw_machine *m, int side);
void sw_frptgn(sw_machine *m);
void sw_mvptgn(sw_machine *m);
void sw_vwptgn(sw_machine *m);
/* WSBASE.MAC (game/base.c): the trench. */
void sw_nwbase(sw_machine *m);
void sw_gnbase(sw_machine *m);
void sw_dofar(sw_machine *m);
void sw_dobase(sw_machine *m);
void sw_vwbase(sw_machine *m);
void sw_vwpanel(sw_machine *m);     /* WSPANL.MAC (game/panel.c) */
/* WSGRND.MAC (game/ground.c): towers and bunkers. */
void sw_igrnd(sw_machine *m);
void sw_vwgrnd(sw_machine *m);
void sw_gdhtgb(sw_machine *m);
/* WSXPLD.MAC (game/xpld.c): the explosions of the others. */
void sw_bgaxpld(sw_machine *m, uint16_t x);
void sw_ixpld(sw_machine *m);
void sw_doxpld(sw_machine *m);
void sw_vwxpld(sw_machine *m);
void sw_bgxpln(sw_machine *m);
void sw_bgbkxp(sw_machine *m);
void sw_bgtwxp(sw_machine *m);
void sw_vwxpln(sw_machine *m);

/* the score (game/gas.c) */
void sw_aduscr(sw_machine *m, uint16_t u);
void sw_scrtie(sw_machine *m);
void sw_scrdarth(sw_machine *m);
void sw_scrpnl(sw_machine *m);
void sw_scrbnk(sw_machine *m);
void sw_scrtrt(sw_machine *m);
void sw_scrsht(sw_machine *m);
void sw_scrport(sw_machine *m);
void sw_scrall(sw_machine *m);
void sw_scrfrc(sw_machine *m);
void sw_scrshld(sw_machine *m);
void sw_scrwav(sw_machine *m);
void sw_scrtwr(sw_machine *m);
void sw_vwfrc(sw_machine *m);

/* WSGLOW.MAC (game/glow.c), WSGAS.MAC (game/gas.c) */
void sw_bg1glw(sw_machine *m);
void sw_kpglow(sw_machine *m);
void sw_do1glw(sw_machine *m);
void sw_do1qglw(sw_machine *m);
void sw_donglw(sw_machine *m);
void sw_vwglw(sw_machine *m);
void sw_adcgas(sw_machine *m);
void sw_do1gas(sw_machine *m);
void sw_vwgas(sw_machine *m);

/* WSMAIN.MAC, the ships and the far Death Star in view (game/shipview.c) */
void sw_snvw(sw_machine *m);
void sw_vwdtha(sw_machine *m);
void sw_vwdthb(sw_machine *m);

/* WSMAIN.MAC, MOVE THE PLAYER (game/player.c) */
void sw_stwsp1(sw_machine *m);
void sw_stwsp2(sw_machine *m);
void sw_s1twgd(sw_machine *m);
void sw_untwgd(sw_machine *m, uint16_t d);
void sw_smvsp1(sw_machine *m);
void sw_residu(sw_machine *m);

/* WSMATH.MAC, WSXMT.MAC, WSSITE.MAC (game/gmath.c) */
void sw_mgowt(sw_machine *m, uint8_t a);
void sw_unitv(sw_machine *m, uint16_t u);
void sw_s1rl(sw_machine *m);
void sw_s1pt(sw_machine *m);
void sw_s1yw(sw_machine *m);
void sw_rnd8(sw_machine *m);
void sw_doxmt(sw_machine *m, uint8_t a);
void sw_vwcurs(sw_machine *m);
void sw_vwcurc(sw_machine *m);
void sw_vwsite(sw_machine *m);
void sw_vwsitc(sw_machine *m);

/* TCSPLS.MAC SPLASH: four random laser splash pictures into VG RAM. */
void sw_splash(sw_machine *m);

/* The splash vector table (TSPLSH) as the source defines it; 0 if the
 * same 96 bytes are found in the program ROM, as they should be. */
int sw_splash_verify(const sw_machine *m);

#endif
