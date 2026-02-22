FLAGS := -g -O3 -DBENCHMARK -Wall -I..
BUILD_DIR ?= ../.build/headless
OUTPUT := $(BUILD_DIR)/firebird-headless

TOOLCHAIN_ARCH = $(shell LANG=C $(CC) -v 2>&1 | awk '/Target:(.*)/{ print $$2 }' | cut -d- -f1)
TRANSLATION_ENABLED ?= TRUE
SUPPORT_LINUX ?= TRUE

ifeq "$(TOOLCHAIN_ARCH)" ""
    $(error Could not determine the toolchain architecture, please specify manually)
endif

ifeq "$(TRANSLATION_ENABLED)" "TRUE"
    ifneq "$(filter i%86,$(TOOLCHAIN_ARCH))" ""
        ASMSOURCES += ../core/jit/asmcode_x86.S
        CSOURCES += ../core/cpu/translate_x86.c
    else ifeq "$(TOOLCHAIN_ARCH)" "x86_64"
        ASMSOURCES += ../core/jit/asmcode_x86_64.S
        CSOURCES += ../core/jit/asmcode.c ../core/cpu/translate_x86_64.c
    else ifneq "$(filter arm%,$(TOOLCHAIN_ARCH))" ""
        ASMSOURCES += ../core/jit/asmcode_arm.S
        CSOURCES += ../core/jit/asmcode.c
        CPPSOURCES += ../core/cpu/translate_arm.cpp
    else ifneq "$(filter aarch64 arm64,$(TOOLCHAIN_ARCH))" ""
        ASMSOURCES += ../core/jit/asmcode_aarch64.S
        CSOURCES += ../core/jit/asmcode.c
        CPPSOURCES += ../core/cpu/translate_aarch64.cpp
    else
        $(error Unknown architecture $(TOOLCHAIN_ARCH))
    endif
else
    CSOURCES += ../core/jit/asmcode.c
    FLAGS += -DNO_TRANSLATION
endif

ifeq "$(SUPPORT_LINUX)" "TRUE"
    FLAGS += -DSUPPORT_LINUX
endif

CFLAGS += -std=c11 $(FLAGS)
CXXFLAGS += -std=c++11 $(FLAGS)
LFLAGS +=
LIBS := -lz

CSOURCES   += ../core/jit/armsnippets_loader.c ../core/soc/casplus.c ../core/crypto/des.c ../core/disassembly/disasm.c ../core/debug/gdbstub.c \
              ../core/peripherals/interrupt.c ../core/peripherals/lcd.c ../core/peripherals/link.c ../core/memory/mem.c ../core/peripherals/misc.c \
              ../core/memory/mmu.c ../core/timing/schedule.c ../core/peripherals/serial.c ../core/crypto/sha256.c ../core/usb/usb.c ../core/usb/usblink.c \
              ../core/os/os-linux.c

CPPSOURCES += ../core/cpu/arm_interpreter.cpp ../core/cpu/coproc.cpp ../core/cpu/cpu.cpp ../core/debug/debug.cpp ../core/emu.cpp \
              ../core/storage/flash.cpp ../core/gif.cpp ../core/cpu/thumb_interpreter.cpp ../core/usb/usblink_queue.cpp main.cpp \
              ../core/peripherals/keypad.cpp ../core/soc/cx2.cpp ../core/usb/usb_cx2.cpp ../core/usb/usblink_cx2.cpp ../core/storage/fieldparser.cpp

REL_ASMSOURCES := $(patsubst ../%,%,$(ASMSOURCES))
REL_CSOURCES := $(patsubst ../%,%,$(CSOURCES))
REL_CPPSOURCES := $(patsubst ../%,%,$(CPPSOURCES))

OBJS = $(addprefix $(BUILD_DIR)/obj/,$(REL_ASMSOURCES:.S=.o))
OBJS += $(addprefix $(BUILD_DIR)/obj/,$(REL_CSOURCES:.c=.o))
OBJS += $(addprefix $(BUILD_DIR)/obj/,$(REL_CPPSOURCES:.cpp=.o))

all: $(OUTPUT)

$(BUILD_DIR)/obj/%.o: ../%.S Makefile
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/obj/%.o: ../%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/obj/%.o: ../%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OUTPUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f $(OBJS) $(OUTPUT)
