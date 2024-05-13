# Makefile
#
# Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
#
# SPDX-License-Identifier: MIT

# GCC is default -- simply because it's faster to compile!
# From cursory tests, clang doesn't really add anything in terms of perfs.
CC				= gcc
SHELL			= /bin/bash
# This is where (g)make looks for the source files for implicit rules
VPATH			:= src src/format src/drivers contrib
VPATH 			+= ui_gl

CPPFLAGS		+= -Isrc -Isrc/format -Isrc/roms -Isrc/drivers
CPPFLAGS		+= -Icontrib
CPPFLAGS		+= -Ilibmish/src
CPPFLAGS		+= -Ilibmui/src

OPTIMIZE		?= -O2 -march=native -ffast-math
CFLAGS			+= --std=gnu99 -Wall -Wextra -g
#CFLAGS			+= -fno-omit-frame-pointer
CFLAGS			+= $(OPTIMIZE)
CFLAGS			+= -Wno-unused-parameter -Wno-unused-function
LDLIBS			+= -lX11 -lGL -lGLU
LDLIBS			+= -lpthread -lutil -lm

# better/faster linker
HAS_MOLD		:= $(shell which mold && echo 1)
ifeq ($(HAS_MOLD),1)
LDFLAGS 		+= -B/usr/libexec/mold
endif

VERSION			:= ${shell \
						echo $$(git describe --tags --abbrev=0 2>/dev/null || \
							echo "(dev)") \
						$$(git log -1 --date=short --pretty="%h %cd")}
CPPFLAGS		+= -DMII_VERSION="\"$(VERSION)\""

HAS_ALSA		:= $(shell pkg-config --exists alsa && echo 1)
ifeq ($(HAS_ALSA),1)
LDLIBS			+= $(shell pkg-config --libs alsa)
CPPFLAGS		+= $(shell pkg-config --cflags alsa) -DHAS_ALSA
else
${warning ALSA not found, no sound support}
endif

O 				:= build-$(shell $(CC) -dumpmachine)
BIN 			:= $(O)/bin
LIB 			:= $(O)/lib
OBJ 			:= $(O)/obj

all				: $(BIN)/mii_emu_gl

