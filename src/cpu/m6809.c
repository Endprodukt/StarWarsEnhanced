#include "cpu/m6809.h"

#include <string.h>

#define CC_C M6809_CC_C
#define CC_V M6809_CC_V
#define CC_Z M6809_CC_Z
#define CC_N M6809_CC_N
#define CC_I M6809_CC_I
#define CC_H M6809_CC_H
#define CC_F M6809_CC_F
#define CC_E M6809_CC_E

#define RD(a)     (c->read(c->user, (uint16_t)(a)))
#define WR(a, v)  (c->write(c->user, (uint16_t)(a), (uint8_t)(v)))
#define D(c)      ((uint16_t)(((c)->a << 8) | (c)->b))
#define SETD(c, v) do { uint16_t v_ = (uint16_t)(v); (c)->a = (uint8_t)(v_ >> 8); (c)->b = (uint8_t)v_; } while (0)

/* Cycles of the instructions without a prefix (indexed modes add theirs,
 * pushes and pulls one per byte, RTI nine more when it restores all). */
static const uint8_t CYCLES[256] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6,     /* 00 direct */
    0, 0, 2, 2, 2, 2, 5, 9, 2, 2, 3, 2, 3, 2, 8, 6,     /* 10 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,     /* 20 branches */
    4, 4, 4, 4, 5, 5, 5, 5, 2, 5, 3, 6, 20, 11, 2, 19,  /* 30 */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,     /* 40 A */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,     /* 50 B */
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6,     /* 60 indexed */
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 7,     /* 70 extended */
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 7, 3, 2,     /* 80 immediate */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,     /* 90 direct */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,     /* A0 indexed */
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6,     /* B0 extended */
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 3, 2,     /* C0 immediate */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,     /* D0 direct */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,     /* E0 indexed */
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,     /* F0 extended */
};

void m6809_init(m6809 *c, m6809_read_fn rd, m6809_write_fn wr, void *user)
{
    memset(c, 0, sizeof *c);
    c->read = rd;
    c->write = wr;
    c->user = user;
}

static uint16_t rd16(m6809 *c, uint16_t a) { uint8_t h = RD(a); return (uint16_t)((h << 8) | RD(a + 1)); }
static void wr16(m6809 *c, uint16_t a, uint16_t v) { WR(a, v >> 8); WR(a + 1, v); }
static uint8_t fetch8(m6809 *c) { return RD(c->pc++); }
static uint16_t fetch16(m6809 *c) { uint16_t v = rd16(c, c->pc); c->pc += 2; return v; }

void m6809_reset(m6809 *c)
{
    c->cc = CC_I | CC_F;
    c->dp = 0;
    c->irq = c->firq = 0;
    c->nmi_pending = c->nmi_armed = 0;
    c->waiting = 0;
    c->pc = rd16(c, 0xfffe);
}

void m6809_nmi(m6809 *c)
{
    if (c->nmi_armed) c->nmi_pending = 1;
}

/* --- stacks ------------------------------------------------------------------- */

static void pushs8(m6809 *c, uint8_t v) { WR(--c->s, v); }
static void pushs16(m6809 *c, uint16_t v) { pushs8(c, (uint8_t)v); pushs8(c, (uint8_t)(v >> 8)); }
static uint8_t pulls8(m6809 *c) { return RD(c->s++); }
static uint16_t pulls16(m6809 *c) { uint8_t h = pulls8(c); return (uint16_t)((h << 8) | pulls8(c)); }
static void pushu8(m6809 *c, uint8_t v) { WR(--c->u, v); }
static void pushu16(m6809 *c, uint16_t v) { pushu8(c, (uint8_t)v); pushu8(c, (uint8_t)(v >> 8)); }
static uint8_t pullu8(m6809 *c) { return RD(c->u++); }
static uint16_t pullu16(m6809 *c) { uint8_t h = pullu8(c); return (uint16_t)((h << 8) | pullu8(c)); }

