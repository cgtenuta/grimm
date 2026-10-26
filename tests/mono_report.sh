#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM and contributors
# Renders the MONO source on track 1 (dry): MIDI notes 36, 48, 60, 72, 84, 1.2 s apart, each held 0.8 s, in host mode,
# and reports pitch, level and brightness (tests/mono_report.py). MONO_P="0 40 60 40" (WAVE SUB CUTOFF RESONANCE, 0..127).
# For DUAL: ENG=ENG_DUAL DEF=DEF_DUAL MONO_P="0 64 0 80" (WAVE OSC2 MODE AMOUNT).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/monotest
P="${MONO_P:-0 40 60 40}"
set -- $P
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DDEF_ENG="${ENG:-ENG_MONO}" -DDEF_REV=0 -DDEF_DRIVE="${DRIVE:-0}" -D"${DEF:-DEF_MONO}"="{$1, $2, $3, $4}" \
   -DAPP_VER="\"monotest\"" -Ifirmware/src -Ifirmware/gen -o build/monotest/grimm firmware/src/grimm.c
python3 - <<'PY'
lines, t = [], 500
for n in (36, 48, 60, 72, 84):
    lines += [f"{t} midi 0x90 {n} 100", f"{t + 800} midi 0x80 {n} 0"]
    t += 1200
lines.append(f"{t + 800} end")
open("build/monotest/script.txt", "w").write("\n".join(lines) + "\n")
PY
(cd build/monotest && ./grimm out script.txt >/dev/null 2>&1)
python3 tests/mono_report.py build/monotest/out.wav
