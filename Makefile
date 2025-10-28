CC ?= cc
AR ?= ar

CFLAGS = -std=c89 -Wall -Wextra -Werror -pedantic -pedantic-errors \
         -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual \
         -Wdeclaration-after-statement -Wfloat-equal -Wlogical-op \
         -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes \
         -Wnested-externs -Wpointer-arith -Wredundant-decls -Wsequence-point \
         -Wstrict-prototypes -Wswitch -Wundef -Wunreachable-code \
         -Wwrite-strings -Wconversion \
         -O2 -fPIC
LDFLAGS = -shared
ARFLAGS = rcs

LIB_DIR   := lib
GEN_DIR   := gen
BUILD_DIR := build
DATA_DIR  := data
INCLUDES  := -I$(LIB_DIR) -I$(BUILD_DIR)

LIB_SRC := \
  $(LIB_DIR)/str_unicode.c \
  $(LIB_DIR)/unicode_runtime.c

GEN_SRC_C := $(BUILD_DIR)/unicode_tables.c
GEN_SRC_H := $(BUILD_DIR)/unicode_tables.h

LIB_OBJ      := $(LIB_SRC:.c=.o)
LIB_OBJ_SAFE := $(patsubst %.c,%_safe.o,$(LIB_SRC))
GEN_OBJ      := $(GEN_SRC_C:.c=.o)

LIB_STATIC      := libds.a
LIB_SHARED      := libds.so
LIB_STATIC_SAFE := libds_safe.a
LIB_SHARED_SAFE := libds_safe.so

GEN_BIN := $(BUILD_DIR)/gen_ucd
GEN_SRC := $(GEN_DIR)/gen_ucd.c

UCD_FILES := \
  $(DATA_DIR)/UnicodeData.txt \
  $(DATA_DIR)/CaseFolding.txt \
  $(DATA_DIR)/CompositionExclusions.txt \
  $(DATA_DIR)/GraphemeBreakProperty.txt \
  $(DATA_DIR)/emoji-data.txt

UCD_BASE := https://www.unicode.org/Public/UCD/latest/ucd
UCD_AUX  := $(UCD_BASE)/auxiliary
EMOJI_URL := https://www.unicode.org/Public/UCD/latest/ucd/emoji

.PHONY: all clean distclean unicode-data
all: $(LIB_STATIC) $(LIB_SHARED) $(LIB_STATIC_SAFE) $(LIB_SHARED_SAFE)

$(BUILD_DIR) $(DATA_DIR):
	@mkdir -p $@

$(DATA_DIR)/UnicodeData.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_BASE)/UnicodeData.txt
$(DATA_DIR)/CaseFolding.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_BASE)/CaseFolding.txt
$(DATA_DIR)/CompositionExclusions.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_BASE)/CompositionExclusions.txt
$(DATA_DIR)/GraphemeBreakProperty.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_AUX)/GraphemeBreakProperty.txt
$(DATA_DIR)/emoji-data.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(EMOJI_URL)/emoji-data.txt

unicode-data: $(UCD_FILES)

$(GEN_BIN): $(GEN_SRC) | $(BUILD_DIR)
	$(CC) -std=c89 -Wall -Wextra -O2 $(INCLUDES) -o $@ $<

$(GEN_SRC_C) $(GEN_SRC_H): $(GEN_BIN) $(UCD_FILES) | $(BUILD_DIR)
	$(GEN_BIN) \
	  --unicode-data $(DATA_DIR)/UnicodeData.txt \
	  --case-folding $(DATA_DIR)/CaseFolding.txt \
	  --comp-excl    $(DATA_DIR)/CompositionExclusions.txt \
	  --gb-prop      $(DATA_DIR)/GraphemeBreakProperty.txt \
	  --emoji-data   $(DATA_DIR)/emoji-data.txt \
	  --out-c        $(GEN_SRC_C) \
	  --out-h        $(GEN_SRC_H)

%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%_safe.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D DS_THREAD_SAFE $(INCLUDES) -c -o $@ $<

$(GEN_OBJ): $(GEN_SRC_C) $(GEN_SRC_H)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(GEN_SRC_C)

$(LIB_DIR)/unicode_runtime.o: $(GEN_SRC_H)
$(LIB_DIR)/unicode_runtime_safe.o: $(GEN_SRC_H)

$(LIB_STATIC): $(LIB_OBJ) $(GEN_OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(LIB_SHARED): $(LIB_OBJ) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(LIB_STATIC_SAFE): $(LIB_OBJ_SAFE) $(GEN_OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(LIB_SHARED_SAFE): $(LIB_OBJ_SAFE) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(LIB_DIR)/*.o $(LIB_DIR)/*_safe.o $(BUILD_DIR)/*.o \
	      $(LIB_STATIC) $(LIB_SHARED) $(LIB_STATIC_SAFE) $(LIB_SHARED_SAFE) \
	      $(GEN_BIN)
	rm -f $(GEN_SRC_C) $(GEN_SRC_H)

distclean: clean
	rm -rf $(DATA_DIR)