static void push_all(m6809 *c)
{
    pushs16(c, c->pc);
    pushs16(c, c->u);
    pushs16(c, c->y);
    pushs16(c, c->x);
    pushs8(c, c->dp);
    pushs8(c, c->b);
    pushs8(c, c->a);
    pushs8(c, c->cc);
}

/* --- flags --------------------------------------------------------------------- */

static void nz8(m6809 *c, uint8_t v)
{
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z));
}

static void nz16(m6809 *c, uint16_t v)
{
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | (v & 0x8000 ? CC_N : 0) | (v ? 0 : CC_Z));
}

static uint8_t add8(m6809 *c, uint8_t a, uint8_t b, unsigned carry)
{
    unsigned r = (unsigned)a + b + carry;
    c->cc &= (uint8_t)~(CC_H | CC_N | CC_Z | CC_V | CC_C);
    if ((a ^ b ^ r) & 0x10) c->cc |= CC_H;
    if (r & 0x100) c->cc |= CC_C;
    if ((a ^ r) & (b ^ r) & 0x80) c->cc |= CC_V;
    if (r & 0x80) c->cc |= CC_N;
    if (!(r & 0xff)) c->cc |= CC_Z;
    return (uint8_t)r;
}

static uint8_t sub8(m6809 *c, uint8_t a, uint8_t b, unsigned carry)
{
    unsigned r = (unsigned)a - b - carry;
    c->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x100) c->cc |= CC_C;
    if ((a ^ b) & (a ^ r) & 0x80) c->cc |= CC_V;
    if (r & 0x80) c->cc |= CC_N;
    if (!(r & 0xff)) c->cc |= CC_Z;
    return (uint8_t)r;
}

static uint16_t add16(m6809 *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    c->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x10000) c->cc |= CC_C;
    if ((a ^ r) & (b ^ r) & 0x8000) c->cc |= CC_V;
    if (r & 0x8000) c->cc |= CC_N;
    if (!(r & 0xffff)) c->cc |= CC_Z;
    return (uint16_t)r;
}

static uint16_t sub16(m6809 *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    c->cc &= (uint8_t)~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x10000) c->cc |= CC_C;
    if ((a ^ b) & (a ^ r) & 0x8000) c->cc |= CC_V;
    if (r & 0x8000) c->cc |= CC_N;
    if (!(r & 0xffff)) c->cc |= CC_Z;
    return (uint16_t)r;
}

static void logic8(m6809 *c, uint8_t v)
{
    nz8(c, v);
    c->cc &= (uint8_t)~CC_V;
}

/* NEG COM LSR ROR ASR ASL ROL DEC INC TST CLR, by the opcode's low nibble. */
static uint8_t rmw(m6809 *c, unsigned op, uint8_t v)
{
    unsigned carry = c->cc & CC_C;
    uint8_t r;
    switch (op & 0x0f) {
    case 0x0: case 0x1:                     /* NEG */
        return sub8(c, 0, v, 0);
    case 0x2: case 0x3:                     /* COM */
        r = (uint8_t)~v;
        logic8(c, r);
        c->cc |= CC_C;
        return r;
    case 0x4: case 0x5:                     /* LSR */
        r = v >> 1;
        c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z | CC_C)) | (v & 1) | (r ? 0 : CC_Z));
        return r;
    case 0x6:                               /* ROR */
        r = (uint8_t)((v >> 1) | (carry << 7));
        nz8(c, r);
        c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));
        return r;
    case 0x7:                               /* ASR */
        r = (uint8_t)((v >> 1) | (v & 0x80));
        nz8(c, r);
        c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));
        return r;
    case 0x8:                               /* ASL */
        r = (uint8_t)(v << 1);
        nz8(c, r);
        c->cc = (uint8_t)((c->cc & ~(CC_C | CC_V)) | (v >> 7) | (((v ^ r) & 0x80) ? CC_V : 0));
        return r;
    case 0x9:                               /* ROL */
        r = (uint8_t)((v << 1) | carry);
        nz8(c, r);
        c->cc = (uint8_t)((c->cc & ~(CC_C | CC_V)) | (v >> 7) | (((v ^ r) & 0x80) ? CC_V : 0));
        return r;
    case 0xa: case 0xb:                     /* DEC */
        r = (uint8_t)(v - 1);
        nz8(c, r);
        c->cc = (uint8_t)((c->cc & ~CC_V) | (v == 0x80 ? CC_V : 0));
        return r;
    case 0xc:                               /* INC */
        r = (uint8_t)(v + 1);
        nz8(c, r);
        c->cc = (uint8_t)((c->cc & ~CC_V) | (v == 0x7f ? CC_V : 0));
        return r;
    case 0xd:                               /* TST */
        logic8(c, v);
        return v;
    default:                                /* CLR */
        c->cc = (uint8_t)((c->cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
        return 0;
    }
}

