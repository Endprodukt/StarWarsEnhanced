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
    m->note.w = sw_divider_quotient(&m->div);   /* (for the renderer: how far away) */
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
    m->note.w = sw_divider_quotient(&m->div);
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
        m->pt_w[((x - SW_BJ_PNT) >> 2) & 63] = sw_divider_quotient(&m->div);
        project(m);                         /* 1/X (*Y,*Z) */
        W16(x + 0, R16(SW_M_YP));
        W16(x + 2, R16(SW_M_ZP));
        x += 4;
        W8(SW_L_TMPCNT, R8(SW_L_TMPCNT) - 1);
    } while ((int8_t)R8(SW_L_TMPCNT) > 0);
}

/* For the renderer (sw_machine.vgnote): the lines that follow are those of
 * object BJ.ID, each drawn to one of its points. */
static void note_object(sw_machine *m)
{
    if (++m->inst_n == 0) m->inst_n = 1;
    m->note.inst = m->inst_n;
    m->note.model = (uint8_t)(m->ram[SW_BJ_ID - 0x4800] + 1);
}

static void note_point(sw_machine *m, unsigned n)
{
    m->note.pt = (uint8_t)(n + 1);
    m->note.w = m->pt_w[n & 63];
}

static void note_end(sw_machine *m)
{
    m->note.inst = 0;
    m->note.model = m->note.pt = 0;
}

/* OBJDRW: the object's lines between the projected points. A line code is
 * the point's offset; bit 0: move without drawing; bit 1: the end, or the
 * end of what shows at this much damage. */
void sw_objdrw(sw_machine *m)
{
    uint16_t u = O16(SW_TDRWLN + 2 * R8(SW_BJ_ID));
    u++;                                    /* (a first byte not 0: NOT AN INTERPRETABLE OBJECT) */
    note_object(m);
    for (;;) {
        uint8_t b = O8(u++);
        if (b & 0x02) {
            if (b == 0xff || (b >> 2) <= R8(SW_BJ_DMC)) {
                note_end(m);
                return;
            }
            continue;                       /* CONTINUE IF BACK DAMAGE */
        }
        note_point(m, b >> 2);
        W8(SW_BJ_BRT, (b & 0x03) ? 0x00 : 0xe0);        /* 0 CODE HAS BRITNESS: PRE FORMATTED */
        uint16_t x = (uint16_t)(SW_BJ_PNT + (b & 0xfc));    /* ADD OFFSET TO POINT TABLE */
        VG((R16(x + 2) - R16(SW_BJ_CY)) & 0x1fff);      /* Y */
        VG((((R16(x) - R16(SW_BJ_CX)) & 0x1fff) | (R8(SW_BJ_BRT) << 8)));   /* X */
        W16(SW_BJ_CX, R16(x));
        W16(SW_BJ_CY, R16(x + 2));
    }
}

/* --- ground objects ------------------------------------------------------------------ */

/* The things on the Death Star (towers, bunkers, what stands on the walls
 * of the trench, the exhaust port) take another way through the math box:
 * BJGROT turns the points of the object once (they all face the same way)
 * into blocks of 16 bytes at M.GD; for each one of the kind BJGPNT or
 * BJBCLP then only moves them to its place and projects them. */

#define GD_BLOCK 0x10       /* 16. BYTES PER POINT */

/* BJGROT, BJFROT: ROTATE GROUND OBJECT'S POINTS AND SAVE AT M.GD;
 * BJFROT with the Y DIRECTION FLIPPED, for the right wall */
static void rotate_ground(sw_machine *m, int flip)
{
    W16(SW_M_U1 + M_TX, 0);                 /* CENTER OF OBJECT IS AT ZERO */
    W16(SW_M_U1 + M_TY, 0);
    W16(SW_M_U1 + M_TZ, 0);

    uint8_t b = R8(SW_BJ_ID);
    W8(SW_L_TMPCNT, O8(SW_TDRWNM + b));     /* GET # OF POINTS */
    uint16_t u = O16(SW_TDRWPC + 2 * b);    /* U-->ROM POINTS FOR OBJECT */

    uint16_t x = SW_M_GDFE;                 /* FRONT END FOR LATER FAST TRANSFORM */
    do {                                    /* DO THE ROTATION */
        W16(SW_M_GD + 0, O16(u));           /* X */
        W16(SW_M_GD + 2, flip ? (uint16_t)(0 - O16(u + 2)) : O16(u + 2));   /* Y */
        W16(SW_M_GD + 4, O16(u + 4));       /* Z */
        u += 6;
        W16(SW_M_BIC, SW_MSGDBC);           /* POINT TO TRANSFORM BLOCK */
        sw_mgowt(m, SW_MSPSUB);             /* STANDARD PRESUBTRACT */
        W16(x + SW_MSGDXR, R16(SW_M_XP));
        W16(x + SW_MSGDYR, R16(SW_M_YP));
        W16(x + SW_MSGDZR, R16(SW_M_ZP));
        x += GD_BLOCK;
        W8(SW_L_TMPCNT, R8(SW_L_TMPCNT) - 1);
    } while ((int8_t)R8(SW_L_TMPCNT) > 0);

    W16(SW_M_U1 + M_TX, R16(SW_M_S1 + M_TX));   /* RESTORE ORIGINAL VALS */
    W16(SW_M_U1 + M_TY, R16(SW_M_S1 + M_TY));
    W16(SW_M_U1 + M_TZ, R16(SW_M_S1 + M_TZ));
}

