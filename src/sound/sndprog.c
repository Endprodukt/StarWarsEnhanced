/* sndprog.c - the program of the sound board, in C.
 *
 * The board's own 6809 ran SNDAUX.MAC and its parts: a main line that is
 * kicked every 4 ms by the RIOT's timer and then lets each of three
 * drivers do a step, and an interrupt that takes the requests of the main
 * board into a queue:
 *   SNDAUD  special effects: lists of (repeat, time, value, step) for the
 *           frequency and the control register of a POKEY channel
 *   SNDPM   "Rusty's POKEY Music": tunes in voices with envelopes
 *   SNDSPK  speech: sentences as lists of words, each word a run of bytes
 *           fed to the TMS5220 as it asks for them
 * What they play (the effect lists, the tunes, the speech data) stays in
 * the board's ROMs and is read from there; the chips are the models in
 * this folder. Variables keep their place in the board's RAM, so the
 * program can be checked against the original running on the 6809 core
 * (tests/test_sound.c): the same writes to the chips at the same ticks.
 *
 * Line by line after the 1983 source; comments in capitals are the
 * original's. Names and addresses: sn_symbols.h, from the program's load
 * map. */
#include "sound/sndboard.h"
#include "sound/sn_symbols.h"

#define R8(a)     sw_snd_rd(s, (uint16_t)(a))
#define W8(a, v)  sw_snd_wr(s, (uint16_t)(a), (uint8_t)(v))
#define R16(a)    ((uint16_t)((R8(a) << 8) | R8((a) + 1)))
#define W16(a, v) do { uint16_t v_ = (uint16_t)(v); W8((a), v_ >> 8); W8((a) + 1, v_); } while (0)

/* the board (SNDCMN.MAC) */
#define SOUT    0x0000      /* WRITE SOUND DATA LATCH TO BE READ BY MAIN PROCESSOR */
#define SIN     0x0800      /* READ MAIN DATA LATCH, WRITTEN BY MAIN PROCESSOR */
#define PAD     0x1080      /* 6532, PORT A, DATA */
#define PADD    0x1081
#define PBD     0x1082      /* PORT B, DATA (SPEECH DATA) */
#define PBDD    0x1083
#define RIF     0x1085      /* READ INTERRUPT FLAG, RESETS PA7 FLAG */
#define EIPED   0x1087      /* ENABLE PA7 INTERRUPTS, POSITIVE EDGE DETECTS */
#define E1024T  0x109F      /* WRITE TO TIMER 1024T, ENABLE TIMER INTERRUPT */
#define TESTBT  0x10        /* PA4: SOUND BOARD DIAGNOSTIC SWITCH, 1=OFF */
#define DPRAM   0x1000
#define DPRAMZ  0x1080
#define RAM     0x2000
#define RAMZ    0x2800
#define SPKQUE  0x2300      /* SPKRAM: 0 THRU 1F, WORD POINTERS (16 SENTENCES) */
#define P_SND   0x1800      /* the POKEYs' sound registers */
#define P_SNDZ  0x1820

#define PBX_OK  0x5a        /* WATCHDOG OK TO MAIN */
#define DG_VIB  3           /* NO VIBRATO FOR 3 DIAGNOSTICS */

/* --- SNDAUD: AUDIO SOUND GENERATOR --------------------------------------------------- */

#define AU_PNT 0            /* POINTER INTO the phrase list, CHANNEL INFO */
#define AU_TMR 2            /* TIMER COUNT TO NEXT PHRASE */
#define AU_RPT 3            /* COUNTER FOR REPEATING PHRASES */
#define AU_SND 4            /* CURRENT OUTPUT PHRASE VALUE */
#define AU_IZ  5            /* SIZE OF SET PER CHANNEL */
#define AU_EQ  16           /* # OF SETS: frequency and control of 8 channels */

#define TA_RPT 0            /* REPEAT PHRASE N TIMES */
#define TA_TMR 1            /* EACH PHRASE LASTS M BEATS */
#define TA_SND 2            /* STARTING VALUE FOR FIRST PHRASE */
#define TA_INC 3            /* MODIFIES SND FOR SUBSEQUENT PHRASES */
#define TA_IZ  4

/* AUDNEW - NEW AUDIO. Y-->POINTER TABLE: a byte of which channels, then
 * for each of them its frequency list and its control/volume list */
