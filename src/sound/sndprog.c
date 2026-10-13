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

/* --- SNDPM: (RUSTY'S POKEY MUSIC) DRIVER, 6809 VERSION ------------------------------- */

/* A tune is a list of two bytes each: a note (0: a rest; else an index
 * into the table of pitches, shifted by the voice's key) and its length
 * (bit 0: tied to the note before), or a function (bit 7 set) and its
 * value. The length 0 ends it. A voice runs through its tune at its own
 * rate, and while a note sounds an amplitude and a frequency envelope
 * shape it. Four voices are 16 bit POKEY channel pairs; voices 5 to 8
 * share voice 4's pair and are heard only when the ones before them are
 * silent ("priority"). Assembled with ..SV3=0, ..SV4=0, ..PRI=5, ..DRE=1,
 * ..SYM=1, ..FRE=1, ..AME=1, ..SCK=0, ..CNT=0. */

/* a voice's RAM */
#define OPTR   0            /* POINTER TO START OF CURRENT TUNE: VOICE IN USE IF HI BYTE NON-ZERO */
#define APTR   2            /* POINTER TO START OF CURRENT AMP. ENVELOPE */
#define FPTR   4            /* POINTER TO START OF CURRENT FREQ. ENVELOPE */
#define ONOTE  6            /* CURRENT OUTPUT NOTE COUNT */
#define ODUR   8            /* CURRENT OUTPUT DURATION REMAINING */
#define VVOL   10           /* MEDIAN VOICE VOLUME */
#define VSEQ   11           /* VOICE DURATION SEQUENCE # */
#define ORATE  12           /* NEG OF CURRENT OUTPUT RATE (BIGGER # = SHORTER DUR.) */
#define OKEY   13           /* CURRENT KEY OFFSET */
#define VAC    14           /* VOICE AMPLITUDE CONTROL BITS */
#define VSF    15           /* VOICE SYNTH. MODE FLAG */
#define VSA    16           /* VOICE SYNTH. MODE FREQ. ADJUST */
#define OPTSV  18           /* OUTPUT POINTER SUB-PHRASE SAVE */
#define OPTLL  20           /* OUTPUT POINTER LOOP SAVE */
#define VLC    22           /* LOOP COUNTER */
#define OPTRET 23           /* RETURN ADDRESS FROM GOSUB */

/* a voice's descriptor (TVC) */
#define TVC    0x74DB       /* TABLE OF VOICE PARAMETERS */
#define VC_RAM 0            /* POINTER TO VOICE'S RAM */
#define VC_BIT 2            /* BIT MASK FOR THIS VOICE */
#define VC_PKF 3            /* BASE POINTER INTO HARDWARE POKEY */
#define VC_PRI 5            /* PRIORITY MASK */
#define VC_PKD 6            /* WHEN DYNAMICALLY RECONFIGURED, USE THIS POKEY ADDRESS INSTEAD */
#define VC_IZE 8

/* local tables (addresses from the program ROM) */
#define TUNTAB 0x58E5       /* the directory of tunes and sub-phrases */
#define NOTTAB 0x7953       /* RPM NOTE TABLE (FOR 1.512 MHZ CLOCK) */
#define FETAB  0x7A17       /* the frequency envelopes */
#define AETAB  0x7A1B       /* the amplitude envelopes */

#define PKC1   0x1830       /* the control registers of the music's two POKEYs */
#define PKC2   0x1838
#define PKI1   0x1837       /* and their SKCTL */
#define PKI2   0x183F

