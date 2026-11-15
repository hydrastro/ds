# Testing and validation

This project has several validation layers. Use the smallest target that proves
what you changed, then run the broader targets before submitting.

## Normal tests

```sh
make test
```

Builds every `test/*_test.c` binary and runs them sequentially.

## Sanitizers

```sh
make sanitize
```

Runs the test suite under AddressSanitizer and UndefinedBehaviorSanitizer.
This target is good at catching use-after-free, invalid access, allocator
mismatches, and undefined behavior. Every report is fatal
(`-fno-sanitize-recover=all`, in the library as in the tests): by default
UBSan only prints and lets a test pass.

## Valgrind

```sh
make valgrind
```

Runs:

```txt
make valgrind-test
make valgrind-examples
```

Each executable is run sequentially. The Makefile prints a banner before each
run, for example:

```txt
== valgrind build/test/hash_table_test ==
```

If Valgrind finds an error or definite/possible leak, it exits with the
configured `--error-exitcode`, the loop stops, and `make` fails. The failing
binary is the last banner printed.

Recommended logging:

```sh
make valgrind 2>&1 | tee valgrind.log
```

Useful search:

```sh
grep -n "ERROR SUMMARY\|definitely lost\|possibly lost\|== valgrind" valgrind.log
```

Valgrind may be overridden:

```sh
make valgrind VALGRIND=/usr/bin/valgrind
make valgrind VALGRIND_FLAGS="--leak-check=full --track-origins=yes --error-exitcode=99"
```

## Build checks

```sh
make check-headers
make check-cpp
make check-noinline
make check-o3
make format-check
```

- `check-headers` installs into a scratch prefix and compiles every
  installed header on its own, as a consumer without pkg-config sees it.
- `check-cpp` builds and runs a C++ program over the headers.
- `check-noinline` compiles every source at -O0 under the strict warnings:
  `-Waggregate-return` only reports functions that survive inlining, so a
  static helper returning a struct can hide at -O2.
- `check-o3` compiles every source at -O3, whose flow analysis reports
  uninitialized reads that -O2 misses.
- `format-check` checks every source with `clang-format`.

## Examples

```sh
make examples
```

This only builds examples. It is useful for catching public-header or link
breakage in user-facing code.

## Review-regression tests

The review-driven stabilization tests are intentionally focused on bug classes
that ordinary examples miss:

- heap index invariants
- string-map update ownership
- allocator hardening paths
- history oracle/randomized replay
- persistent trie/RBT version isolation

When fixing a review item, add a regression test near the bug's natural module
or in `test/stability_review_test.c`.