static void audnew(sw_sndboard *s, uint16_t y)
{
    W8(SN_AU_SHF, R8(y++));
    uint16_t x = SN_AU_BLK;
    do {
        uint8_t a = R8(SN_AU_SHF);
        W8(SN_AU_SHF, a << 1);
        if (a & 0x80) {
            W16(x + AU_PNT, R16(y)); y += 2;            /* FREQ POINTER */
            W16(x + AU_PNT + AU_IZ, R16(y)); y += 2;    /* CONTROL/VOLUME POINTER */
            W8(x + AU_RPT, 1);
            W8(x + AU_TMR, 1);
            W8(x + AU_RPT + AU_IZ, 1);
            W8(x + AU_TMR + AU_IZ, 1);
        }
        x += 2 * AU_IZ;
    } while (R8(SN_AU_SHF) != 0);
}

/* AUDDO - UPDATE AUDIO (every 8 ms) */
static void auddo(sw_sndboard *s)
{
    uint16_t x = SN_AU_BLK;                 /* AUDIO CHANNEL INFO */
    for (uint8_t b = 0; b < AU_EQ; b++, x += AU_IZ) {   /* POKEY CHANNEL OFFSET */
        uint16_t y = R16(x + AU_PNT);       /* Y-->AUDIO PHRASE TABLE */
        uint8_t a;
        if (y == 0) continue;               /* ? POINTER VALID? */
        a = (uint8_t)(R8(x + AU_TMR) - 1);  /* THEN PROCEED WITH CHANNEL */
        W8(x + AU_TMR, a);
        if (a != 0) continue;               /* ? DONE WITH CURRENT NOTE? */
        a = (uint8_t)(R8(x + AU_RPT) - 1);
        W8(x + AU_RPT, a);
        if (a == 0) {                       /* ? DONE WITH CURRENT PHRASE? */
            y += TA_IZ;                     /* THEN BUMP TO NEXT PHRASE */
            W16(x + AU_PNT, y);
            a = R8(y - TA_IZ + TA_RPT);
            if (a == 0) {                   /* ?END OF ALL PHRASES? */
                W16(x + AU_PNT, 0);         /* THEN TURN OFF AUDIO POINTER */
                W8(P_SND + b, 0);           /* TURN OFF CHANNEL */
                continue;
            }
            W8(x + AU_RPT, a);              /* ANOTHER PHRASE EXISTS: REPEAT COUNT */
            W8(x + AU_SND, R8(y - TA_IZ + TA_SND));     /* NEW CURRENT SOUND */
        } else {                            /* ELSE CONTINUE WITH PHRASE */
            W8(x + AU_SND, R8(x + AU_SND) + R8(y - TA_IZ + TA_INC));    /* INCREMENT SOUND CHANNEL VALUE */
        }
        W8(x + AU_TMR, R8(y - TA_IZ + TA_TMR));         /* RESET TIMER FOR PHRASE */
        W8(P_SND + b, R8(x + AU_SND));      /* A:=NEW CHANNEL VALUE */
    }
}

/* --- SNDSPK: SPEAK UP ---------------------------------------------------------------- */

/* WSBGNA: WRITE STROBE IS LO TRUE */
static void wsbgn(sw_sndboard *s) { W8(PAD, R8(PAD) & 0xfe); }
/* WSENDA: WRITE STROBE IS HI FALSE, HI OFF; turn off read strobe also */
static void wsend(sw_sndboard *s) { W8(PAD, R8(PAD) | 0x03); }

/* SPKWRM: WARM START THE SPEECH SYSTEM */
static void spkwrm(sw_sndboard *s)
{
    W8(SN_SP_RES, 0xff);                    /* BEGIN POWER OFF SEQUENCE */
    W8(SN_SP_QBT, R8(SN_SP_QBT) & 0x0f);    /* DON'T LOSE SOFTWARE, JUST MAKE IT REASONABLE */
    W8(SN_SP_QTP, R8(SN_SP_QTP) & 0x0f);
}

/* SPKRES: RESET THE SPEECH ROUTINE/CHIP */
static void spkres(sw_sndboard *s)
{
    W8(SN_SP_QBT, 0);                       /* COLDSTART */
    W8(SN_SP_QTP, 0);                       /* CLEAR ALL WAITING MESSAGES */
    W8(SN_SP_DOG, 0);                       /* RESET WATCHDOG ALSO */
    spkwrm(s);
}