/* --- addressing ------------------------------------------------------------------ */

static uint16_t *index_reg(m6809 *c, unsigned pb)
{
    switch ((pb >> 5) & 3) {
    case 0: return &c->x;
    case 1: return &c->y;
    case 2: return &c->u;
    default: return &c->s;
    }
}

/* The effective address of an indexed operand; *extra gets its cycles. */
static uint16_t ea_indexed(m6809 *c, int *extra)
{
    unsigned pb = fetch8(c);
    uint16_t *r = index_reg(c, pb);
    uint16_t ea;
    if (!(pb & 0x80)) {                     /* 5-bit offset */
        *extra += 1;
        return (uint16_t)(*r + (int16_t)((pb & 0x10) ? (int)(pb & 0x1f) - 32 : (int)(pb & 0x1f)));
    }
    switch (pb & 0x0f) {
    case 0x0: ea = *r; *r += 1; *extra += 2; break;
    case 0x1: ea = *r; *r += 2; *extra += 3; break;
    case 0x2: *r -= 1; ea = *r; *extra += 2; break;
    case 0x3: *r -= 2; ea = *r; *extra += 3; break;
    case 0x4: ea = *r; break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)c->b); *extra += 1; break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)c->a); *extra += 1; break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)fetch8(c)); *extra += 1; break;
    case 0x9: ea = (uint16_t)(*r + fetch16(c)); *extra += 4; break;
    case 0xb: ea = (uint16_t)(*r + D(c)); *extra += 4; break;
    case 0xc: { int8_t o = (int8_t)fetch8(c); ea = (uint16_t)(c->pc + o); *extra += 1; break; }
    case 0xd: { uint16_t o = fetch16(c); ea = (uint16_t)(c->pc + o); *extra += 5; break; }
    case 0xf: ea = fetch16(c); *extra += 2; break;
    default:  ea = *r; break;               /* undefined post bytes */
    }
    if (pb & 0x10) {                        /* indirect */
        ea = rd16(c, ea);
        *extra += 3;
    }
    return ea;
}

/* Operand address by the opcode's mode nibble: 0 direct, 6/A/E indexed,
 * 7/B/F extended, 9/D direct. */
static uint16_t ea_mode(m6809 *c, unsigned op, int *extra)
{
    switch (op >> 4) {
    case 0x0: case 0x9: case 0xd: return (uint16_t)((c->dp << 8) | fetch8(c));
    case 0x6: case 0xa: case 0xe: return ea_indexed(c, extra);
    default: return fetch16(c);             /* 7, B, F */
    }
}

/* --- registers by number (TFR, EXG) ----------------------------------------------- */

static uint16_t get_reg(m6809 *c, unsigned n)
{
    switch (n) {
    case 0x0: return D(c);
    case 0x1: return c->x;
    case 0x2: return c->y;
    case 0x3: return c->u;
    case 0x4: return c->s;
    case 0x5: return c->pc;
    case 0x8: return (uint16_t)(0xff00 | c->a);
    case 0x9: return (uint16_t)(0xff00 | c->b);
    case 0xa: return (uint16_t)(0xff00 | c->cc);
    case 0xb: return (uint16_t)(0xff00 | c->dp);
    default: return 0xffff;
    }
}

