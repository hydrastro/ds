# ds

`ds` is an intrusive, allocator-aware C data-structure library with structured
diagnostics/errors, persistent/versioned containers, and a generic retroactive
history layer.

The library is written to keep the low-level C control surface visible while
also offering newer high-level wrappers such as config-based hash tables,
string maps/sets, persistent tries, persistent red-black trees, graph
algorithms, and timeline/history utilities.

## Features

Core mutable containers:

- Array deque (by-value ring that grows or lives in a fixed buffer, overwrite mode)
- AVL tree
- Adaptive radix tree (ART, adaptive node sizing + path compression)
- Aho-Corasick automaton (multi-pattern string matching)
- Binary search tree
- Bip buffer (circular byte buffer with contiguous reserve/read regions)
- Bitset (dynamic bit array)
- Bitvector with rank/select (succinct, two-level index)
- Bloom filter (probabilistic membership)
- B-tree
- B+ tree (with optional B* split policy)
- Circular buffer (ring buffer)
- Count-Min sketch (frequency estimates, conservative update, mergeable)
- Cuckoo filter (membership with deletion, 16-bit fingerprints, ~95% load)
- Deque
- Disjoint set (union-find)
- Doubly linked list
- Fenwick tree (binary indexed tree)
- Fibonacci heap (O(1) amortized insert/merge/decrease-key)
- Gap buffer (text buffer with O(1) edits at the cursor)
- Hash array mapped trie (HAMT, bitmap + popcount sparse nodes)
- Hash table
- Heap
- HyperLogLog (distinct counting in 2^p bytes, mergeable)
- Indexed d-ary heap (priority queue with decrease/increase-key by id)
- LRU cache (fixed capacity, by-value entries, no allocation after init)
- Min-max heap (double-ended priority queue by value)
- Monotonic queue (sliding-window min/max in amortized O(1))
- Order-statistic tree (size-augmented AVL: select/rank)
- Packed integer array (k-bit values packed into words)
- Pairing heap
- Radix trie (path-compressed)
- Robin Hood hash map (by-value keys/values, backward-shift deletion, fixed-buffer mode)
- Rope (chunked string with O(log n) splice)
- Segment tree (lazy propagation: range add/assign, range sum/min/max)
- SIEVE cache (FIFO with visited bits and a sweeping hand; no allocation after init)
- Singly linked list
- Circular singly linked list (ring list)
- Queue
- Red-black tree
- Skip list
- Slot map (generational handles that detect stale references, dense values)
- Sparse set (O(1) insert/remove/contains/clear over small integers)
- Sparse table (O(1) static range queries; idempotent and disjoint variants)
- Splay tree
- Stack
- Timer wheel (hierarchical, intrusive timers, O(1) start/cancel, no allocation)
- Treap
- Trie
- Union-find with rollback (snapshot/undo of unions)
- Vector (by value; growable or fixed caller buffer, heapsort, binary search)
- Wavelet tree (succinct sequence: access/rank/select/quantile)

Algorithms (`algo/`, umbrella header `ds_algo.h`, built into the same library):

- sorting: insertion, Shell (Ciura gaps), heap, intro, stable merge (scratch
  buffer), in-place stable (SymMerge), LSD radix by key
- selection: nth_element (introselect), partial_sort (top k)
- searching: lower/upper bound, equal range, binary, exponential (galloping), interpolation, find/count/min/max
- sequences: rotate, next/prev permutation, shuffle, unique, (stable) partition, merge and multiset set operations, inversions, LIS, majority vote, max subarray
- hashing: FNV-1a, MurmurHash3, xxHash32, integer mixer; checksums CRC-32, CRC-32C, Adler-32, Fletcher-16
- bits: popcount, clz/ctz, parity, power-of-two and log2 helpers, rotations, bit reversal, Gray code, Morton (Z-order) codes
- random: xoshiro128** generator with jump, unbiased bounded/range draws, uniform/normal/exponential/Bernoulli variates, Vose alias tables
- numeric: gcd (Euclid, binary), lcm, extended gcd, overflow-free mulmod/powmod/modinv, isqrt, deterministic Miller-Rabin, sieve, Kahan-Neumaier sum, Welford mean/variance

Newer high-level modules:

- `ds_status_t`, `ds_error_t`, `ds_diagnostic_t`, and `ds_context_t`
- context-backed arena, pool, and debug allocators
- config-based hash table and trie APIs
- custom hash-table bucket stores
- `ds_string_map_t` and `ds_string_set_t`
- structural-sharing persistent trie
- structural-sharing persistent red-black tree
- generic history/timeline layer with branching, retroactive edits,
  checkpoints, transactions, merge hooks, and portable archive helpers
- directed/weighted graph module with BFS, DFS, topological sort, Dijkstra,
  Bellman-Ford, connected components, strongly connected components, and MST
  helpers

## Build

### With Nix

```sh
nix build
```

### With Make

```sh
make
make test
```

By default, `make` builds the library:

```txt
libds.a
libds.so
```

Install and uninstall are available:

```sh
make install
make uninstall
```

## Testing and validation

```sh
make test        # build and run all normal tests
make sanitize    # run tests under ASAN/UBSAN
make examples    # build example programs
make valgrind    # run tests and examples under Valgrind
make check       # alias for make test
```

