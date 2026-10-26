# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM and contributors
"""Report on tests/mono_report.sh's WAV: per note, expected and measured pitch, peak, brightness, sub and alias energy."""
import sys, wave
import numpy as np

w = wave.open(sys.argv[1])
fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).astype(float) / 32768
m = x.mean(axis=1)
print("note  exp Hz   meas Hz   peak dBFS  centroid Hz  sub/fund dB  inharmonic dB  clipped")
for k, n in enumerate((36, 48, 60, 72, 84)):
    f0 = 440 * 2 ** ((n - 69) / 12)
    s0 = int((0.5 + 1.2 * k) * fs)
    seg = m[s0 + int(0.1 * fs):s0 + int(0.7 * fs)]          # the sustained part
    pk = np.abs(m[s0:s0 + int(0.8 * fs)]).max()
    N = 1 << 17
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N))
    fr = np.fft.rfftfreq(N, 1 / fs)
    band = (fr > f0 * 0.8) & (fr < f0 * 1.2)
    meas = fr[band][sp[band].argmax()]
    fund = sp[band].max()
    sb = (fr > f0 * 0.4) & (fr < f0 * 0.6)
    sub = sp[sb].max() / fund
    # energy away from the harmonics of the note (and its sub): aliasing and noise
    h = np.zeros_like(fr, bool)
    for j in range(1, int(20000 / (f0 / 2)) + 1):
        h |= np.abs(fr - j * f0 / 2) < max(8.0, f0 * 0.03)
    inh = np.sqrt((sp[~h & (fr > 20)] ** 2).sum() / (sp[fr > 20] ** 2).sum())
    cen = (fr * sp).sum() / sp.sum()
    print(f"{n:4d} {f0:8.1f} {meas:9.1f} {20 * np.log10(pk + 1e-9):10.1f} {cen:12.0f} {20 * np.log10(sub + 1e-9):11.1f} {20 * np.log10(inh + 1e-9):13.1f}  {(np.abs(m) > 0.98).sum():5d}")
