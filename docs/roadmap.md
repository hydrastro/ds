# Roadmap and current status

This file tracks the current direction of `ds`: an intrusive,
allocator-aware, diagnostic-rich, versionable C data-structure library.

## Implemented foundation

- `ds_status_t`, `ds_error_t`, `ds_diagnostic_t`, and `ds_context_t`
- diagnostic sinks and diagnostic collectors
- context-aware arena, pool, and debug allocators
- config-based hash table API
- config-based trie API
- `ds_`-prefixed APIs for new hash/trie surfaces
- generic iterator shell
- intrusive ordered skip list with search/remove/walk/clone
- intrusive circular buffer (ring buffer) with optional overwrite mode
- disjoint set (union-find) with path compression and union by rank
- Fenwick tree (binary indexed tree) with point update and range sum
- intrusive splay tree with bottom-up splaying
- intrusive treap (randomized BST) with priority heap ordering
- intrusive pairing heap with decrease-key and merge
- intrusive circular singly linked list (ring list) with rotate
- dynamic bitset with rank/popcount, find-first/next, and set algebra
- Bloom filter sized from expected item count and target false-positive rate
- path-compressed radix trie over byte keys (upsert insert, prune/merge remove)
- intrusive B+ tree with linked leaves, range scans, and optional B* split policy
- Aho-Corasick automaton for multi-pattern string matching with failure links
- succinct bitvector with two-level rank/select index (rank0/1, select0/1)
- wavelet tree over the bitvector with access, rank, select, and range quantile
- order-statistic tree (size-augmented AVL) with select(k) and rank(key)
- segment tree with lazy propagation (range add/assign, range sum/min/max)
- hash array mapped trie (HAMT) with 32-way bitmap + popcount sparse nodes
- intrusive Fibonacci heap with lazy insert, consolidation, and cascading cuts
- rope (balanced tree of string chunks) with O(log n) insert/delete/concat/split
- adaptive radix tree (ART) with Node4/16/48/256 adaptive sizing and path
  compression (portable scalar implementation)
- string map/string set convenience wrappers
- by-value vector with growable or fixed caller-buffer storage, heapsort and
  binary search
- by-value array deque (growable ring) with fixed-buffer and overwrite modes
- sparse set (Briggs-Torczon) with O(1) insert/remove/contains/clear
- generational slot map with stale-handle detection
- indexed d-ary heap with update/remove by id (decrease-key)
- min-max heap (double-ended priority queue)
- sparse table: idempotent (min/max/gcd) and disjoint (any associative op)
- union-find with snapshot/rollback
- hierarchical timer wheel with intrusive timers and no allocation
- bit-packed integer array (k-bit values)
- Robin Hood hash map with by-value keys/values and backward-shift deletion
- fixed-capacity LRU cache (no allocation after init)
- HyperLogLog, Count-Min sketch and cuckoo filter (caller-supplied hashes)
- bip buffer and gap buffer
- SIEVE cache, monotonic queue, crit-bit tree, implicit k-d tree
- trie child stores over rh_map and sorted vectors (pluggable store ops)
- implicit treap with pluggable monoid summaries and lazy tags
- augmented treap (pluggable subtree summaries, guided descent) and the
  interval tree built on it
- `algo/graph_view`: graph algorithms over a pluggable graph view
- `algo/sort_classic` (twelve classic sorts) and `algo/stringology`
  (matching, suffix/LCP arrays, palindromes, Lyndon words, edit distances)
- `algo/`: sorting and selection, searching, sequence and sorted-set
  operations, hashes and checksums, bit manipulation, xoshiro128** RNG and
  alias sampling, number theory and compensated numerics