/* POKVIY: INIT A POKEY VOICE. Y:=POINTER TO THIS VOICE'S DESCRIPTOR TABLE */
static void pokviy(sw_sndboard *s, uint16_t y)
{
    uint16_t u = R16(y + VC_RAM);           /* U-->VOICE RAM BLOCK */
    W8(SN_VIU, (uint8_t)~R8(y + VC_BIT) & R8(SN_VIU));     /* TURN OFF VIU BIT */

    W8(u + VVOL, 7);                        /* SET UP TO MEDIAN VOLUME */
    W8(u + ORATE, (uint8_t)-64);            /* REMEMBER TO PRE-NEGATE: SET UP TO MED. RATE */
    W8(u + VAC, 0xa0);                      /* DEFAULT TO PURE TONES */

    W16(u + OPTR, 0);                       /* CLEAR VOICE IN USE FLAG(HI BYTE POINTER) */
    W8(u + OKEY, 0);                        /* SET BACK TO UNUSED KEY */
    W8(u + VSEQ, 0);                        /* CLEAR ENV. SEQ. COUNT */
    W16(u + ODUR, 0);
    W8(u + VLC, 0);                         /* CLEAR LOOP COUNT */
    W16(u + OPTSV, 0);                      /* CLEAR SUB-PHRASE POINTER (FLAG) */
    W8(u + VSF, 0);                         /* CLEAR SYNTH. MODE */
    W16(u + VSA, 0);                        /* CLEAR SYNTH. MODE ADJUSTMENT */

    uint16_t d = R16(FETAB);                /* the null envelope */
    W16(u + APTR, d);
    W16(u + FPTR, d);

    /* TURN OFF POKEY AMPLITUDE */
    uint16_t x = R16(y + VC_PKF);           /* SELECT THE POKEY OUTPUT PORT */
    if (R8(SN_DREF) != 0) {                 /* ?DYNAMICALLY RECONFIG'D? */
        x = R16(y + VC_PKD);                /* THEN USE DRE PORT INSTEAD */
        (void)R8(x + 1);
        W8(x + 1, 0);                       /* ONLY DO LO 8 BITS */
        return;
    }
    (void)R8(x + 3);
    W8(x + 3, 0);                           /* AMPLITUDE GOES IN SECOND CHANNEL FOR 16 BIT */
}

/* The .TUNE macro: JSR PKCUTn (CLEAR OUT AND INIT THIS VOICE), LDB #2*tune,
 * JSR NWTUNE. PKCUTn is LDA #VC$n, BRA POKVI. */
static void tune(sw_sndboard *s, uint16_t pkcut, uint8_t b)
{
    uint16_t y = (uint16_t)(TVC + R8(pkcut + 1));   /* Y-->VOICE DESCRIPTOR TABLE */
    pokviy(s, y);
    /* NWTUNE: SETS UP NEW TUNE INTO GIVEN VOICE. B:= TUNE # WORD OFFSET INTO TUNE TABLE */
    uint16_t d = R16(TUNTAB + b);           /* POINTER TO TUNE */
    W16(R16(y + VC_RAM) + OPTR, d);
    W8(SN_VIU, R8(SN_VIU) | R8(y + VC_BIT));    /* SET VOICE IN USE BIT */
}

/* ISND: INITIALIZE POKEY FOR USE */
static void isnd(sw_sndboard *s)
{
    W8(PKI1, 0);                            /* ALSO, INIT POKEY DEVICES */
    W8(PKI2, 0);
    W8(SN_DREF, 0);                         /* CLEAR DYN. RECON. FLAG */
    for (int v = 0; v < 8; v++)             /* INIT ALL DEFINED VOICES */
        pokviy(s, (uint16_t)(TVC + v * VC_IZE));
    W8(PKI1, 3);                            /* OUTPUT SECOND BYTE TO POKEY INIT. */
    W8(PKI2, 3);
    W8(PKC1, 0x78);                         /* VOICES 3 AND 4 ARE 16 BIT EACH */
    W8(PKC2, 0x78);                         /* TIE CHANNEL 2 TO 1, 4 TO 3 */
}