/* SPKNEW: a sentence (D: its list of words; 0: SPECIAL FLAG FOR WARMSTART)
 * into the queue */
static void spknew(sw_sndboard *s, uint16_t d)
{
    W16(SN_SP_NXT, d);
    uint8_t b = (uint8_t)(R8(SN_SP_QTP) + 1) & 0x0f;    /* UPDATE TO NEXT QUEUE POSITION: 16 ITEMS */
    W8(SN_SP_QTP, b);
    W16(SPKQUE + 2 * b, R16(SN_SP_NXT));    /* SENTENCE FIFO QUEUE */
}

/* SPITE: IF I CAN'T BE FIRST, I WON'T BE AT ALL. */
static void spite(sw_sndboard *s, uint16_t x)
{
    if (R8(SN_SP_QTP) != R8(SN_SP_QBT)) return;     /* ?NO OTHER VOICES IN THE WAY? */
    if (R8(SN_SP_STA) != 0) return;         /* ?AND NO VOICE CURRENTLY BEING SPOKEN? */
    spknew(s, x);
}

/* SPKSKP: SKIP OVER WAITING MESSAGES */
static void spkskp(sw_sndboard *s)
{
    W8(SN_SP_QBT, R8(SN_SP_QTP));           /* POINT BOTTOM TO TOP, SKIPPING MESSAGES */
}

/* 29$: END OF WORD DELAY STARTUP */
static void word_pause(sw_sndboard *s)
{
    W8(SN_SP_STA, 1);                       /* DO DELAY */
    W8(SN_SP_DLY, 0x80);                    /* 128.*2MS = 256.MS = 1/4 SECOND PAUSE */
}

/* SPKIRQ: SPEECH DRIVER, twice every 4 ms. SP.RES: the chip is being
 * switched off and on and reset; SP.STA: 0 idle, 3 next word of the
 * sentence, 2 its bytes one by one as the chip is ready for them, 1 the
 * pause after a word. */
