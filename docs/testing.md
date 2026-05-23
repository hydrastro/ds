# Testing and validation

This project has several validation layers. Use the smallest target that proves
what you changed, then run the broader targets before submitting.

## Normal tests

```sh
make test
```

Builds every `test/*_test.c` binary and runs them sequentially.

## Thread-safe tests

```sh
make test-safe
```

Builds the safe library variant and runs thread-safe smoke/regression tests.
This is especially important for code that touches `LOCK`, `UNLOCK`, recursive
walks, or APIs that call other locked APIs internally.

## Sanitizers

```sh
make sanitize
```

Runs the test suite under AddressSanitizer and UndefinedBehaviorSanitizer.
This target is good at catching use-after-free, invalid access, allocator
mismatches, and undefined behavior.

## Valgrind

```sh
make valgrind
```

Runs:

```txt
make valgrind-test
make valgrind-safe
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

## Examples

```sh
make examples
```

This only builds examples. It is useful for catching public-header or link
breakage in user-facing code.

## Review-regression tests

The review-driven stabilization tests are intentionally focused on bug classes
that ordinary examples miss:

- lock/unlock balance in safe builds
- B-tree/AVL walks under `DS_THREAD_SAFE`
- heap index invariants
- string-map update ownership
- allocator hardening paths
- history oracle/randomized replay
- persistent trie/RBT version isolation

When fixing a review item, add a regression test near the bug's natural module
or in `test/stability_review_test.c` / `test/thread_safe_all_structures_test.c`.
