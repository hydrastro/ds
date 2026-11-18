# GNU Make 4.3 groups the Unicode generator outputs into one build step.
ifeq ($(filter grouped-target,$(.FEATURES)),)
$(error GNU Make 4.3 or newer is required)
endif
.DEFAULT_GOAL := all

CC ?= cc
AR ?= ar
INSTALL ?= install
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck

CFLAGS ?= -std=c89 -Wall -Wextra -Werror -pedantic -pedantic-errors \
          -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual \
          -Wdeclaration-after-statement -Wfloat-equal -Wlogical-op \
          -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes \
          -Wnested-externs -Wpointer-arith -Wredundant-decls -Wsequence-point \
          -Wstrict-prototypes -Wswitch -Wundef -Wunreachable-code \
          -Wwrite-strings -Wconversion -O2 -fPIC
EXAMPLE_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic -O2
GEN_CFLAGS ?= -std=c89 -Wall -Wextra -O2
LDLIBS ?= -lm
ARFLAGS = rcs
# Expansion arithmetic requires unfused floating-point operations.
FP_STRICT_CFLAGS := -ffp-contract=off

PREFIX ?= /usr/local
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
BUILD_DIR ?= build

LIB_SRC := $(sort $(wildcard src/core/*.c src/structures/*.c src/algorithms/*.c))
LIB_OBJ := $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(LIB_SRC))
PUBLIC_HEADERS := $(sort $(wildcard include/*.h include/dsa/core/*.h \
                    include/dsa/structures/*.h include/dsa/algorithms/*.h))
FORMAT_SRC := $(LIB_SRC) $(PUBLIC_HEADERS) $(wildcard src/core/*.h tools/*.c examples/*.c)
GEN_DIR := $(BUILD_DIR)/generated
GEN_C := $(GEN_DIR)/unicode_tables.c
GEN_H := $(GEN_DIR)/unicode_tables.h
GEN_OBJ := $(BUILD_DIR)/obj/unicode_tables.o
GEN_BIN := $(BUILD_DIR)/tools/gen_ucd
INCLUDES := -Iinclude -Isrc -I$(GEN_DIR)
LIB_STATIC := $(BUILD_DIR)/libdsa.a
LIB_SHARED := $(BUILD_DIR)/libdsa.so
EXAMPLE_SRC := $(sort $(wildcard examples/*.c))
EXAMPLE_BIN := $(patsubst examples/%.c,$(BUILD_DIR)/examples/%,$(EXAMPLE_SRC))
UCD_FILES := data/UnicodeData.txt data/CaseFolding.txt \
             data/CompositionExclusions.txt data/GraphemeBreakProperty.txt \
             data/emoji-data.txt data/DerivedCoreProperties.txt

.PHONY: all examples unicode-data install uninstall clean check-headers \
        check-noinline check-o3 format format-check lint
all: $(LIB_STATIC) $(LIB_SHARED)
examples: $(EXAMPLE_BIN)
unicode-data: $(GEN_C) $(GEN_H)

$(GEN_BIN): tools/gen_ucd.c Makefile
	@mkdir -p $(@D)
	$(CC) $(GEN_CFLAGS) -o $@ $<

# Unicode inputs are part of the source tree; builds need no download.
$(GEN_C) $(GEN_H) &: $(GEN_BIN) $(UCD_FILES)
	@mkdir -p $(GEN_DIR)
	$(GEN_BIN) --unicode-data data/UnicodeData.txt \
	  --case-folding data/CaseFolding.txt \
	  --comp-excl data/CompositionExclusions.txt \
	  --gb-prop data/GraphemeBreakProperty.txt \
	  --emoji-data data/emoji-data.txt \
	  --derived-core data/DerivedCoreProperties.txt \
	  --out-c $(GEN_C) --out-h $(GEN_H)

$(GEN_DIR):
	@mkdir -p $@

$(BUILD_DIR)/obj/%.o: src/%.c Makefile | $(GEN_DIR)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/obj/structures/unicode_runtime.o: $(GEN_H)
$(BUILD_DIR)/obj/algorithms/robust_predicates.o: override CFLAGS += $(FP_STRICT_CFLAGS)

$(GEN_OBJ): $(GEN_C) $(GEN_H) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $(GEN_C)

-include $(LIB_OBJ:.o=.d) $(GEN_OBJ:.o=.d)

$(LIB_STATIC): $(LIB_OBJ) $(GEN_OBJ) Makefile
	@rm -f $@
	$(AR) $(ARFLAGS) $@ $(LIB_OBJ) $(GEN_OBJ)

$(LIB_SHARED): $(LIB_OBJ) $(GEN_OBJ) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $(LIB_OBJ) $(GEN_OBJ) $(LDLIBS)

$(BUILD_DIR)/examples/%: examples/%.c $(LIB_STATIC) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(EXAMPLE_CFLAGS) -Iinclude $(LDFLAGS) -o $@ $< $(LIB_STATIC) $(LDLIBS)

install: all
	@set -e; for h in $(PUBLIC_HEADERS); do \
	  target=$(DESTDIR)$(INCLUDEDIR)/$${h#include/}; \
	  $(INSTALL) -d "$${target%/*}"; \
	  $(INSTALL) -m 0644 "$$h" "$$target"; \
	done
	$(INSTALL) -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 0644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/libdsa.a
	$(INSTALL) -m 0755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/libdsa.so
	printf 'prefix=%s\nincludedir=%s\nlibdir=%s\n\nName: dsa\nDescription: Data structures and algorithms in C\nVersion: 0\nCflags: -I$${includedir}\nLibs: -L$${libdir} -ldsa\nLibs.private: -lm\n' '$(PREFIX)' '$(INCLUDEDIR)' '$(LIBDIR)' > $(DESTDIR)$(PKGCONFIGDIR)/dsa.pc

uninstall:
	rm -f $(addprefix $(DESTDIR)$(INCLUDEDIR)/,$(PUBLIC_HEADERS:include/%=%))
	-rmdir $(addprefix $(DESTDIR)$(INCLUDEDIR)/dsa/,core structures algorithms) $(DESTDIR)$(INCLUDEDIR)/dsa 2>/dev/null
	rm -f $(DESTDIR)$(LIBDIR)/libdsa.a $(DESTDIR)$(LIBDIR)/libdsa.so $(DESTDIR)$(PKGCONFIGDIR)/dsa.pc

# Compile the installed headers exactly as an application includes them.
HDR_CHECK_DIR := $(BUILD_DIR)/header-check
check-headers: all
	@mkdir -p $(HDR_CHECK_DIR)
	@$(MAKE) --no-print-directory install DESTDIR=$(abspath $(HDR_CHECK_DIR)) PREFIX=/usr >/dev/null
	@set -e; for h in $(PUBLIC_HEADERS:include/%=%); do \
	  printf '#include <%s>\nint main(void) { return 0; }\n' "$$h" > $(HDR_CHECK_DIR)/header.c; \
	  $(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror \
	    -I$(HDR_CHECK_DIR)/usr/include -c $(HDR_CHECK_DIR)/header.c -o $(HDR_CHECK_DIR)/header.o; \
	done
	@echo 'Every installed header compiles on its own.'

check-noinline check-o3: all
	@set -e; opt=-O0; [ '$@' != check-o3 ] || opt=-O3; \
	for src in $(LIB_SRC); do \
	  $(CC) $(CPPFLAGS) $(filter-out -O%,$(CFLAGS)) $$opt $(FP_STRICT_CFLAGS) $(INCLUDES) \
	    -c $$src -o $(BUILD_DIR)/$@.o; \
	done; rm -f $(BUILD_DIR)/$@.o
	@echo 'Every library source compiles with $@.'

format:
	$(CLANG_FORMAT) -i $(FORMAT_SRC)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SRC)

lint: $(GEN_H)
	$(CPPCHECK) --std=c89 --enable=warning,performance,portability \
	  --error-exitcode=1 --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  $(INCLUDES) $(LIB_SRC)

clean:
	rm -rf $(BUILD_DIR)