/* POKSN: HAVE SAME NOTE AS LAST TIME, PLAY IT WITH ENVELOPES */
static void poksn(sw_sndboard *s, uint16_t y, uint16_t u)
{
    /* IF AT HIGHEST PRIORITY, PLAY NOTE; ELSE IF RECONFIGURED, THEN ALL ARE
     * AT BEST PRIORITY; ELSE, RETURN WITHOUT POKEY UPDATE */
    if ((R8(SN_VIU) & R8(y + VC_PRI)) != 0 && R8(SN_DREF) == 0) return;

    if ((R8(SN_SINTCT) & 0x06) == 0) {      /* ?TIME TO CHANGE ADJUST? */
        /* DIVIDE SYNTH ADJUST BY 2 SO ADD 1/2 AS MUCH EACH TIME */
        W16(u + VSA, (uint16_t)((int16_t)R16(u + VSA) >> 1));
    }
    /* FIRST ADD FREQ. ENV. TO NOTE FREQ. */
    uint16_t x = (uint16_t)(R16(u + FPTR) + (R8(u + VSEQ) >> 1));   /* FOR 128 BYTE TABLE */
    uint16_t d = (uint16_t)(int16_t)(int8_t)R8(x);  /* GET CURRENT FREQ. ENV. SETTING */
    if (R8(SN_DREF) == 0) d = (uint16_t)(d << 3);   /* MUSIC FREQ IS *8, SOUNDS ARE AS IS. */
    d = (uint16_t)(d + R16(u + ONOTE));     /* ADD IN THE NOTES FREQUENCY */
    d = (uint16_t)(d + R16(u + VSA));       /* ADD SYNTH ADJUST TO SUM OF NOTE FREQ. AND ENV. */

    x = R16(y + VC_PKF);                    /* SELECT THE POKEY OUTPUT PORT */
    if (R8(SN_DREF) != 0) x = R16(y + VC_PKD);  /* THEN USE DRE PORT INSTEAD AND ONLY DO LO 8 BITS */
    else W8(x + 2, d >> 8);                 /* OUTPUT MSB OF 16 BIT */
    W8(x + 0, d);                           /* OUTPUT LO 8 BIT */

    /* CHECK FOR A REST(NO OUTPUT): MASK TO ZERO OUT AMPLITUDE, or TO PERMIT
     * CURRENT VOLUME */
    W8(SN_FASV, R16(u + ONOTE) != 0 ? 0xff : 0xf0);

    uint8_t b = R8(u + VSEQ);               /* USE STRAIGHT, LIMIT AT 31(QUARTER SECOND) */
    if (b >= 31) b = 31;
    uint8_t a = (uint8_t)(R8(R16(u + APTR) + b) + R8(u + VVOL));    /* GET CURRENT VOLUME ENV. ADJ. */
    if (a & 0x80) a = 0;                    /* IF NUM .LT. 0, SET VOLUME TO OFF */
    if (a >= 0x10) a = 0x0f;                /* CHECK FOR VOLUME TOO LARGE: SET TO MAX. */
    a |= R8(u + VAC);                       /* SET UP CURRENT VOICE CONTROL */
    a &= R8(SN_FASV);                       /* MASK WITH NOTE ON/OFF SETTING */
    x = R16(y + VC_PKF);
    if (R8(SN_DREF) != 0) {
        W8(R16(y + VC_PKD) + 1, a);         /* ONLY DO LO 8 BITS */
        return;
    }
    W8(x + 3, a);                           /* AMPLITUDE GOES IN SECOND CHANNEL FOR 16 BIT */
}

