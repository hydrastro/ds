CC ?= cc
CXX ?= c++
AR ?= ar
VALGRIND ?= valgrind
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck

CFLAGS = -std=c89 -Wall -Wextra -Werror -pedantic -pedantic-errors \
         -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual \
         -Wdeclaration-after-statement -Wfloat-equal -Wlogical-op \
         -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes \
         -Wnested-externs -Wpointer-arith -Wredundant-decls -Wsequence-point \
         -Wstrict-prototypes -Wswitch -Wundef -Wunreachable-code \
         -Wwrite-strings -Wconversion \
         -O2 -fPIC
TEST_CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -O2
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS = -shared
LDLIBS = -pthread -lm
ARFLAGS = rcs
VALGRIND_FLAGS ?= --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,possible --error-exitcode=99 --track-origins=yes

PREFIX ?= /usr/local
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
INSTALL ?= install

LIB_DIR   := lib
GEN_DIR   := gen
TEST_DIR  := test
BUILD_DIR := build
DATA_DIR  := data
EXAMPLE_DIR := examples
TEST_BUILD_DIR := $(BUILD_DIR)/test
INCLUDES  := -I. -I$(LIB_DIR) -I$(BUILD_DIR)

LIB_SRC := \
  $(LIB_DIR)/allocators.c \
  $(LIB_DIR)/avl.c \
  $(LIB_DIR)/bst.c \
  $(LIB_DIR)/btree.c \
  $(LIB_DIR)/aho_corasick.c \
  $(LIB_DIR)/bitset.c \
  $(LIB_DIR)/bloom_filter.c \
  $(LIB_DIR)/bplus_tree.c \
  $(LIB_DIR)/circular_buffer.c \
  $(LIB_DIR)/common.c \
  $(LIB_DIR)/deque.c \
  $(LIB_DIR)/disjoint_set.c \
  $(LIB_DIR)/dlist.c \
  $(LIB_DIR)/fenwick_tree.c \
  $(LIB_DIR)/hash_table.c \
  $(LIB_DIR)/heap.c \
  $(LIB_DIR)/history.c \
  $(LIB_DIR)/list.c \
  $(LIB_DIR)/context.c \
  $(LIB_DIR)/diagnostic.c \
  $(LIB_DIR)/error.c \
  $(LIB_DIR)/graph.c \
  $(LIB_DIR)/iter.c \
  $(LIB_DIR)/pairing_heap.c \
  $(LIB_DIR)/persistent_rbt.c \
  $(LIB_DIR)/persistent_trie.c \
  $(LIB_DIR)/queue.c \
  $(LIB_DIR)/rbt.c \
  $(LIB_DIR)/radix_trie.c \
  $(LIB_DIR)/ring_list.c \
  $(LIB_DIR)/skip_list.c \
  $(LIB_DIR)/splay_tree.c \
  $(LIB_DIR)/stack.c \
  $(LIB_DIR)/status.c \
  $(LIB_DIR)/string_map.c \
  $(LIB_DIR)/str.c \
  $(LIB_DIR)/str_algo.c \
  $(LIB_DIR)/str_io.c \
  $(LIB_DIR)/str_io_posix.c \
  $(LIB_DIR)/str_unicode.c \
  $(LIB_DIR)/treap.c \
  $(LIB_DIR)/trie.c \
  $(LIB_DIR)/art.c \
  $(LIB_DIR)/bitvector.c \
  $(LIB_DIR)/fibonacci_heap.c \
  $(LIB_DIR)/hamt.c \
  $(LIB_DIR)/order_statistic_tree.c \
  $(LIB_DIR)/rope.c \
  $(LIB_DIR)/segment_tree.c \
  $(LIB_DIR)/wavelet_tree.c \
  $(LIB_DIR)/unicode_runtime.c \
  $(LIB_DIR)/vec.c \
  $(LIB_DIR)/array_deque.c \
  $(LIB_DIR)/sparse_set.c \
  $(LIB_DIR)/slot_map.c \
  $(LIB_DIR)/indexed_heap.c

GEN_SRC_C := $(BUILD_DIR)/unicode_tables.c
GEN_SRC_H := $(BUILD_DIR)/unicode_tables.h

