#!/usr/bin/env python3
"""Check the crash: the trombone's notes are where they should be, the kick has low weight twice, the cymbals are bright and die
away, and nothing clips. Needs numpy. Usage: python3 test/crash.py <path to the built build/crash>"""
import os, subprocess, sys, tempfile
import numpy as np
SR = 44100
exe = sys.argv[1]
with tempfile.TemporaryDirectory() as tmp:
    raw = os.path.join(tmp, 'c.raw'); subprocess.run([exe, raw], check=True)
    a = np.fromfile(raw, dtype=np.int16).reshape(-1, 2).astype(np.float32) / 32768
mono = a.mean(1); ok = True
def band_peak(t0, t1, lo, hi):
    seg = mono[int(t0 * SR):int(t1 * SR)]; sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 65536)); fr = np.fft.rfftfreq(65536, 1 / SR)
    m = (fr > lo) & (fr < hi); return fr[m][sp[m].argmax()]
def energy(t0, t1, lo, hi):
    seg = mono[int(t0 * SR):int(t1 * SR)]; sp = np.abs(np.fft.rfft(seg, 65536)) ** 2; fr = np.fft.rfftfreq(65536, 1 / SR)
    return sp[(fr > lo) & (fr < hi)].sum()
def check(name, cond, detail=''):
    global ok; print(('PASS ' if cond else 'FAIL ') + name + (' ' + detail if detail else '')); ok &= bool(cond)
mid = lambda f: 440 * 2 ** ((f - 69) / 12)
for name, t0, t1, m in [('trombone, wah 1 (Bb3)', 0.22, 0.42, 58), ('trombone, wah 2 (A3)', 0.67, 0.87, 57), ('trombone, wah 3 (Ab3)', 1.12, 1.32, 56)]:
    e = mid(m); f = band_peak(t0, t1, e * 0.85, e * 1.15); check(name, abs(1200 * np.log2(f / e)) < 40, f'{f:.1f} Hz, wanted {e:.1f}')
for t0, t1, label in [(1.75, 1.95, 'start'), (2.25, 2.45, 'middle'), (2.85, 3.05, 'end')]:
    k = ((t0 + t1) / 2 - 1.45) / 1.40; e = mid(55) + (mid(50) - mid(55)) * k
    f = band_peak(t0, t1, e * 0.85, e * 1.15); check(f'trombone slide, {label}', abs(1200 * np.log2(f / e)) < 120, f'{f:.1f} Hz, wanted about {e:.1f}')
base = energy(0.60, 0.70, 30, 130)
check('kick as the carol breaks off', energy(0.00, 0.15, 30, 130) > 5 * base)
check('kick under the last note', energy(1.45, 1.60, 30, 130) > 5 * base)
check('kick again', energy(1.72, 1.87, 30, 130) > 5 * base)
hf = lambda t0, t1: energy(t0, t1, 5000, 16000)
check('cymbals crash on the last note', hf(1.50, 1.70) > 20 * hf(0.60, 0.70))
check('and die away', hf(3.60, 3.80) < 0.2 * hf(1.50, 1.70))
check('no clipping', np.abs(a).max() < 0.999, f'peak {np.abs(a).max():.2f}')
check('quiet at the end', np.sqrt((mono[-int(0.3 * SR):] ** 2).mean()) < 0.01)
print('ok' if ok else 'FAILED'); sys.exit(0 if ok else 1)
