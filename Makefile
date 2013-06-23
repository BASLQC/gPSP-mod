# -x assembler-with-cpp
# gpSP makefile
# Gilead Kutnick - Exophase


# Configuration

#BUILD_CFW_M33 = 1


# Global definitions

PSPSDK          = ${shell psp-config --pspsdk-path}
PREFIX          = ${shell psp-config --psp-prefix}

COMMON_FILE     = common.h cpu.h memory.h video.h input.h sound.h main.h      \
                  gui.h zip.h cheats.h fbm_print.h message.h                  \
                  SystemButtons.h exception.h

OBJS            = main.o cpu.o video.o memory.o sound.o input.o gui.o zip.o   \
                  cheats.o fbm_print.o mips_stub.o                            \
                  SystemButtons.o exception.o

TARGET          = UO_gpSP

VPATH           = src

CFLAGS          = -O2 -G0 -funsigned-char -Wall                               \
                  -fomit-frame-pointer -fstrict-aliasing -falign-functions=32 \
                  -falign-loops -falign-labels -falign-jumps

ASFLAGS         = ${CFLAGS}

PSP_FW_VERSION  = 371

ifdef BUILD_CFW_M33
  PSP_LARGE_MEMORY = 1
  CFLAGS          += -DBUILD_CFW_M33=1
endif

PSP_EBOOT_TITLE = GAMEBOY ADVANCE emulator

ifdef BUILD_CFW_M33
  PSP_EBOOT_ICON = res/gpSP_mod_m33.png
else
  PSP_EBOOT_ICON = res/gpSP_mod.png
endif

EXTRA_TARGETS   = EBOOT.PBP

INCDIR          = SDK/include
LIBDIR          = SDK/lib

LIBS            = -lpspgu -lpsprtc -lpspaudio -lpsppower -lz                  \
                  -lpspkubridge -lpspsystemctrl_user

include ${PSPSDK}/lib/build.mak

main.o         : ${COMMON_FILE} main.c
cpu.o          : ${COMMON_FILE} cpu.c mips_emit.h
video.o        : ${COMMON_FILE} video.c
memory.o       : ${COMMON_FILE} memory.c
sound.o        : ${COMMON_FILE} sound.c
input.o        : ${COMMON_FILE} input.c
gui.o          : ${COMMON_FILE} gui.c
zip.o          : ${COMMON_FILE} zip.c
cheats.o       : ${COMMON_FILE} cheats.c
fbm_print.o    : ${COMMON_FILE} fbm_print.c
exception.o    : ${COMMON_FILE} exception.c
mips_stub.o    : mips_stub.S
SystemButtons.o: SystemButtons.S

