/* obj.c - WSOBJ.MAC: the 3-D objects.
 *
 * An object is a set of points and a list of lines between them, both in
 * the overlay bank of the program ROM (TDRWPC, TDRWLN). Before a phase
 * uses an object its points are copied into the math box RAM (ADBOBJ);
 * each frame the math box turns them into the viewer's frame, they are
 * projected (OBJPNT) and the lines drawn as relative vectors (OBJDRW).
 *
 * Ported so far: the routines for objects in space. The ground and trench
 * objects (BJGROT, BJGPNT, BJGDRW ...) follow with their phases.
 * Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

/* INOBJ: INIT MBOX FOR NEW OBJECT SHAPES */
void sw_inobj(sw_machine *m)
{
    for (uint16_t x = SW_BJBIC; x < SW_BJBICZ; x++) W8(x, 0);     /* CLEAR OUT BIC LIST */
    W8(SW_BJ_BIC, SW_MSBJBC >> 1);          /* STARTING BIC VALUE (PAIRS OF POINTS) */
    W16(SW_BJ_RAM, SW_M_OBJ);               /* STARTING RAM ADDRESS */
}

/* ADBOBJ: ADD OBJECT POINTS TO MBOX. B:=OBJECT # */
void sw_adbobj(sw_machine *m, uint8_t b)
{
    W8(SW_BJ_ID, b);
    sw_adobj(m);
}

void sw_adobj(sw_machine *m)
{
    uint8_t b = R8(SW_BJ_ID);
    if (R8(SW_BJBIC + b) != 0) return;      /* ALREADY PRESENT IN MBOX */
    uint8_t a = R8(SW_BJ_BIC);              /* THEN START THE TRANSFER */
    W8(SW_BJBIC + b, a);                    /* SAVE BIC ADDRESS */
    a = O8(SW_TDRWNM + b);                  /* # OF POINTS TO XFER */
    W8(SW_L_TMPCNT, a);
    W8(SW_BJ_BIC, (a >> 1) + R8(SW_BJ_BIC));    /* PAIRS OF POINTS: UPDATE BIC */
    uint16_t x = O16(SW_TDRWPC + 2 * b);    /* X-->6 BYTE ROM */
    uint16_t u = R16(SW_BJ_RAM);            /* U-->8 BYTE MRAM */
    do {
        W16(u + 0, O16(x + 0));             /* X */
        W16(u + 2, O16(x + 2));             /* Y */
        W16(u + 4, O16(x + 4));             /* Z */
        x += 6;
        u += 8;
        W8(SW_L_TMPCNT, R8(SW_L_TMPCNT) - 1);
    } while ((int8_t)R8(SW_L_TMPCNT) > 0);  /* LOOP THRU THEM ALL */
    W16(SW_BJ_RAM, u);                      /* RECORD LATEST MRAM USAGE */
}

/* The perspective of the point just transformed: 1/X, then Y and Z times it. */
static void project(sw_machine *m)
{
    W16(SW_M_XP, R16(SW_M_QUO));
    sw_mgowt(m, SW_MSPERS);
}

/* OBJCEN (BJCNTR): EXPECTS VG AT CENTER, INFO AT QUO, YP, XP */
void sw_objcen(sw_machine *m)
{
    project(m);
    uint16_t d = R16(SW_M_ZP);
    W16(SW_BJ_CY, d);                       /* NEW CURRENT Y */
    VG((d + SW_VGOFFY) & 0x1fff);           /* Y LONG VECTOR, VIDEO CENTER OFFSET */
    d = R16(SW_M_YP);
    W16(SW_BJ_CX, d);
    VG(d & 0x1fff);                         /* X LONG VECTOR */
}

static void line_to(sw_machine *m, uint8_t brite)
{
    project(m);
    VG((R16(SW_M_ZP) - R16(SW_BJ_CY)) & 0x1fff);    /* CURRENT UP/DOWN POSITION */
    W16(SW_BJ_CY, R16(SW_M_ZP));
    uint16_t d = (uint16_t)(R16(SW_M_YP) - R16(SW_BJ_CX));     /* LEFT/RIGHT */
    if (brite) d |= 0xe000;
    else d &= 0x1fff;                       /* BLANK */
    VG(d);
    W16(SW_BJ_CX, R16(SW_M_YP));
}

/* BJLNV: DRAW VISIBLE OBJECT LINE */
void sw_bjlnv(sw_machine *m) { line_to(m, 1); }

/* BJLNB: DRAW BLANK OBJECT LINE */
void sw_bjlnb(sw_machine *m) { line_to(m, 0); }

/* OBJBPNT, OBJPNT: every point of object BJ.ID through the math box (the
 * viewer looking at the other ship's points) and the perspective, into
 * BJ.PNT as screen x, y. */
void sw_objbpnt(sw_machine *m, uint8_t b)
{
    W8(SW_BJ_ID, b);
    sw_objpnt(m);
}

void sw_objpnt(sw_machine *m)
{
    uint8_t b = R8(SW_BJ_ID);
    W8(SW_L_TMPCNT, O8(SW_TDRWNM + b));     /* # OF POINTS */
    uint8_t a = R8(SW_BJBIC + b);
    W8(SW_M_BICL, a << 1);                  /* ALWAYS IN PAIRS, SHIFT UP TO 9 BITS */
    W8(SW_M_BICH, (uint8_t)(a << 2) | (a >> 7));
    uint16_t x = SW_BJ_PNT;
    do {
        sw_mgowt(m, SW_MSPAD);
        W16(SW_M_DVD, R16(SW_M_XP));        /* X */
        project(m);                         /* 1/X (*Y,*Z) */
        W16(x + 0, R16(SW_M_YP));
        W16(x + 2, R16(SW_M_ZP));
        x += 4;
        W8(SW_L_TMPCNT, R8(SW_L_TMPCNT) - 1);
    } while ((int8_t)R8(SW_L_TMPCNT) > 0);
}

/* OBJDRW: the object's lines between the projected points. A line code is
 * the point's offset; bit 0: move without drawing; bit 1: the end, or the
 * end of what shows at this much damage. */
void sw_objdrw(sw_machine *m)
{
    uint16_t u = O16(SW_TDRWLN + 2 * R8(SW_BJ_ID));
    u++;                                    /* (a first byte not 0: NOT AN INTERPRETABLE OBJECT) */
    for (;;) {
        uint8_t b = O8(u++);
        if (b & 0x02) {
            if (b == 0xff) return;
            if ((b >> 2) > R8(SW_BJ_DMC)) continue;     /* CONTINUE IF BACK DAMAGE */
            return;
        }
        W8(SW_BJ_BRT, (b & 0x03) ? 0x00 : 0xe0);        /* 0 CODE HAS BRITNESS: PRE FORMATTED */
        uint16_t x = (uint16_t)(SW_BJ_PNT + (b & 0xfc));    /* ADD OFFSET TO POINT TABLE */
        VG((R16(x + 2) - R16(SW_BJ_CY)) & 0x1fff);      /* Y */
        VG((((R16(x) - R16(SW_BJ_CX)) & 0x1fff) | (R8(SW_BJ_BRT) << 8)));   /* X */
        W16(SW_BJ_CX, R16(x));
        W16(SW_BJ_CY, R16(x + 2));
    }
}