void sw_bjgrot(sw_machine *m) { rotate_ground(m, 0); }
void sw_bjfrot(sw_machine *m) { rotate_ground(m, 1); }

/* BJGPNT, BJBCLP, BJGCLP: TRANSLATE AND PROJECT THE POINTS OF GROUND
 * OBJECT to where the math box has its center (XP, YP, ZP); with a clip
 * the points too near or behind are thrown far out and the screen values
 * kept to so much beyond the edges. */
static void project_ground(sw_machine *m, int clip)
{
    W16(SW_M_XP, 0 - R16(SW_M_XP));         /* NEGATE XP,YP,ZP */
    W16(SW_M_YP, 0 - R16(SW_M_YP));
    W16(SW_M_ZP, 0 - R16(SW_M_ZP));

    W16(SW_M_BIC, SW_MSGDBC);               /* SET BIC TO Xr+1 */
    /* USING FRONT END INITIALIZOR, TRANSLATE Xr: Xr-(-Xp)=Xr+Xp. START THE DIVIDER */
    W16(SW_M_DVD, R16(SW_M_GDFE + SW_MSGDXR) - R16(SW_M_XP));
    W8(SW_M_PCGO, SW_MSPR2I);               /* PERSPECTIVE MULT 2 INIT: DOIT */
    uint8_t n = O8(SW_TDRWNM + R8(SW_BJ_ID));   /* # OF POINTS */
    uint8_t a = n;
    do {
        uint16_t u = R16(SW_M_QUO);         /* 1/X */
        /* AFTER UNSIGNED DIVIDE, X TOO CLOSE(LE 100) GIVES NEGATIVE QUOTIENT,
         * X BEHIND(HS 8000) GIVES LO QUOTIENT(LS 100): SIGNED LE 100 CATCHES BOTH */
        if (clip && (int16_t)u <= 0x100) u = 0x7fff;
        uint16_t x = R16(SW_M_XT);          /* X FOR THE NEXT LOOP */
        m->pt_w[(n - a) & 63] = u;
        W16(SW_M_YT, u);                    /* SPECIAL PERS USES YT FOR MULT */
        W8(SW_M_PCGO, SW_MSPRS2);           /* DO SPECIAL PERS */
        W16(SW_M_DVD, x);                   /* START NEXT 1/X */
        a--;
    } while ((int8_t)a > 0);
    if (!clip) return;

    /* NOW SET SCREEN VALS TO REASONABLE VG NUMBERS */
    uint16_t u = SW_M_GDXS;                 /* U-->OUTPUT BLOCKS */
    int16_t top = (int16_t)(uint16_t)(SW_VGEDGT - SW_VGOFFY);
    int16_t bottom = (int16_t)(uint16_t)(SW_VGEDGB - SW_VGOFFY);
    a = n;
    do {
        int16_t v = (int16_t)R16(u);        /* VG X */
        if (v > (int16_t)SW_VGEDGR + clip) W16(u, SW_VGEDGR + clip);
        else if (v < (int16_t)SW_VGEDGL - clip) W16(u, SW_VGEDGL - clip);

        v = (int16_t)R16(u + 2);            /* VG Y */
        if (v > top + clip) W16(u + 2, top + clip);
        else if (v < bottom - clip) W16(u + 2, bottom - clip);

        u += GD_BLOCK;                      /* NEXT OUTPUT BLOCK */
        a--;
    } while ((int8_t)a > 0);
}

