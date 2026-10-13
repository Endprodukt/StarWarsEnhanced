/* coin.c - COINS (WSCOIN.MAC) and MOOLAH, Atari's coin routine (COIN69.MAC,
 * Greg Rivera), as Star Wars assembles it:
 *
 *   COIN=0 coin switches low true, COIN01=1 coins in D0 (right), D1
 *   (middle: the L coin input), D2 (left: the AUX input) of $COINA,
 *   SLAM=0 slam low true, CNTINT=0 ($INTCT counted by the IRQ), OFFSET=1,
 *   PRST=30, POST=30, coin mode from the EEROM options (OPTS0).
 *
 * A coin counts when its switch is closed for 16..200 ms and open for 33 ms
 * before and after; then it waits POST frames for a slam. All timers run
 * at the IRQ rate, as on the board. */
#include "game/game.h"

#define PRST 30
#define POST 30

/* Twice the unit coins needed for a bonus coin, per bonus-adder mode. */
static const uint8_t MODLO[8] = { 0xff, 4, 8, 8, 10, 0xff, 0xff, 0xff };

static void moolah(sw_machine *m);
static void em_counters(sw_machine *m);

void sw_coins(sw_machine *m)
{
    uint8_t *mootim = sw_ram(m, SW_MOOTIM);
    uint8_t *crdt = sw_ram(m, SW_SSCRDT);
    uint8_t *cctim = sw_ram(m, SW_SCCTIM);

    if ((sw_rd8(m, SW_SCOINA) & 0x0f) != 0x0f)   /* coin or slam switch activity */
        *mootim = 0xff;                          /* give 'em a couple of seconds */
    if (*mootim == 0) return;                    /* timer ran out, no MOOLAH */
    (*mootim)--;

    if (*crdt > SW_MAXCRD) *crdt = SW_MAXCRD;    /* verify proper credit range */

    /* Electro-mechanical counters: right on EMCNT0, left on EMCNT1. */
    sw_wr8(m, SW_EMCNT0, cctim[2]);
    sw_wr8(m, SW_EMCNT1, cctim[1]);
    if (cctim[2] | cctim[1] | cctim[0]) *mootim = 0xff;

    if ((sw_rd8(m, SW_TESTSW) & SW_TESTBT) == 0) {   /* self test: no coins, */
        em_counters(m);                              /* keep the counters timing */
        return;
    }

    /* $CMODE from the EEROM options: OPTS0 high nibble, OPTS0+1 low nibble. */
    RAM8(m, SW_SCMODE) = (uint8_t)((sw_rd8(m, SW_OPTS0) << 4) | (sw_rd8(m, SW_OPTS0 + 1) & 0x0f));

    if ((RAM8(m, SW_SINTCT) & 3) == 0) sw_eeaccu(m);  /* coin accounting, every 16 ms */

    moolah(m);
}

