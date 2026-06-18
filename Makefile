TARGET  = psp-music-player
OBJS    = main.o player.o audio.o ui.o

CFLAGS  = -O2 -G0 -Wall -mfpu=vfpu -mips2 \
          -I$(PSPSDK)/include \
          -I$(PSPDEV)/include \
          -DPSP_FW_VERSION=371

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti

# No OSLib — uses pspgu + intraFont (both in base PSPSDK)
LIBS    = -lintrafont \
          -lpspgu -lpspge \
          -lpspaudio -lpspaudiolib \
          -lmad \
          -lpspctrl -lpspkernelutils \
          -lpspsdk -lpspuser -lpspkernel \
          -lm -lc -lstdc++

EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Music Player
BUILD_PRX       = 1

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