static void spkirq(sw_sndboard *s)
{
    uint8_t a = R8(SN_SP_RES);              /* CHECK RESET-COUNT */
    if (a != 0) {                           /* ?RESET MODE? */
        if (a & 0x80) {                     /* ?POWER OF CHIP RESET? */
            if (a & 0x40) {                 /* 0FF TO 0C0, 40*TWICE EVERY 4 MS =128 MS */
                W8(PAD, R8(PAD) | 0x23);    /* WRITE HI,READ HI,POWER OFF */
                W8(PBD, 0xff);              /* PULL DATA LINES HI ALSO */
            } else {                        /* ELSE C0 TO 7F, TIME TO TURN ON CHIP, 128 MS */
                W8(PAD, R8(PAD) & (uint8_t)~0x20);      /* POWER ON */
            }
            a = (uint8_t)(R8(SN_SP_RES) - 1);           /* STARTS AT 0FF, GOES DOWN TO 7F */
            W8(SN_SP_RES, a);
            if (!(a & 0x80))                /* ?FINISHED WITH CHIP POWER ON/OFF? */
                W8(SN_SP_RES, 0x0a * 4);    /* TAKES 10. RESET BYTES, FACTOR OF 4 SAFETY */
        } else {                            /* ELSE SENDING RESET DATA TO LIVE CHIP */
            wsend(s);                       /* END ANY PREVIOUS WRITE STROBE */
            W8(PBD, 0xff);                  /* THEN SEND A RESET */
            wsbgn(s);                       /* START A NEW WRITE STROBE */
            a = (uint8_t)(R8(SN_SP_RES) - 1);
            W8(SN_SP_RES, a);
            if (a == 0) W8(SN_SP_STA, 0);   /* ?FINISHED WITH CHIP RESET? THEN RESET SOFTWARE TOO: IDLE STATUS */
        }
        return;                             /* END OF SEND RESET */
    }

    if (R8(PAD) & 0x04) {                   /* CHECK FOR READY: ?STILL BUSY? */
        if ((R8(SN_SINTCT) & 0x03) == 0) {  /* TWICE EVERY 16 MS IS 8 MS */
            a = (uint8_t)(R8(SN_SP_DOG) + 1);
            W8(SN_SP_DOG, a);
            if (a & 0x80) spkwrm(s);        /* 128. * 8 MILLS IS ONE SECOND */
        }
        return;                             /* AT LEAST EXIT IF NOT READY */
    }
    /* HARDWARE READY */
    W8(SN_SP_DOG, 0);                       /* TURN DOG OFF FOR A WHILE */
    wsend(s);                               /* END THE WRITE STROBE(EITHER ALREADY ENDED OR WILL COMPLETE THE WRITE) */
    a = R8(SN_SP_STA);                      /* CHECK TALKSTATUS */
    if (a == 0) {                           /* ?IDLE? */
        if (R8(SN_SP_DLY) != 0) {           /* ?STILL INTER-MESSAGE DELAYING? */
            W8(SN_SP_DLY, R8(SN_SP_DLY) - 1);   /* THEN TIME IT DOWN */
            return;
        }
        uint8_t b = R8(SN_SP_QBT);          /* GET NEXT SENTENCE FROM FIFO QUEUE, IF ANY */
        if (b == R8(SN_SP_QTP)) return;     /* EXIT IF QUEUE EMPTY, ALREADY UNLOADED */
        b = (uint8_t)(b + 1) & 0x0f;        /* INDEX TO NEXT SENTENCE: 16 MAX SENTENCE QUEUE */
        W8(SN_SP_QBT, b);
        uint16_t d = R16(SPKQUE + 2 * b);   /* GET THE SENTENCE POINTER */
        W16(SN_SP_MES, d);                  /* SET IT UP */
        if (d == 0) { spkwrm(s); return; }  /* SPECIAL CODE FOR SOFTWARE WARMSTART */
        W8(SN_SP_STA, 3);                   /* AND LET 'ER RIP */
        return;
    }
    if (a == 2) {                           /* TALK STATUS 2 */
        uint16_t x = R16(SN_SP_BGN);        /* SEND NEXT SPEECH BYTE */
        W8(PBD, R8(x++));                   /* OUTPUT */
        wsbgn(s);                           /* BEGIN WRITE STROBE */
        W16(SN_SP_BGN, x);
        if (x == R16(SN_SP_END)) word_pause(s);
        return;
    }
    if (a < 2) {                            /* TALK STATUS 1 */
        a = (uint8_t)(R8(SN_SP_DLY) - 1);   /* INTERWORD DELAY */
        W8(SN_SP_DLY, a);
        if (a == 0) W8(SN_SP_STA, 3);
        return;
    }
    /* TALK STATUS 3 */
    uint16_t x = R16(SN_SP_MES);
    uint8_t b = R8(x++);                    /* GET NEXT WORD IN THE MESSAGE */
    W16(SN_SP_MES, x);                      /* UPDATE MESSAGE POINTER */
    if (b == 0xfe) { word_pause(s); return; }   /* FE=1/4 SECOND PAUSE */
    if (b == 0xff) {                        /* FF=END OF MESSAGE */
        (void)R8(SN_SP_STA);
        W8(SN_SP_STA, 0);                   /* NO MORE WORDS -- IDLE LOOP WHILE WAITING FOR MORE */
        W8(SN_SP_DLY, 0xff);                /* LONG INTER-MESSAGE DELAY */
        return;
    }
    x = (uint16_t)(SN_SPKVTB + 4 * b);      /* GET BASE VOICE TABLE, ADD IN INDEX*4 */
    if (x >= SN_SPKVTZ) { spkres(s); return; }  /* RESET SPEECH ROUTINE/CHIP IF BAD ADDRESSES FOUND */
    W16(SN_SP_BGN, R16(x));                 /* GET STARTING AND ENDING ADDRESSES */
    W16(SN_SP_END, R16(x + 2));
    W8(PBD, 0x60);                          /* GENERATE SPEAK EXTERNAL COMMAND */
    wsbgn(s);
    W8(SN_SP_STA, R8(SN_SP_STA) - 1);
}

/* --- SNDPM: the music (not ported yet) ----------------------------------------------- */

static void isnd(sw_sndboard *s) { (void)s; }
static void pkdr(sw_sndboard *s) { (void)s; }
static void tune(sw_sndboard *s, uint16_t pkcut, uint8_t b) { (void)s; (void)pkcut; (void)b; }

/* --- SNDAUX: the main line ----------------------------------------------------------- */

void sw_sndprog_init(sw_sndboard *s);

