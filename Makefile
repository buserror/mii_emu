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

OPTIMIZE		?= -O3 -march=native -ffast-math -ftree-vectorize
#OPTIMIZE		?= -O0 -g -fno-omit-frame-pointer
CFLAGS			+= --std=gnu99 -Wall -Wextra -g
# This is useful for debugging, not so much for actual use
#CFLAGS			+= -fno-omit-frame-pointer
CFLAGS			+= $(OPTIMIZE)
CFLAGS			+= -Wno-unused-parameter -Wno-unused-function \
						-Wno-unused-result
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

# ROMS: This bits convert the binary roms with the product number in the name
# to a C header file that can be included in the project. I used to use
# incbin.h for this, but there were issues with some of the linker used, for
# example, webassembly, so we now use xxd to generate the .h file.
# You'd NEED the xxd tool if you want to re-generate these files, but well,
# they've been constant for over 40 years, my guess is that they ain't going
# anywhere.
#
# The reason these roms are used like this here is that I always found that it
# was a massive pain to have to deal with the roms in *any* apple II (or Mac)
# emulator. You had to find the roms, put them in the right place, name them
# correctly, and then, if you wanted to use a different version, you had to
# rename them, and so on. Dreadful.
# I think it prevents a lot of people learning about the Apple II, because it's
# just too much hassle to get started. So, I've included the roms in the source
# code, and they are compiled in the binary. For the user. Not for convenience,
# Not for 'stealing' from apple, but for the user. For the user to have a
# seamless experience. To be able to just run the emulator, and have it work.
# And be amazed *at the brand*.
#
# Now, I understand that strictly speaking these are copyrighted material, but
# they are so old, and so widely available, and are used here for educational
# purposes, and with the upmost respect for all the original authors, and for
# what 'the brand' represented for us at the time. With that in mind, I think
# that there shouldn't be an issue. But if you, Mr&Mrs Apple Lawyer think
# otherwise, please let me know, I'll remove them. Reluctantly. I'll cry&scream!
.PHONY : roms
define rom_to_h
$(1) :
	@if [ ! -f $$@ ]; then \
		echo "ROM file $$@ not found, relying on the exiting .h"; \
		touch $$@; \
	fi
src/roms/$(2).h : $(1)
	@if [ ! -s "$$<" ]; then \
		touch $$@; \
	else { echo "#pragma once"; \
			xxd -n $$(shell basename $$<|sed 's/_[0-9].*//') -i $$< |\
				sed 's/unsigned/static const unsigned/' ; \
		}>$$@ ; \
		echo "ROM $$@ Generated"; \
	fi

$(OBJ)/mii.o : src/roms/$(2).h
roms: src/roms/$(2).h
endef

# 38063e08c778503fc03ecebb979769e9  contrib/mii_rom_iiee_3420349b.bin
$(eval $(call rom_to_h,contrib/mii_rom_iiee_3420349b.bin,mii_rom_iiee))
# 9123fff3442c0e688cc6816be88dd4ab  contrib/mii_rom_iiee_video_3420265a.bin
$(eval $(call rom_to_h,contrib/mii_rom_iiee_video_3420265a.bin,mii_rom_iiee_video))
# e0d67bb1aabe2030547b4cbdf3905b60  contrib/mii_rom_iic_3420033a.bin
$(eval $(call rom_to_h,contrib/mii_rom_iic_3420033a.bin,mii_rom_iic))
# 67c0d61ab0911183faf05270f881a97e  contrib/mii_rom_ssc_3410065a.bin
$(eval $(call rom_to_h,contrib/mii_rom_ssc_3410065a.bin,mii_rom_ssc))
# 9123fff3442c0e688cc6816be88dd4ab  contrib/mii_rom_iic_video_3410265a.bin
$(eval $(call rom_to_h,contrib/mii_rom_iic_video_3410265a.bin,mii_rom_iic_video))

# This is the ROM file for the EEPROM card, with some games too...
$(eval $(call rom_to_h,disks/GamesWithFirmware.po,mii_rom_epromcard))
# And the smartport driver
$(eval $(call rom_to_h,test/asm/mii_smartport_driver.bin,mii_rom_smartport))

clean			:
	rm -rf $(O); make -C libmui clean; make -C libmish clean

.PHONY			: watch tests
# This is for development purpose. This will recompile the project
# everytime a file is modified.
watch			:
	while true; do \
		clear; $(MAKE) -j all ; \
		inotifywait -qre close_write src src/format ui_gl test \
					libmui libmui/src; \
	done

tests				: $(BIN)/mii_test $(BIN)/mii_cpu_test $(BIN)/mii_asm


ifeq ($(V),1)
Q :=
else
Q := @
endif

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


$(BIN)/mii_cpu_test	: CFLAGS := -O0 -Og ${filter-out -O%, $(CFLAGS)}
$(BIN)/mii_cpu_test	: CPPFLAGS += -DMII_TEST -DMII_65C02_DIRECT_ACCESS=0
$(BIN)/mii_cpu_test : test/mii_cpu_test.c src/mii_65c02*.c
	@echo "  TEST" ${filter -O%, $(CPPFLAGS) $(CFLAGS)} $@
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

# Assembler for the 6502 -- it picks the .c it needs, no need for other objects

$(BIN)/mii_asm	 	: test/mii_asm.c
	@echo "  CC+LD" ${filter -O%, $(CPPFLAGS) $(CFLAGS)} $@
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

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

.PHONY		: install avail

install:
	mkdir -p $(DESTDIR)/bin
	cp $(BIN)/mii_emu_gl $(DESTDIR)/bin/

avail:
	mkdir -p $(DESTDIR)/bin
	rm -f $(DESTDIR)/bin/mii_emu_gl && \
		ln -sf $(realpath $(BIN)/mii_emu_gl) $(DESTDIR)/bin/mii_emu_gl

-include Makefile-extras*.local
