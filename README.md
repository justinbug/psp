# PSP Music Player
### Pitch & Speed control · MP3 / OGG / WAV · PSP 2000 optimised

A homebrew music player for the PSP that lets you independently control
**playback speed** (0.25× – 2.0×) and **pitch** (±12 semitones) in real time.
Built for the PSP 2000 Slim (64 MB RAM) but will run on any PSP with
Custom Firmware.

---

## Controls

| Button | Action |
|---|---|
| **×** | Play / Pause |
| **← →** | Previous / Next track |
| **↑ ↓** | Volume up / down |
| **L Trigger** (hold) | Speed down (−0.05× per frame) |
| **R Trigger** (hold) | Speed up (+0.05× per frame) |
| **□** (hold) | Pitch down (−0.5 semitone per frame) |
| **△** (hold) | Pitch up (+0.5 semitone per frame) |
| **○** | Reset pitch & speed to defaults |
| **START** | Toggle shuffle |
| **SELECT** | Cycle repeat (Off → One → All) |

---

## Requirements

### Toolchain
- **PSPSDK** — https://github.com/pspdev/pspsdk
- **psp-pacman** with `pspdev-oslib` installed:
  ```
  psp-pacman -S pspdev-oslib
  ```
- **libmad** (MP3 decoder) — many PSPSDK distributions include this;
  if not, grab `psp-libmad` from the pspdev pacman repo.

### Runtime (on the PSP)
- PSP 2000, 3000, or Go with **Custom Firmware** (e.g. PRO-C, ME, or LME)
- Music files in `ms0:/MUSIC/` (the `MUSIC` folder on your Memory Stick)

---

## Build

```bash
# 1. Clone / extract this project
cd psp-music-player

# 2. Drop stb_vorbis.c next to audio.c  (single-header OGG lib)
#    https://github.com/nothings/stb/blob/master/stb_vorbis.c
wget https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c

# 3. Build
make clean && make

# This produces EBOOT.PBP
```

### Cross-compiling on Linux / macOS
```bash
export PATH="$PATH:/usr/local/pspdev/bin"
make
```

### Cross-compiling on Windows
Use the **MSYS2** environment that ships with PSPDev, then run the same
`make` command in the MSYS2 shell.

---

## Install

1. Connect your PSP via USB (or remove the Memory Stick).
2. Copy the entire folder to:
   ```
   ms0:/PSP/GAME/PSPMusicPlayer/EBOOT.PBP
   ```
3. Put your music in:
   ```
   ms0:/MUSIC/
   ```
   Supported formats: `.mp3`, `.ogg`, `.wav`

4. Launch from the PSP XMB → Game → Memory Stick.

---

## How pitch & speed work

### Speed change (OLA resampling)
Overlap-Add (OLA) resampling in the time domain adjusts how many input
samples are consumed per output sample.  Speed > 1 eats more input →
shorter playback time.  Speed < 1 stretches audio out.

### Independent pitch shift
After the speed pass, a second linear-interpolation resample changes the
frequency content by a factor of 2^(semitones/12) without affecting duration,
because the OLA step already corrected for the time stretch.

Both operations run on the PSP's MIPS R4000 CPU.  The PSP 2000's extra RAM
(64 MB vs 32 MB on a PSP 1000) is used for a larger decode ring buffer
(`DECODE_BUF_SAMPLES`), which reduces stutter at extreme pitch/speed values.

---

## File structure

```
psp-music-player/
├── main.c          — Entry point, input, main loop
├── player.c/h      — Track list, navigation, state
├── audio.c/h       — Decode (MP3/OGG/WAV) + DSP (pitch/speed) + output
├── ui.c/h          — 480×272 display via OSLib
├── stb_vorbis.c    — (you download this — see Build section)
└── Makefile
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| **No tracks found** | Make sure files are in `ms0:/MUSIC/` with .mp3/.ogg/.wav extension |
| **Crackling at high speed** | Lower speed slightly; the ring buffer may be draining faster than decode |
| **MP3 not decoding** | Ensure `psp-libmad` is installed in your PSPSDK |
| **Linker errors with oslib** | Run `psp-pacman -S pspdev-oslib` |
| **Black screen** | Check that your CFW supports user-mode PRX homebrew |

---

## Licence
MIT — do whatever you like.
