# Prebuilt firmware

`grimm-ebm-1.1.fwsc` is the GR!MM EBM build (identity FM-1_966): DRUM, MONO and DUAL engines, per-track DRIVE and the redesigned tab-strip UI (HOME rows, OSC/FILT/ENV/LFO pages, T1–T4 sequencer tabs),
from the sources in this repository at the commit that added this file.

- **Untested on hardware.** It passed the device build's static checks and the host tests only. Tune by ear.
- **Back up** projects and samples before flashing.
- **Install:** open `web/installer.html` in Chrome or Edge and drop the file on it, or run
  `py tools\fm1_install.py releases\grimm-ebm-1.1.fwsc`.
- **Built with** the bootloader (`uboot.boot`), `cfg_tool.bin` and `eq_cfg_hw.bin` taken from the official V15 package,
  not the Gitee SDK files `tools/build.py` pins, so the build's hash check was overridden for it. The V15 app area
  rebuilt from these three files matches the official one byte for byte.
- To go back to the official firmware, use the M-VAVE workbench ("return to V15").

SHA-256: `4e408a46fb73566698078cb02d6fa3814dab8348df352276442faeee29cad8c6`
