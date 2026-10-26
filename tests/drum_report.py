# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM and contributors
"""Report on tests/drum_report.sh's WAV: per drum, peak, time to -20 / -40 / -60 dB, spectral centroid of the first 60 ms."""
import sys, wave
import numpy as np

NAMES = "KICK KICKH SNARE CLAP RIM TOML HATC TOMM HATO TOMH METAL NOISE".split()
w = wave.open(sys.argv[1])
fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).astype(float) / 32768
m = x.mean(axis=1)
print("drum    peak  dBFS   -20dB  -40dB  -60dB (ms)   centroid(Hz)  clipped")
for k, nm in enumerate(NAMES):
    s0 = int((0.5 + 2 * k) * fs)
    seg = m[s0:s0 + 2 * fs]
    pk = np.abs(seg).max()
    win = int(0.005 * fs)
    em = np.array([np.abs(seg[i:i + win]).max() for i in range(0, len(seg) - win, win)])

    def t(db):
        i = np.where(em < pk * 10 ** (-db / 20))[0]
        i = i[i > 3]
        return f"{i[0] * 5:5d}" if len(i) else " >2000"

    a = seg[:int(0.06 * fs)]
    sp = np.abs(np.fft.rfft(a * np.hanning(len(a))))
    fr = np.fft.rfftfreq(len(a), 1 / fs)
    print(f"{nm:6s} {pk:5.2f} {20 * np.log10(pk + 1e-9):6.1f}  {t(20)} {t(40)} {t(60)}      {(fr * sp).sum() / sp.sum():8.0f}  {(np.abs(seg) > 0.98).sum():5d}")
