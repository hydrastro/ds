# Roadmap

Implemented foundation:

- status/error/diagnostic/context layer
- config-based hash table and trie constructors
- `ds_`-prefixed APIs for hash table/trie additions
- iterator abstraction
- string map/string set wrappers
- generic graph module
- structural persistent trie module
- structural persistent red-black tree module
- history transaction batching
- history oracle randomized test
- `make sanitize` and `make test-safe`

## Current hardening status

The largest trie/RBT/history limitations have been addressed:

- Persistent trie supports structural-sharing insert, exact-key removal, prefix existence, longest-prefix lookup, prefix traversal, inclusive range traversal, and `ds_ptrie_remove_prefix` for bulk subtree removal.
- Persistent red-black tree uses an immutable, refcounted, path-copying left-leaning red-black implementation. It supports insert, get, remove, full ordered traversal, inclusive range traversal, floor, ceiling, rank, select, and version-isolated subtree sizes.
- History supports transaction batching, branch-local operation serialization/deserialization, full multi-branch history serialization/deserialization, portable little-endian metadata helpers, portable full-history serialization/deserialization, and deterministic operation-log branch merge.
- The graph module supports directed storage, weighted edges, BFS, DFS, topological sort, Dijkstra shortest paths, weakly connected components, and strongly connected components.
- `make sanitize` cleans after its sanitizer run so later normal builds do not accidentally link against sanitizer-instrumented objects.

## Remaining high-value work

- Migrate every legacy API to `ds_status_t` while keeping compatibility wrappers.
- Convert all complex constructors to config structs.
- Add context-aware allocation throughout older modules.
- Add persistent trie prefix counting, prefix copy/move helpers, and compressed/radix-node storage.
- Add persistent RBT delete-min/delete-max, predecessor/successor convenience wrappers, and validation/debug introspection.
- Add history semantic conflict hooks for merges; current merge is a deterministic operation-log merge and does not try to interpret payload conflicts.
- Add a first-class portable archive object for history instead of only exposing helper callbacks.
- Add graph weighted-edge update/remove, Bellman-Ford for negative weights, minimum spanning tree helpers, and adjacency iterators.
- Add arena/pool/debug allocators through `ds_context_t`.

## High-value hardening pass

Additional work now implemented:

- `make valgrind` runs the full test suite, safe smoke test, and examples under Valgrind when Valgrind is installed. Split targets are also available: `make valgrind-test`, `make valgrind-safe`, and `make valgrind-examples`.
- `ds_context_t` now has first-class allocator backends in `lib/allocators.h`:
  - `ds_arena_t` for bump allocation
  - `ds_pool_t` for fixed-size block allocation
  - `ds_debug_allocator_t` for allocation/free/realloc statistics
- Persistent trie now includes high-level prefix helpers:
  - `ds_ptrie_count_prefix`
  - `ds_ptrie_copy_prefix`
  - `ds_ptrie_move_prefix`
- Persistent red-black tree now includes:
  - `ds_prbt_delete_min`
  - `ds_prbt_delete_max`
  - `ds_prbt_predecessor`
  - `ds_prbt_successor`
  - `ds_prbt_validate`
- History now includes:
  - `ds_history_archive_t`, an owned in-memory archive object
  - archive read/write callbacks usable directly with portable serialization
  - `ds_history_branch_merge_with`, a callback-driven semantic merge policy hook
- Graph now includes:
  - edge update/remove APIs
  - edge visitation
  - Bellman-Ford shortest paths
  - minimum spanning tree / forest helper over the graph's weak undirected view

Still intentionally open:

- Full legacy API migration to `ds_status_t` remains incremental because it changes a large public surface.
- Compressed/radix persistent trie storage remains a separate performance-oriented backend, not a small extension to the current byte-edge trie.
- History merge conflict handling is now hookable, but domain-specific conflict resolution belongs in adapters.
