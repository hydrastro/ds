CC = gcc
AR = ar

CFLAGS = -c -fPIC -std=c89
CFLAGS += -O2 -march=native -mtune=native
CFLAGS += -Wall -Wextra -Werror -pedantic -pedantic-errors
CFLAGS += -fno-common -Wl,--gc-sections -Wredundant-decls -Wno-unused-parameter
CFLAGS += -fstack-protector-strong #-fPIE
CFLAGS += -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security
CFLAGS += -fstack-clash-protection -z noexecstack -z relro -z now
CFLAGS += -Wl,-z,relro,-z,now -Wl,-pie #-fpie
CFLAGS += -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual -Wdeclaration-after-statement
CFLAGS += -Wfloat-equal -Wlogical-op -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes -Wnested-externs
CFLAGS += -Wpointer-arith -Wredundant-decls -Wsequence-point -Wstrict-prototypes
CFLAGS += -Wswitch -Wundef -Wunreachable-code -Wwrite-strings -Wconversion

CPPFLAGS += -I$(LIB_DIR) -I$(BUILD_DIR)

CFLAGS_DEBUG = -g -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize=leak
CFLAGS_SAFE = -D DS_THREAD_SAFE
LDFLAGS = -shared

LIB_DIR = lib
INCLUDE_DIR = include
PREFIX ?= /usr/local
OUT_LIB_DIR = $(PREFIX)/lib
OUT_INCLUDE_DIR = $(PREFIX)/include/lib

GEN_DIR   := gen
BUILD_DIR := build

GEN_SRC := $(GEN_DIR)/gen_ucd.c
GEN_BIN := $(BUILD_DIR)/gen_ucd

SRC_FILES = $(wildcard $(LIB_DIR)/*.c)
OBJ_FILES = $(SRC_FILES:.c=.o)
OBJ_FILES_SAFE = $(SRC_FILES:.c=_safe.o)

GEN_C := $(BUILD_DIR)/unicode_tables.c
GEN_H := $(BUILD_DIR)/unicode_tables.h
GEN_OBJ := $(BUILD_DIR)/unicode_tables.o

UNICODE_DIR := data
UNICODE_FILES := \
  $(UNICODE_DIR)/UnicodeData.txt \
  $(UNICODE_DIR)/CaseFolding.txt \
  $(UNICODE_DIR)/DerivedNormalizationProps.txt \
  $(UNICODE_DIR)/CompositionExclusions.txt \
  $(UNICODE_DIR)/GraphemeBreakProperty.txt \
  $(UNICODE_DIR)/emoji-data.txt


all: $(BUILD_DIR) unicode-tables libds.a libds.so libds_safe.a libds_safe.so

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(UNICODE_DIR):
	@mkdir -p $(UNICODE_DIR)

$(UNICODE_DIR)/UnicodeData.txt: | $(UNICODE_DIR)
	@curl -fsSL -o $@ https://www.unicode.org/Public/UCD/latest/ucd/UnicodeData.txt

$(UNICODE_DIR)/CaseFolding.txt: | $(UNICODE_DIR)
	@curl -fsSL -o $@ https://www.unicode.org/Public/UCD/latest/ucd/CaseFolding.txt

$(UNICODE_DIR)/DerivedNormalizationProps.txt: | $(UNICODE_DIR)
	@curl -fsSL -o $@ https://www.unicode.org/Public/UCD/latest/ucd/DerivedNormalizationProps.txt

$(UNICODE_DIR)/CompositionExclusions.txt: | $(UNICODE_DIR)
	@curl -fsSL -o $@ https://www.unicode.org/Public/UCD/latest/ucd/CompositionExclusions.txt

$(UNICODE_DIR)/GraphemeBreakProperty.txt: | $(UNICODE_DIR)
	@curl -fsSL -o $@ https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakProperty.txt

$(UNICODE_DIR)/emoji-data.txt: | $(UNICODE_DIR)
	@curl -fsSL -o $@ https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt

$(GEN_BIN): $(GEN_SRC) | $(BUILD_DIR)
	$(CC) -std=c89 -O2 -Wall -Wextra -o $@ $<

$(GEN_C) $(GEN_H): $(GEN_BIN) $(UNICODE_FILES) | $(BUILD_DIR)
	$(GEN_BIN) \
	  --unicode-data $(UNICODE_DIR)/UnicodeData.txt \
	  --case-folding $(UNICODE_DIR)/CaseFolding.txt \
	  --comp-excl $(UNICODE_DIR)/CompositionExclusions.txt \
	  --gb-prop $(UNICODE_DIR)/GraphemeBreakProperty.txt \
	  --emoji-data $(UNICODE_DIR)/emoji-data.txt \
	  --out-c $(GEN_C) \
	  --out-h $(GEN_H)

unicode-tables: $(GEN_C) $(GEN_H)

$(LIB_DIR)/unicode_runtime.o: $(GEN_H)

$(GEN_OBJ): $(GEN_C) $(GEN_H)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GEN_C) -o $(GEN_OBJ)

libds.a: $(OBJ_FILES) $(GEN_OBJ)
	$(AR) rcs $@ $^

libds.so: $(OBJ_FILES) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

libds_safe.a: $(OBJ_FILES_SAFE) $(GEN_OBJ)
	$(AR) rcs $@ $^

libds_safe.so: $(OBJ_FILES_SAFE) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c $(GEN_H)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@

%_safe.o: %.c $(GEN_H)
	$(CC) $(CFLAGS) $(CFLAGS_SAFE) $(CPPFLAGS) $< -o $@

install: all
	mkdir -p $(OUT_LIB_DIR)
	mkdir -p $(OUT_INCLUDE_DIR)
	cp libds.a libds.so $(OUT_LIB_DIR)/
	cp libds_safe.a libds_safe.so $(OUT_LIB_DIR)/
	cp $(LIB_DIR)/*.h $(OUT_INCLUDE_DIR)/
	cp ds.h $(PREFIX)/include/

uninstall:
	rm -f $(OUT_LIB_DIR)/libds.a $(OUT_LIB_DIR)/libds.so
	rm -f $(OUT_LIB_DIR)/libds_safe.a $(OUT_LIB_DIR)/libds_safe.so
	rm -rf $(OUT_INCLUDE_DIR)
	rm -f $(PREFIX)/include/ds.h

clean:
	rm -f $(OBJ_FILES) $(OBJ_FILES_SAFE) $(GEN_OBJ) libds.a libds.so libds_safe.a libds_safe.so
	rm -f $(GEN_C) $(GEN_H) $(GEN_BIN)

unicode-data: $(UNICODE_FILES)
