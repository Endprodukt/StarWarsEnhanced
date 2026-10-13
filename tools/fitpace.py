"""fitpace.py CSV...: fit the cost of a frame (cpu_us as MAME measured it) to
the work the native routines counted. Costs are kept non-negative (features
that come out negative are dropped and the rest fitted again). Prints the
costs and the error by phase."""
import csv
import sys

import numpy as np

rows = []
for path in sys.argv[1:]:
    rows += list(csv.DictReader(open(path)))
names = [n for n in rows[0].keys() if n in ("rd", "wr", "chars", "digits", "mess", "shown", "rnd", "words")
         or (n.startswith("mb") and n != "mbox")]
y = np.array([float(r["cpu_us"]) for r in rows])
ph = np.array([int(r["phase"]) for r in rows])
ok = (y > 1000) & (y < 200000) & (ph >= 0x1f)
X = np.array([[float(r[n]) for n in names] for r in rows])
keep = [i for i in range(len(names)) if X[ok][:, i].sum() > 0]


phases = sorted(set(ph[ok]))
P = np.array([[1.0 if q == f else 0.0 for f in phases] for q in ph[ok]])


def fit(cols, label):
    cols = list(cols)
    while True:
        A = np.hstack([X[ok][:, cols], P])
        c, *_ = np.linalg.lstsq(A, y[ok], rcond=None)
        neg = [cols[i] for i in range(len(cols)) if c[i] < 0]
        if not neg:
            break
        worst = min(range(len(cols)), key=lambda i: c[i])
        del cols[worst]
    res = y[ok] - A @ c
    print(label, "rms %.0f us, max %.0f, n=%d" % (res.std(), abs(res).max(), len(res)))
    p = ph[ok]
    for q in sorted(set(p)):
        s = p == q
        if s.sum() < 20:
            continue
        print("   phase %02X: n=%5d mean cpu %6.0f us, rms err %5.0f, mean err %6.0f" %
              (q, s.sum(), y[ok][s].mean(), res[s].std(), res[s].mean()))
    print("costs (master clock cycles, 12.096 per us):")
    for i, v in zip(cols, c):
        print("   %-6s %8.0f" % (names[i], v * 12.096))
    for f, v in zip(phases, c[len(cols):]):
        print("   phase %02X %8.0f" % (f, v * 12.096))


fit(keep, "all features, non-negative:")
