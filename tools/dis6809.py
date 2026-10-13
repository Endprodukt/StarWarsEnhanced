#!/usr/bin/env python3
"""Disassemble a piece of the Star Wars program ROM, with the names of the load map.

    python3 -I tools/dis6809.py ROMZIP START [COUNT] [--bank1]

ROMZIP is the MAME set (starwars.zip with starwars1 inside, or a folder is not
supported). START is a hex address or a name from src/core/sw_symbols.h
(without SW_). COUNT instructions are printed (default 40). --bank1 reads
6000-7FFF from the overlay bank.

Used when porting: local labels of the 1983 source are not in the load map,
their addresses are read off the code that calls them.
"""
import os
import re
import sys
import zipfile
import zlib

ROMS = {0xe75ff867: 0x6000, 0xf725e344: 0x8000, 0xf6da0a00: 0xa000, 0x7e406703: 0xc000, 0xc7e51237: 0xe000}

INH = {0x12: "NOP", 0x13: "SYNC", 0x19: "DAA", 0x1d: "SEX", 0x39: "RTS", 0x3a: "ABX", 0x3b: "RTI",
       0x3d: "MUL", 0x3f: "SWI"}
MEM = ["NEG", "?", "?", "COM", "LSR", "?", "ROR", "ASR", "ASL", "ROL", "DEC", "?", "INC", "TST", "JMP", "CLR"]
ACC = ["SUB", "CMP", "SBC", None, "AND", "BIT", "LD", "ST", "EOR", "ADC", "OR", "ADD", None, None, None, None]
BR = ["BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ", "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE"]
REG = {0: "D", 1: "X", 2: "Y", 3: "U", 4: "S", 5: "PC", 8: "A", 9: "B", 10: "CC", 11: "DP"}
PSH = ["CC", "A", "B", "DP", "X", "Y", "U", "PC"]


def load(zip_path):
    mem = bytearray(0x12000)
    z = zipfile.ZipFile(zip_path)
    for n in z.namelist():
        d = z.read(n)
        base = ROMS.get(zlib.crc32(d) & 0xffffffff)
        if base is None:
            continue
        if base == 0x6000:
            mem[0x6000:0x8000] = d[:0x2000]
            mem[0x10000:0x12000] = d[0x2000:]
        else:
            mem[base:base + len(d)] = d
    return mem


def symbols():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "core", "sw_symbols.h")
    by_addr, by_name = {}, {}
    for line in open(path):
        m = re.match(r"#define SW_(\S+)\s+0x([0-9A-F]{4})", line)
        if m:
            by_name[m.group(1)] = int(m.group(2), 16)
            if int(m.group(2), 16) >= 0x4300:
                by_addr.setdefault(int(m.group(2), 16), m.group(1))
    return by_addr, by_name


