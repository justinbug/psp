TARGET  = psp-music-player
OBJS    = main.o player.o audio.o ui.o

# ── Compiler flags ────────────────────────────────────────────────
# -O2       : good balance of speed and code size
# -mfpu=vfpu -mips2 : enable PSP VFPU (used in DSP code)
# -G0       : no GP-relative addressing (required for PSP user-mode)
CFLAGS  = -O2 -G0 -Wall -mfpu=vfpu -mips2 \
          -I$(PSPSDK)/include -I$(PSPDEV)/include \
          -DPSP_FW_VERSION=371

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti

# ── Link libraries ────────────────────────────────────────────────
# pspaudio     - hardware audio output
# pspgu/pspge  - GPU (via OSLib)
# oslib        - 2D drawing, text rendering
# mad          - MP3 decoding
# m            - libm (powf, etc.)
# pspctrl      - controller input
LIBS    = -loslib -lpspgu -lpspge \
          -lpspaudio -lpspaudiolib \
          -lmad \
          -lpspctrl -lpspkernelutils \
          -lpspsdk -lpspuser -lpspkernel \
          -lm -lc -lstdc++

# ── PSP-specific metadata ─────────────────────────────────────────
EXTRA_TARGETS  = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Music Player
PSP_EBOOT_ICON  = ICON0.PNG   # 144×80 PNG; remove line if you have no icon

BUILD_PRX = 1    # build as PRX for homebrew loaders

# ── SDK boilerplate ───────────────────────────────────────────────
PSPSDK  := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