void sw_bjgpnt(sw_machine *m) { project_ground(m, 0); }
void sw_bjbclp(sw_machine *m) { project_ground(m, 24); }    /* WITH TIGHT CLIPPING */
void sw_bjgclp(sw_machine *m) { project_ground(m, 512); }   /* WITH GENEROUS CLIPPING */

/* The lines of a ground object. The original has them as code, a load and
 * a store for every word of the list (FAST (BUT BULKY) VG DRAW LINE
 * ROUTINES); here they are the arguments of its macros again:
 * PLOT n starts at point n from the center of the screen, TO draws to a
 * point, MOVE goes there dark, WORD puts a command into the list and MEM
 * one that the caller has left in the math box (scale and colors). */
enum { G_END, G_PLOT, G_TO, G_MOVE, G_WORD, G_MEM };
typedef struct { uint8_t op; uint16_t arg; } gline;
#define PLOT(n)   { G_PLOT, n }
#define TO(n)     { G_TO, n }
#define MOVE(n)   { G_MOVE, n }
#define WORD(w)   { G_WORD, (uint16_t)(w) }
#define MEM(a)    { G_MEM, a }
#define ENDPLOT   { G_END, 0 }
#define COLOR(c)  WORD(((c) << 8) + SW_VGCOPC)

static const gline G_BNK[] = {              /* BUNKER */
    PLOT(0), MEM(SW_M_GDSC), MEM(SW_M_GDCT),    /* DRAW TOP OF BUNKER */
    MOVE(1), TO(2), TO(14), TO(13), TO(1), TO(3), TO(15), TO(13),
    MOVE(14), TO(15), ENDPLOT
};
static const gline G_TWR[] = {              /* the tower (TWR, GND) */
    PLOT(0), MEM(SW_M_GDSC), MEM(SW_M_GDCB),    /* COLOR THE BASE */
    MOVE(1), TO(3), TO(15), TO(12), TO(9),
    MEM(SW_M_GDCT), TO(6), TO(4), TO(7),        /* UP THE TOP AND DOWN THE MIDDLE */
    MEM(SW_M_GDCB), TO(10), TO(13), TO(1), TO(2), TO(14), TO(11), TO(8),    /* UP LEFT SIDE */
    MEM(SW_M_GDCT), TO(5), TO(4),               /* AND NOW THE TOP */
    MOVE(7), TO(9),                             /* BOTTOM OF CANNON */
    MOVE(7), TO(8), ENDPLOT                     /* OTHER SIDE OF BOTTOM OF CANNON */
};
static const gline G_STB[] = {              /* STUB OF TOWER WITHOUT BUNKER HAT ON TOP */
    PLOT(0), MEM(SW_M_GDSC), MEM(SW_M_GDCB),
    MOVE(1), TO(3), TO(15), TO(12), TO(9),      /* UP RIGHT SIDE */
    TO(7), TO(10), TO(13), TO(1),               /* DOWN CENTER */
    TO(2), TO(14), TO(11), TO(8), TO(7), ENDPLOT    /* UP LEFT SIDE */
};
static const gline G_WGA[] = {              /* WALL GUN, TYPE A and B */
    PLOT(4), COLOR(VGCRED),
    TO(3), TO(2), TO(1), TO(4), TO(9), TO(10), TO(1),
    MOVE(10), TO(6), TO(2),
    MOVE(3), TO(7), TO(6), TO(5), TO(8), TO(7), TO(9), TO(8),
    TO(13), TO(14), TO(9),
    MOVE(14), TO(12), TO(10), TO(5), TO(11), TO(12),
    MOVE(11), TO(13), ENDPLOT
};
static const gline G_WPN[] = {              /* WALL PANEL */
    PLOT(0), COLOR(VGCGRN),
    TO(1), TO(2), TO(3), TO(0),
    MOVE(4), TO(5), TO(6), TO(7), TO(4), ENDPLOT
};
static const gline G_WFF[] = {              /* WALL FORCE FIELD: COLOR SET BY CALLER */
    PLOT(1), TO(0), TO(2), TO(3), TO(1), TO(5), TO(4), TO(0), ENDPLOT
};
static const gline G_WFG[] = {              /* WALL FORCE FIELD, COLLIDED VERSION */
    PLOT(1), WORD(SW_VJFLS),                /* CATWALK COLOR WHEN COLLIDED */
    TO(0), TO(2), TO(3), TO(1), TO(5), TO(6), TO(3),
    MOVE(5), TO(4), TO(0), ENDPLOT
};
static const gline G_PORT[] = {             /* THERMAL EXHAUST PORT */
    PLOT(5), COLOR(VGCGRN),                 /* OUTER BASE */
    TO(9), TO(8), TO(4),
    MOVE(6), TO(10), TO(11), TO(7),
    COLOR(VGCTRQ),                          /* INNER BERM */
    TO(6), TO(2),
    MOVE(6), TO(4), TO(0),
    MOVE(4), TO(5), TO(1),
    MOVE(5), TO(7), TO(3),
    WORD(((VGCRED << 8) + SW_VGCOPC) | 0xff),   /* PORTHOLE */
    TO(2), TO(0), TO(1), TO(3), ENDPLOT
};

