/* alien.c - WSCPU.MAC: the enemy ships.
 *
 * A space wave is a table of ships (shape, choreography, starting place);
 * NWNSHP builds the first three, ADASHP fills an empty slot with the next.
 * What a ship does is a small program of its own, the choreography: IF,
 * GOTO, GOSUB, RETURN and timed TWIRL steps, run by NWCHOR/CHNXT.
 *
 * Ported so far: setting up a wave and the choreography interpreter. The
 * ships' flying, aiming and shooting (CPU, AIM) follow with the space
 * waves. Line by line after the 1983 source; comments in capitals are the
 * original's. */
#include "game/game.h"
#include "game/m6809.h"

/* The choreography's op-codes, by the routine that does them. */
static void chnxt(sw_machine *m, uint16_t x);

/* CHOPDO: DO THE OP-CODE */
void sw_chopdo(sw_machine *m, uint16_t x)
{
    uint16_t u;
    /* CHECK CONTROL FLAGS FIRST */
    if ((R8(x + A_CHST) & R8(x + A_CHCN)) || (R8(x + A_CHST + 1) & R8(x + A_CHCN + 1))) {
        /* CHCN.E: DO CONTROL: SKIP UNTIL PROPER ROUTINE FOUND */
        u = R16(x + A_CHPC);
        while (R8(u) != 0) {
            u += 3;                         /* CONTROL STATUS CHECK */
            W16(x + A_CHPC, u);
        }
        chnxt(m, x);
        return;
    }
    switch (R8(x + A_CHOP) & 7) {
    case 0:                                 /* CHIF.D: IF MASK IS TRUE */
        u = R16(x + A_CHPC);
        for (;;) {
            uint16_t d = R16(u + 1);
            if (d == 0) break;              /* DEFAULT IF, ALWAYS TRUE */
            if ((d >> 8) & R8(x + A_CHST)) break;       /* HI MASK IS TRUE */
            if ((d & 0xff) & R8(x + A_CHST + 1)) break; /* LO MASK IS TRUE */
            do {                            /* SKIP TO NEXT IF */
                u += 3;
            } while (R8(u) != 0x80);        /* NON-ZERO OP: TRY AGAIN; ALL ZERO: UNTIL OP; 80: 'IF' FOUND */
        }
        W16(x + A_CHPC, u + 3);
        chnxt(m, x);
        return;
    case 1:                                 /* CHGO.D: GOTO */
        W16(x + A_CHPC, R16(R16(x + A_CHPC) + 1));     /* SET NEW PC */
        chnxt(m, x);
        return;
    case 2:                                 /* CHGS.D: GOSUB */
        u = (uint16_t)(R16(x + A_CHPC) + 3);
        W16(x + A_CHRT, u);                 /* SAVE RETURN ADDRESS */
        W16(x + A_CHPC, R16(u + 1 - 3));    /* PREVIOUS PARM IS GOSUB ADDRESS */
        chnxt(m, x);
        return;
    case 3:                                 /* CHRT.D: RETURN */
        W16(x + A_CHPC, R16(x + A_CHRT));   /* RESTORE PC */
        chnxt(m, x);
        return;
    case 4:                                 /* CHTW.D: TWIRL, DECODE */
        u = R16(x + A_CHPC);
        W8(x + A_CHTM, R8(u) >> 1);         /* PULL IN TIMER IN QUARTER SECONDS */
        W16(x + A_CHTW, R16(u + 1));        /* TWIRL AND MOVE */
        W16(x + A_CHPC, u + 3);
        W8(x + A_CHOP, R8(x + A_CHOP) + 1); /* BUMP TO EXECUTE PHASE */
        return;
    case 5:                                 /* CHTW.E: TWIRL, EXECUTE */
        W8(x + A_CHTM, R8(x + A_CHTM) - 1); /* TRICK OFF TIMER */
        if (R8(x + A_CHTM) & 0x80) chnxt(m, x);        /* IF DONE, GET NEXT OP */
        return;                             /* ELSE WAIT FOR NEXT FRAME */
    default:                                /* CHERR: UNASSIGNED */
        return;
    }
}

/* CHNXT: the next op-code; an UNTIL (op 0) only sets the control mask. */
static void chnxt(sw_machine *m, uint16_t x)
{
    for (;;) {
        uint16_t u = R16(x + A_CHPC);
        uint8_t a = R8(u);
        W8(x + A_CHOP, a);
        if (a != 0) break;
        W16(x + A_CHCN, R16(u + 1));        /* CHCN.D: DECODE CONTROL */
        W16(x + A_CHPC, u + 3);
    }
    sw_chopdo(m, x);
}

/* NWCHOR: NEW CHOREOG */
void sw_nwchor(sw_machine *m, uint16_t x)
{
    W16(x + A_CHTW, 0);                     /* NO TWIST OR MOVE */
    W8(x + A_CHTM, 0);                      /* TIME LEFT PER ROUTINE */
    W16(x + A_CHCN, 0);                     /* UNTIL CONTROL MASK */
    W16(x + A_CHST, 0);                     /* STATUS FLAGS */
    chnxt(m, x);
}

/* CPUGON */
void sw_cpugon(sw_machine *m)
{
    W8(SW_WV_LIV, R8(SW_WV_LIV) - 1);
    if ((int8_t)R8(SW_WV_LIV) <= 0) W8(SW_WV_LIV, 0);  /* NO LIVES LEFT IN CURRENT SET */
}

