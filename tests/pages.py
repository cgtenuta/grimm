#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM and contributors
"""Screenshots of the pages in host mode: build/pages/NAME.png (480 x 480).
   tests/pages.py [NAME ...]   (no names: all)   Each screen is a list of (button taps / encoder turns) from power-on,
   after the boot screen; ALGORITHM picks the track. Needs Pillow and a C compiler."""
import subprocess, sys, os
from pathlib import Path
from PIL import Image
os.chdir(Path(__file__).resolve().parent.parent)
BTN = dict(OCT_DOWN=0, OCT_UP=1, FX=2, SEL=3, ENV=4, LFO=5, EDIT=6, GLO=7, HOME=8, SAVE=9, ARP=10, SEQ=11, PLAY=12, REC=13)
ENC = dict(SELECT=0, ALGORITHM=1, KNOB1=2, KNOB2=3, KNOB3=4, KNOB4=5, PRESETS=6)
# name: [steps]; a step: "BTN" | "ENC:KNOB1:+3" | "KEYS:0x80" ; track t = turns of ALGORITHM from T1
def track(n): return [f"ENC:ALGORITHM:{n}"] if n else []
SCREENS = {}
for t in range(4):
    SCREENS[f"home-t{t+1}"] = track(t)
    SCREENS[f"osc-t{t+1}"] = track(t) + ["EDIT"]
    SCREENS[f"filt-t{t+1}"] = track(t) + ["EDIT"] * 2
    SCREENS[f"env-t{t+1}"] = track(t) + ["ENV"]
    SCREENS[f"lfo-t{t+1}"] = track(t) + ["LFO"]
    SCREENS[f"mix-t{t+1}"] = track(t) + ["HOME"]
    SCREENS[f"seq-t{t+1}"] = track(t) + ["SEQ"]
SCREENS["seq-set"] = ["SEQ", "SEQ"]
SCREENS["filt-closed"] = ["EDIT", "EDIT", "ENC:KNOB1:-75", "ENC:KNOB2:60", "ENC:KNOB3:30"]
def steps_on(t, every):                 # SEQ GRID: a note on every `every`th step of 16 for track t, then HOME
    st = track(t) + ["SEQ"]
    for i in range(16):
        if i % every == 0: st.append("ENC:KNOB1:40")
        st.append("ENC:SELECT:1")
    return st
SCREENS["home-steps"] = steps_on(0, 4) + ["ENC:ALGORITHM:1"] + steps_on(0, 2)[2:] + ["HOME"]
SCREENS["fx-tape"] = ["FX"]
SCREENS["fx-grain"] = ["FX", "FX"]
SCREENS["fx-delay"] = ["FX"] * 3
SCREENS["fx-space"] = ["FX"] * 4
SCREENS["sel-scale"] = ["SEL"]
SCREENS["arp"] = ["ARP"]
SCREENS["glo-song"] = ["GLO"]
SCREENS["save-sounds"] = ["SAVE"]
SCREENS["save-projects"] = ["SAVE", "SAVE"]
SCREENS["mix2"] = ["HOME", "HOME"]
SCREENS["fm-algo-t4"] = track(3) + ["EDIT", "EDIT"]
SCREENS["fm-op-t4"] = track(3) + ["EDIT", "EDIT", "EDIT"]
SCREENS["source"] = ["EDIT", "EDIT", "EDIT"]
def script(steps):
    t, out = 4000, []
    for s in steps:
        k = s.split(":")
        if k[0] == "ENC": out.append(f"{t} enc {ENC[k[1]]} {int(k[2])}")
        elif k[0] == "KEYS": out.append(f"{t} keys {k[1]}")
        else: out.append(f"{t} btn {BTN[k[0]]}")
        t += 150
    out += [f"{t + 400} shot", f"{t + 500} end"]
    return "\n".join(out) + "\n", t + 400
def main():
    names = sys.argv[1:] or list(SCREENS)
    Path("build/pages").mkdir(parents=True, exist_ok=True)
    cc = ["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="1.0"', "-Ifirmware/src", "-Ifirmware/gen",
          "-o", "build/pages/grimm", "firmware/src/grimm.c"]
    subprocess.run(cc, check=True)
    for n in names:
        txt, shot = script(SCREENS[n])
        Path(f"build/pages/{n}.txt").write_text(txt)
        subprocess.run(["./grimm", n, f"{n}.txt"], cwd="build/pages", check=True, stdout=subprocess.DEVNULL)
        ppm = Path(f"build/pages/{n}-{shot}.ppm")
        Image.open(ppm).resize((480, 480), Image.NEAREST).save(f"build/pages/{n}.png")
        for f in Path("build/pages").glob(f"{n}-*.ppm"): f.unlink()
        print(n, "->", f"build/pages/{n}.png")
main()
