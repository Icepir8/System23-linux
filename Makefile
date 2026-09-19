# ============================================================================
#  System/23 (IBM Datamaster) emulator — Linux / GTK3 port
#  Plain Makefile: needs only gcc, make, pkg-config and the GTK3 dev package.
#
#    sudo apt install build-essential pkg-config libgtk-3-dev   # Zorin / Ubuntu
#    make            # builds bin/system23
#    make run        # builds and runs
#    make clean
# ============================================================================

CC      ?= cc
PKGS    := gtk+-3.0

# C99 + the POSIX/Linux bits we use (readlink /proc/self/exe).
CFLAGS  ?= -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS  += -Iinclude $(shell pkg-config --cflags $(PKGS))

LDLIBS  := $(shell pkg-config --libs $(PKGS)) -lm

BIN     := bin/system23
SRC     := $(shell find src -name '*.c')
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC))
DEP     := $(OBJ:.o=.d)

.PHONY: all run clean test

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(OBJ) -o $@ $(LDLIBS)
	@echo "  ->  built $@"

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(BIN)
	./$(BIN)

# CPU self-tests + real-ROM smoke run (off-tree harness; links the non-GUI
# objects only).  Usage:  make test  &&  ./build/boottest Roms
TEST_OBJ := build/cpu8085.o build/memory.o build/ioports.o build/i8259.o \
            build/i8253.o build/i8275.o build/i8257.o build/i765a_fdc.o build/i8251.o build/floppy.o
test: $(BIN)
	$(CC) $(CFLAGS) tools/boottest.c $(TEST_OBJ) -o build/boottest -lm
	@echo "  ->  built build/boottest   (run: ./build/boottest Roms)"

clean:
	$(RM) -r build $(BIN)
	@rmdir bin 2>/dev/null || true

-include $(DEP)
