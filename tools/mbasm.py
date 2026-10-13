#!/usr/bin/env python3
"""Star Wars math box: assemble the original micro-program and turn it into C.

    python3 -I tools/mbasm.py SWMP.MAC --check            # PROM CRCs vs MAME's
    python3 -I tools/mbasm.py SWMP.MAC --c src/mbox/mbox_programs.c
    python3 -I tools/mbasm.py SWMP.MAC --prom-header tests/mbox_prom.h
    python3 -I tools/mbasm.py SWMP.MAC --list

SWMP.MAC (Jed Margolin, 1983) is the math box micro-program and TCODE2.MAC
its micro-assembler, both in https://github.com/historicalsource/star-wars.
This reimplements TCODE2's macros:

  one source line = one 16-bit micro-word
    D15 LDA addr   D14 LDB addr   D13 LDC addr,MAC   D12 CLA
    D11 IBC        D10 HALT       D9  SAC addr       D8  LAC addr
    D7  1 = direct address, 0 = indexed (IND)
    D6-D0 direct address; indexed: D1-D0 within the block BIC points at
  after a HALT the next word is aligned to 4 (entry points are PC*4)
  NXTPAG moves to the next 256-word page

The assembled words are split as the board's four 1K x 4 PROMs hold them
(136021-110..113) and checked against MAME's CRCs: when they match, the
source is exactly what the game runs, and the C generated from it is the
math box, instruction for instruction.
"""
import argparse
import re
import sys
import zlib

SYMS = {
    "XP": 0x00, "YP": 0x01, "ZP": 0x02, "AX": 0x03, "AY": 0x04, "AZ": 0x05,
    "BX": 0x06, "BY": 0x07, "BZ": 0x08, "CX": 0x09, "CY": 0x0A, "CZ": 0x0B,
    "XT": 0x0C, "YT": 0x0D, "ZT": 0x0E, "ZERO": 0x0F, "ONE": 0x10,
    "SIN": 0x11, "COS": 0x12, "HALF": 0x13,
    "AX1": 0x14, "AY1": 0x15, "AZ1": 0x16, "VX": 0x17,
    "BX1": 0x18, "BY1": 0x19, "BZ1": 0x1A, "VY": 0x1B,
    "CX1": 0x1C, "CY1": 0x1D, "CZ1": 0x1E, "VZ": 0x1F,
    "XT1": 0x20, "YT1": 0x21, "ZT1": 0x22, "FX": 0x23,
    "AX2": 0x24, "AY2": 0x25, "AZ2": 0x26, "FY": 0x27,
    "BX2": 0x28, "BY2": 0x29, "BZ2": 0x2A, "FZ": 0x2B,
    "CX2": 0x2C, "CY2": 0x2D, "CZ2": 0x2E, "THRUST": 0x2F,
    "XT2": 0x30, "YT2": 0x31, "ZT2": 0x32, "GRAV": 0x33,
    "C40": 0x34, "C80": 0x35, "SCALE": 0x36, "XSCALE": 0x37, "VSQ": 0x37,
    "XPS": 0x38, "PZ1": 0x38, "YPS": 0x39, "PY1": 0x39, "ZPS": 0x3A, "PZ2": 0x3A,
    "XCS": 0x3B, "PY2": 0x3B, "QW": 0x3C, "PX": 0x3C, "FW": 0x3D, "T": 0x3D,
    "QR": 0x3E, "TAX": 0x3E, "ASPD": 0x3F, "TBX": 0x3F,
    "X": 0, "Y": 1, "Z": 2, "PDR": 0, "K1": 1, "XR": 3, "YR": 0, "ZR": 1, "XS": 2, "YS": 3,
}

# Preferred name for a direct address when printing (the first meaning).
DIRECT_NAME = {}
for _n, _v in SYMS.items():
    if len(_n) > 1 and _n not in ("PDR", "K1", "XR", "YR", "ZR", "XS", "YS") and _v not in DIRECT_NAME:
        DIRECT_NAME[_v] = _n
# The names src/mbox/mbox.h uses for words with several meanings.
DIRECT_NAME.update({0x3D: "T", 0x3E: "TAX", 0x3F: "TBX"})
INDEX_NAME = {0: "X", 1: "Y", 2: "Z", 3: "W"}

ADDR_OPS = {"LDA": 0x8080, "LDB": 0x4080, "LDC": 0x2080, "SAC": 0x0280, "LAC": 0x0180}
FLAG_OPS = {"CLA": 0x1000, "IBC": 0x0800, "HALT": 0x0400}

PARTSZ = 1024

# MAME "starwars1": 136021-110 (bits 15-12), -111 (11-8), -112 (7-4), -113 (3-0)
PROM_CRCS = [("136021-110.7h", 12, 0x810E040E), ("136021-111.7j", 8, 0xAE69881C),
             ("136021-112.7k", 4, 0xECF22628), ("136021-113.7l", 0, 0x83FEBFDE)]