class Dis:
    def __init__(self, mem, bank1, names):
        self.mem, self.bank1, self.names = mem, bank1, names

    def b(self, a):
        a &= 0xffff
        if self.bank1 and 0x6000 <= a < 0x8000:
            return self.mem[0x10000 + a - 0x6000]
        return self.mem[a]

    def w(self, a):
        return (self.b(a) << 8) | self.b(a + 1)

    def name(self, a):
        n = self.names.get(a & 0xffff)
        return "%04X%s" % (a & 0xffff, " " + n if n else "")

    def indexed(self, pc):
        pb = self.b(pc)
        pc += 1
        r = "XYUS"[(pb >> 5) & 3]
        if not pb & 0x80:
            o = pb & 0x1f
            return pc, "%d,%s" % (o - 32 if o & 0x10 else o, r)
        m = pb & 0x0f
        if m == 0: s = ",%s+" % r
        elif m == 1: s = ",%s++" % r
        elif m == 2: s = ",-%s" % r
        elif m == 3: s = ",--%s" % r
        elif m == 4: s = ",%s" % r
        elif m == 5: s = "B,%s" % r
        elif m == 6: s = "A,%s" % r
        elif m == 8:
            o = self.b(pc); pc += 1
            s = "%d,%s" % (o - 256 if o & 0x80 else o, r)
        elif m == 9:
            s = "$%04X,%s" % (self.w(pc), r); pc += 2
        elif m == 0xb: s = "D,%s" % r
        elif m == 0xc:
            o = self.b(pc); pc += 1
            s = "%s,PCR" % self.name(pc + (o - 256 if o & 0x80 else o))
        elif m == 0xd:
            o = self.w(pc); pc += 2
            s = "%s,PCR" % self.name(pc + o)
        elif m == 0xf:
            s = "%s" % self.name(self.w(pc)); pc += 2
        else: s = "?"
        if pb & 0x10:
            s = "[" + s + "]"
        return pc, s

    def operand(self, pc, mode, wide):
        if mode == 0:
            if wide:
                return pc + 2, "#$%04X" % self.w(pc)
            return pc + 1, "#$%02X" % self.b(pc)
        if mode == 1:
            return pc + 1, "<%s" % self.name(0x4800 | self.b(pc))
        if mode == 2:
            return self.indexed(pc)
        return pc + 2, self.name(self.w(pc))

    def one(self, pc):
        start = pc
        op = self.b(pc); pc += 1
        text = "?"
        if op in (0x10, 0x11):
            op2 = self.b(pc); pc += 1
            if op == 0x10 and 0x21 <= op2 <= 0x2f:
                o = self.w(pc); pc += 2
                text = "L%s %s" % (BR[op2 & 15], self.name(pc + o))
            elif op2 == 0x3f:
                text = "SWI2" if op == 0x10 else "SWI3"
            else:
                mode = (op2 >> 4) & 3
                lo = op2 & 0x0f
                if op == 0x10:
                    mn = {3: "CMPD", 0xc: "CMPY", 0xe: "LDS" if op2 & 0x40 else "LDY", 0xf: "STS" if op2 & 0x40 else "STY"}.get(lo, "?")
                else:
                    mn = {3: "CMPU", 0xc: "CMPS"}.get(lo, "?")
                pc, s = self.operand(pc, mode, True)
                text = "%s %s" % (mn, s)
        elif op in INH:
            text = INH[op]
        elif op < 0x10 or 0x60 <= op < 0x80:
            mode = 1 if op < 0x10 else 2 if op < 0x70 else 3
            pc, s = self.operand(pc, mode, False)
            text = "%s %s" % (MEM[op & 15], s)
        elif op == 0x16 or op == 0x17:
            o = self.w(pc); pc += 2
            text = "%s %s" % ("LBRA" if op == 0x16 else "LBSR", self.name(pc + o))
        elif op in (0x1a, 0x1c, 0x3c):
            text = "%s #$%02X" % ({0x1a: "ORCC", 0x1c: "ANDCC", 0x3c: "CWAI"}[op], self.b(pc)); pc += 1
        elif op in (0x1e, 0x1f):
            pb = self.b(pc); pc += 1
            text = "%s %s,%s" % ("EXG" if op == 0x1e else "TFR", REG.get(pb >> 4, "?"), REG.get(pb & 15, "?"))
        elif 0x20 <= op < 0x30:
            o = self.b(pc); pc += 1
            text = "%s %s" % (BR[op & 15], self.name(pc + (o - 256 if o & 0x80 else o)))
        elif 0x30 <= op < 0x34:
            pc, s = self.indexed(pc)
            text = "LEA%s %s" % ("XYSU"[op & 3], s)
        elif 0x34 <= op < 0x38:
            pb = self.b(pc); pc += 1
            regs = [PSH[i] if i != 6 or op < 0x36 else "S" for i in range(8) if pb & (1 << i)]
            text = "%s %s" % (["PSHS", "PULS", "PSHU", "PULU"][op - 0x34], ",".join(regs))
        elif 0x40 <= op < 0x60:
            text = MEM[op & 15] + ("A" if op < 0x50 else "B")
        elif op >= 0x80:
            mode = (op >> 4) & 3
            lo = op & 0x0f
            hi = op & 0x40
            if op == 0x8d:
                o = self.b(pc); pc += 1
                text = "BSR %s" % self.name(pc + (o - 256 if o & 0x80 else o))
            else:
                if ACC[lo]:
                    mn, wide = ACC[lo] + ("B" if hi else "A"), False
                else:
                    mn = {3: "ADDD" if hi else "SUBD", 0xc: "LDD" if hi else "CMPX", 0xd: "STD" if hi else "JSR",
                          0xe: "LDU" if hi else "LDX", 0xf: "STU" if hi else "STX"}[lo]
                    wide = True
                pc, s = self.operand(pc, mode, wide)
                text = "%s %s" % (mn, s)
        raw = " ".join("%02X" % self.b(a) for a in range(start, pc))
        return pc, "%04X: %-14s %s" % (start, raw, text)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    by_addr, by_name = symbols()
    start = by_name[args[1]] if args[1] in by_name else int(args[1], 16)
    count = int(args[2]) if len(args) > 2 else 40
    d = Dis(load(args[0]), "--bank1" in sys.argv, by_addr)
    pc = start
    for _ in range(count):
        if pc in by_addr:
            print("%s:" % by_addr[pc])
        pc, line = d.one(pc)
        print("  " + line)


main()