# Sources maintained to the canonical clang-format style. `make format` and
# `make format-check` operate on these. The wider legacy tree can be formatted
# explicitly with `make format-all` when desired.
FORMAT_SRC := \
  $(LIB_DIR)/art.c $(LIB_DIR)/art.h \
  $(LIB_DIR)/bitvector.c $(LIB_DIR)/bitvector.h \
  $(LIB_DIR)/fibonacci_heap.c $(LIB_DIR)/fibonacci_heap.h \
  $(LIB_DIR)/hamt.c $(LIB_DIR)/hamt.h \
  $(LIB_DIR)/order_statistic_tree.c $(LIB_DIR)/order_statistic_tree.h \
  $(LIB_DIR)/rope.c $(LIB_DIR)/rope.h \
  $(LIB_DIR)/segment_tree.c $(LIB_DIR)/segment_tree.h \
  $(LIB_DIR)/wavelet_tree.c $(LIB_DIR)/wavelet_tree.h \
  $(LIB_DIR)/indexed_heap.c $(LIB_DIR)/indexed_heap.h \
  $(LIB_DIR)/slot_map.c $(LIB_DIR)/slot_map.h \
  $(LIB_DIR)/sparse_set.c $(LIB_DIR)/sparse_set.h \
  $(LIB_DIR)/array_deque.c $(LIB_DIR)/array_deque.h \
  $(LIB_DIR)/vec.c $(LIB_DIR)/vec.h \
  $(TEST_DIR)/art_test.c $(TEST_DIR)/bitvector_test.c \
  $(TEST_DIR)/fibonacci_heap_test.c $(TEST_DIR)/hamt_test.c \
  $(TEST_DIR)/order_statistic_tree_test.c $(TEST_DIR)/rope_test.c \
  $(TEST_DIR)/segment_tree_test.c $(TEST_DIR)/wavelet_tree_test.c \
  $(TEST_DIR)/graph_test.c $(TEST_DIR)/persistent_rbt_test.c \
  $(TEST_DIR)/persistent_trie_test.c \
  $(EXAMPLE_DIR)/intrusive_list.c $(EXAMPLE_DIR)/avl_set.c \
  $(EXAMPLE_DIR)/heap_priority_queue.c $(EXAMPLE_DIR)/art_dictionary.c \
  $(EXAMPLE_DIR)/sequence_containers.c $(EXAMPLE_DIR)/succinct_structures.c

