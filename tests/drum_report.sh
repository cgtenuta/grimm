#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM and contributors
# Renders the DRUM source's 12 drums (track 1, dry, one every 2 s, MIDI notes 36 .. 47) in host mode and reports each
# one's peak, decay times and brightness (tests/drum_report.py). Optional knobs: DRUM_P="64 50 40 60" (TUNE DECAY DRIVE
# SNAP, 0..127).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/drumtest
P="${DRUM_P:-64 50 40 60}"
set -- $P
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DDEF_ENG=ENG_DRUM -DDEF_REV=0 -DDEF_DRUM="{$1, $2, $3, $4}" \
   -DAPP_VER="\"drumtest\"" -Ifirmware/src -Ifirmware/gen -o build/drumtest/grimm firmware/src/grimm.c
python3 - <<'PY'
lines, t = [], 500
for n in range(36, 48):
    lines += [f"{t} midi 0x90 {n} 100", f"{t + 120} midi 0x80 {n} 0"]
    t += 2000
lines.append(f"{t + 500} end")
open("build/drumtest/script.txt", "w").write("\n".join(lines) + "\n")
PY
(cd build/drumtest && ./grimm out script.txt >/dev/null 2>&1)
python3 tests/drum_report.py build/drumtest/out.wav