`make valgrind` runs each executable sequentially and prints a banner before
it runs each one. If Valgrind reports an error or definite/possible leak, the
current executable exits nonzero and the loop stops. The failed binary is the
last `== valgrind ... ==` banner printed.

The split Valgrind targets are:

```sh
make valgrind-test
make valgrind-examples
```

You can override the tool and flags:

```sh
make valgrind VALGRIND=/usr/bin/valgrind
make valgrind VALGRIND_FLAGS="--leak-check=full --error-exitcode=99"
```

## Health check and coverage

```sh
make summary         # one-shot verdict: build, tests, examples,
                     # lint, format, valgrind, and a coverage figure, each
                     # reported PASS/FAIL with a final overall verdict
make coverage        # line/function/branch coverage summary (needs gcovr)
make coverage-detail # per-file coverage, least-covered first
make coverage-html   # browsable HTML report under build/coverage/
```

`make summary` is the single "is everything OK?" command. It splits checks
into **correctness gates** (build, unit tests, examples, valgrind) that determine the exit status, and **style gates** (lint, format)
that are reported as advisory `WARN` because their output depends on the exact
`clang-format` / `cppcheck` version installed. A style WARN does not fail the
verdict or CI; run `make lint` / `make format-check` to see the detail. The
style configuration was developed against **clang-format 18** and **cppcheck
2.13**; other versions may format or warn slightly differently. To normalize
formatting to your local clang-format, run `make format`. Coverage targets
require `gcovr` (`pip install gcovr`); valgrind and the style tools are skipped
automatically if not installed.



```sh
make format        # clang-format the maintained sources in place
make format-check  # verify formatting without writing (CI-friendly, fails if dirty)
make format-all    # clang-format the entire hand-written tree (legacy included)
make lint          # cppcheck the maintained structures; warnings fail the build
make lint-style    # cppcheck style suggestions for the maintained structures
make lint-all      # informational cppcheck over the whole library
```

Formatting follows the `.clang-format` at the repository root (LLVM base,
two-space indent, 80-column limit, pointers bound to the identifier). `make
format` and `make lint` are scoped to the actively maintained structures so
they stay green in CI; the `-all` variants cover the wider tree on demand. Both
clang-format and cppcheck are optional: if the tool is absent, the target prints
an install hint and exits nonzero. Override the binaries with
`make format CLANG_FORMAT=clang-format-18` or `make lint CPPCHECK=/path/to/cppcheck`.


## Usage

Use the central header for the normal build:

```c
#include <ds.h>
```

Compile against an installed library with:

```sh
gcc -o example example.c -lds
```

For concrete usage, see:

```txt
examples/intrusive_list.c       # the embed-a-node intrusive pattern
examples/avl_set.c              # ordered set with an in-order walk
examples/heap_priority_queue.c  # binary heap as a min-priority-queue
examples/art_dictionary.c       # adaptive radix tree as a string map
examples/sequence_containers.c  # stack, queue, and deque side by side
examples/succinct_structures.c  # bitset, rank/select bitvector, wavelet tree
examples/hash_table_config.c
examples/string_map.c
examples/persistent_trie.c
examples/allocators.c
```

## API style

The library currently has two API generations.

Legacy APIs expose the original intrusive containers directly and often return
private sentinels such as `tree->nil` or `table->nil` for missing values. These
remain for compatibility, but new code should prefer the newer `ds_`-prefixed
APIs where available.

New APIs generally use:

```txt
ds_status_t return value
out-parameters for returned data
ds_context_t for allocation, diagnostics, and primary errors
config structs for callbacks and ownership policy
```

Example style:

```c
void *value;
ds_status_t status;

status = ds_string_map_get(map, "name", &value);
if (status == DS_OK) {
  /* value found */
} else if (status == DS_NOT_FOUND) {
  /* key missing */
}
```

## Diagnostics and errors

`ds` separates failure handling from observability:

- `ds_status_t` is the cheap control-flow result.
- `ds_error_t` is the primary failure object stored in a `ds_context_t`.
- `ds_diagnostic_t` is a structured event emitted to a diagnostic sink.

A successful operation may emit diagnostics. A failed operation returns a status
and may set one primary error in the context.

See `docs/diagnostics.md`.

## Ownership

By default, containers own only the internal memory they allocate. User keys,
values, payloads, and intrusive nodes are borrowed unless a config or destroy
callback says otherwise.

See `docs/ownership.md`.

## Thread-safety

Containers do no internal locking. The contract is the usual one for C and
C++ libraries: a ds object may be used by one thread at a time, and different
objects are independent. When threads share a container, synchronize around
whole sequences of operations (check-then-act, iterate-then-modify) with your
own lock, since per-call locking cannot make such sequences atomic.

Structures that are immutable after construction (for example a built sparse
table) may be queried from any number of threads at once.

For multi-threaded code that cares about diagnostics or last-error state, prefer
an explicit `ds_context_t` per thread or per object. The default context is a
process-global fallback.

## Documentation

Additional docs:

```txt
docs/api-overview.md
docs/diagnostics.md
docs/history.md
docs/ownership.md
docs/roadmap.md
docs/testing.md
```

## Contributing

Before submitting changes:

```sh
make test
make sanitize
make examples
make valgrind    # when Valgrind is installed
```

Also format C changes with `clang-format`.