static const gline *ground_lines(unsigned id)
{
    switch (id) {
    case SW_TDSBNK:  return G_BNK;
    case SW_TDSTWR:
    case SW_TDSGND:  return G_TWR;
    case SW_TDSSTB:  return G_STB;
    case SW_TDSWGA:
    case SW_TDSWGB:  return G_WGA;
    case SW_TDSWPN:  return G_WPN;
    case SW_TDSWFF:  return G_WFF;
    case SW_TDSWFG:  return G_WFG;
    case SW_TDSPOR:  return G_PORT;
    default:         return NULL;
    }
}

/* For the renderer (render/solid.h): the points of object id as they are
 * in the ROM and the lines between them, whatever way it is drawn. Returns
 * the number of points, 0 if there is no such object. */
int sw_obj_model(const sw_machine *m, unsigned id, int16_t (*pts)[3], int max_pts,
                 uint8_t (*edges)[2], int max_edges, int *n_edges)
{
    *n_edges = 0;
    if (id > SW_TDSPOR) return 0;
    int n = O8(SW_TDRWNM + id);
    if (n > max_pts) return 0;
    uint16_t x = O16(SW_TDRWPC + 2 * id);
    for (int i = 0; i < n; i++, x += 6)
        for (int k = 0; k < 3; k++) pts[i][k] = (int16_t)O16(x + 2 * k);
    int from = -1;
    const gline *g = ground_lines(id);
    if (g) {
        for (; g->op != G_END; g++) {
            if (g->op != G_PLOT && g->op != G_TO && g->op != G_MOVE) continue;
            if (g->op == G_TO && from >= 0 && *n_edges < max_edges) {
                edges[*n_edges][0] = (uint8_t)from;
                edges[*n_edges][1] = (uint8_t)g->arg;
                (*n_edges)++;
            }
            from = g->arg;
        }
    } else {
        uint16_t u = (uint16_t)(O16(SW_TDRWLN + 2 * id) + 1);
        for (int k = 0; k < 400; k++) {
            uint8_t b = O8(u++);
            if (b == 0xff) break;
            if (b & 0x02) continue;         /* (the end of what shows at some damage) */
            if (!(b & 0x01) && from >= 0 && *n_edges < max_edges) {
                edges[*n_edges][0] = (uint8_t)from;
                edges[*n_edges][1] = (uint8_t)(b >> 2);
                (*n_edges)++;
            }
            from = b >> 2;
        }
    }
    return n;
}

/* BJGDRW: the lines of ground object BJ.ID between its projected points */
void sw_bjgdrw(sw_machine *m)
{
    const gline *g = ground_lines(R8(SW_BJ_ID));
    if (!g) return;                         /* (SWI: OOPS, NOT CORRECT TYPE) */
    uint16_t old = 0;
    note_object(m);
    for (;; g++) {
        uint16_t p = (uint16_t)(SW_M_GDXS + g->arg * GD_BLOCK);
        if (g->op == G_PLOT || g->op == G_TO || g->op == G_MOVE) note_point(m, g->arg);
        switch (g->op) {
        case G_PLOT:
            /* ASSUMING START FROM CENTER OF VG -- MUST OFFSET */
            VG((R16(p + 2) + SW_VGOFFY) & 0x1fff);
            VG(R16(p) & 0x1fff);            /* DELTA X IS ABS X */
            old = p;
            break;
        case G_TO:
        case G_MOVE: {
            VG((R16(p + 2) - R16(old + 2)) & 0x1fff);           /* Y */
            uint16_t d = (uint16_t)(R16(p) - R16(old));         /* X */
            VG(g->op == G_TO ? (d | 0xe000) : (d & 0x1fff));
            old = p;
            break;
        }
        case G_WORD: VG(g->arg); break;
        case G_MEM:  VG(R16(g->arg)); break;
        default:
            note_end(m);
            VG(SW_VGSCAL);                  /* RESET SCALE */
            VG(SW_VGCNTR);                  /* AND CENTER */
            return;
        }
    }
}