/* The set of waves for the current space wave (TSPWAV). */
static uint16_t wave_set(sw_machine *m)
{
    uint16_t x = (uint16_t)(SW_TSPWAV + (uint8_t)(R8(SW_SP_WAV) << 1));
    if (x >= SW_ZSPWAV) {                   /* PAST END OF TABLE? */
        if (!(R8(SW_SP_WAV) & 1)) x = SW_ZSPWAV - 4;    /* EVEN: DARTH COMES SECOND ON SELECTED ODD WAVES */
        else x = SW_ZSPWAV - 2;             /* USE LAST ENTRY IN TABLE, DARTH FIRST WITH MUSIC */
    }
    return R16(x);                          /* POINTS TO CURRENT SET OF WAVES */
}

/* NWASHP: a live ship in slot X from WV.SHP, WV.CHP, WV.BGP */
static void nwashp(sw_machine *m, uint16_t x)
{
    W8(x + A_TYP, 1);                       /* LIVE ALIEN */
    uint16_t u = R16(x + A_UVP);
    sw_unitv(m, u);
    W8(u + M_AX, 0xc0);                     /* FACE ALIEN TOWARDS PLAYER */
    W8(u + M_BY, 0xc0);
    W8(x + A_ROL, 0);
    W8(x + A_DMC, 0);
    W8(x + A_GLW, 0);
    W8(x + A_DIE, 0);
    W8(x + A_TRGT, 0);
    W8(x + A_HTA, R8(R16(SW_WV_SHP) + 1));  /* DRONES DIE ON N HITS */
    uint16_t bg = R16(SW_WV_BGP);           /* BEGINNING LOCATION */
    W16(u + M_TX, R16(bg + 0));
    W16(u + M_TY, R16(bg + 2));
    W16(u + M_TZ, R16(bg + 4));
    x = R16(SW_S1_PRM);
    W16(x + A_CHPC, R16(SW_WV_CHP));
    sw_nwchor(m, x);
    x = R16(SW_S1_PRM);
    uint8_t b = R8(R16(SW_WV_SHP));
    W8(x + A_PIC, b);                       /* CURRENT SHIP PICTURE */
    sw_adbobj(m, b);
}

/* ADASHP: one more ship into an empty slot, from the wave's table; at the
 * table's end the next level of the wave begins. */
void sw_adashp(sw_machine *m)
{
    uint16_t u = R16(SW_WV_LP);
    if (u == 0 || R8(u) == 0) {             /* RESTART IF INVALID WITHIN WAVES; ?END OF CURRENT LEVEL IN WAVE? */
        W8(SW_WV_LVL, R8(SW_WV_LVL) + 1);   /* GET NEXT LEVEL WITHIN WAVE */
        uint16_t x = wave_set(m);
        uint8_t b = R8(SW_WV_LVL);          /* INDEX INTO SET OF WAVES */
        if (b > R8(x)) b = R8(x);           /* KEEP IT WITHIN RANGE */
        x++;
        W8(SW_WV_LVL, b);                   /* KEEP IT CORRECT */
        W16(SW_WV_LP, R16(x + (int8_t)(uint8_t)(b << 1)));  /* LOOP TABLE POINTER */
    }
    uint16_t x = SW_ALIEN;
    for (;;) {
        W16(SW_S1_PRM, x);
        if (R8(x + A_TYP) == 0) break;      /* DO IT IF AN EMPTY SLOT */
        x = (uint16_t)(R16(SW_S1_PRM) + A_IZE);
        if (x >= SW_ALIENZ) return;         /* IF NO SLOTS AVAILABLE, EXIT NOW */
    }
    u = R16(SW_WV_LP);
    if (u == 0 || R8(u) == 0) return;       /* IGNORE ALIEN IF NO CHOREOG SET; ?CONSTRUCT THIS SHIP? */
    W8(SW_WV_LIV, R8(SW_WV_LIV) + 1);       /* ONE MORE LIFE IN THIS WAVE */
    W16(SW_WV_SHP, R16(u + 0));             /* SHIP SHAPE AND PARMS */
    W16(SW_WV_CHP, R16(u + 2));             /* CHOREOGRAPHY */
    W16(SW_WV_BGP, R16(u + 4));             /* BEGIN LOCATION */
    W16(SW_WV_LP, u + 6);
    nwashp(m, x);
}

/* NWNSHP: the ships of the current wave and level */
void sw_nwnshp(sw_machine *m)
{
    sw_inobj(m);                            /* INITIALIZE OBJECTS */
    uint16_t x = wave_set(m);
    uint8_t b = R8(SW_WV_LVL);              /* INDEX INTO SET OF WAVES */
    if (b > R8(x)) b = R8(x);               /* KEEP IT WITHIN RANGE */
    x++;
    W16(SW_WV_LP, R16(x + (int8_t)(uint8_t)(b << 1)));  /* LOOP TABLE POINTER */
    W8(SW_WV_LIV, 0);                       /* NO ALIENS CURRENTLY ALIVE */
    x = SW_ALIEN;
    do {
        W16(SW_S1_PRM, x);
        W8(x + A_TYP, 0);                   /* KILL ALL ALIENS FIRST */
        uint16_t u = R16(SW_WV_LP);
        if (u != 0 && R8(u) != 0) {         /* IGNORE ALIEN IF NO CHOREOG SET; ?CONSTRUCT THIS SHIP? */
            W8(SW_WV_LIV, R8(SW_WV_LIV) + 1);   /* ONE MORE LIFE IN THIS WAVE */
            W16(SW_WV_SHP, R16(u + 0));     /* SHIP SHAPE AND PARMS */
            W16(SW_WV_CHP, R16(u + 2));     /* CHOREOGRAPHY */
            W16(SW_WV_BGP, R16(u + 4));     /* BEGIN LOCATION */
            W16(SW_WV_LP, u + 6);
            nwashp(m, x);
        }
        x = (uint16_t)(R16(SW_S1_PRM) + A_IZE);
    } while (x < SW_ALIENZ);
}
