"""build.py - makes package/sounds from the recordings tools/swsamples/samples.txt names.

    python tools/swsamples/build.py [--sfx "SW Ultimate SFX Collection"] [--voice sounds/voice] [--out package/sounds]

Each recording: to mono (the board's sound is mono), its silence before
and after cut off, cut to its longest and faded out, brought to 16 kHz
(filtered: nothing above 8 kHz), to its loudness (the RMS of its loud
part, dB full scale) without its peaks going over -1 dB, and written as IMA
ADPCM (4 bit, a quarter of 16-bit PCM). The engine's loops are cut from the
middle of their sources and joined end to start with a crossfade, so that
they go round without a click. Several sources of one name become NAME.wav,
NAME_2.wav, ... (multisamples: one of them each time, src/sound/samples.h).
A source of pieces joined by " + " is one recording made of them, one after
the other with a short pause (Vader's breath before and after his line, as
the arcade game has it); a piece with "(-9 dB)" after it is that much below
the loudness of the piece without one.

Needs numpy, scipy, soundfile. Prints what it made, and how loud each is.
"""
import argparse
import os
import re
import shutil
import sys
from math import gcd

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RATE = 16000
FOLDERS = {'SPK': 'voice', 'PM': 'music'}


def folder_of(name):
    for k, v in FOLDERS.items():
        if name.startswith(k):
            return v
    return 'effects'


def frames_db(m, sr, per=100):
    fr = max(1, sr // per)
    n = len(m) // fr
    if n == 0:
        return np.zeros(1), fr
    return np.sqrt(np.mean(m[:n * fr].reshape(n, fr) ** 2, axis=1)), fr


def loudness(m, sr):
    """the RMS of the loud part: frames above a tenth of the loudest"""
    e, _ = frames_db(m, sr)
    top = e.max()
    loud = e[e > top * 0.1] if top > 0 else e
    return 20 * np.log10(np.sqrt(np.mean(loud ** 2)) + 1e-12)


def read_mono(path):
    a, sr = sf.read(path, dtype='float64', always_2d=True)
    return a.mean(1), sr


def to_rate(m, sr):
    g = gcd(RATE, sr)
    return resample_poly(m, RATE // g, sr // g) if sr != RATE else m


def trim(m, sr, longest):
    """silence before and after off; at most `longest` seconds, faded out"""
    e, fr = frames_db(m, sr, 200)
    top = e.max() if len(e) else 0.0
    on = np.where(e > top * 10 ** (-45 / 20))[0] if top > 0 else np.array([0])
    if len(on) == 0:
        return m
    a = max(0, on[0] * fr - int(0.003 * sr))
    b = min(len(m), (on[-1] + 1) * fr)
    m = m[a:b]
    if len(m) > longest * sr:
        m = m[: int(longest * sr)]
    fade_in, fade_out = int(0.002 * sr), min(len(m) // 4, int(0.06 * sr))
    if fade_in:
        m[:fade_in] *= np.linspace(0.0, 1.0, fade_in)
    if fade_out:
        m[-fade_out:] *= np.linspace(1.0, 0.0, fade_out) ** 2
    return m


def make_loop(m, sr, length, start):
    """`length` seconds from `start`, its end crossfaded into its beginning"""
    x = int(0.4 * sr)
    a, n = int(start * sr), int(length * sr)
    if a + n + x > len(m):
        a = max(0, len(m) - n - x)
    seg = m[a:a + n + x].copy()
    k = np.linspace(0.0, 1.0, x)
    head = seg[:x] * np.sqrt(k) + seg[n:n + x] * np.sqrt(1.0 - k)
    out = seg[:n].copy()
    out[:x] = head
    return out


def level(m, sr, db):
    g = 10 ** ((db - loudness(m, sr)) / 20)
    peak = np.abs(m).max() * g
    if peak > 10 ** (-1 / 20):                  # (its peaks no higher than -1 dB)
        g *= 10 ** (-1 / 20) / peak
    return m * g


def resolve(src, args, name):
    if src.startswith('@sfx/'):
        path = os.path.join(args.sfx, src[5:])
    elif src.startswith('@voice/'):
        path = os.path.join(args.voice, src[7:])
    else:
        sys.exit('%s: a source is @sfx/ or @voice/: %s' % (name, src))
    if not os.path.exists(path):
        sys.exit('%s: no such file: %s' % (name, path))
    return path


PIECE = re.compile(r'^(.*?)\s*\((-?\d+(?:\.\d+)?) dB\)$')
PAUSE = 0.12                                    # seconds between the pieces of one recording


def pieces(src, args, name, db):
    """one recording of one or more pieces: each to mono, 16 kHz, its silence cut, to its loudness"""
    parts = []
    for piece in src.split(' + '):
        piece = piece.strip()
        rel = 0.0
        mm = PIECE.match(piece)
        if mm:
            piece, rel = mm.group(1), float(mm.group(2))
        m, sr = read_mono(resolve(piece, args, name))
        m = trim(to_rate(m, sr), RATE, 60.0)
        parts.append(level(m, RATE, db + rel))
    if len(parts) == 1:
        return parts[0]
    gap = np.zeros(int(PAUSE * RATE))
    out = []
    for i, m in enumerate(parts):
        out += ([gap] if i else []) + [m]
    return np.concatenate(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(here, '..', '..'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--sfx', default=os.path.join(root, 'SW Ultimate SFX Collection'))
    ap.add_argument('--voice', default=os.path.join(root, 'sounds', 'voice'))
    ap.add_argument('--out', default=os.path.join(root, 'package', 'sounds'))
    ap.add_argument('--table', default=os.path.join(here, 'samples.txt'))
    args = ap.parse_args()

    entries = []
    for line in open(args.table, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        name, db, longest, rest = line.split(None, 3)
        srcs = [s.strip() for s in rest.split(';') if s.strip()]
        entries.append((name, float(db), longest, srcs))

    # what was made before goes (only the folders this makes)
    for sub in ('voice', 'music', 'effects'):
        p = os.path.join(args.out, sub)
        if os.path.isdir(p):
            shutil.rmtree(p)
        os.makedirs(p)

    total = 0
    for name, db, longest, srcs in entries:
        for k, src in enumerate(srcs):
            if '@' in longest:
                m, sr = read_mono(resolve(src, args, name))
                length, start = (float(v) for v in longest.split('@'))
                m = make_loop(to_rate(m, sr), RATE, length, start)
                m = level(m, RATE, db)
            else:
                m = pieces(src, args, name, db)
                m = trim(m, RATE, float(longest))
                if ' + ' not in src:
                    m = level(m, RATE, db)
            out = os.path.join(args.out, folder_of(name), name + ('.wav' if k == 0 else '_%d.wav' % (k + 1)))
            sf.write(out, np.clip(m, -1.0, 1.0).astype('float32'), RATE, format='WAV', subtype='IMA_ADPCM')
            size = os.path.getsize(out)
            total += size
            back, _ = sf.read(out, dtype='float64')
            print('%-12s %5.2fs %6.1f dB  peak %5.2f  %6d bytes  <- %s' %
                  (os.path.basename(out), len(back) / RATE, loudness(back, RATE), np.abs(back).max(), size, src))
    print('%d bytes in all' % total)


if __name__ == '__main__':
    main()
