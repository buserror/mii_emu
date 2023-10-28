
# GCC is default -- simply because it's faster!
CC				= gcc
SHELL			= /bin/bash
# This is where (g)make looks for the source files for implicit rules
VPATH			:= src src/format src/drivers nuklear contrib

CPPFLAGS		+= -Isrc -Isrc/format -Isrc/roms -Isrc/drivers
CPPFLAGS		+= -Icontrib -Inuklear
CPPFLAGS		+= -Ilibmish/src
CFLAGS			+= --std=gnu99 -Wall -Wextra -O2 -g
CFLAGS			+= -Wno-unused-parameter -Wno-unused-function
LDLIBS			+= -lX11 -lm -lGL -lGLU
LDLIBS			+= -lpthread -lutil

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

all				: $(BIN)/mii_emu

MII_SRC			:= $(wildcard src/*.c src/format/*.c \
							src/drivers/*.c contrib/*.c)
UI_SRC			:= $(wildcard nuklear/*.c)
SRC				:= $(MII_SRC) $(UI_SRC)
ALL_OBJ			:= ${patsubst %, ${OBJ}/%, ${notdir ${SRC:.c=.o}}}

$(BIN)/mii_emu	: $(ALL_OBJ)
$(BIN)/mii_emu	: $(LIB)/libmish.a

libmish 		: $(LIB)/libmish.a
LDLIBS 			+= $(LIB)/libmish.a
$(LIB)/libmish.a : | $(LIB) $(OBJ) $(BIN)
	make -j -C libmish O="$(PWD)" CC="$(CC)" V="$(V)"

#  Smartport firmware needs the assembler first
test/asm/%.bin	: test/asm/%.asm | $(BIN)/mii_asm
	$(BIN)/mii_asm -v -o $@ $<
# And it also INCBIN the firmware driver
$(OBJ)/mii_smarport.o : test/asm/mii_smartport_driver.bin

$(OBJ)/libsofd.o : CPPFLAGS += -DHAVE_X11

clean			:
	rm -rf $(O)

# This is for development purpose. This will recompile the project
# everytime a file is modified.
watch			:
	while true; do \
		clear; $(MAKE) -j all tests; \
		inotifywait -qre close_write src src/format nuklear test; \
	done

tests				: $(BIN)/mii_test $(BIN)/mii_cpu_test $(BIN)/mii_asm

# Just the library for mii, not any of the UI stuff
TEST_OBJ			:= ${patsubst %, ${OBJ}/%, ${notdir ${MII_SRC:.c=.o}}}
VPATH 				+= test
# Base test without the UI, for performance testing
$(BIN)/mii_test 	: $(TEST_OBJ)
$(BIN)/mii_test 	: $(OBJ)/mii_test.o $(OBJ)/mii_mish.o
$(OBJ)/mii_test.o	: CFLAGS += -O0 -Og

$(OBJ)/mii_cpu_test.o	: CFLAGS += -O0 -Og
$(BIN)/mii_cpu_test : $(OBJ)/mii_cpu_test.o $(TEST_OBJ)

$(BIN)/mii_asm	 	: $(OBJ)/mii_asm.o $(TEST_OBJ)

ifeq ($(V),1)
Q :=
else
Q := @
endif

$(OBJ)/%.o		: %.c | $(OBJ)
	@echo "  CC      $<"
	$(Q)$(CC) -MMD $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BIN)/%			:  | $(BIN)
	@echo "  LD      $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ) $(BIN) $(LIB) :
	@mkdir -p $@

# Generates the necessary file to help clangd index the files properly.
# This currently has to be done manually, but helps a lot if you use 'kate'
# editor or anthing else that is compatible with the LSP protocol
compile_commands.json: lsp
lsp:
	{ $$(which gmake) CC=gcc V=1 --always-make --dry-run all tests; \
		$$(which gmake) CC=gcc V=1 --always-make --dry-run -C libmish ; } | \
		sh utils/clangd_gen.sh >compile_commands.json

-include $(O)/*.d
-include $(O)/obj/*.d
