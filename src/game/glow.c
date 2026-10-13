/* glow.c - WSGLOW.MAC: the flash when the player is hit, and the glow of a
 * ship that was. Line by line after the 1983 source; comments in capitals
 * are the original's. */
#include "game/game.h"
#include "game/m6809.h"

/* BG1GLW: BEGIN GLOWING */
void sw_bg1glw(sw_machine *m)
{
    if (R8(SW_GS_GLW) == 0) {               /* ?READY FOR ANOTHER HIT? */
        W8(SW_S_GLW, 0x10);                 /* FOR THIS LONG */
        W8(SW_GS_GLW, R8(SW_GS_GLW) + 1);   /* TELL GAS THAT WE HAVE GLOWED ONCE MORE */
    }
}

/* KPGLOW: KEEP ON GLOWING, DONT TIME OUT OR STICK */
void sw_kpglow(sw_machine *m)
{
    W8(SW_S_GLW, R8(SW_FRAMEL) & 3);
}

/* DO1GLW */
void sw_do1glw(sw_machine *m)
{
    uint8_t b = R8(SW_S_GLW);
    if (b != 0) W8(SW_S_GLW, b - 1);        /* NOT FINISHED YET */
}

/* DO1QGLW: CUT GLOW DOWN TO SIZE */
void sw_do1qglw(sw_machine *m)
{
    if (R8(SW_S_GLW) > 0x08) W8(SW_S_GLW, 0x08);    /* NEED QUICK GLOW FOR TRENCH SEQUENCES, INSTEAD OF 10 FROM BG1GLW */
    sw_do1glw(m);
}

/* DONGLW: GLOW THE ALIENS */
void sw_donglw(sw_machine *m)
{
    (void)R8(SW_FRAMEL);                    /* (LDA FRAMEL, ANDA #0: every frame) */
    for (uint16_t x = SW_ALIEN; x < SW_ALIENZ; x += A_IZE) {
        uint8_t g = (uint8_t)(R8(x + A_GLW) - 1);
        if (g & 0x80) g = 0;                /* FORCE TO ZERO */
        W8(x + A_GLW, g);
    }
}

/* VWGLW: VIEW GLOW IF WE ARE HIT */
void sw_vwglw(sw_machine *m)
{
    if ((R8(SW_S_GLW) & 0x03) == 0) return; /* MAKE IT FLASH */
    TAG(SW_TAG_NONE, 1);
    VG(SW_VGSCAL - 0x100);                  /* SUPER LARGE TO BACKLIGHT SCREEN */
    VG((VGCOLR + (VGCWHT << 8)) | 0xff);    /* MAX WHITE */
    VG(SW_VJFCWN);                          /* BOX */
    VG(SW_VGSCAL);                          /* RESET SCALE */
}