MII_SRC			:= $(wildcard src/*.c src/format/*.c \
							src/drivers/*.c contrib/*.c)
UI_SRC			:= $(wildcard ui_gl/*.c)

SRC				:= $(MII_SRC) $(UI_SRC)
ALL_OBJ			:= ${patsubst %, ${OBJ}/%, ${notdir ${SRC:.c=.o}}}

CPPFLAGS		+= ${shell pkg-config --cflags pixman-1}
LDLIBS			+= ${shell pkg-config --libs pixman-1}

# this requires 64 bits ints, as it's used by xorg
# This uses tinycc, which is handy to run that sort of tools
ui_gl/mii_icon64.h	: contrib/mii-icon-64.png
	tcc -run libmui/utils/png2raw.c -t "unsigned long" -n mii_icon64 -o $@ $<
ui_gl/mii_mui_apple_logo.h : docs/Apple_logo_rainbow_version2_28x28.png
	tcc -run libmui/utils/png2raw.c -n mii_mui_apple_logo -o $@ $<

$(BIN)/mii_emu_gl	: $(ALL_OBJ) | mui mish
$(BIN)/mii_emu_gl	: $(LIB)/libmish.a
$(BIN)/mii_emu_gl	: $(LIB)/libmui.a

.PHONY			: mish mui
mish 			: $(LIB)/libmish.a
LDLIBS 			+= $(LIB)/libmish.a
$(LIB)/libmish.a : ${wildcard libmish/src/*} | $(LIB) $(OBJ) $(BIN)
	mkdir -p $(OBJ)/libmish && \
	make -j -C libmish O="../" CC="$(CC)" V="$(V)" static

LDLIBS 			+= $(LIB)/libmui.a
mui 			: $(LIB)/libmui.a
$(LIB)/libmui.a : ${wildcard libmui/src/*} | $(LIB) $(OBJ) $(BIN)
	mkdir -p $(OBJ)/libmui && \
	make -j -C libmui BUILD_DIR="../" CC="$(CC)" \
			V="$(V)" OPTIMIZE="$(OPTIMIZE)" static

#  Smartport firmware needs the assembler first
test/asm/%.bin	: test/asm/%.asm | $(BIN)/mii_asm
	$(BIN)/mii_asm -v -o $@ $<
# And it also INCBIN the firmware driver
$(OBJ)/mii_smarport.o : test/asm/mii_smartport_driver.bin

clean			:
	rm -rf $(O); make -C libmui clean; make -C libmish clean

# This is for development purpose. This will recompile the project
# everytime a file is modified.
watch			:
	while true; do \
		clear; $(MAKE) -j all ; \
		inotifywait -qre close_write src src/format ui_gl test \
					libmui libmui/src; \
	done

tests				: $(BIN)/mii_test $(BIN)/mii_cpu_test $(BIN)/mii_asm

# Just the library for mii, not any of the UI stuff
TEST_OBJ			:= ${patsubst %, ${OBJ}/%, ${notdir ${MII_SRC:.c=.o}}}

# Base test without the UI -- this re-include all C source in one big
# executable, ignoring the existing .o files, just so we can have custom flags
# for the test
$(BIN)/mii_test 	: test/mii_test.c ${MII_SRC}
$(BIN)/mii_test		: CFLAGS = --std=gnu99 -Wall -Wextra -g -O0 -Og \
							-Wno-unused-parameter -Wno-unused-function
$(BIN)/mii_test		: CPPFLAGS = -DMII_TEST \
							-Isrc -Isrc/format -Isrc/roms -Isrc/drivers -Icontrib \
							-Ilibmish/src
$(BIN)/mii_test 	:
	@echo "  TEST" ${filter -O%, $(CPPFLAGS) $(CFLAGS)} $@
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LIB)/libmish.a

VPATH 				+= test
$(BIN)/mii_cpu_test : $(OBJ)/mii_cpu_test.o $(TEST_OBJ)
$(OBJ)/mii_cpu_test.o	: CFLAGS := -O0 -Og ${filter-out -O%, $(CFLAGS)}
$(OBJ)/mii_cpu_test.o	: CPPFLAGS += -DMII_TEST -DMII_65C02_DIRECT_ACCESS=0

# Assembler for the 6502
$(BIN)/mii_asm	 	: $(OBJ)/mii_asm.o $(TEST_OBJ)

ifeq ($(V),1)
Q :=
else
Q := @
endif

$(OBJ)/%.o 			: %.c | $(OBJ)
	@echo "  CC" ${filter -O%, $(CPPFLAGS) $(CFLAGS)} "$<"
	$(Q)$(CC) -MMD $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BIN)/%			:  | $(BIN)
	@echo "  LD      $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ) $(BIN) $(LIB) :
	@mkdir -p $@

# Generates the necessary file to help clangd index the files properly.
# This currently has to be done manually, but helps a lot if you use 'kate'
# editor or anthing else that is compatible with the LSP protocol (vscode too)
compile_commands.json: lsp
lsp:
	{ $$(which gmake) CC=gcc V=1 --always-make --dry-run all tests; \
		$$(which gmake) CC=gcc V=1 --always-make --dry-run -C libmish ; \
		$$(which gmake) CC=gcc V=1 --always-make --dry-run -C libmui all tests ; } | \
		sh utils/clangd_gen.sh >compile_commands.json

-include $(O)/*.d
-include $(O)/obj/*.d

DESTDIR		:= /usr/local

install:
	mkdir -p $(DESTDIR)/bin
	cp $(BIN)/mii_emu_gl $(DESTDIR)/bin/

avail:
	mkdir -p $(DESTDIR)/bin
	rm -f $(DESTDIR)/bin/mii_emu_gl && \
		ln -sf $(realpath $(BIN)/mii_emu_gl) $(DESTDIR)/bin/mii_emu_gl

-include Makefile-extras*.local
