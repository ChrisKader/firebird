CC := emcc
CXX := em++
FLAGS := -O3 -Wall -I.. -I../core -I../core/debug
CFLAGS := -std=c11 $(FLAGS)
CXXFLAGS := -std=c++17 $(FLAGS)
LFLAGS := -s USE_ZLIB=1 -s DISABLE_EXCEPTION_CATCHING=1 -s INVOKE_RUN=0 -s NO_EXIT_RUNTIME=1 -s ASSERTIONS=0 -s EXPORTED_RUNTIME_METHODS='["callMain","cwrap"]' --closure 0 --pre-js firebird-utils.js -s INITIAL_MEMORY=536870912
BUILD_DIR ?= ../.build/web
OUTPUT := $(BUILD_DIR)/firebird

CSOURCES :=    ../core/jit/armsnippets_loader.c ../core/jit/asmcode.c ../core/soc/casplus.c ../core/crypto/des.c ../core/disassembly/disasm.c \
	      ../core/debug/gdbstub.c ../core/peripherals/interrupt.c ../core/peripherals/lcd.c ../core/peripherals/link.c ../core/memory/mem.c \
	      ../core/peripherals/misc.c ../core/memory/mmu.c ../core/timing/schedule.c ../core/peripherals/serial.c ../core/crypto/sha256.c ../core/usb/usb.c \
              ../core/usb/usblink.c ../core/os/os-emscripten.c

CPPSOURCES := ../core/cpu/arm_interpreter.cpp ../core/cpu/coproc.cpp ../core/cpu/cpu.cpp ../core/debug/debug.cpp ../core/debug/debug_api.cpp ../core/debug/debug_api_peek.cpp ../core/debug/debug_cli.cpp ../core/debug/debug_remote.cpp ../core/debug/nspire_log_hook.cpp ../core/emu.cpp ../core/power/powercontrol.cpp \
	      ../core/storage/flash.cpp ../core/gif.cpp ../core/cpu/thumb_interpreter.cpp ../core/usb/usblink_queue.cpp ../core/usb/usb_cx2.cpp ../core/usb/usb_cx2_state.cpp ../core/usb/usblink_cx2.cpp \
	      ../core/peripherals/keypad.cpp ../core/peripherals/cx2_peripherals.cpp ../core/soc/cx2.cpp main.cpp \
	      ../core/storage/fieldparser.cpp

REL_CSOURCES := $(patsubst ../%,%,$(CSOURCES))
REL_CPPSOURCES := $(patsubst ../%,%,$(CPPSOURCES))

OBJS = $(addprefix $(BUILD_DIR)/obj/,$(REL_CSOURCES:.c=.o))
OBJS += $(addprefix $(BUILD_DIR)/obj/,$(REL_CPPSOURCES:.cpp=.o))

all: $(OUTPUT).js

$(BUILD_DIR)/obj/%.o: ../%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.o: ../%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(OUTPUT).js
	emrun --safe_firefox_profile --browser firefox $(OUTPUT).html

$(OUTPUT).js: $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)/obj $(OUTPUT).js $(OUTPUT).data $(OUTPUT).js.mem $(OUTPUT).wasm
