# Build and run the FreeType vertex-buffer overflow tests.
#
# These tests link the FreeType text sources as they are in the tree. The
# overflow cases abort while the bug is present and exit 0 once text that
# cannot fit in a 32-bit vertex count is rejected or buffered in full.

ROOT := $(abspath ../..)
BUILD := $(abspath build)
CC := gcc
PKG_CFLAGS := $(shell pkg-config --cflags freetype2 fontconfig jansson)
PKG_LIBS := $(shell pkg-config --libs freetype2 fontconfig jansson)

CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
	-fno-omit-frame-pointer -fsanitize=address \
	-D_FILE_OFFSET_BITS=64 \
	-I$(BUILD) -I$(ROOT)/libobs -I$(ROOT)/plugins/text-freetype2 \
	$(PKG_CFLAGS)
LDFLAGS := -fsanitize=address -pthread
LIBS := $(PKG_LIBS) -luuid -ldl -lm

SRCS := \
	vertex_overflow_test.c \
	graphics_stub.c \
	$(ROOT)/plugins/text-freetype2/text-functionality.c \
	$(ROOT)/plugins/text-freetype2/obs-convenience.c \
	$(ROOT)/plugins/text-freetype2/find-font-unix.c \
	$(ROOT)/libobs/obs-data.c \
	$(ROOT)/libobs/util/platform.c \
	$(ROOT)/libobs/util/platform-nix.c \
	$(ROOT)/libobs/util/dstr.c \
	$(ROOT)/libobs/util/utf8.c \
	$(ROOT)/libobs/util/bmem.c \
	$(ROOT)/libobs/util/base.c

.PHONY: all run clean

all: $(BUILD)/freetype-vertex-overflow-test

$(BUILD)/obsconfig.h:
	mkdir -p $(BUILD)
	printf '%s\n' \
		'#pragma once' \
		'#define OBS_DATA_PATH "data"' \
		'#define OBS_PLUGIN_PATH "obs-plugins"' \
		'#define OBS_PLUGIN_DESTINATION "obs-plugins"' \
		'#define OBS_RELEASE_CANDIDATE 0' \
		'#define OBS_BETA 0' \
		> $(BUILD)/obsconfig.h

$(BUILD)/freetype-vertex-overflow-test: $(SRCS) $(BUILD)/obsconfig.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS) $(LIBS)

# short must pass. inline and file must also pass once the overflow is fixed;
# they abort (non-zero) while the wrapped allocation is still written past.
run: $(BUILD)/freetype-vertex-overflow-test
	ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
		$(BUILD)/freetype-vertex-overflow-test short
	ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
		$(BUILD)/freetype-vertex-overflow-test inline
	ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
		$(BUILD)/freetype-vertex-overflow-test file

clean:
	rm -rf $(BUILD)