static void set_reg(m6809 *c, unsigned n, uint16_t v)
{
    switch (n) {
    case 0x0: SETD(c, v); break;
    case 0x1: c->x = v; break;
    case 0x2: c->y = v; break;
    case 0x3: c->u = v; break;
    case 0x4: c->s = v; c->nmi_armed = 1; break;
    case 0x5: c->pc = v; break;
    case 0x8: c->a = (uint8_t)v; break;
    case 0x9: c->b = (uint8_t)v; break;
    case 0xa: c->cc = (uint8_t)v; break;
    case 0xb: c->dp = (uint8_t)v; break;
    default: break;
    }
}

/* --- pushes and pulls --------------------------------------------------------------- */

static int psh(m6809 *c, int user_stack)
{
    unsigned mask = fetch8(c);
    int n = 0;
    #define P8(v)  do { if (user_stack) pushu8(c, (v)); else pushs8(c, (v)); n += 1; } while (0)
    #define P16(v) do { if (user_stack) pushu16(c, (v)); else pushs16(c, (v)); n += 2; } while (0)
    if (mask & 0x80) P16(c->pc);
    if (mask & 0x40) P16(user_stack ? c->s : c->u);
    if (mask & 0x20) P16(c->y);
    if (mask & 0x10) P16(c->x);
    if (mask & 0x08) P8(c->dp);
    if (mask & 0x04) P8(c->b);
    if (mask & 0x02) P8(c->a);
    if (mask & 0x01) P8(c->cc);
    #undef P8
    #undef P16
    return n;
}

static int pul(m6809 *c, int user_stack)
{
    unsigned mask = fetch8(c);
    int n = 0;
    #define L8()  (n += 1, user_stack ? pullu8(c) : pulls8(c))
    #define L16() (n += 2, user_stack ? pullu16(c) : pulls16(c))
    if (mask & 0x01) c->cc = L8();
    if (mask & 0x02) c->a = L8();
    if (mask & 0x04) c->b = L8();
    if (mask & 0x08) c->dp = L8();
    if (mask & 0x10) c->x = L16();
    if (mask & 0x20) c->y = L16();
    if (mask & 0x40) { if (user_stack) c->s = L16(); else c->u = L16(); }
    if (mask & 0x80) c->pc = L16();
    #undef L8
    #undef L16
    return n;
}

/* --- branches ------------------------------------------------------------------------ */

static int condition(const m6809 *c, unsigned op)
{
    unsigned cc = c->cc;
    int n = (cc & CC_N) != 0, z = (cc & CC_Z) != 0, v = (cc & CC_V) != 0, cy = (cc & CC_C) != 0;
    switch (op & 0x0f) {
    case 0x0: return 1;                     /* BRA */
    case 0x1: return 0;                     /* BRN */
    case 0x2: return !(cy || z);            /* BHI */
    case 0x3: return cy || z;               /* BLS */
    case 0x4: return !cy;                   /* BCC */
    case 0x5: return cy;                    /* BCS */
    case 0x6: return !z;                    /* BNE */
    case 0x7: return z;                     /* BEQ */
    case 0x8: return !v;                    /* BVC */
    case 0x9: return v;                     /* BVS */
    case 0xa: return !n;                    /* BPL */
    case 0xb: return n;                     /* BMI */
    case 0xc: return n == v;                /* BGE */
    case 0xd: return n != v;                /* BLT */
    case 0xe: return !z && n == v;          /* BGT */
    default:  return z || n != v;           /* BLE */
    }
}

/* --- interrupts ------------------------------------------------------------------------ */

static int take_interrupt(m6809 *c, uint16_t vector, int all, unsigned mask)
{
    int cycles = all ? 19 : 10;
    if (c->waiting == 2) {
        cycles = 7;                         /* CWAI has pushed everything already */
    } else if (all) {
        c->cc |= CC_E;
        push_all(c);
    } else {
        c->cc &= (uint8_t)~CC_E;
        pushs16(c, c->pc);
        pushs8(c, c->cc);
    }
    c->waiting = 0;
    c->cc |= (uint8_t)mask;
    c->pc = rd16(c, vector);
    return cycles;
}

