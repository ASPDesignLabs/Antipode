#!/usr/bin/env python3
"""Check that every note of every carol is rendered at the pitch it is meant to be, across its loops (so across its key changes
and its minor mode too). Needs numpy. Usage: python3 test/melody.py <path to the built build/melody>"""
import os, subprocess, sys, tempfile
import numpy as np
SR = 44100
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
TRACKS = {1: ('Silent Night', 2), 2: ('Jingle Bells', 3), 3: ('Deck the Halls', 3), 4: ('Joy to the World', 3), 5: ('We Wish You a Merry Christmas', 2)}
exe = sys.argv[1]
ok = True
with tempfile.TemporaryDirectory() as tmp:
    for tid, (title, loops) in TRACKS.items():
        raw = os.path.join(tmp, f'{tid}.raw')
        lines = subprocess.run([exe, str(tid), str(loops), raw], capture_output=True, text=True, check=True).stdout.split('\n')
        notes = [(float(l.split()[1]), float(l.split()[2]), int(l.split()[3])) for l in lines if l.startswith('N ')]
        a = np.fromfile(raw, dtype=np.int16).reshape(-1, 2).astype(np.float32).mean(1) / 32768
        bad = checked = 0
        for t0, d, m in notes:
            s = int((t0 + min(d * 0.15, 0.05)) * SR); e = min(int((t0 + d * 0.75) * SR), s + int(0.20 * SR)); seg = a[s:e]
            if len(seg) < int(0.03 * SR): continue
            sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 32768)); fr = np.fft.rfftfreq(32768, 1 / SR)
            exp = 440 * 2 ** ((m - 69) / 12); band = (fr > exp * 0.8) & (fr < exp * 1.25)
            f = fr[band][sp[band].argmax()]; checked += 1
            if abs(1200 * np.log2(f / exp)) > 40: bad += 1
        print(f'{title:30s} {checked:4d} notes, {bad} off pitch')
        ok &= bad == 0
print('ok' if ok else 'FAILED')
sys.exit(0 if ok else 1)