# Every hand-written source and header, excluding generated tables.
FORMAT_ALL_SRC := ds.h \
  $(filter-out $(LIB_DIR)/unicode_tables.c $(LIB_DIR)/unicode_tables.h \
               $(LIB_DIR)/unicode_tables_glue.c, \
    $(wildcard $(LIB_DIR)/*.c) $(wildcard $(LIB_DIR)/*.h)) \
  $(wildcard $(TEST_DIR)/*.c) $(wildcard $(TEST_DIR)/*.cpp) \
  $(wildcard $(EXAMPLE_DIR)/*.c)

TEST_UCD_DIR := $(TEST_DIR)/ucd
TEST_UCD_FILES := \
  $(TEST_UCD_DIR)/UnicodeData.txt \
  $(TEST_UCD_DIR)/CaseFolding.txt \
  $(TEST_UCD_DIR)/CompositionExclusions.txt \
  $(TEST_UCD_DIR)/GraphemeBreakProperty.txt \
  $(TEST_UCD_DIR)/emoji-data.txt
TEST_GEN_SRC_C := $(TEST_BUILD_DIR)/unicode_tables.c
TEST_GEN_SRC_H := $(TEST_BUILD_DIR)/unicode_tables.h

LIB_OBJ      := $(LIB_SRC:.c=.o)
LIB_OBJ_SAFE := $(patsubst %.c,%_safe.o,$(LIB_SRC))
GEN_OBJ      := $(GEN_SRC_C:.c=.o)
TEST_GEN_OBJ := $(TEST_GEN_SRC_C:.c=.o)

LIB_STATIC      := libds.a
LIB_SHARED      := libds.so
LIB_STATIC_SAFE := libds_safe.a
LIB_SHARED_SAFE := libds_safe.so
TEST_LIB_STATIC := $(TEST_BUILD_DIR)/libds_test.a

TEST_SRC := $(wildcard $(TEST_DIR)/*_test.c)
TEST_BIN := $(patsubst $(TEST_DIR)/%.c,$(TEST_BUILD_DIR)/%,$(TEST_SRC))
EXAMPLE_BUILD_DIR := $(BUILD_DIR)/examples
EXAMPLE_SRC := $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BIN := $(patsubst $(EXAMPLE_DIR)/%.c,$(EXAMPLE_BUILD_DIR)/%,$(EXAMPLE_SRC))

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

.PHONY: all clean distclean unicode-data install uninstall test test-build check check-cpp sanitize test-safe examples valgrind valgrind-test valgrind-safe valgrind-examples format format-all format-check lint lint-all lint-style coverage coverage-detail coverage-html summary
all: $(LIB_STATIC) $(LIB_SHARED) $(LIB_STATIC_SAFE) $(LIB_SHARED_SAFE)

install: all
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)/lib $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0644 ds.h ds_safe.h $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -m 0644 $(LIB_DIR)/*.h $(DESTDIR)$(INCLUDEDIR)/lib
	$(INSTALL) -m 0644 $(LIB_STATIC) $(LIB_STATIC_SAFE) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0755 $(LIB_SHARED) $(LIB_SHARED_SAFE) $(DESTDIR)$(LIBDIR)

uninstall:
	rm -f $(DESTDIR)$(INCLUDEDIR)/ds.h $(DESTDIR)$(INCLUDEDIR)/ds_safe.h
	rm -rf $(DESTDIR)$(INCLUDEDIR)/lib
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC) $(DESTDIR)$(LIBDIR)/$(LIB_STATIC_SAFE)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED) $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_SAFE)


$(BUILD_DIR) $(DATA_DIR) $(TEST_BUILD_DIR) $(EXAMPLE_BUILD_DIR):
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

$(TEST_GEN_SRC_C) $(TEST_GEN_SRC_H): $(GEN_BIN) $(TEST_UCD_FILES) | $(TEST_BUILD_DIR)
	$(GEN_BIN) \
	  --unicode-data $(TEST_UCD_DIR)/UnicodeData.txt \
	  --case-folding $(TEST_UCD_DIR)/CaseFolding.txt \
	  --comp-excl    $(TEST_UCD_DIR)/CompositionExclusions.txt \
	  --gb-prop      $(TEST_UCD_DIR)/GraphemeBreakProperty.txt \
	  --emoji-data   $(TEST_UCD_DIR)/emoji-data.txt \
	  --out-c        $(TEST_GEN_SRC_C) \
	  --out-h        $(TEST_GEN_SRC_H)

%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%_safe.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D DS_THREAD_SAFE $(INCLUDES) -c -o $@ $<

$(GEN_OBJ): $(GEN_SRC_C) $(GEN_SRC_H)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(GEN_SRC_C)

$(TEST_GEN_OBJ): $(TEST_GEN_SRC_C) $(TEST_GEN_SRC_H)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(TEST_GEN_SRC_C)

$(LIB_STATIC): $(LIB_OBJ) $(GEN_OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(LIB_SHARED): $(LIB_OBJ) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(LIB_STATIC_SAFE): $(LIB_OBJ_SAFE) $(GEN_OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(LIB_SHARED_SAFE): $(LIB_OBJ_SAFE) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_LIB_STATIC): $(LIB_OBJ) $(TEST_GEN_OBJ) | $(TEST_BUILD_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(TEST_BUILD_DIR)/%: $(TEST_DIR)/%.c $(TEST_LIB_STATIC) | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $(INCLUDES) -o $@ $< $(TEST_LIB_STATIC) $(LDLIBS)

test-build: $(TEST_BIN)

test: $(TEST_BIN)
	@set -e; \
	for test_bin in $(TEST_BIN); do \
	  printf '\n== %s ==\n' "$$test_bin"; \
	  "$$test_bin"; \
	done

check: test

$(TEST_BUILD_DIR)/cpp_smoke: $(TEST_DIR)/cpp_smoke.cpp $(LIB_STATIC) | $(TEST_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIB_STATIC) $(LDLIBS)

check-cpp: $(TEST_BUILD_DIR)/cpp_smoke
	$(TEST_BUILD_DIR)/cpp_smoke

# Line-coverage report. Builds an instrumented copy of the library, links and
# runs every (non-thread-safe) test against it, then aggregates with gcovr.
# Requires gcovr (pip install gcovr). The plain-symbol tests are used because
# coverage of the _safe variants exercises the same lines plus locking.
COV_DIR := $(BUILD_DIR)/coverage
COV_FLAGS := -std=c99 -g -O0 --coverage $(INCLUDES)
COV_TEST_SRC := $(filter-out $(SAFE_TEST_SRC),$(TEST_SRC))

coverage: $(GEN_SRC_C)
	@command -v gcovr >/dev/null 2>&1 || { \
	  echo "gcovr not found; install it with: pip install gcovr"; exit 1; }
	@rm -rf $(COV_DIR)
	@mkdir -p $(COV_DIR)
	@echo "Instrumenting library ($(words $(LIB_SRC)) sources)..."
	@for src in $(LIB_SRC) $(GEN_SRC_C); do \
	  $(CC) $(COV_FLAGS) -c $$src -o $(COV_DIR)/$$(basename $$src .c).o; \
	done
	@echo "Building and running $(words $(COV_TEST_SRC)) tests under coverage..."
	@set -e; for t in $(COV_TEST_SRC); do \
	  bin=$$(basename $$t .c); \
	  $(CC) $(COV_FLAGS) -o $(COV_DIR)/$$bin $$t $(COV_DIR)/*.o $(LDLIBS); \
	  ( cd $(COV_DIR) && ./$$bin >/dev/null 2>&1 ) \
	    || echo "  WARNING: $$bin exited nonzero"; \
	done
	@echo ""
	@echo "==================== COVERAGE SUMMARY ===================="
	@gcovr --root . --filter '$(LIB_DIR)/.*' \
	  --gcov-ignore-parse-errors=all --print-summary \
	  --object-directory $(COV_DIR) $(COV_DIR) 2>/dev/null | tail -3
	@echo "=========================================================="
	@echo "Per-file detail: make coverage-detail"

coverage-detail: coverage
	@gcovr --root . --filter '$(LIB_DIR)/.*' --gcov-ignore-parse-errors=all \
	  --sort uncovered-number --object-directory $(COV_DIR) $(COV_DIR) 2>/dev/null

coverage-html: coverage
	@gcovr --root . --filter '$(LIB_DIR)/.*' --gcov-ignore-parse-errors=all \
	  --html-details -o $(COV_DIR)/index.html \
	  --object-directory $(COV_DIR) $(COV_DIR) 2>/dev/null
	@echo "HTML report: $(COV_DIR)/index.html"

# One-shot health verdict: build, tests, thread-safe tests, examples, lint,
# format, valgrind, and coverage, each reported pass/fail, with a final line
# coverage figure. Intended as the single "is everything OK?" command.
summary:
	@echo "Running full health check (this takes a few minutes)..."
	@pass="PASS"; fail="FAIL"; status=0; \
	gate() { name="$$1"; shift; \
	  if "$$@" >/dev/null 2>&1; then printf "  %-22s %s\n" "$$name" "$$pass"; \
	  else printf "  %-22s %s\n" "$$name" "$$fail"; status=1; fi; }; \
	advisory() { name="$$1"; shift; \
	  if "$$@" >/dev/null 2>&1; then printf "  %-22s %s\n" "$$name" "$$pass"; \
	  else printf "  %-22s %s\n" "$$name" "WARN (advisory)"; fi; }; \
	echo "==================== ds health summary ===================="; \
	echo "  correctness gates (determine the verdict):"; \
	gate "build (normal+safe)" $(MAKE) all; \
	gate "unit tests"          $(MAKE) test; \
	gate "thread-safe tests"   $(MAKE) test-safe; \
	gate "examples"            $(MAKE) examples; \
	if command -v $(VALGRIND) >/dev/null 2>&1; then \
	  gate "valgrind (tests)"  $(MAKE) valgrind-test; \
	  gate "valgrind (safe)"   $(MAKE) valgrind-safe; \
	else printf "  %-22s %s\n" "valgrind" "SKIP (not installed)"; fi; \
	echo "  style gates (advisory; tool-version sensitive):"; \
	if command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  advisory "lint (cppcheck)"  $(MAKE) lint; \
	else printf "  %-22s %s\n" "lint (cppcheck)" "SKIP (not installed)"; fi; \
	if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  advisory "format check"     $(MAKE) format-check; \
	else printf "  %-22s %s\n" "format check" "SKIP (not installed)"; fi; \
	echo "----------------------------------------------------------"; \
	if command -v gcovr >/dev/null 2>&1; then \
	  $(MAKE) coverage >/tmp/ds_cov.$$$$ 2>/dev/null; \
	  grep -E "^(lines|functions|branches):" /tmp/ds_cov.$$$$ | \
	    sed 's/^/  coverage /'; \
	  rm -f /tmp/ds_cov.$$$$; \
	else printf "  %-22s %s\n" "coverage" "SKIP (gcovr not installed)"; fi; \
	echo "=========================================================="; \
	if [ $$status -eq 0 ]; then \
	  echo "  VERDICT: ALL CORRECTNESS CHECKS PASSED"; \
	  echo "  (style WARNs above, if any, are advisory - see 'make lint' /"; \
	  echo "   'make format-check' for detail; they are tool-version sensitive)"; \
	else echo "  VERDICT: CORRECTNESS FAILURES PRESENT (see above)"; fi; \
	echo "=========================================================="; \
	exit $$status

$(EXAMPLE_BUILD_DIR)/%: $(EXAMPLE_DIR)/%.c $(TEST_LIB_STATIC) | $(EXAMPLE_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $(INCLUDES) -o $@ $< $(TEST_LIB_STATIC) $(LDLIBS)

examples: $(EXAMPLE_BIN)

clean:
	rm -f $(LIB_DIR)/*.o $(LIB_DIR)/*_safe.o $(BUILD_DIR)/*.o \
	      $(LIB_STATIC) $(LIB_SHARED) $(LIB_STATIC_SAFE) $(LIB_SHARED_SAFE) \
	      $(GEN_BIN)
	rm -f $(GEN_SRC_C) $(GEN_SRC_H)
	rm -rf $(TEST_BUILD_DIR)

distclean: clean
	rm -rf $(BUILD_DIR)
	rm -f valgrind.log *.log
	rm -f $(LIB_DIR)/*.gch *.gch


SAFE_TEST_BUILD_DIR := $(TEST_BUILD_DIR)/safe
TEST_LIB_STATIC_SAFE_TEST := $(SAFE_TEST_BUILD_DIR)/libds_test_safe.a
SAFE_TEST_SRC := \
  $(TEST_DIR)/thread_safe_smoke_test.c \
  $(TEST_DIR)/thread_safe_all_structures_test.c
TEST_BIN_SAFE := $(patsubst $(TEST_DIR)/%.c,$(SAFE_TEST_BUILD_DIR)/%,$(SAFE_TEST_SRC))

$(SAFE_TEST_BUILD_DIR):
	@mkdir -p $@

$(TEST_LIB_STATIC_SAFE_TEST): $(LIB_OBJ_SAFE) $(TEST_GEN_OBJ) | $(SAFE_TEST_BUILD_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(SAFE_TEST_BUILD_DIR)/%: $(TEST_DIR)/%.c $(TEST_LIB_STATIC_SAFE_TEST) | $(SAFE_TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) -D DS_THREAD_SAFE $(INCLUDES) -o $@ $< $(TEST_LIB_STATIC_SAFE_TEST) $(LDLIBS)

test-safe: $(TEST_BIN_SAFE)
	@set -e; \
	for test_bin in $(TEST_BIN_SAFE); do \
	  printf '\n== %s ==\n' "$$test_bin"; \
	  "$$test_bin"; \
	done

sanitize:
	$(MAKE) clean
	@status=0; \
	$(MAKE) CFLAGS='-std=c89 -Wall -Wextra -Werror -pedantic -pedantic-errors -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' TEST_CFLAGS='-std=c99 -Wall -Wextra -Werror -pedantic -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' LDLIBS='-pthread -lm -fsanitize=address,undefined' test || status=$$?; \
	$(MAKE) clean; \
	exit $$status

# Rewrite the maintained sources in place using clang-format.
format:
	@if ! command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  echo "$(CLANG_FORMAT) not found; install clang-format or set CLANG_FORMAT=/path/to/clang-format"; \
	  exit 1; \
	fi
	@echo "Formatting $(words $(FORMAT_SRC)) files with $$($(CLANG_FORMAT) --version)"
	@$(CLANG_FORMAT) -i $(FORMAT_SRC)
	@echo "Done."

# Rewrite every hand-written source in the tree (legacy modules included).
format-all:
	@if ! command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  echo "$(CLANG_FORMAT) not found; install clang-format or set CLANG_FORMAT=/path/to/clang-format"; \
	  exit 1; \
	fi
	@echo "Formatting $(words $(FORMAT_ALL_SRC)) files with $$($(CLANG_FORMAT) --version)"
	@$(CLANG_FORMAT) -i $(FORMAT_ALL_SRC)
	@echo "Done."

# Verify formatting without modifying files (suitable for CI). Fails if any
# file would change.
format-check:
	@if ! command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  echo "$(CLANG_FORMAT) not found; install clang-format or set CLANG_FORMAT=/path/to/clang-format"; \
	  exit 1; \
	fi
	@status=0; \
	for f in $(FORMAT_SRC); do \
	  if ! $(CLANG_FORMAT) --dry-run --Werror "$$f" >/dev/null 2>&1; then \
	    echo "needs formatting: $$f"; \
	    status=1; \
	  fi; \
	done; \
	if [ $$status -eq 0 ]; then echo "all files correctly formatted"; fi; \
	exit $$status

# Sources covered by the strict lint gate. These are the structures added in
# this line of work; they are held to a no-findings standard. The wider legacy
# tree is checked informationally via `make lint-all`.
LINT_SRC := \
  $(LIB_DIR)/art.c $(LIB_DIR)/bitvector.c $(LIB_DIR)/fibonacci_heap.c \
  $(LIB_DIR)/hamt.c $(LIB_DIR)/order_statistic_tree.c $(LIB_DIR)/rope.c \
  $(LIB_DIR)/segment_tree.c $(LIB_DIR)/wavelet_tree.c \
  $(LIB_DIR)/graph.c $(LIB_DIR)/persistent_rbt.c $(LIB_DIR)/persistent_trie.c

# Static analysis with cppcheck over the maintained structures. Warnings,
# performance, and portability findings fail the build.
lint:
	@if ! command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  echo "$(CPPCHECK) not found; install cppcheck or set CPPCHECK=/path/to/cppcheck"; \
	  exit 1; \
	fi
	$(CPPCHECK) --std=c89 --enable=warning,performance,portability \
	  --error-exitcode=1 --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  --suppress=checkersReport --suppress=unmatchedSuppression \
	  $(INCLUDES) $(LINT_SRC)

# Informational static analysis across the entire library (does not fail the
# build; legacy modules surface known macro/callback false positives).
lint-all:
	@if ! command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  echo "$(CPPCHECK) not found; install cppcheck or set CPPCHECK=/path/to/cppcheck"; \
	  exit 1; \
	fi
	-$(CPPCHECK) --std=c89 --enable=warning,performance,portability,style \
	  --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  --suppress=va_list_usedBeforeStarted \
	  $(INCLUDES) $(wildcard $(LIB_DIR)/*.c)

# Stylistic suggestions for the maintained structures; informational only.
lint-style:
	@if ! command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  echo "$(CPPCHECK) not found; install cppcheck or set CPPCHECK=/path/to/cppcheck"; \
	  exit 1; \
	fi
	-$(CPPCHECK) --std=c89 --enable=style --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  $(INCLUDES) $(LINT_SRC)


valgrind-test: test-build
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	  echo "valgrind not found; install Valgrind or set VALGRIND=/path/to/valgrind"; \
	  exit 127; \
	}
	@set -e; \
	for test_bin in $(TEST_BIN); do \
	  printf '\n== valgrind %s ==\n' "$$test_bin"; \
	  $(VALGRIND) $(VALGRIND_FLAGS) "$$test_bin"; \
	done

valgrind-safe: test-safe
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	  echo "valgrind not found; install Valgrind or set VALGRIND=/path/to/valgrind"; \
	  exit 127; \
	}
	@set -e; \
	for test_bin in $(TEST_BIN_SAFE); do \
	  printf '\n== valgrind %s ==\n' "$$test_bin"; \
	  $(VALGRIND) $(VALGRIND_FLAGS) "$$test_bin"; \
	done

valgrind-examples: examples
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	  echo "valgrind not found; install Valgrind or set VALGRIND=/path/to/valgrind"; \
	  exit 127; \
	}
	@set -e; \
	for example_bin in $(EXAMPLE_BIN); do \
	  printf '\n== valgrind %s ==\n' "$$example_bin"; \
	  $(VALGRIND) $(VALGRIND_FLAGS) "$$example_bin"; \
	done

valgrind: valgrind-test valgrind-safe valgrind-examples