/* --- one instruction --------------------------------------------------------------------- */

static int step_page2(m6809 *c);
static int step_page3(m6809 *c);

/* The 8-bit accumulator instructions of 80-FF: op's low nibble, the
 * accumulator, the operand's value. */
static void acc_op(m6809 *c, unsigned lo, uint8_t *reg, uint8_t v)
{
    switch (lo) {
    case 0x0: *reg = sub8(c, *reg, v, 0); break;
    case 0x1: sub8(c, *reg, v, 0); break;
    case 0x2: *reg = sub8(c, *reg, v, c->cc & CC_C); break;
    case 0x4: *reg &= v; logic8(c, *reg); break;
    case 0x5: logic8(c, *reg & v); break;
    case 0x6: *reg = v; logic8(c, v); break;
    case 0x8: *reg ^= v; logic8(c, *reg); break;
    case 0x9: *reg = add8(c, *reg, v, c->cc & CC_C); break;
    case 0xa: *reg |= v; logic8(c, *reg); break;
    case 0xb: *reg = add8(c, *reg, v, 0); break;
    default: break;
    }
}

static int execute(m6809 *c)
{
    unsigned op = fetch8(c);
    int cycles = CYCLES[op], extra = 0;
    uint16_t ea, v16;
    uint8_t v8;

    if (op == 0x10) return step_page2(c);
    if (op == 0x11) return step_page3(c);

    switch (op >> 4) {
    case 0x0: case 0x6: case 0x7:           /* memory: NEG ... CLR, JMP */
        ea = ea_mode(c, op, &extra);
        if ((op & 0x0f) == 0x0e) { c->pc = ea; break; }             /* JMP */
        if ((op & 0x0f) == 0x0f) { (void)RD(ea); rmw(c, op, 0); WR(ea, 0); break; }   /* CLR (reads first, as the chip does) */
        v8 = rmw(c, op, RD(ea));
        if ((op & 0x0f) != 0x0d) WR(ea, v8);                        /* TST writes nothing */
        break;

    case 0x1:
        switch (op) {
        case 0x12: break;                                           /* NOP */
        case 0x13: c->waiting = 1; break;                           /* SYNC */
        case 0x16: v16 = fetch16(c); c->pc = (uint16_t)(c->pc + v16); break;    /* LBRA */
        case 0x17: v16 = fetch16(c); pushs16(c, c->pc); c->pc = (uint16_t)(c->pc + v16); break;   /* LBSR */
        case 0x19: {                                                /* DAA */
            unsigned corr = 0, lo = c->a & 0x0f, hi = c->a >> 4;
            if ((c->cc & CC_H) || lo > 9) corr |= 0x06;
            if ((c->cc & CC_C) || hi > 9 || (hi > 8 && lo > 9)) corr |= 0x60;
            unsigned r = c->a + corr;
            c->a = (uint8_t)r;
            nz8(c, c->a);
            if (r & 0x100) c->cc |= CC_C;
            break;
        }
        case 0x1a: c->cc |= fetch8(c); break;                       /* ORCC */
        case 0x1c: c->cc &= fetch8(c); break;                       /* ANDCC */
        case 0x1d: c->a = (c->b & 0x80) ? 0xff : 0x00; nz16(c, D(c)); break;   /* SEX */
        case 0x1e: {                                                /* EXG */
            unsigned pb = fetch8(c);
            uint16_t a = get_reg(c, pb >> 4), b = get_reg(c, pb & 15);
            set_reg(c, pb >> 4, b);
            set_reg(c, pb & 15, a);
            break;
        }
        case 0x1f: {                                                /* TFR */
            unsigned pb = fetch8(c);
            set_reg(c, pb & 15, get_reg(c, pb >> 4));
            break;
        }
        default: break;
        }
        break;

    case 0x2:                               /* short branches */
        v8 = fetch8(c);
        if (condition(c, op)) c->pc = (uint16_t)(c->pc + (int8_t)v8);
        break;

    case 0x3:
        switch (op) {
        case 0x30: c->x = ea_indexed(c, &extra); c->cc = (uint8_t)((c->cc & ~CC_Z) | (c->x ? 0 : CC_Z)); break;
        case 0x31: c->y = ea_indexed(c, &extra); c->cc = (uint8_t)((c->cc & ~CC_Z) | (c->y ? 0 : CC_Z)); break;
        case 0x32: c->s = ea_indexed(c, &extra); c->nmi_armed = 1; break;
        case 0x33: c->u = ea_indexed(c, &extra); break;
        case 0x34: extra += psh(c, 0); break;
        case 0x35: extra += pul(c, 0); break;
        case 0x36: extra += psh(c, 1); break;
        case 0x37: extra += pul(c, 1); break;
        case 0x39: c->pc = pulls16(c); break;                       /* RTS */
        case 0x3a: c->x = (uint16_t)(c->x + c->b); break;           /* ABX */
        case 0x3b:                                                  /* RTI */
            c->cc = pulls8(c);
            if (c->cc & CC_E) {
                c->a = pulls8(c);
                c->b = pulls8(c);
                c->dp = pulls8(c);
                c->x = pulls16(c);
                c->y = pulls16(c);
                c->u = pulls16(c);
                extra += 9;
            }
            c->pc = pulls16(c);
            break;
        case 0x3c:                                                  /* CWAI */
            c->cc &= fetch8(c);
            c->cc |= CC_E;
            push_all(c);
            c->waiting = 2;
            break;
        case 0x3d: {                                                /* MUL */
            uint16_t r = (uint16_t)(c->a * c->b);
            SETD(c, r);
            c->cc = (uint8_t)((c->cc & ~(CC_Z | CC_C)) | (r ? 0 : CC_Z) | ((r & 0x80) ? CC_C : 0));
            break;
        }
        case 0x3f:                                                  /* SWI */
            c->cc |= CC_E;
            push_all(c);
            c->cc |= CC_I | CC_F;
            c->pc = rd16(c, 0xfffa);
            break;
        default: break;
        }
        break;

    case 0x4: c->a = rmw(c, op, c->a); break;
    case 0x5: c->b = rmw(c, op, c->b); break;

    default: {                              /* 80-FF */
        unsigned lo = op & 0x0f;
        int imm = ((op >> 4) & 3) == 0;
        uint8_t *reg = (op & 0x40) ? &c->b : &c->a;
        switch (lo) {
        case 0x3:                           /* SUBD / ADDD */
            v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
            if (op & 0x40) SETD(c, add16(c, D(c), v16));
            else SETD(c, sub16(c, D(c), v16));
            break;
        case 0x7:                           /* STA / STB */
            if (imm) { fetch8(c); break; }
            ea = ea_mode(c, op, &extra);
            WR(ea, *reg);
            logic8(c, *reg);
            break;
        case 0xc:                           /* CMPX / LDD */
            v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
            if (op & 0x40) { SETD(c, v16); nz16(c, v16); c->cc &= (uint8_t)~CC_V; }
            else sub16(c, c->x, v16);
            break;
        case 0xd:                           /* BSR, JSR / STD */
            if (op == 0x8d) {
                v8 = fetch8(c);
                pushs16(c, c->pc);
                c->pc = (uint16_t)(c->pc + (int8_t)v8);
            } else if (op & 0x40) {
                if (imm) { fetch16(c); break; }
                ea = ea_mode(c, op, &extra);
                wr16(c, ea, D(c));
                nz16(c, D(c));
                c->cc &= (uint8_t)~CC_V;
            } else {
                ea = ea_mode(c, op, &extra);
                pushs16(c, c->pc);
                c->pc = ea;
            }
            break;
        case 0xe:                           /* LDX / LDU */
            v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
            if (op & 0x40) c->u = v16;
            else c->x = v16;
            nz16(c, v16);
            c->cc &= (uint8_t)~CC_V;
            break;
        case 0xf:                           /* STX / STU */
            if (imm) { fetch16(c); break; }
            ea = ea_mode(c, op, &extra);
            v16 = (op & 0x40) ? c->u : c->x;
            wr16(c, ea, v16);
            nz16(c, v16);
            c->cc &= (uint8_t)~CC_V;
            break;
        default:
            v8 = imm ? fetch8(c) : RD(ea_mode(c, op, &extra));
            acc_op(c, lo, reg, v8);
            break;
        }
        break;
    }
    }
    return cycles + extra;
}

