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

- `lib/association_list`: association lists (shadowing bindings) and property lists
- `lib/bang_file`: EXCELL, BANG files and R-files
- `lib/binary_tree_repr`: first child-next sibling (filial-heir) trees and doubly-chained (de la Briandais) tries, stack trees
- `lib/bsp_tree`: BSP-trees and cell trees
- `lib/buddy_allocator`: binary buddy system allocator (in-place realloc, context hook)
- `lib/cactus_stack`: cactus (saguaro, spaghetti) stacks with reference-counted shared frames
- `lib/d_tree`: D-trees (planar point location over subdivisions)
- `lib/diet`: discrete interval encoding trees (integer sets as merged intervals)
- `lib/dynamic_hash`: virtual hashing, expandable hashing and hashbelts
- `lib/elastic_trie`: bucket tries (compact trie for b = 1, elastic buckets) and blind tries
- `lib/enfilade`: enfilades (width-summarised sequences with retrieve and rearrange)
- `lib/grid_file`: fixed grids, grid files, twin, two-level and multilayer grid files
- `lib/hash_heap`: hash heaps (priority queues addressable by key)
- `lib/hb_tree`: hB-trees, buddy trees, BD-trees, GBD-trees, BV-trees
- `lib/jump_list`: jump lists (uniquely represented, O(n^(1/3)))
- `lib/k2_tree`: k^2-trees (compact adjacency matrices)
- `lib/kdb_tree`: k-d-B-trees, adaptive k-d trees, extended k-d trees, skd-trees
- `lib/ld_signature`: linked data signatures, SHA-256, HMAC-SHA256
- `lib/multiset`: hash multisets (bags) with sum, union, intersection, difference and inclusion
- `lib/mway_btree`: B-trees of any order m (2-3 and 2-3-4 tree presets)
- `lib/p_tree`: P-trees (k-DOP bounded R-trees)
- `lib/pagoda`: pagoda meldable priority queues
- `lib/quadtree`: region trees (PR quadtree, octree, bintree), point quadtrees, linear (Morton) quadtrees
- `lib/rbst`: randomized binary search trees (Martinez-Roura) with select/rank, split and join
- `lib/rtree`: R-trees (Guttman, R*) and bulk R+-trees
- `lib/scapegoat_tree`: scapegoat trees with pluggable α
- `lib/self_org_list`: self-organizing sequential search (move-to-front, transpose, frequency count, move-ahead-k, custom) and move-to-root / simple-exchange trees
- `lib/signature_file`: superimposed codes and signature files
- `lib/sorted_list`: sorted arrays (binary search, bounds, merge), ordered linked lists and unsorted lists
- `lib/sparse_matrix`: orthogonal-list sparse matrices (sum, product, transpose, CSR), ragged matrices, huge sparse arrays
- `lib/spiral_storage`: spiral storage (Martin's directory-free dynamic hashing)
- `lib/threaded_tree`: fully and right-threaded binary search trees, stackless (Cupif-Giannini / Morris) traversal
- `lib/tst`: ternary search trees with prefix, partial-match and near-neighbour searches
- `lib/two_choice_map`: d-choice (2-choice) and d-left hash maps
- `lib/ub_tree`: universal B-trees (Z-order, BIGMIN) and unlimited branching trees
- `lib/wb_tree`: weight-balanced BB[α] trees with pluggable (δ, γ), select/rank and root balance
- Array deque (by-value ring that grows or lives in a fixed buffer, overwrite mode)
- Augmented treap (ordered map with pluggable subtree summaries, rank/select, range summary, guided descent)
- AVL tree
- Adaptive radix tree (ART, adaptive node sizing + path compression)
- Aho-Corasick automaton (multi-pattern string matching)
- Binary decision diagrams (reduced ordered, hash-consed, pluggable variable order, iterative ITE/apply, quantifiers, sat counting)
- Binary search tree
- Binomial heap (intrusive mergeable heap; decrease, update and remove by handle)
- Bip buffer (circular byte buffer with contiguous reserve/read regions)
- Bitset (dynamic bit array)
- Bitvector with rank/select (succinct, two-level index)
- Bloom filter (probabilistic membership)
- B-tree
- B+ tree (with optional B* split policy)
- Calendar queue (event-time priority queue, pluggable width policy)
- Circular buffer (ring buffer)
- Coalesced hashing (cellar, LISCH/EISCH, tombstone-free removal)
- Count-Min sketch (frequency estimates, conservative update, mergeable)
- Crit-bit tree (binary PATRICIA string map, sorted and prefix iteration)
- Cuckoo filter (membership with deletion, 16-bit fingerprints, ~95% load)
- Cuckoo hash map (2-4 ways, 1-8 slots per bucket, BFS displacement, stash)
- Deque
- Disjoint set (union-find)
- Doubly linked list
- Extendible hashing (directory doubling/halving, bucket split/merge)
- Fenwick tree (binary indexed tree)
- Fibonacci heap (O(1) amortized insert/merge/decrease-key)
- Finite automata (DFA/NFA, subset construction, Hopcroft minimization, products, union, equivalence) and transducers (Mealy, Moore, FST)
- Gap buffer (text buffer with O(1) edits at the cursor)
- Hash array mapped trie (HAMT, bitmap + popcount sparse nodes)
- Hash table
- Heap
- HyperLogLog (distinct counting in 2^p bytes, mergeable)
- Implicit treap (sequence with pluggable monoid aggregates and lazy tags; split, append, reverse)
- Indexed d-ary heap (priority queue with decrease/increase-key by id)
- Interval tree (closed intervals of any endpoint type, overlap and stabbing queries)
- Inversion list (range set as sorted boundaries: splice updates, boolean merges by truth table, rank/select, bit-run conversion)
- Invertible Bloom lookup table (IBLT: listing, set reconciliation by subtraction)
- k-d tree (implicit static tree; nearest, k-nearest, box and radius queries)
- Linear hashing (Litwin; split by load or on overflow, incremental growth and shrink)
- LRU cache (fixed capacity, by-value entries, no allocation after init)
- Meldable heaps with pluggable shape policy: leftist, weight-biased leftist, skew, randomized (intrusive)
- Merkle tree (pluggable digest, RFC 6962 or Bitcoin odd-node rule, append/update, audit proofs)
- Min-max heap (double-ended priority queue by value)
- Monotonic queue (sliding-window min/max in amortized O(1))
- Open-addressing map with pluggable probe sequence (linear, quadratic, double hashing) and discipline (FCFS, LCFS, Robin Hood)
- Order-statistic tree (size-augmented AVL: select/rank)
- Packed integer array (k-bit values packed into words)
- Pairing heap
- Radix heap (monotone priority queue over unsigned long keys)
- Radix trie (path-compressed)
- Robin Hood hash map (by-value keys/values, backward-shift deletion, fixed-buffer mode)
- Rope (chunked string with O(log n) splice)
- Segment tree (lazy propagation: range add/assign, range sum/min/max)
- Shadow heap (binary heap with an unordered insertion table, shadow merge insert, shadow merge, pluggable threshold)
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
- Suffix automaton / DAWG (online construction, occurrence counts, distinct substrings, LCS) and compact DAWG
- Suffix tree (Ukkonen; generalized multi-string trees, occurrences, longest repeat/common substring, suffix array) plus BWT, inverse BWT and inverse suffix array
- Swiss table (Abseil-style control bytes, word-parallel group probing, in-place tombstone rehash, fixed-buffer mode)
- Timer wheel (hierarchical, intrusive timers, O(1) start/cancel, no allocation)
- Treap
- Trie
- Trie child stores: Robin Hood map and sorted vector (pluggable via ds_trie_store_ops_t)
- Union-find with rollback (snapshot/undo of unions)
- van Emde Boas tree (O(log log U) ordered integer set and priority queue, lazy clusters)
- Vector (by value; growable or fixed caller buffer, heapsort, binary search)
- Wavelet tree (succinct sequence: access/rank/select/quantile)
- Weak heap (by value, fixed-buffer mode) and weak-heap sort

Algorithms (`algo/`, umbrella header `ds_algo.h`, built into the same library):

- sorting: insertion, Shell (Ciura gaps), heap, intro, stable merge (scratch
  buffer), in-place stable (SymMerge), LSD radix by key
- selection: nth_element (introselect), partial_sort (top k)
- searching: lower/upper bound, equal range, binary, exponential (galloping), interpolation, jump/block, Fibonaccian, ternary, interpolation-sequential, Bond two-key search, find/count/min/max
- sequences: rotate, next/prev permutation, shuffle, perfect shuffle (in place, O(n)), unique, (stable) partition, merge and multiset set operations, inversions, LIS, majority vote, max subarray
- hashing: FNV-1a, MurmurHash3, xxHash32, integer mixer; checksums CRC-32, CRC-32C, Adler-32, Fletcher-16
- bits: popcount, clz/ctz, parity, power-of-two and log2 helpers, rotations, bit reversal, Gray code, Morton (Z-order) codes
- random: xoshiro128** generator with jump, unbiased bounded/range draws, uniform/normal/exponential/Bernoulli/Zipf variates, Vose alias tables, linear congruential generators with full-period and 2D spectral tests, reservoir (R, L) and selection sampling, von Neumann unbiased coin
- numeric: gcd (Euclid, binary), lcm, extended gcd, overflow-free mulmod/powmod/modinv, isqrt, deterministic Miller-Rabin, sieve, Kahan-Neumaier sum, Welford mean/variance
- graphs over any representation (ds_gview_t view, CSR helper): BFS, Dijkstra, Bellman-Ford with negative-cycle detection, topological sort, Tarjan SCC (iterative), bipartite check, Prim MST forest, Floyd-Warshall with paths
- classic sorts: bubble, cocktail shaker, selection, gnome, binary insertion, comb, odd-even transposition, cycle (minimum writes), pancake, bitonic (any n), stooge, counting sort
- strings over bytes: KMP, Rabin-Karp, prefix and Z functions, suffix array (prefix doubling), LCP (Kasai), Manacher, minimal rotation, Lyndon factorization (Duval), Levenshtein, OSA/Damerau, LCS, Hamming
- hash families: Pearson, multiplicative (Knuth), order-preserving, universal (Carter-Wegman, multiply-shift, tabulation, twisted tabulation), rolling polynomial hash and buzhash
- perfect hashing: order-preserving minimal perfect hash (Czech-Havas-Majewski)
- locality-sensitive hashing: MinHash, banding, SimHash
- network flow: residual networks; Edmonds-Karp, Dinic, Malhotra-Kumar-Maheshwari and push-relabel max flow; min cut; min-cost flow; edge/vertex connectivity
- matching: Hopcroft-Karp, Hungarian (assignment), Edmonds blossom, Gale-Shapley stable marriage
- graph structure: DFS forest and edge classes, articulation points, bridges, biconnected components, Euler trails, condensation, transitive closure and reduction, feedback edge set, tree diameter and centroid
- graph paths: Johnson APSP, DAG shortest/longest (critical) paths, best-first/A*, Yen k shortest paths, all simple paths, Chu-Liu/Edmonds arborescence, Kruskal, Boruvka, Chinese postman
- geometry: distances (L_p, Hausdorff), predicates, convex hull (Andrew, Graham, Jarvis, quickhull), rotating calipers, closest pair, polygon kernel, optimal triangulation, slope selection, Bresenham and Wu rasterization
- combinatorics: binomials and counts, combination/permutation ranking, Johnson-Trotter, derangements, partitions, n queens, knight's tour, towers of Hanoi, Collatz
- knapsack and packing: 0/1, unbounded and fractional knapsack, bin packing policies (NF, FF, BF, WF, FFD, BFD), cutting stock, strip packing (NFDH, FFDH), matrix chain
- number theory: Chinese remainder theorem (any moduli), Fibonacci and k-bonacci, Ackermann and inverse Ackermann, Doomsday rule, Zeller's congruence
- numerical methods: Horner, bisection/secant/Newton roots, square and cube roots, CORDIC, Gamma (Lanczos), Stirling, BBP pi digits, mode, midrange
- exact string matching: brute force, Morris-Pratt, Boyer-Moore, Horspool, Raita, Quick Search, Smith, Zhu-Takaoka, Turbo-BM, Apostolico-Giancarlo, Apostolico-Crochemore, Not So Naive, Two Way, Shift-Or, DFA, Skip, Alpha Skip and KmpSkip Search, Colussi, Reverse Colussi, Galil-Giancarlo, Galil-Seiferas, Maximal Shift, Optimal Mismatch, Reverse Factor, Turbo Reverse Factor, matching on ordered alphabets, k-mismatch search, Commentz-Walter multi-pattern search
- `kway_merge`: tournament trees (loser/winner), k-way merge with loser-tree, winner-tree, heap, ideal and linear policies, optimal merge patterns
- `scan`: inclusive/exclusive/suffix/segmented scans, multiprefix, Hillis-Steele and Blelloch parallel prefix over a pluggable monoid; typed prefix sums
- `select`: FIND, MODIFIND, Floyd-Rivest, median of medians, Randomized-Select and introselect behind one dispatcher; median and mode
- `sort_misc`: configurable quicksort (pivot, partition, cutoff, introspection; q sort, qm sort, balanced), dual-pivot quicksort, two-/three-/k-way and adaptive merge sorts, strand, bingo, Dutch national flag, joke sorts (bogosort, bozo, permutation, lucky, taco)
- `sort_dist`: bucket/range/pigeonhole/rapid/restricted-universe, histogram (interpolation), address-calculation, flash, linear probing and distributive partitioning sorts; MSD radix, American flag, multikey quicksort and blind sort for byte strings; postman's, shuffle and J sort; UnShuffle sort (piles and a heap merge)
- `sort_heap`: smoothsort, adaptive heap sort, dominance tree sort, tournament sort, tree sort (plain or treap), JSort
- `external_sort`: tape ops with memory and FILE backends; balanced, polyphase (perfect distribution, dummy runs), cascade and oscillating merge sorts with load-sort or replacement-selection runs; external radix sort; external quicksort
- `string_approx`: edit scripts with pluggable costs, Needleman-Wunsch/Smith-Waterman with affine gaps, Sellers/Ukkonen and Myers approximate search, longest common substring, shortest common super-sequence/-string, heaviest common subsequence, dominant matches, Jaro-Winkler, Ratcliff/Obershelp
- `prefix_code`: binary/k-ary Huffman, Shannon-Fano and Garsia-Wachs alphabetic code lengths, length limiting, canonical/alphabetic codes, coding trees, bit I/O, static Huffman compression
- `compress`: Witten-Neal-Cleary arithmetic coding with pluggable models, LZW with freeze/reset dictionaries, move-to-front, BSTW, star encoding
- `adaptive_huffman`: dynamic Huffman coding with FGK or Vitter updates, fixed-array trees, invariant check
- `phonetic`: Soundex, NYSIIS, Metaphone, Double Metaphone, Caverphone 2.0 behind one dispatcher
- Cartesian trees: linear construction, RMQ via LCA, adaptive Cartesian tree sort
- Schorr-Waite (Deutsch-Schorr-Waite) stackless graph marking by pointer reversal
- game-tree search: minimax, alpha-beta, negamax with move ordering
- Fourier transforms: DFT, FFT (radix-2, Bluestein), FFT convolution, number-theoretic transform
- matrices: naive, blocked and Strassen multiplication; packed (strictly) lower/upper triangular matrices
- hidden Markov models: scaled forward-backward, posteriors, Viterbi, Baum-Welch
- PRAM simulations: pointer jumping, recursive doubling, shortcutting, list contraction, tree contraction
- lowest common ancestors: binary lifting, Euler tour + sparse table, Tarjan's offline LCA
- graph connectivity: reverse graphs, single-destination shortest paths, separation pairs, triconnectivity
- NP-hard graph problems: cliques, independent sets, vertex cover, (edge) colouring, chromatic number/index, Hamiltonian paths, path cover, feedback vertex set, (sub)graph isomorphism, bisection
- traveling salesman: Held-Karp, nearest neighbour, 2-opt, Christofides, Euclidean and bottleneck TSP; Clarke-Wright vehicle routing
- covering and location: set cover, set packing, (capacitated) facility location, discrete p-center, k-clustering
- Steiner trees: Dreyfus-Wagner, KMB 2-approximation, rectilinear (Hanan grid), Euclidean heuristic
- linear programming: two-phase simplex with duals, dual programs, branch and bound and Gomory cutting planes for (mixed) integer programs
- `algo/lattice`: LLL reduced bases, shortest vectors, spectral test in t dimensions, integer relations (PSLQ / Ferguson-Forcade, LLL)
- `algo/rice_integral`: Rice's method (Norlund-Rice integrals for alternating binomial sums)
- `algo/geo_opt`: geometric optimisation (Welzl, Megiddo prune and search, Seidel 2-D LP, Weiszfeld)
- `algo/space_order`: space ordering methods (row, row-prime, Morton, Hilbert, Gray, double Gray, U, Cantor, spiral; k-D Morton and Hilbert)

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

Formatting is clang-format's default LLVM style (there is no `.clang-format`
file: two-space indent, 80-column limit, pointers bound to the identifier). `make
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