# The programs the game starts (WSGLOB.MAC: M$xxx = PC, the address / 4).
GAME_ENTRIES = {
    "ROLL": 0x000, "PITCH": 0x038, "YAW": 0x070, "PRESUB": 0x0A8, "CONCAT": 0x100,
    "POSTADD": 0x140, "POST2": 0x180, "PRE2": 0x19C, "DNLD1": 0x1DC, "DNLD2": 0x200,
    "PERS": 0x218, "PER2": 0x2B8, "PER2I": 0x2C0,
}


class Word:
    def __init__(self, addr, value, text, comment, lineno):
        self.addr, self.value, self.text, self.comment, self.lineno = addr, value, text, comment, lineno


def assemble(path):
    words, labels = [], {}
    pc = 0
    skipping = False                      # inside .REPT 0 ... .ENDR (a comment block)
    for lineno, raw in enumerate(open(path, "rb").read().decode("ascii", "replace").splitlines(), 1):
        line = raw.replace("\r", "").replace("\x00", "")
        code, _, comment = line.partition(";")
        code = code.strip()
        if skipping:
            skipping = not code.startswith(".ENDR")
            continue
        if re.match(r"^\.REPT\s+0\b", code):
            skipping = True
            continue
        comment = comment.strip()
        m = re.match(r"^([A-Z][A-Z0-9]*):\s*(.*)$", code)
        if m:
            labels[m.group(1)] = pc
            code = m.group(2).strip()
        if not code:
            continue
        head = code.split()[0]
        if head.startswith("."):
            if head == ".=" or code.replace(" ", "") == ".=0":
                pc = 0
            continue                      # .TITLE, .SBTTL, .PAGE, .INCLUDE, .END ...
        if head == "NXTPAG":
            pc = (pc + 0xFF) & ~0xFF
            continue
        if head in (".REPT", ".ENDR"):
            continue
        tokens = [t.strip() for t in re.split(r"[,\s]+", code) if t.strip()]
        value, i = 0, 0
        ind = False
        while i < len(tokens):
            t = tokens[i]
            if t in ADDR_OPS:
                a = SYMS[tokens[i + 1]]
                value |= ADDR_OPS[t] | a
                i += 2
            elif t in FLAG_OPS:
                value |= FLAG_OPS[t]
                i += 1
            elif t == "IND":
                ind = True
                i += 1
            elif t == "MAC":
                i += 1
            else:
                raise SystemExit("%s:%d: unknown token %r" % (path, lineno, t))
        if ind:
            value &= 0xFF7F
        words.append(Word(pc, value, code, comment, lineno))
        pc += 1
        if value & 0x400:                 # HALT: align the next entry point
            pc = (pc + 3) & ~3
    return words, labels


def image(words):
    img = [0] * PARTSZ
    for w in words:
        img[w.addr] = w.value
    return img


def prom_crcs(img, fill_hi=0):
    out = []
    for name, shift, want in PROM_CRCS:
        data = bytes(((v >> shift) & 0xF) | fill_hi for v in img)
        out.append((name, zlib.crc32(data) & 0xFFFFFFFF, want))
    return out


def disasm(v):
    parts = []
    direct = bool(v & 0x80)
    a = v & 0x7F
    an = DIRECT_NAME.get(a, "0x%02X" % a) if direct else INDEX_NAME[a & 3] + ",IND"
    for name, bits in ADDR_OPS.items():
        if v & 0xFF00 & bits:
            parts.append("%s %s" % (name, an))
    for name, bits in FLAG_OPS.items():
        if v & bits:
            parts.append(name)
    return ", ".join(parts) if parts else "NOP"


# ---------------------------------------------------------------------------
# C generation: each micro-word becomes the statements the hardware performs,
# in MAME's order (starwars_state::run_mproc): the RAM word is read once at
# the address of the BIC before IBC; then CLA, LAC, SAC, IBC, the
# multiply-accumulate with the old A and B, LDB, LDA.

def c_addr(v):
    if v & 0x80:
        a = v & 0x7F
        return "MB_%s" % DIRECT_NAME[a] if a in DIRECT_NAME else "0x%02X" % a
    return "MB_IND(m, %d)" % (v & 3)


def c_word(v):
    s = []
    reads = v & (0x8000 | 0x4000 | 0x2000 | 0x0100)
    if reads or v & 0x0200:
        s.append("ma = %s;" % c_addr(v))
    if reads:
        s.append("w = mb_rd(m, ma);")
    if v & 0x1000:
        s.append("m->acc = 0;")
    if v & 0x0100:
        s.append("m->acc = (int32_t)((uint32_t)w << 16);")
    if v & 0x0200:
        s.append("mb_wr(m, ma, (uint16_t)((uint32_t)m->acc >> 16));")
    if v & 0x0800:
        s.append("m->bic = (m->bic + 1) & 0x1ff;")
    if v & 0x2000:
        s.append("mb_mac(m, (int16_t)w);")
    if v & 0x4000:
        s.append("m->b = (int16_t)w;")
    if v & 0x8000:
        s.append("m->a = (int16_t)w;")
    return s