static int step_page2(m6809 *c)
{
    unsigned op = fetch8(c);
    int extra = 0;
    uint16_t v16, ea;
    int imm = ((op >> 4) & 3) == 0;

    if (op >= 0x21 && op <= 0x2f) {         /* long branches */
        v16 = fetch16(c);
        if (condition(c, op)) { c->pc = (uint16_t)(c->pc + v16); return 6; }
        return 5;
    }
    if (op == 0x3f) {                       /* SWI2 */
        c->cc |= CC_E;
        push_all(c);
        c->pc = rd16(c, 0xfff4);
        return 20;
    }
    int base = imm ? 0 : (op >> 4 & 3) == 3 ? 3 : 2;   /* over the immediate form: direct/indexed 2, extended 3 */
    switch (op & 0xcf) {
    case 0x83:                              /* CMPD */
        v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
        sub16(c, D(c), v16);
        return 5 + base + extra;
    case 0x8c:                              /* CMPY */
        v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
        sub16(c, c->y, v16);
        return 5 + base + extra;
    case 0x8e:                              /* LDY */
    case 0xce:                              /* LDS */
        v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
        if (op & 0x40) { c->s = v16; c->nmi_armed = 1; }
        else c->y = v16;
        nz16(c, v16);
        c->cc &= (uint8_t)~CC_V;
        return 4 + base + extra;
    case 0x8f:                              /* STY */
    case 0xcf:                              /* STS */
        if (imm) { fetch16(c); return 4; }
        ea = ea_mode(c, op, &extra);
        v16 = (op & 0x40) ? c->s : c->y;
        wr16(c, ea, v16);
        nz16(c, v16);
        c->cc &= (uint8_t)~CC_V;
        return 4 + base + extra;
    default:
        return 2;
    }
}