/* PKFUN: HAVE A FUNCTION, NOT A NOTE. B:= OPCODE, A:= DATA VALUE */
static void pkfun(sw_sndboard *s, uint16_t u, uint8_t b, uint8_t a)
{
    switch (b & 0x7f) {
    case 1: a = (uint8_t)(a - R8(u + ORATE));   /* PKCRAT: CHANGE RATE, ADD NEW(POSITIVE) TO OLD(NEGATIVE) */
        /* fall through */
    case 0: W8(u + ORATE, (uint8_t)-a); break;  /* PKARAT: FORM NEGATIVE; NEW RATE */
    case 3: a = (uint8_t)(a + R8(u + VVOL));    /* PKCAMP: CHANGE AMPLITUDE, ADD NEW TO OLD */
        /* fall through */
    case 2: W8(u + VVOL, a); break;             /* PKAAMP: NEW MEDIAN AMPLITUDE */
    case 5: a = (uint8_t)(a + R8(u + OKEY));    /* PKCKEY: CHANGE KEY, ADD NEW TO OLD */
        /* fall through */
    case 4: W8(u + OKEY, a); break;             /* PKAKEY: NEW KEY OFFSET */
    case 6: W16(u + FPTR, R16(FETAB + (uint8_t)(a << 1))); break;   /* PKFRQE: A FREQ. ENVELOPE */
    case 7: W16(u + APTR, R16(AETAB + (uint8_t)(a << 1))); break;   /* PKAMPE: AN AMPLITUDE ENVELOPE */
    case 10: W8(u + VAC, a); break;             /* PKCON: NEW VOICE CONTROL BITS */
    case 11: W8(PKC1, a); break;                /* PKPKC: NEW POKEY CONTROL BITS (FOR POKEY 1) */
    case 12: W8(u + VSF, a); break;             /* PKSYN: SET OR RESET SYNTH MODE */
    case 13:                                    /* PKCPH: CALL A SUB-PHRASE */
        W16(u + OPTSV, R16(u + OPTR));          /* SAVE CURRENT PC */
        W16(u + OPTR, R16(TUNTAB + (uint8_t)(a << 1)));     /* GET NEW SUBTUNE */
        break;
    case 14:                                    /* PKSL: START A PHRASE LOOP */
        W8(u + VLC, a);                         /* SAVE LOOP COUNT FOR THIS PHRASE */
        W16(u + OPTLL, R16(u + OPTR));          /* GET CURRENT PC AND SAVE IT */
        break;
    case 15:                                    /* PKEL: END A PHRASE LOOP */
        a = (uint8_t)(R8(u + VLC) - 1);         /* CHECK FOR DONE WITH LOOP */
        W8(u + VLC, a);
        if (a != 0) W16(u + OPTR, R16(u + OPTLL));  /* NOT DONE YET: REINSTATE ADDRESSES OF LOOP START */
        break;
    case 16: {                                  /* PKGOSB */
        uint16_t x = (uint16_t)(R16(u + OPTR) + 1); /* BUMP PAST THE 3 BYTE INSTRUCTION */
        W16(u + OPTRET, x);                     /* SAVE IT IN RETURN POINTER */
        W16(u + OPTR, R16(x - 2));              /* GET CURRENT ADDRESS TO JUMP TO (SUBPHRASE) */
        break;
    }
    case 17: W16(u + OPTR, R16(u + OPTRET)); break;     /* PKRET */
    default: break;                             /* (sync check: not assembled) NULL OUT IF NOT IN RANGE */
    }
}

/* POKOUT: COMMON OUTPUT ROUTINE FOR ALL POKEY VOICES. A:= OFFSET INTO
 * CURRENT VOICE DESCRIPTOR TABLE */
