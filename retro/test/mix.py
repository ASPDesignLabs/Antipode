#!/usr/bin/env python3
"""Check each carol as it is actually mixed: no DC offset, a sensible level that is not clipped, bass present, and no flood of
voices. Needs numpy. Usage: python3 test/mix.py <path to the built build/mix>"""
import os, subprocess, sys, tempfile
import numpy as np
SR = 44100
exe = sys.argv[1]; ok = True
NAMES = {1: 'Silent Night', 2: 'Jingle Bells', 3: 'Deck the Halls', 4: 'Joy to the World', 5: 'We Wish You a Merry Christmas'}
with tempfile.TemporaryDirectory() as tmp:
    for tid, name in NAMES.items():
        raw = os.path.join(tmp, f'{tid}.raw')
        voices = int(subprocess.run([exe, str(tid), '20', raw], capture_output=True, text=True, check=True).stdout.split()[0])
        a = np.fromfile(raw, dtype=np.int16).reshape(-1, 2).astype(np.float32) / 32768; m = a.mean(1)
        mean = float(m.mean()); ac = float((m - mean).std()); peak = float(np.abs(a).max())
        sp = np.abs(np.fft.rfft(m[SR:SR * 11] * np.hanning(SR * 10))) ** 2; fr = np.fft.rfftfreq(SR * 10, 1 / SR)
        bass = float(sp[(fr > 60) & (fr < 200)].sum() / sp.sum())
        quiet_secs = int((np.abs(m[:len(m) // SR * SR]).reshape(-1, SR).max(1) < 0.01).sum())
        checks = [('no DC offset', abs(mean) < 0.005, f'mean {mean:+.4f}'), ('audible, not loud', 0.02 < ac < 0.12, f'ac rms {ac:.3f}'),
                  ('not clipped', peak < 0.6, f'peak {peak:.2f}'), ('bass present', bass > 0.02, f'{bass * 100:.0f}% of the energy under 200 Hz'),
                  ('no flood of voices', voices <= 14, f'at most {voices} at once'), ('never goes quiet', quiet_secs == 0, f'{quiet_secs} silent seconds')]
        bad = [c for c in checks if not c[1]]; ok &= not bad
        print(f'{name:30s} ' + ('ok  ' if not bad else 'FAIL ') + '; '.join(f'{c[0]}: {c[2]}' for c in checks if not c[1] or c[0] in ("audible, not loud", "no flood of voices")))

# every layer at once, as the game's last tier plays it: still no offset, still not clipped, drums and brass present, voices in bounds
with tempfile.TemporaryDirectory() as tmp:
    for tid, name in NAMES.items():
        raw = os.path.join(tmp, f'L{tid}.raw')
        voices = int(subprocess.run([exe, str(tid), '20', raw, '15'], capture_output=True, text=True, check=True).stdout.split()[0])
        a = np.fromfile(raw, dtype=np.int16).reshape(-1, 2).astype(np.float32) / 32768; m = a.mean(1)
        mean = float(m.mean()); ac = float((m - mean).std()); peak = float(np.abs(a).max())
        sp = np.abs(np.fft.rfft(m[SR:SR * 11] * np.hanning(SR * 10))) ** 2; fr = np.fft.rfftfreq(SR * 10, 1 / SR)
        low = float(sp[(fr > 30) & (fr < 90)].sum() / sp.sum())
        c = [('no DC', abs(mean) < 0.005), ('level', 0.03 < ac < 0.16), ('not clipped', peak < 0.8), ('kick weight', low > 0.002), ('voices', voices <= 22)]
        bad = [n for n, good in c if not good]; ok &= not bad
        print(f'{name:30s} all layers: ' + ('ok  ' if not bad else 'FAIL ' + ','.join(bad) + '  ') + f'ac rms {ac:.3f}, peak {peak:.2f}, at most {voices} voices, {low * 100:.1f}% under 90 Hz')
print('ok' if ok else 'FAILED'); sys.exit(0 if ok else 1)