static int step_page3(m6809 *c)
{
    unsigned op = fetch8(c);
    int extra = 0;
    uint16_t v16;
    int imm = ((op >> 4) & 3) == 0;

    if (op == 0x3f) {                       /* SWI3 */
        c->cc |= CC_E;
        push_all(c);
        c->pc = rd16(c, 0xfff2);
        return 20;
    }
    int base = imm ? 0 : (op >> 4 & 3) == 3 ? 3 : 2;
    switch (op & 0xcf) {
    case 0x83:                              /* CMPU */
        v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
        sub16(c, c->u, v16);
        return 5 + base + extra;
    case 0x8c:                              /* CMPS */
        v16 = imm ? fetch16(c) : rd16(c, ea_mode(c, op, &extra));
        sub16(c, c->s, v16);
        return 5 + base + extra;
    default:
        return 2;
    }
}

int m6809_step(m6809 *c)
{
    int cycles;
    if (c->nmi_pending) {
        c->nmi_pending = 0;
        cycles = take_interrupt(c, 0xfffc, 1, CC_I | CC_F);
    } else if (c->firq && !(c->cc & CC_F)) {
        cycles = take_interrupt(c, 0xfff6, 0, CC_I | CC_F);
    } else if (c->irq && !(c->cc & CC_I)) {
        cycles = take_interrupt(c, 0xfff8, 1, CC_I);
    } else if (c->waiting) {
        if (c->waiting == 1 && (c->irq || c->firq)) c->waiting = 0;    /* SYNC ends on any interrupt line */
        cycles = 1;
    } else {
        cycles = execute(c);
    }
    c->cycles += (uint64_t)cycles;
    return cycles;
}
