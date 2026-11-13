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
- algo/string_search: KmpSkip, Colussi, Reverse Colussi, Galil-Giancarlo, Galil-Seiferas, Maximal Shift, Optimal Mismatch, Reverse Factor, Turbo Reverse Factor, ordered-alphabet matching, and Commentz-Walter multi-pattern search.
- lib/self_org_list: self-organizing lists with pluggable policies and move-to-root trees.
- lib/sorted_list: sorted arrays, ordered linked lists and unsorted lists.
- lib/binary_tree_repr: binary tree representation of trees and doubly-chained tries.
- lib/threaded_tree: threaded and right-threaded trees, stackless traversals.
- lib/wb_tree: weight-balanced BB[alpha] trees with order statistics.
- lib/rbst: randomized binary search trees with split, join and order statistics.
- lib/scapegoat_tree: scapegoat trees with pluggable alpha.
- lib/multiset: hash multisets with the bag algebra.
- lib/association_list: association lists and property lists.
- lib/cactus_stack: cactus stacks with reference-counted frames.
- lib/diet: discrete interval encoding trees.
- lib/tst: ternary search trees with partial-match and near-neighbour search.
- lib/two_choice_map: 2-choice / d-choice and d-left hashing.
- algo/cartesian_tree: Cartesian trees, RMQ and Cartesian tree sort.
- algo/schorr_waite: stackless pointer-reversal graph marking.
- algo/game: minimax, alpha-beta and negamax game-tree search.
- lib/sparse_matrix: orthogonal lists, ragged matrices, huge sparse arrays.
- lib/elastic_trie: bucket, compact, elastic-bucket and blind tries.
- lib/dynamic_hash: virtual hashing, expandable hashing, hashbelts.
- lib/mway_btree: order-m B-trees, 2-3 and 2-3-4 trees.
- lib/pagoda: pagoda meldable priority queues.
- lib/buddy_allocator: the binary buddy system.
- lib/hash_heap: hash heaps addressable by key.
- lib/enfilade: enfilades with retrieval by address and rearrange.
- lib/signature_file: superimposed coding and signature files.
- algo/fft: DFT, FFT for any length, convolution, NTT.
- algo/matrix: matrix multiplication and packed triangular matrices.
- algo/hmm: hidden Markov models, Viterbi and Baum-Welch.
- algo/parallel_sim: pointer jumping, recursive doubling, shortcutting, list and tree contraction.
- algo/lca: lowest common ancestors three ways.
- algo/graph_conn: single-destination shortest paths and triconnectivity.
- algo/graph_np: exact and approximate NP-hard graph algorithms.
- algo/tsp: TSP (exact, heuristic, Christofides), bottleneck TSP, VRP.
- algo/facility: set cover/packing, facility location, p-center, k-clustering.
- algo/steiner: graph, rectilinear and Euclidean Steiner trees.
- algo/lp: simplex, duality, MILP by branch and bound and cutting planes.
- lib/quadtree: PR quadtrees, octrees, bintrees, point and linear quadtrees.
- lib/rtree: R-trees, R*-trees and R+-trees.
- lib/kdb_tree: k-d-B-trees, adaptive, extended and spatial (skd) k-d trees.
- lib/grid_file: fixed-grid method, grid files, twin, two-level and multilayer grid files.
- lib/bang_file: EXCELL, BANG files and R-files.
- lib/hb_tree: hB-trees, buddy trees, BD-trees, GBD-trees and BV-trees.
- lib/bsp_tree: BSP-trees and cell trees (convex pieces in a line partition).
- lib/p_tree: P-trees (polyhedral trees).
- lib/d_tree: D-trees for planar point queries.
- lib/ub_tree: universal B-trees and unlimited branching trees.
- lib/k2_tree: k^2-trees for compact sparse matrices.
- lib/jump_list: jump lists.
- lib/ld_signature: linked data signatures (canonical N-Quads, SHA-256, HMAC).
- algo/lattice: LLL, shortest vectors, spectral test, integer relations.
- algo/rice_integral: Rice's method.
- algo/geo_opt: geometric optimisation problems.
- `algo/hash_family`: twisted tabulation hashing.
- `algo/sort_dist`, `algo/sort_misc`: UnShuffle sort, taco sort.
- `algo/sequence`: in-place perfect shuffle and unshuffle.
- `algo/random`: Zipfian distribution sampling (rejection-inversion).
- `lib/binary_tree_repr`: stack trees (check and build).
- algo/space_order: space ordering methods.
- lib/spiral_storage: spiral storage.
- lib/sstable: SSTables; format 3 carries an AMQ filter image from any backend.
- lib/zipper: zippers over persistent binary trees
- algo/markov: Markov chain analysis
- algo/anneal: simulated annealing
- lib/cellular_automaton: cellular automata
- lib/turing_machine: Turing machines
- lib/pushdown: pushdown automata and transducers
- lib/tree_automaton: finite tree automata
- algo/model_check: CTL model checking
- algo/tree_edit: tree and forest edit distance
- algo/visibility: visibility maps
- lib/inverted_index: inverted and forward indexes
- algo/dynamization: logarithmic-method dynamization
- lib/plop_hash: PLOP-hashing
- lib/confluent_list: confluently persistent lists
- lib/topology_tree: topology trees
- lib/checked: checked size arithmetic
- algo/weighted_matching: weighted general matching
- lib/map_iface: pluggable map capability
- lib/xfast_trie: X-fast tries on pluggable maps
- lib/yfast_trie: Y-fast tries
- lib/link_cut_tree: link-cut trees
- lib/lazy_segment_tree: generic lazy segment tree
- lib/elias_fano: Elias-Fano sequences
- lib/roaring: Roaring bitmaps with array/bitmap/run containers, rank/select, and/or/xor/andnot (run containers natively), optimize
- algo/robust_predicates: Shewchuk-style expansion arithmetic, filtered exact orient2d/3d, incircle, insphere
- lib/persistent_hamt: persistent CHAMP HAMT with refcounted sharing, canonical deletion, structural equality, in-place transient edits
- lib/pq_iface: addressable priority-queue ops table with eight heap backends and traits
- lib/fm_index: FM-index with sampled SA/ISA, occurrence-table and wavelet-matrix rank backends, configurable suffix array builder
- lib/euler_tour_tree: Euler-tour trees with component/subtree summaries and lazy actions, edge lookup via map_iface
- lib/persistent_vector: Clojure-style persistent vector with transients and configurable branching
- lib/ordered_iface: ordered-map ops table with split/join, sorted-array and treap backends
- lib/rank_select: rank/select ops table; plain directory, RRR and Elias-Fano backends
- lib/wavelet_matrix: wavelet matrix over pluggable rank/select bit vectors
- lib/finger_tree: Hinze-Paterson finger trees with caller-defined measures
- lib/pager: page stores and a pin/dirty/LRU buffer pool over map_iface
- lib/lsm_tree: LSM tree over SSTables with leveled compaction and merging iterators
- lib/paged_btree: pager-backed B+ tree with linked leaves and lazy deletion
- lib/replacement: replacement policies (LRU, FIFO, CLOCK, SIEVE) as an ops table
- lib/quotient_filter: quotient filter with delete and enumeration
- lib/xor_filter: Graf-Lemire xor filter
- lib/amq_iface: AMQ ops table with Bloom, cuckoo, quotient and xor backends; images (save, load with full validation)
- lib/adj_matrix: dense adjacency-matrix graph with sparse-set rows, graph-view adapter.
- lib/schema: element schema descriptors shared by containers (layout, ownership, transfer).
- algo/delaunay: Delaunay triangulation built on the robust predicates.
- lib/hollow_heap: one-tree hollow heap, pq_iface backend
- lib/louds: LOUDS succinct tree over the rank/select capability
- algo/voronoi: Voronoi diagram from the Delaunay triangulation, box-clipped cells
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