def gen_c(words, labels, out, src_name):
    by_addr = {w.addr: w for w in words}
    # Only aligned labels can be started (MW0 sets PC*4); SUB, MATRIX, SQA
    # are places inside PRE2.
    entries = sorted(((n, a) for n, a in labels.items() if a % 4 == 0), key=lambda kv: kv[1])
    o = []
    o.append("/* mbox_programs.c - GENERATED by tools/mbasm.py from %s. Do not edit." % src_name)
    o.append(" *")
    o.append(" * The Star Wars math box micro-program (Jed Margolin, 1983), one C")
    o.append(" * function per entry point, each micro-word turned into what the")
    o.append(" * hardware does with it. The assembled source matches the PROMs")
    o.append(" * 136021-110..113 (tools/mbasm.py --check), so this is the math box")
    o.append(" * instruction for instruction; tests/test_mbox.c holds it to the")
    o.append(" * micro-code interpreter. Source lines and comments are kept. */")
    o.append('#include "mbox/mbox.h"')
    o.append("")
    o.append("#define MB_IND(m, k) ((uint16_t)((((m)->bic & 0x1ff) << 2) | (k)))")
    o.append("")
    for name, start in entries:
        o.append("/* %s: PC %02X (address %03X) */" % (name, start >> 2, start))
        o.append("void mb_prog_%s(sw_mbox *m)" % name)
        o.append("{")
        o.append("    uint16_t ma, w;")
        o.append("    (void)ma;")
        o.append("    (void)w;")
        pc = start
        while True:
            w = by_addr.get(pc)
            if w is None:
                raise SystemExit("entry %s runs into unassembled address %03X" % (name, pc))
            cmt = (w.text + ("   ; " + w.comment if w.comment else "")).replace("\t", " ")
            o.append("    /* %03X  %s */" % (pc, cmt.replace("*/", "* /")))
            for st in c_word(w.value):
                o.append("    " + st)
            if w.value & 0x400:
                break
            pc = (pc & 0x300) | ((pc + 1) & 0xFF)   # the counter wraps within its page
        o.append("}")
        o.append("")
    o.append("const sw_mbox_entry sw_mbox_entries[] = {")
    for name, start in entries:
        o.append('    { "%s", 0x%02X, mb_prog_%s },' % (name, start >> 2, name))
    o.append("    { 0, 0, 0 }")
    o.append("};")
    open(out, "w").write("\n".join(o) + "\n")


def gen_prom_header(img, out):
    o = ["/* mbox_prom.h - GENERATED by tools/mbasm.py: the assembled math box",
         " * micro-program as 16-bit words (the four PROMs joined). For tests. */",
         "#ifndef SWE_MBOX_PROM_H", "#define SWE_MBOX_PROM_H", "#include <stdint.h>",
         "static const uint16_t mbox_prom[1024] = {"]
    for i in range(0, PARTSZ, 8):
        o.append("    " + ", ".join("0x%04X" % v for v in img[i:i + 8]) + ",")
    o += ["};", "#endif"]
    open(out, "w").write("\n".join(o) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source")
    ap.add_argument("--check", action="store_true", help="compare the PROM CRCs with MAME's")
    ap.add_argument("--list", action="store_true", help="print the assembled listing")
    ap.add_argument("--c", metavar="FILE", help="write the generated C programs")
    ap.add_argument("--prom-header", metavar="FILE", help="write the PROM image as a C header")
    args = ap.parse_args()

    words, labels = assemble(args.source)
    img = image(words)
    status = 0

    for name, want in GAME_ENTRIES.items():
        got = labels.get(name)
        if got != want:
            print("entry %s at %s, the game expects %03X" % (name, "%03X" % got if got is not None else "-", want))
            status = 1

    if args.list:
        inv = {v: k for k, v in labels.items()}
        for w in words:
            lab = inv.get(w.addr, "")
            print("%03X %04X  %-8s %-28s %-30s %s" % (w.addr, w.value, lab, w.text, disasm(w.value), w.comment))

    if args.check:
        ok = True
        for fill in (0x00, 0xF0):
            res = prom_crcs(img, fill)
            if all(got == want for _, got, want in res):
                print("PROMs match MAME's CRCs (unused high nibble %02X): the source is the shipped micro-code" % fill)
                break
        else:
            ok = False
            for name, got, want in prom_crcs(img):
                print("%s: assembled %08X, MAME %08X %s" % (name, got, want, "ok" if got == want else "DIFFERENT"))
        if not ok:
            status = 1

    if args.c:
        gen_c(words, labels, args.c, "SWMP.MAC")
    if args.prom_header:
        gen_prom_header(img, args.prom_header)
    sys.exit(status)


if __name__ == "__main__":
    main()
