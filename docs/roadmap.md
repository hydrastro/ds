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
- `make test`, `make test-safe`, `make sanitize`, `make examples`, and
  `make valgrind`
- `ds_safe.h`, `libds_safe.a`, and `libds_safe.so`

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

The test suite now includes focused review-regression tests and multi-container
safe-build smoke coverage.

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