static void pokout(sw_sndboard *s, uint8_t voice)
{
    uint16_t y = (uint16_t)(TVC + voice);   /* Y--> THIS VOICE'S DESCRIPTOR TABLE */
    uint16_t u = R16(y + VC_RAM);           /* U--> RAM BLOCK FOR THIS VOICE */
    if (R8(u + OPTR) == 0) return;          /* ?VOICE IN USE? (INUSE IF HI POINTER SET) */

    /* SEE IF TIME TO UPDATE CURRENT NOTE */
    uint8_t a = (uint8_t)(R8(u + VSEQ) + 1);    /* INC. ENVELOPE SEQ. COUNT */
    W8(u + VSEQ, a);
    if (a == 0) W8(u + VSEQ, 0xff);         /* KEEP AT FINAL ENV. COUNT */
    /* BY ADDING NEGATIVE OF CURRENT RATE, UPDATE CURRENT NOTE DURATION */
    uint16_t d = (uint16_t)((0xff00 | R8(u + ORATE)) + R16(u + ODUR));
    W16(u + ODUR, d);
    if (!(d & 0x8000)) { poksn(s, y, u); return; }  /* STILL SAME NOTE */

    for (;;) {                              /* POKNL: HAVE A NEW NOTE TO PLAY */
        uint16_t x = R16(u + OPTR);         /* GET THIS NOTE IN TUNE */
        uint8_t b = R8(x);
        x += 2;                             /* BUMP TO NEXT INSTRUCTION */
        W16(u + OPTR, x);
        if (b & 0x80) {                     /* A FUNCTION */
            if ((uint8_t)(b << 1) < 2 * 18) pkfun(s, u, b, R8(x - 1));
            continue;
        }
        if (b != 0) b = (uint8_t)(b + R8(u + OKEY));    /* ?SOUNDFUL NOTE?(NOT A REST) THEN POSITION NOTE TO CURRENT KEY */
        if (R8(SN_DREF) != 0) {
            d = b;                          /* ELSE USE 8 BIT VALUE DIRECTLY */
        } else {                            /* ELSE IS 16 BIT -- CALC MUSIC NOTE */
            x = (uint16_t)(NOTTAB + (uint8_t)(b << 1));     /* BASE OF NOTE CONVERSION TABLE */
            W16(u + VSA, R16(u + ONOTE) - R16(x));  /* COMPUTE DELTA BETWEEN NEW AND OLD: FOR SYNTH MODE */
            d = R16(x);                     /* GET NOTE VALUE */
        }
        W16(u + ONOTE, d);                  /* STORE AS OUTPUT NOTE */

        x = R16(u + OPTR);                  /* GET DURATION OF CURRENT NOTE */
        a = R8(x - 1);                      /* (WE PREVIOUSLY BUMPED POINTER TO NEXT NOTE) */
        if (a == 0) {                       /* ?AT FINAL NOTE? */
            d = R16(u + OPTSV);             /* CHECK FOR ENDING SUB-PHRASE OR MAIN TUNE */
            if (d == 0) { pokviy(s, y); return; }   /* END OF MAIN TUNE, INIT THIS VOICE */
            W16(u + OPTR, d);               /* IF HERE, RETURNING FROM SUB-PHRASE */
            W16(u + OPTSV, 0);              /* RESET SUB-PHRASE FLAG */
            continue;                       /* GET A NOTE GOING */
        }
        /* STILL HAVE A NEW NOTE, GET AND STORE DURATION: SHIFT AND REMOVE LSB
         * (TIED NOTE FLAG), MULT. DUR * 128 */
        d = (uint16_t)(((a >> 2) << 8) | ((a & 0x02) ? 0x80 : 0x00));
        d = (uint16_t)(d + R16(u + ODUR));  /* SUBTRACT ANY OVERRUN FROM PREV. NOTE */
        W16(u + ODUR, d);

        if ((R8(x - 1) & 0x01) == 0) {      /* ?NOTE NOT TIED? */
            W8(u + VSEQ, 0);                /* CLEAR SEQ. COUNTER FOR NOT TIED NOTE */
            W16(u + VSA, 0);                /* AND NO SYNTH ADJUST FOR NOT TIED */
            /* TEST IF THIS IS NOT HIGH PRIORITY: LET AMPLITUDE ENVELOPE DO
             * PRE-GAP; ELSE, DON'T UPDATE POKEY WITH THIS VOICE */
            if ((R8(SN_VIU) & R8(y + VC_PRI)) != 0 && R8(SN_DREF) == 0) return;
        } else if (R8(u + VSF) == 0) {      /* NOTE IS TIED, TEST FOR SYNTH MODE ON */
            W16(u + VSA, 0);                /* NOT ON: CLEAR FREQ. ADJUSTMENT */
        }
        poksn(s, y, u);
        return;
    }
}

/* PKDR: POKEY 4MS DRIVER: half of the voices each time */
static void pkdr(sw_sndboard *s)
{
    for (int v = (R8(SN_SINTCT) & 1) ? 0 : 1; v < 8; v += 2)
        pokout(s, (uint8_t)(v * VC_IZE));
}

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
    /* THEN END WITH SPEECH, TWICE IN 4 MS: the 6809 got there a while
     * after the first time, and the speech chip has gone on meanwhile
     * (sw_sndprog_late). */
    s->late = s->late_cycles;
}

/* The end of the main line's pass. */
void sw_sndprog_late(sw_sndboard *s)
{
    spkirq(s);
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