/* A request of the main board. The table PBX has the routine for each;
 * they are stubs that load a pointer and go on to one of the routines
 * above: what is read here is that pointer.
 *   LDY #list, AUDNEW        an effect
 *   LDD #words, SPKNEW       a sentence (0: warm start of the speech chip)
 *   LDX #words, SPITE        a sentence that is dropped if another is on
 *   JSR PKCUTn, LDB #2*tune, JSR NWTUNE (four times), RTS    a piece of music */
static void request(sw_sndboard *s, uint8_t b)
{
    uint16_t r = R16(SN_PBX + 2 * b);
    if (r == SN_RESET) { sw_sndprog_init(s); return; }
    if (r == SN_SPKSKP) { spkskp(s); return; }
    uint8_t op = R8(r);
    if (op == 0x10 && R8(r + 1) == 0x8e) {          /* LDY # */
        audnew(s, R16(r + 2));
    } else if (op == 0xcc) {                        /* LDD # */
        spknew(s, R16(r + 1));
    } else if (op == 0x8e) {                        /* LDX # */
        spite(s, R16(r + 1));
    } else {
        while (R8(r) == 0xbd && R8(r + 3) == 0xc6) {    /* JSR PKCUTn, LDB # */
            tune(s, R16(r + 1), R8(r + 4));
            r += 8;
        }
    }
}

/* DOCMD: DO ANY NEW COMMANDS */
static void docmd(sw_sndboard *s)
{
    uint8_t b = R8(SN_CMDBTM);
    if (b == R8(SN_CMDTOP)) return;         /* ?SOME NEW COMMANDS IN BUFFER? */
    b = (uint8_t)(b + 1) & 0x1f;            /* HANDLE WRAPAROUND */
    W8(SN_CMDBTM, b);                       /* JUST HANDLE ONE NEW REQUEST PER IRQ */
    b = R8(SN_CMD + b);                     /* GOT OUR COMMAND */
    if (b == 0) { sw_sndprog_init(s); return; }     /* START OVER IF ZERO COMMAND FOUND */
    if (b <= SN_PBXSEQ) request(s, b);      /* MAX NUMBER ALLOWED VIA PBX: DROP ON FLOOR IF NOT */
}

/* RIRQ, the part for the main board: a byte waits in its latch. */
void sw_sndprog_command(sw_sndboard *s)
{
    uint8_t a = R8(SIN);                    /* GET THE COMMAND */
    if (a == 0) { sw_sndprog_init(s); return; }     /* SUPERCOMMAND ZERO SAYS RESET RIGHT NOW */
    uint8_t b = (uint8_t)(R8(SN_CMDTOP) + 1) & 0x1f;    /* ROOM FOR PRECISELY 32. */
    W8(SN_CMD + b, a);
    W8(SN_CMDTOP, b);
}

