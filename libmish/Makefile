# Makefile
#
# Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

SHELL			= /bin/bash

SOV				= 1
VERSION			= 0.10
PKG				= 1

TARGET			= libmish
DESC			= Program Shell Access

ifneq ($(CC), emcc)
BASE_LDFLAGS	+= -lutil
endif
BASE_LDFLAGS	+= -lpthread
EXTRA_LDFLAGS	= $(BASE_LDFLAGS)

# Auto load all the .c files dependencies, and object files
LIBSRC			:= ${notdir ${wildcard src/*.c}}

# Tell make/gcc to find the files in VPATH
SRC_VPATH 		= src
SRC_VPATH		+= tests
vpath %.c $(SRC_VPATH)

IPATH 			= src

include ./Makefile.common

TOOLS 			=
TESTS 			= ${BIN}/mish_test \
				  ${BIN}/mish_vt_test ${BIN}/mish_cmd_test \
				  ${BIN}/mish_input_test \
				  ${BIN}/mish_argv_make_test

all : tools tests

debug: all ${BIN}/mish_debug_test
	@echo "*** mish_debug_test is dangerous, do not install anywhere"

.PHONY: static shared tools tests
static: $(LIB)/$(TARGET).a
shared: ${LIB}/$(TARGET).so.$(SOV)
tools: $(TOOLS)
tests: $(TESTS)

LIBOBJ			:= ${patsubst %, ${OBJ}/%, ${notdir ${LIBSRC:.c=.o}}}
$(LIBOBJ)		: | $(OBJ)

$(LIB)/$(TARGET).a : $(LIBOBJ) | $(LIB)
$(LIB)/$(TARGET).so.$(SOV) : $(LIBOBJ) | $(LIB)/$(TARGET).a

# This rule OUGHT to be enough, but somehow it fails to take into account
# the 'shared' dependency, if its written explicitely as it is now, it
# works. I see no reason why it should not work, as the one a line down
# has no problem!!
#$(BIN)/%: | shared
$(TOOLS) $(TESTS): | shared
ifeq ($(CC), emcc)
$(BIN)/%: LDFLAGS_TARGET+=-L${LIB}
endif
$(BIN)/%: LDFLAGS_TARGET += -lmish -lrt

$(BIN)/mish_input_test: LDFLAGS_TARGET =

clean::
	rm -f $(LIB)/$(TARGET).* $(TOOLS) $(TESTS)


install:
	mkdir -p $(DESTDIR)/bin/ $(DESTDIR)/lib/ $(DESTDIR)/include/
	for bin in $(TOOLS); do $(INSTALL) -m 0755 $$bin $(DESTDIR)/bin/$$(basename $$bin) ; done
	$(INSTALL) -m 644 $(LIB)/$(TARGET).a $(DESTDIR)/lib/
	$(INSTALL) -m 644 src/mish.h $(DESTDIR)/include/
	rm -rf $(DESTDIR)/lib/$(TARGET).so*
	$(INSTALL) -m 644 $(LIB)/$(TARGET).so.$(VERSION).$(SOV) $(DESTDIR)/lib/
	cp -f -d  $(LIB)/{$(TARGET).so.$(SOV),$(TARGET).so} $(DESTDIR)/lib/
	mkdir -p $(DESTDIR)/lib/pkgconfig
	sed -e "s|PREFIX|$(PREFIX)|" -e "s|VERSION|$(VERSION)|" \
		mish.pc >$(DESTDIR)/lib/pkgconfig/mish.pc

deb 			:
	rm -rf /tmp/$(TARGET) *.deb
	make clean && make all && make install DESTDIR=/tmp/$(TARGET)/usr
	mkdir -p $(BUILD)/debian; (cd $(BUILD)/debian; \
	fpm -s dir -t deb -C /tmp/$(TARGET) -n $(TARGET) -v $(VERSION) \
		--iteration $(PKG) \
		--description "$(DESC)" \
		usr/lib/{$(TARGET).so.$(SOV),$(TARGET).so.$(VERSION).$(SOV),$(TARGET).so} && \
	fpm -s dir -t deb -C /tmp/$(TARGET) -n $(TARGET)-dev -v $(VERSION) \
		--iteration $(PKG) \
		--description "$(DESC) - development files" \
		-d "$(TARGET) (= $(VERSION))" \
		usr/lib/$(TARGET).a \
		usr/lib/pkgconfig \
		usr/include ; \
	)