static void moolah(sw_machine *m)
{
    uint8_t *cnstt = sw_ram(m, SW_SCNSTT);
    uint8_t *pstsl = sw_ram(m, SW_SPSTSL);
    uint8_t *lmtim = sw_ram(m, SW_SLMTIM);
    uint8_t *cnct = sw_ram(m, SW_SCNCT);
    uint8_t *bccnt = sw_ram(m, SW_SBCCNT);
    uint8_t *crdt = sw_ram(m, SW_SSCRDT);
    uint8_t *cctim = sw_ram(m, SW_SCCTIM);
    uint8_t cmode = RAM8(m, SW_SCMODE);
    uint8_t intct = RAM8(m, SW_SINTCT);

    /* --- detect coins, right (2) to left (0) mech --- */
    for (int x = 2; x >= 0; x--) {
        /* COIN01: right is D0, middle D1, left D2; low true. */
        int absent = (sw_rd8(m, SW_SCOINA) >> (2 - x)) & 1;
        uint8_t a = cnstt[x] & 31;
        int mult = 0;

        if (!absent) {
            if (a != 0) {                       /* stick at 0 (terminal count) */
                if (a >= 27) a--;               /* first five samples: fast */
                else if ((intct & 1) == 0) a--; /* then every other interrupt */
            }
        } else if (a >= 27) {                   /* not valid yet: too short */
            a = 31;
        } else {
            unsigned up = (unsigned)cnstt[x] + 32;     /* bump coin-off up-counter */
            if (up < 0x100) {
                a = (uint8_t)up;                /* no wrap: just store status */
            } else if ((up & 0xff) == 0) {
                a = 31;                         /* wrapped, but on too long: reset */
            } else {
                /* Valid coin: wait for the post-coin slam time. */
                cnstt[x] = 31;
                uint8_t waiting = pstsl[x];
                pstsl[x] = POST * 4;
                if (waiting == 0) continue;
                mult = 1;                       /* a previous coin becomes valid early */
            }
        }

        if (!mult) {
            cnstt[x] = a;
            if ((sw_rd8(m, SW_SLAM) & SW_SLMBIT) == 0)  /* slam (low true) */
                *lmtim = PRST * 8;
            if (*lmtim) {
                (*lmtim)--;
                cnstt[x] = 0;
                pstsl[x] = 0;
            }
            if (pstsl[x] == 0) continue;
            if (--pstsl[x] != 0) continue;      /* becomes zero: a coin */
        }

        /* --- mech multipliers --- */
        uint8_t units = 0;
        if (x == 1) {
            if (cmode & 0x10) units = 1;        /* middle: half multiplier */
        } else if (x == 2) {
            uint8_t gdm = (cmode & 0x0c) >> 2;  /* right: 1, 4, 5, 6 units */
            if (gdm) units = (uint8_t)(gdm + 2);
        }
        units++;
        *bccnt = (uint8_t)(*bccnt + units);
        *cnct = (uint8_t)(*cnct + units);
        cctim[x]++;                              /* queue a pulse for the E.M. counter */
    }

    /* --- bonus adder --- */
    uint8_t mode = (uint8_t)(cmode >> 5);
    uint8_t b2 = (uint8_t)(*bccnt << 1);
    if (b2 >= MODLO[mode]) {
        uint8_t b = (uint8_t)(b2 - MODLO[mode]);
        int carry = (*bccnt & 0x80) != 0;       /* LSL $BCCNT: pull in half-credit marker */
        *bccnt = (uint8_t)(*bccnt << 1);
        b = (uint8_t)((carry ? 0x80 : 0) | (b >> 1));   /* RORB */
        int pair = 1;
        if (mode != 3) {                         /* mode 3: 2 bonus for 4, no odd ones */
            unsigned s = (unsigned)b + 0x80;     /* other modes: add odd bonus */
            b = (uint8_t)s;
            pair = s >= 0x100;                   /* two odds make a pair */
        }
        if (pair) *cnct = (uint8_t)(*cnct + 2);
        *bccnt = b;
    }

    /* --- coins to credits --- */
    uint8_t cm = cmode & 3;
    if (cm == 0) {
        *cnct = 0;                               /* free play: keep coin count clear */
    } else {
        int8_t price = (int8_t)((int8_t)-cm >> 1);   /* -1, -1, -2 unit coins */
        uint8_t a = (uint8_t)(price + *cnct);
        int enough = !(a & 0x80);
        if (!enough && (*bccnt & 0x80)) {        /* extra odd bonus coin present */
            a++;
            if (!(a & 0x80)) {
                *bccnt = (uint8_t)((uint8_t)(*bccnt << 1) >> 1);   /* clear it */
                enough = 1;
            }
        }
        if (enough) {
            if (cm == 1) (*crdt)++;              /* 2 plays per coin */
            (*crdt)++;
            *cnct = a;
        }
    }

    em_counters(m);
}

/* One full on/off cycle per count, never two counters at once; 64 ms on,
 * 64 ms off. */
static void em_counters(sw_machine *m)
{
    uint8_t *cctim = sw_ram(m, SW_SCCTIM);
    if (RAM8(m, SW_SINTCT) & 0x0f) return;
    int active = 0;
    for (int x = 2; x >= 0; x--) {
        if (cctim[x] & 0x80) {
            cctim[x] &= 0x7f;                    /* active on -> active off */
            active = 1;
        }
    }
    if (active) return;
    for (int x = 2; x >= 0; x--) {
        if (cctim[x]) {
            cctim[x] = (uint8_t)(cctim[x] + 0x7f);   /* set MSB, count one off */
            return;
        }
    }
}