/* MAIN: what the main line does when the timer has kicked it (every 4 ms) */
void sw_sndprog_tick(sw_sndboard *s)
{
    W8(E1024T, 0x06);                       /* (RIRQ) MAKE INTO 4 MS: START UP THE TIMER AGAIN */

    W8(SOUT, PBX_OK);                       /* THIS IS THE WATCHDOG VALUE: MAIN PCB CHECKS FOR THIS */

    uint8_t a = (uint8_t)(R8(SN_SINTCT) + 1);   /* COUNTER FOR EVERY 4 MILLS */
    W8(SN_SINTCT, a);
    if (a == 0) {
        W8(SN_SINTHI, R8(SN_SINTHI) + 1);   /* SECONDS COUNTER */
        uint8_t b = R8(SN_DG_SEQ);
        /* TRY EVERY 4 SECONDS; NOT DOING TEST TONES ANYMORE: EVERY 2 SECONDS INSTEAD */
        a = b >= DG_VIB ? 1 : 3;
        if ((a & R8(SN_SINTHI)) == 0) {
            W8(SN_TESTON, 0);
            if ((R8(PAD) & TESTBT) == 0) {  /* ?TEST SWITCH ON?(LO TRUE) */
                W8(SN_TESTON, R8(SN_TESTON) + 1);
                b++;
                if (b >= SN_PBXSEQ) b = 1;  /* ?PAST TOP ENTRY? */
                W8(SN_DG_SEQ, b);           /* SAVE CURRENT DIAG SEQUENCE */
                request(s, b);              /* SHOW OFF NEXT PBX ROUTINE */
            }
        }
    }

    spkirq(s);                              /* START SPEECH AT FRONT */
    if (s->m->sound_out_full) sw_sndprog_command(s);    /* (the interrupt comes whenever a byte does) */
    docmd(s);                               /* DO ANY NEW COMMANDS */

    /* SET FM DUE TO CURRENT SOUND OUTPUT */
    uint8_t b = R8(PAD);                    /* B:= ORIGINAL OUTPUT VALUE */
    int fm = 1;
    if ((R8(SN_SP_DLY) | R8(SN_SP_STA)) != 0) fm = 0;   /* DELAY FOR PAUSES IN VOICE, OR ACTIVELY SPEAKING */
    else if (R8(SN_VIU) != 0) fm = 0;       /* ANY MUSIC IN USE */
    else if (R8(SN_TESTON) != 0 && R8(SN_DG_SEQ) <= DG_VIB) fm = 0;
    /* R2 SOUNDS ON CHANNEL 5 */
    else if (R16(SN_AU_BLK + ((5 - 1) * 2 + 1) * AU_IZ + AU_PNT) >= SN_VIBOFF) fm = 0;
    if (fm) b &= (uint8_t)~0x08;            /* FM NOT(TURN FM ON) */
    else b |= 0x08;                         /* HI FALSE */
    W8(PAD, b);                             /* SHOVE NEW FM OUT */

    if ((R8(SN_SINTCT) & 1) == 0) auddo(s); /* ?8 MILL BOUNDARY? THEN AUDIO SPECIAL EFFECTS */
    pkdr(s);                                /* DONT FORGET THE MUSIC (OK, THEN JUST HUM) */
    spkirq(s);                              /* THEN END WITH SPEECH, TWICE IN 4 MS */
}

/* INIT: RESET INITIALISER */
void sw_sndprog_init(sw_sndboard *s)
{
    uint16_t x;
    (void)R8(SIN);                          /* MAIN BOARD DIAG, CLEAR INPUT */
    W8(PBDD, 0xff);                         /* SET UP PBD AS OUTPUTS */
    W8(PBD, 0xff);                          /* ALL ONES IS THE SAFEST */
    W8(PAD, 0xff);                          /* MAKE SURE CONTROL STROBES ARE HIGH */
    W8(PADD, 0x2b);                         /* SET UP PAD */
    for (x = P_SND; x < P_SNDZ; x++) W8(x, 0);      /* INIT POKEYS */
    W8(SOUT, R8(SIN));                      /* MAIN BOARD DIAG -- LOOP BACK DATA */
    for (x = RAM; x < RAMZ; x++) W8(x, 0);  /* CLEAR RAM */
    for (x = DPRAM; x < DPRAMZ; x++) W8(x, 0);
    /* BEGIN POKEY INIT-ALLOW TIME TO SETTLE. */
    W8(0x1827, 0); W8(0x182f, 0); W8(0x1837, 0); W8(0x183f, 0);     /* P.BGN1 to 4 */
    W8(0x1820, 0); W8(0x1828, 0); W8(0x1830, 0); W8(0x1838, 0);     /* P.ACT1 to 4 */
    W8(0x1827, 7); W8(0x182f, 7); W8(0x1837, 7); W8(0x183f, 7);     /* FAST SCAN: FINISH POKEY INIT */
    /* (TEST SWITCH ON: the board's self test, not here) */
    W8(PAD, R8(PAD) & 0xf7);                /* TURN FM ON */
    W8(SN_SGARBG, 0x3f);                    /* TRAP OUT FIRST BYTE RAM */
    isnd(s);                                /* PM MUSIC INITIALIZOR */
    spkres(s);                              /* RESET SPEECH CHIP/ROUTINE */
    (void)R8(RIF);                          /* MAKE SURE PA7 FLAG IS RESET */
    (void)R8(SIN);                          /* AND THEN CLEAR MAINFLAG SO MAIN PCB CAN TALK AGAIN */
    W8(SOUT, PBX_OK);                       /* TELL CURIOUS MAIN PCB THAT WE ARE GOING AGAIN */
    W8(E1024T, 0x06);                       /* SET UP 4 MS IRQ TIMER */
    W8(EIPED, 0x06);                        /* ENABLE PA7 INTERRUPTS WITH POSITIVE EDGE DETECTS */
}