- intrusive binomial heap with meld, decrease/update/remove by handle
- meldable heap family (leftist, weight-biased, skew, randomized) with pluggable shape policy
- weak heap and weak-heap sort
- van Emde Boas tree with lazily allocated clusters
- radix heap (monotone priority queue)
- calendar queue with pluggable width estimation
- Swiss-table hash map
- open addressing with pluggable probe sequences and FCFS/LCFS/Robin Hood disciplines
- bucketized d-ary cuckoo hashing with BFS insertion and a stash
- coalesced hashing with cellar
- Litwin linear hashing
- extendible hashing
- `algo/hash_family`: Pearson, universal and tabulation hashing, rolling hashes
- `algo/perfect_hash`: order-preserving minimal perfect hashing
- invertible Bloom lookup table
- Merkle tree with audit proofs
- `algo/lsh`: MinHash, banding and SimHash
- `algo/flow`: max flow (four algorithms), min cut, min-cost flow, connectivity
- `algo/matching`: bipartite, assignment, general and stable matchings
- `algo/graph_struct`: articulation/bridges/blocks, Euler, condensation, closure/reduction
- `algo/graph_path`: Johnson, DAG paths, A*, Yen, arborescence, Kruskal/Boruvka, Chinese postman
- `algo/geometry`: hulls, calipers, closest pair, kernels, triangulation, rasterization
- `algo/combinatorics`: ranking, SJT, partitions, n queens, knight's tours, Hanoi
- `algo/knapsack`: knapsack variants, bin/strip packing, cutting stock, matrix chain
- `algo/number_theory`: CRT, Fibonacci, Ackermann, day-of-week rules
- `algo/numerical`: roots, CORDIC, Gamma, Stirling, BBP digits
- `algo/string_search`: seventeen exact matching algorithms and k-mismatch search
- inversion list: range sets as sorted boundaries with linear-time boolean merges and a fixed-buffer mode
- k-way merge: tournament trees, five selection policies, optimal merge plans
- scan: prefix computations over a pluggable (non-commutative) monoid, parallel prefix algorithms, multiprefix
- select: FIND, MODIFIND, Floyd-Rivest, BFPRT and randomized selection, mode
- search extensions: jump/block, Fibonaccian, ternary (sorted, peak, real minimum), interpolation-sequential and Bond sequential search
- sort_misc: pluggable quicksort, dual-pivot, k-way and adaptive merge sorts, strand/bingo sorts, Dutch national flag
- sort_dist: distribution sorts over pluggable key callbacks, byte-string radix sorts, blind sort
- sort_heap: smoothsort, adaptive heap sort, dominance tree, tournament and tree sorts, JSort
- external sort: pluggable tapes, balanced/polyphase/cascade/oscillating merging, replacement selection, external radix sort and quicksort
- random extensions: linear congruential generators with the Hull-Dobell test, 2D spectral test, reservoir sampling (R, L), selection sampling, von Neumann unbiased coin
- shadow heap: binary heap with a lazily merged shadow table; shadow merge and shadow merge insert
- string_approx: alignment, approximate matching, common substrings/super-strings, similarity measures
- prefix codes: Huffman (k-ary, length-limited), Shannon-Fano, order-preserving Huffman, coding trees, static Huffman compression
- compress: arithmetic coding, LZW (space-constrained), move-to-front, BSTW, star encoding
- adaptive Huffman coding: FGK and Vitter's algorithm
- phonetic coding: Soundex, NYSIIS, Metaphone, Double Metaphone, Caverphone
- automaton: DFAs, NFAs, subset construction, Hopcroft minimization, products, Mealy/Moore machines, finite state transducers
- suffix automaton (DAWG) and compact DAWG
- suffix trees (Ukkonen, generalized), Burrows-Wheeler transform, inverse suffix array
- BDD: reduced ordered binary decision diagrams with ITE, apply, quantification and sat counting
- `make test`, `make sanitize`, `make examples`, and
  `make valgrind`

## Implemented persistent/versioned structures

### Persistent trie

The persistent trie is structurally shared and versioned. It supports:

- insert
- exact-key remove
- prefix remove / subtree clear
- get
- prefix existence
- prefix counting
- longest-prefix lookup
- prefix traversal
- inclusive lexicographic range traversal
- prefix copy
- prefix move
- version retain/release

### Persistent red-black tree

The persistent red-black tree is a structurally shared, refcounted,
path-copying left-leaning red-black tree. It supports:

- insert
- remove
- delete-min
- delete-max
- get
- ordered traversal
- inclusive range traversal
- floor / ceiling
- predecessor / successor
- rank / select
- size
- validation
- version retain/release

## Implemented history/timeline support

The history module supports:

- branches with parent/fork relationships
- acyclic timeline model
- retroactive operation insertion/deletion
- `snapshot_at` and `query_at`
- checkpoint replay
- transaction batching
- branch-local operation serialization/deserialization
- full multi-branch serialization/deserialization
- portable little-endian metadata helpers
- owned in-memory archive object
- deterministic operation-log branch merge
- callback-driven merge policy hooks
- randomized oracle testing

Semantic conflict resolution remains adapter-owned. The history layer can order,
copy, serialize, merge, and replay operations, but it should not guess what two
domain-specific payloads mean.

## Implemented graph support

The graph module supports:

- directed vertices/edges
- weighted edges
- edge update/remove
- edge visitation
- BFS
- DFS
- topological sort
- Dijkstra shortest paths
- Bellman-Ford shortest paths and negative-cycle detection
- weakly connected components
- strongly connected components
- minimum spanning tree / forest over the graph's weak undirected view

## Review-driven stabilization status

The review-driven stabilization passes fixed the major correctness bugs found in
legacy structures and the new high-level modules, including:

- bounded error/diagnostic formatting
- safe lock macro wrapping
- recursive mutex attribute cleanup
- AVL rebalance, clone, postorder, rotation, and destroy callback issues
- B-tree lock-balance and walk-helper regressions
- heap index and growth issues
- list/dlist/queue/stack size and lock-balance issues
- string-map update ownership and thread-safe TOCTOU
- graph reverse-edge algorithm complexity
- pool/debug allocator hardening
- README/Makefile drift

The test suite includes focused review-regression tests.

## Threading model

The optional thread-safe build (`DS_THREAD_SAFE`, `ds_safe.h`, `libds_safe`)
was removed. Per-call mutexes could not make check-then-act sequences atomic,
pointers returned by accessors outlived the lock, the struct layout differed
between the two builds under one type name, and several modules were never
covered. Containers are now externally synchronized; lock-free structures
designed for concurrency belong in a dedicated module.

## DADS coverage

`docs/dads-coverage.md` classifies every NIST DADS entry as done, todo
(with its target module) or definition-only, and is the checklist for
the remaining algorithm and data-structure families.

## Still open / future work

High-value work that remains intentionally open:

- migrate every legacy sentinel-returning API to `ds_status_t` plus
  out-parameters while keeping compatibility wrappers
- add `void *user` callback context to older compare/destroy APIs
- continue migrating older modules to `ds_context_t` allocation internally
- document or strengthen iterator synchronization guarantees per container
- add tombstone-density compaction for probing hash tables
- add compressed/radix storage as a persistent-trie backend
- add richer history semantic conflict adapters for common containers
- add file-backed archive helpers on top of the existing history archive API
- make very deep list/tree traversals iterative where practical
- add more graph algorithms only after the current API stabilizes
