
CC				= gcc
CPPFLAGS		+= -Imui
# for bsd_queue.h
CPPFLAGS		+= -I../libmish/src
CPPFLAGS		+= -DUI_HAS_XCB=1 -DUI_HAS_XKB=1
OPTIMIZE		?= -O0 -g
CFLAGS			+= --std=gnu99 -Wall -Wextra
CFLAGS			+= $(OPTIMIZE)
CFLAGS			+= -Wno-unused-parameter -Wno-unused-function
# PIC is necessary for the shared library/plugin to work
CFLAGS 			+= -fPIC

CPPFLAGS		+= ${shell pkg-config --cflags pixman-1}
LDLIBS			+= ${shell pkg-config --libs pixman-1}
LDLIBS			+= $(shell pkg-config --libs \
						xcb xcb-shm xcb-randr \
						xkbcommon-x11)
LDLIBS			+= -lm -ldl

O 				:= $(BUILD_DIR)build-$(shell $(CC) -dumpmachine)
BIN 			:= $(O)/bin
OBJ 			:= $(O)/obj/libmui
LIB 			:= $(O)/lib

MUI_SRC			:= $(wildcard mui/*.c)
SRC				:= $(MUI_SRC)
MUI_OBJ			:= ${patsubst %, $(OBJ)/%, ${notdir ${SRC:.c=.o}}}

SRC_VPATH		:= mui tests
SRC_VPATH		+= ../ui_gl
vpath %.c $(SRC_VPATH)

CPPFLAGS		+= -I../contrib

VERSION			:= ${shell git log -1 --date=short --pretty="%h %cd"}
CPPFLAGS		+= -DUI_VERSION="\"$(VERSION)\""

TARGET_LIB		:= $(LIB)/libmui.a

all 			:  $(BIN)/mui_playground $(LIB)/ui_tests.so

.PHONY			: static

static			: $(TARGET_LIB)

ifeq ($(V),1)
Q :=
else
Q := @
endif

$(TARGET_LIB) : $(MUI_OBJ) | $(LIB)
	@echo "  AR      $@"
	$(Q)$(AR) rcs $@ $^

$(OBJ)/ui_tests.o : CPPFLAGS += -Itests -I../ui_gl
$(LIB)/ui_tests.so : $(OBJ)/mii_mui_slots.o
$(LIB)/ui_tests.so : $(OBJ)/mii_mui_loadbin.o
$(LIB)/ui_tests.so : $(OBJ)/mii_mui_1mb.o
$(LIB)/ui_tests.so : $(OBJ)/mii_mui_2dsk.o
$(LIB)/ui_tests.so : $(OBJ)/mii_mui_about.o

$(OBJ)/mii_mui_about.o : CPPFLAGS+=-DMII_ICON64_DEFINE

# use a .temp file, otherwise the playground tries to reload before the file
# is fully written, and it fails.
# the ${filter} are there to make the sure object files are linked before the .a
$(LIB)/ui_tests.so : $(OBJ)/ui_tests.o $(LIB)/libmui.a | $(O)
	@echo "  LDSO      $@"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC -o $@.temp \
			${filter %.o, $^} ${filter %.a, $^} $(LDLIBS) && \
		mv $@.temp $@

$(BIN)/mui_playground : $(OBJ)/mui_playground.o $(LIB)/libmui.a

$(OBJ)/%.o		: %.c | $(OBJ)
	@echo "  CC" ${filter -O%, $(CPPFLAGS) $(CFLAGS)} "$<"
	$(Q)$(CC) -MMD $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BIN)/%			:  | $(BIN)
	@echo "  LD      $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(O) $(OBJ) $(BIN) $(LIB):
	@mkdir -p $@

clean :
	rm -rf $(O)

# This is for development purpose. This will recompile the project
# everytime a file is modified.
watch			:
	while true; do \
		clear; $(MAKE) -j all; \
		inotifywait -qre close_write mui tests ../ui_gl; \
	done

compile_commands.json: lsp
lsp:
	{ $$(which gmake) CC=gcc V=1 --always-make --dry-run all ; } | \
		sh ../utils/clangd_gen.sh >compile_commands.json

-include $(OBJ)/*.d
