# Contracts between components

The capability interfaces (maps, ordered maps, priority queues, rank/select,
approximate membership, replacement policies, page stores, the inverted
index's dictionaries, codecs and scorers, and the suffix array builders that
`dsa/structures/fm_index.h`, `dsa/structures/suffix_array.h` and `dsa/structures/r_index.h` share) let one structure
be built from another chosen by the caller. An operation table alone does not
make two components compatible, though: they also have to agree on element
layout and ownership, on order, on memory, on what a failure leaves behind,
on how long cursors and handles live, and on what each operation costs. This
document collects those agreements and points to where each is implemented.

## Element schema

`dsa/core/schema.h` describes the values a container stores: size, alignment, copy
and destroy callbacks, user data and a representation id.

- **Policy follows from the callbacks.**
  - *Plain*: no callbacks. Elements are copied as bytes and nothing is released.
  - *Copy only*: a copy callback and no destroy. Copies are made by the callback, and nothing is released per element.
  - *Owned*: copy and destroy. Every copy is a clone that is released exactly once.
  - Destroy without copy is rejected: a structure that shares or hands out elements could not give out an owned copy.
- **Transfer services.** `ds_schema_transfer` names the service for moving
  elements between two containers:
  - *move*: the schemas are equal, so nodes or bytes move as they are and ownership travels with them.
  - *copy*: layout and id agree but the callbacks differ, so each element is cloned with the destination's copy and released with the source's destroy. An owned source requires a destination copy callback.
  - *none*: either schema is invalid, the layouts differ, or copying would discard ownership without cloning. A conversion is the caller's job.

  Structural O(log n) joining and O(n) conversion are different services, and the API keeps them apart.
- **Containers copy the schema at initialization.**
  `ds_pvec_config_from_schema` and `ds_ftree_config_set_schema` build those
  structures' configurations from a schema.
- **Owned values elsewhere.** Two algorithm drivers take ownership
  callbacks of their own:
  - `dsa/algorithms/dynamization.h` moves items as bytes between levels and never
    duplicates them; `ops.release` releases each item exactly once, when a
    rebuild drops it after its removal or at destroy.
  - `dsa/algorithms/anneal.h` takes `clone`, `copy` and `release` for states that own
    memory: the candidate is cloned once and exchanged bytewise on
    acceptance, and the best state is assigned with `copy`.

## Order

- **SSTables and LSM trees.** Both take a `ds_sstable_comparator_t` (compare
  function, user data, id). The id is written into every table, and a table
  opened with a different order is rejected, so persisted data cannot be read
  in the wrong order. Filters hash keys, so with a custom order they need a
  `ds_sstable_key_hash_t` consistent with it (equal keys hash equally,
  stable across runs): tables built with one mark their filter keyed, and
  readers use it only once the same hash is attached; without one, custom
  orders get no filter.
- **Priority queues.** A queue records its compare function and user data. A
  native meld that moves nodes happens only when both queues use the same
  order; otherwise items are copied in the receiving queue's order.
- **Ordered maps.** The ordered capability's `compatible` operation checks
  layout and memory before `split` and `join`.

## Resources

A `ds_context_t` describes its allocator through resource traits:

| Trait | Meaning |
|---|---|
| `DS_RESOURCE_REALLOC` | resizes any live block |
| `DS_RESOURCE_FREE` | individual blocks can be freed |
| `DS_RESOURCE_FALLIBLE` | allocation may return NULL |
| `DS_RESOURCE_BOUNDED` | total capacity is limited |
| `DS_RESOURCE_FIXED_BLOCK` | only one block size is served |

The C heap, arena, pool, buddy and debug contexts describe themselves; custom
functions should be described with `ds_context_set_resource`.

- `ds_context_require` lets a structure refuse a context early. The map
  capability does this: every backend grows by `realloc`.
- `ds_context_compatible` says whether memory from one context may be
  released through the other. Moving nodes between containers needs exactly
  that, not the same context object. Priority-queue meld and ordered
  split/join check it.

## Mutation and failure

[Ownership](ownership.md) describes the two allocation profiles. Where an operation
cannot leave its structure unchanged after a failure, the module's header
says what remains:

| Module | Guarantee |
|---|---|
| `dsa/structures/btree.h` | insert stages every required split node before mutation; removal never allocates and clears the removed node owner; equal keys are allowed as distinct intrusive nodes |
| `dsa/structures/treap.h` | insertion never allocates and reports duplicate keys; checked clones preserve topology and release accepted clones on failure |
| `dsa/structures/paged_btree.h` | put and remove prepare every page (splits, borrows, merges) before changing any; pages a failed put took return to the free list |
| `dsa/structures/topology_tree.h` | reserves cluster slots and work lists for the per-level bound before committing, so a committed update does not allocate; a failed commit still leaves an explicit broken state, and `ds_topology_tree_rebuild` repairs it |
| `dsa/structures/pq_iface.h` | a copying meld stops at the failing item, with both queues valid and every item in exactly one of them |
| `dsa/structures/pager.h` | `ds_pager_close` keeps dirty pages and can be retried; `ds_pager_discard` drops them |
| `dsa/structures/map_iface.h` | put is all or nothing; removing a present key always succeeds |
| `dsa/structures/yfast_trie.h` | a skipped rebalance leaves a bucket outside its bounds until a later update retries it |
| `dsa/structures/zipper.h`, `dsa/structures/trie_stores.h` | release and destruction never need fresh memory |
| `dsa/structures/trie.h` | insertion is all or nothing; with stores that have `next`, cloning is all or nothing (`clone_data` runs only on a complete copy) and destruction needs no memory |
| `dsa/structures/hash_table.h` | a resize that runs out of memory leaves the table as it was, every entry included |
| `dsa/structures/sstable.h`, `dsa/structures/paged_btree.h` | malformed stored data gives `DS_ERR_INVALID`, never a read outside the data; each also has a separate full verification call |
| `dsa/structures/spiral_storage.h` | a put whose growth fails (memory or slot numbers) keeps the key; the table only gets fuller |
| `dsa/structures/inverted_index.h` | a failed add restores every posting list it touched, bits and skip tables included, and removes the terms it created; compaction builds every new list before freeing an old one, so it is all or nothing |
| `dsa/structures/dynamic_connectivity.h` | insert is all or nothing; a delete that runs out of memory while recording a replacement leaves an explicit broken state (`ds_dc_broken`), repaired by `ds_dc_rebuild`, which can be retried |
| `dsa/structures/rrb_vector.h` | every update builds a new version; on failure the inputs are unchanged and no clone leaks |
| `dsa/algorithms/delaunay.h` constraints | memory is reserved before any flip; a failure leaves a valid constrained triangulation of the segments already inserted, which are counted; a crossing segment is refused before anything changes |
| `dsa/structures/rank_pairing_heap.h` | update and remove never allocate, so they cannot fail on a queued item |
| `dsa/structures/chromatic_tree.h` | inserts and removes reserve their memory (nodes, a pending slot) before changing anything; rebalancing never allocates in eager mode (the path buffer covers any red-black height), and a deferred rebalance that cannot grow its path returns `DS_ERR_ALLOC` with the tree valid and the work still pending |
| `dsa/structures/top_tree.h` | all memory is taken at init: no operation allocates or fails for lack of memory |
| `dsa/algorithms/planarity.h` | every call allocates its working memory before it starts; an embedding is filled only on success and is otherwise left empty, safe to destroy; a too small certificate buffer gives `DS_ERR_RANGE` with the size needed |
| `dsa/structures/suffix_array.h`, `dsa/structures/r_index.h` | builds are all or nothing: a failed build frees what it took and leaves nothing to destroy |
| `dsa/structures/hollow_heap.h` | an update that cannot allocate its new node changes nothing |
| `dsa/structures/strict_fibonacci_heap.h` | push takes its node, cell and at most two rank records before changing anything; update, remove and meld never allocate (an increase re-inserts the item's own node, and a meld recycles the absorbed heap's rank records) |
| `dsa/structures/fusion_tree.h` | an insert takes every node its splits need before changing anything; remove and the queries never allocate; a bulk load builds the new tree before releasing the old one |
| `dsa/structures/decremental_msf.h` | a deletion that runs out of memory while promoting or recording a replacement leaves an explicit broken state; `ds_dmsf_rebuild` starts over from the remaining edges |
| `dsa/structures/dynamic_msf.h` | an insertion that cannot get its edge record changes nothing; an update that runs out of memory while rebuilding a deletions-only structure (or recording replacements) leaves an explicit broken state, in which updates return `DS_ERR_STATE` until `ds_dyn_msf_rebuild` builds everything again from the live edges |
| `dsa/structures/fibonacci_heap.h` (item heap) | push takes its node before changing anything; pop, update, remove and meld never allocate (consolidation links through a degree table inside the heap) |
| `dsa/structures/ordered_iface.h` fusion-tree backend | split and join build both new trees before releasing the old ones, so a failure leaves both sides as they were |
| `dsa/algorithms/alternating_tm.h` | all or nothing: an error frees everything the exploration took and leaves `*out` unchanged |
| `dsa/algorithms/sdp.h` | a solve takes its memory before iterating; after an error status the solution holds nothing, after any other status it must be destroyed; problems only grow by `ds_sdp_add`, which changes nothing when it cannot |
| `dsa/algorithms/matrix.h` factorizations | they work in place: a failed Cholesky or LU leaves the factorization partly done; determinants and inverses factor a copy, taken before anything else |
| `dsa/structures/ab_tree.h` | every insert and remove, and every split and join, takes all the nodes it may need before changing anything, so each is all or nothing |
| `dsa/structures/ordered_iface.h` (a,b)-tree backend | split and join are the tree's own, all or nothing; they move nodes, so the two contexts must be compatible (`DS_ERR_INVALID` otherwise, with nothing moved) |
| `dsa/algorithms/rdf_canon.h` | adding a quad or parsing a document changes nothing when it fails (a syntax error adds no quad and names the line); a canonicalization that fails (memory, a poison-dataset limit) leaves its result empty |
| `dsa/algorithms/parallel_sim.h` | every call takes its memory before the first round; on failure the outputs are untouched |
| `dsa/structures/lsm_tree.h` with a page store | an update whose log record cannot be buffered changes nothing; a flush or merge whose tables cannot be written changes nothing in memory; a failed publish keeps every update in the log; a publish that fails after its slot write began leaves the tree broken (`DS_ERR_STATE` for updates) until it is reopened, which recovers it |
| `dsa/structures/amq_iface.h` images | a load checks the whole image, including a quotient filter's slot structure, so a loaded filter is one its backend could have built; images from another word width are `DS_ERR_UNSUPPORTED` |
| `dsa/structures/wal_store.h` | a failed write or truncation changes nothing; a failed sync leaves its commit pending, and the next write, truncation or sync completes it first (failing, with nothing else changed, while it cannot); a failed checkpoint leaves the log as it was, and a log restart that fails is pending in the same way; after a crash the store opens as of the last completed sync (or the one under way, if its commit record landed) |
| `dsa/structures/fault_store.h` | an injected failure changes nothing; a base-store failure during sync keeps the operations not yet applied for the next sync |
| intrusive containers (`dsa/structures/list.h`, `dsa/structures/stack.h`, `dsa/structures/queue.h`, `dsa/structures/deque.h`, `dsa/structures/dlist.h`, `dsa/structures/ring_list.h`, `dsa/structures/circular_buffer.h`, `dsa/structures/heap.h`, `dsa/structures/bst.h`, `dsa/structures/avl.h`, `dsa/structures/rbt.h`, `dsa/structures/splay_tree.h`, `dsa/structures/treap.h`, `dsa/structures/btree.h`, `dsa/structures/bplus_tree.h`, `dsa/structures/skip_list.h`) | constructors return NULL when memory runs out, releasing what they took; `clone_ex` rolls back every clone it made through `destroy_clone` (`DS_ERR_CALLBACK` for a NULL clone, `DS_ERR_ALLOC`); a pointer-only clone returns NULL, and the clones it made before are not released; `dsa/structures/heap.h` and `dsa/structures/skip_list.h` insertion through `insert_ex` reports a failed allocation with nothing changed (the void insert leaves the node out); `dsa/structures/bplus_tree.h` insertion stages all split nodes and B* scratch space before mutation, and its checked clone rolls back an insertion failure |
| pointer-returning constructors (`dsa/structures/order_statistic_tree.h`, `dsa/structures/bitvector.h`, `dsa/structures/wavelet_tree.h`, `dsa/structures/bloom_filter.h`, `dsa/structures/fenwick_tree.h`, `dsa/structures/segment_tree.h`, `dsa/structures/disjoint_set.h`, `dsa/structures/pairing_heap.h`, `dsa/structures/fibonacci_heap.h`, `dsa/structures/radix_trie.h`, `dsa/structures/aho_corasick.h`, `dsa/structures/hamt.h`, `dsa/structures/art.h`, `dsa/structures/rope.h`, `dsa/structures/bitset.h`, `dsa/structures/str.h`) | return NULL when memory runs out, releasing what they took, as do the clones they offer; `ds_bitvector_build_ex` keeps the old rank directory when the new one cannot be allocated |
| `dsa/structures/hamt.h`, `dsa/structures/art.h`, `dsa/structures/radix_trie.h` | checked insertion prepares every new node, key and layout before publishing; errors preserve keys and values; ART/radix removal may skip optional compression without losing the deletion |
| `dsa/structures/aho_corasick.h` | checked add constructs a missing path privately; checked compile reserves the BFS queue before replacing failure links; checked searches report compilation failure |
| `dsa/structures/rope.h` | checked append, insert, delete, concat and rebalance prepare a balanced replacement root; errors preserve both inputs; concat rejects different allocator pairs and self-concat |
| `dsa/core/result.h` | operations stage the payload handle and persistent diagnostic sequence; failure preserves inputs and output; callback side effects are outside this guarantee |
| `dsa/core/i18n.h` | registration copies storage before replacing an entry; rendering publishes complete output only on success, including when a formatter ignores a writer failure |
| `dsa/structures/persistent_rbt.h`, `dsa/structures/persistent_trie.h` | every update builds a new version; when memory or a clone is refused it returns NULL with its base unchanged and nothing leaked (also a prefix copy or move that fails partway) |

## Sources inside the destination

An operation that copies caller data into a container may be given data
that the container itself holds, such as one of a vector's elements or a
part of a string. It reads the source as it was before the call: growth
copies the source into the new block before the old one is freed, and an
operation in place stages it where the move cannot reach. No path
compares pointers into unrelated objects.

The hash maps stage their key and value arguments with `dsa/core/stage.h`: a
copy on the stack up to `DS_STAGE_BYTES` (256 by default, a compile-time
setting), a block from the map's context beyond that. A map in fixed
storage must not allocate, so it stages only what fits on the stack.
That is all the Swiss and coalesced maps need there, since without growth
an insertion moves no entry; the cuckoo map (displacement chains) and the
open-addressing map (Robin Hood placement) still move entries, so in
fixed storage their arguments over `DS_STAGE_BYTES` must not come from
the map.

| Module | Operations |
|---|---|
| `dsa/structures/vec.h` | push, append, insert and insert_n, the fill of resize |
| `dsa/structures/array_deque.h` | push_back, push_front, insert, set, push_back_overwrite |
| `dsa/structures/gap_buffer.h` | insert (a stretch on either side of the gap) |
| `dsa/structures/str.h` | append, insert, assign, and the arguments of the formatted appends |
| `dsa/structures/slot_map.h` | insert (a value from `ds_slot_map_get`) |
| `dsa/structures/lru_cache.h`, `dsa/structures/sieve_cache.h` | put (a value peeked from the entry it evicts) |
| `dsa/structures/min_max_heap.h`, `dsa/structures/monotonic_queue.h` | push (one of the heap's ends, the queue's own front) |
| `dsa/structures/rh_map.h` | put, insert and emplace (keys and values read from the map, which moves entries as it places one) |
| `dsa/structures/swiss_map.h`, `dsa/structures/cuckoo_map.h`, `dsa/structures/open_map.h`, `dsa/structures/coalesced_map.h`, `dsa/structures/linear_hash_map.h`, `dsa/structures/extendible_hash.h` | put, insert and emplace (another entry's value as the value or the key, while the table grows or splits) |
| `dsa/structures/map_iface.h` | put on every backend (it reports the backend's own status) |
| `dsa/structures/implicit_treap.h` | insert and set (an element from `ds_itreap_at`), apply (a tag that is an element of the range it updates) |
| `dsa/structures/rope.h` | append and insert may read bytes in the current rope; the old root stays alive until publication |
| `dsa/structures/graph.h` | update_edge keeps data it is given back: the old data is destroyed only when replaced |

## Cursors and handles

| Kind | Valid until | Notes |
|---|---|---|
| map cursors | the next put or remove | a position a packed cursor cannot hold is reported (`DS_ERR_OVERFLOW` from `ds_map_next_status`) instead of ending the iteration silently |
| priority-queue handles | their item is popped or removed | |
| priority-queue item pointers (`top`, `item`) | the item leaves; for `DS_PQ_MOVING_ITEMS` backends (packed, weak, hollow), the next modification | |
| ordered-map cursors | the next modification | `ds_ordered_for_each` walks in linear time without allocating a cursor per step |
| SSTable iterators | the table bytes are freed | they borrow the bytes |
| SSTables opened with `ds_sstable_open_ex` | `ds_sstable_close` | the loaded AMQ filter is owned by the table |
| LSM iterators and returned values | the next modification | |
| (a,b)-tree item pointers and cursors | the next insert, remove, split or join | items move within and between leaves; finger searches start from any valid cursor |
| RDF dataset term strings | `ds_rdf_dataset_clear` or destroy | strings live in chunks that never move; the quad array itself may move when it grows |
| RDFC results (canonical form, identifier map) | `ds_rdfc_result_destroy` | copies: the dataset may be destroyed first |
| graph-view workspaces | reuse across calls | one call at a time |
| inverted-index term texts | the index is destroyed | term bytes live in chunks that never move |
| inverted-index posting cursors | the next add, remove or compaction | cursors skip deleted documents |
| inverted-index dictionary cursors | the next add | ordered dictionaries only |
| Result payloads and diagnostic views | the owning handle is released/replaced, unless another handle retains the owner | borrowed payloads additionally require the application owner to remain alive |
| i18n output text | its text handle is destroyed or successfully rendered again | a failed render leaves the previous text intact |
| RRB vector element pointers and iterators | the version is released | versions share nodes; nothing is edited in place |
| rank-pairing heap item pointers | the item leaves | nodes never move, even on increase |
| strict Fibonacci heap handles and item pointers | the item leaves | items live in cells that never move; a decrease below the minimum swaps cells between nodes, not items between cells |
| fusion-tree value pointers (`ds_fusion_get`; for sets, the stored key) | the next insert or remove | values move with their keys inside and between nodes |
| Fibonacci item-heap handles and item pointers | the item leaves | nodes never move, even on increase |
| dynamic-MSF edge ids | the edge is deleted | ids are reused by later insertions |
| SDP solution arrays (`x`, `y`, `z`) | `ds_sdp_solution_destroy` | they belong to the solution, not to the problem |
| chromatic-tree element pointers (get, select, floor, ceiling) | the element is removed | leaves never move; rebalancing rotates internal nodes only |
| top-tree vertex values (`ds_top_value`) | the tree is destroyed | vertex nodes are allocated once |
| planar embeddings and their graph views | `ds_planar_embedding_destroy` | a view also needs its `ds_gview_csr_t` alive |

## Costs

- **Stack depth.** Intrusive, implicit, augmented, ordered-capability,
  Euler-tour and enfilade treaps, the randomized BST, and the visibility
  sweep's status tree use iterative paths even with degenerate shapes.
  Expected logarithmic running time is not a deterministic height bound.
  Other balanced trees and divide-and-conquer routines retain recursion,
  bounded by their structural height or shrinking subproblem; index widths,
  explicit depth limits and fixed Unicode data bound other traversals.
  Generic-trie stores without `next` and user callbacks can still recurse.
  [Mutation and lifetimes](container-lifetimes.md) covers borrowed cursors.
- **Rope.** Nodes form an AVL tree with at most 64 bytes per leaf. Indexing
  and joining use logarithmic tree height; building inserted text is linear
  in its byte length. Split-based edits have a conservative O(log² leaves)
  bound plus copied bytes. Reference counting keeps old paths alive until
  the new root is ready; release follows balanced height.
- **Result logs.** Copying a version retains a payload owner and RRB root.
  Same-config binding concatenates RRB logs in O(M log_M n), where M is the
  configurable branching factor. Different configs import the preceding log
  into the callback's config; that conversion is not a logarithmic join.
- **Priority queues.** Each operation states a worst-case and an amortized
  (or expected) cost class. Algorithms that take a queue inherit those costs:
  the graph-view `*_with` functions say what that means for Dijkstra, Prim
  and A*.
- **Succinct structures.** The Elias–Fano and rank/select headers give
  practical cost and space formulas, not only asymptotic ones. LOUDS
  navigation costs one or two selects of the chosen rank/select backend.
- **Budgets.** Constructions that can blow up take explicit limits and
  report `DS_ERR_RANGE` when one runs out: the tree-automaton subset
  construction and product (`ds_fta_budget_t`: states, transitions, work)
  and tree transductions (output size); the tree edit distance checks its
  work-table sizes before allocating (`DS_ERR_OVERFLOW`). Where a single
  operation's work is not bounded (dynamization rebuilds),
  counters (`items_built`, `rebuilds`) let callers watch it.
- **Ranking.** The inverted index's ranking visits documents one at a time
  across the query terms' cursors: disjunctive ranking costs one queue
  offer (O(log k) on a binary heap) per document holding a query term;
  conjunctive ranking leapfrogs the cursors with skip-table advances.
  Memory is O(terms + k).
- **Dynamic graphs.** Fully dynamic connectivity costs O(log^2 n) amortized
  per update and O(log n) expected per query, in O(n log n + m) space (one
  Euler-tour forest per level).
- **Succinct trees.** Balanced-parentheses and DFUDS operations cost a
  rank or select of the chosen backend plus one or two excess searches,
  each O(B / 8 + log(n / B)) over blocks of B = 512 bits.
- **Planarity.** The test, its embedding and the embedding check are
  O(n + m); a straight-line drawing is O(n). A Kuratowski certificate costs
  one planarity test per shrinking attempt on graphs that shrink
  geometrically while deletions succeed, which is the usual case; the worst
  case is O(m^2 log m). A maximal planar subgraph costs O((r + 1) log m)
  tests for r rejected edges.
- **Relaxed balance.** Chromatic-tree updates walk one path and never
  rotate, so in deferred mode the height can reach n; rebalancing costs
  O(1) amortized transformations per update, each found by a walk of the
  current height, and eager mode keeps the height of a red-black tree.
- **Dynamic trees.** Top-tree operations cost O(log n) amortized splay
  steps, each calling the cluster callbacks O(1) times. Topology-tree link
  and cut build O(1) clusters per level, O(log n) worst case
  (`ds_topology_tree_work` reports the count), apart from the cluster array
  growing by doubling; path and component aggregates cost O(log n).
- **Worst-case heaps.** The strict Fibonacci heap does O(1) work in the
  worst case for push, decrease and meld (at most one loss, six active root
  and four root degree reductions per decrease) and O(log n) for pop and
  remove; active roots and total loss stay within 2 lg n + 7, the root's
  degree within 2 lg n + 9 and every other degree within 2 lg n + 12.
- **Word-level search.** A fusion-tree node is searched with a constant
  number of word operations plus a sketch gathered bit by bit (at most K - 1
  bits) and a highest-bit search by halving (log w steps); a tree query
  visits O(log n / log K) nodes, and an update recomputes the sketches of
  the O(log n / log K) nodes it changes, O(K^2) each.
- **Decremental MSF.** O(m log m + n log n) to build, O(log^2 n) amortized
  per deletion (expected, through the Euler-tour treaps); a deletion
  reports its replacement edge.
- **Fully dynamic MSF.** O(log^4 n) amortized per insertion or deletion
  (expected, through the deletions-only structures' treaps); the heaviest
  forest edge on a path and connectivity cost O(log n) amortized (splay
  steps of the link-cut tree).
- **Alternating machines.** Time and memory linear in the configurations
  explored and their transitions, times the length of an encoding (the
  windows of the tapes); each configuration is stored once, so a machine
  in space s explores at most its 2^O(s) configurations.
- **Semidefinite programs.** Each interior-point iteration builds the
  Schur matrix in O(m s nnz) for sparse constraint matrices (O(m^2 s^2 +
  m s^3) when dense), factors it in O(m^3) and spends O(s^3) per dense
  block of order s on products, factorizations and the Jacobi eigenvalue
  sweeps of the step lengths; typical problems take 10 to 30 iterations.
  A Jacobi sweep costs O(n^3), and few sweeps are needed once the matrix
  is nearly diagonal.
- **(a,b)-trees.** Insert, remove, rank and select in O(log n) (base a
  logarithms); a finger search O(log d) for a target d items away; join and
  split O(log n). Restructuring is O(1) amortized per update when b >= 2a
  (the tree's split, fusion and share counters show it), not for
  b = 2a - 1. A bulk load is O(n).
- **RDF canonicalization.** First-degree hashing costs O(q log q) for q
  quads, plus hashing the quads' text. Hash N-Degree Quads can take
  exponential time on symmetric blank-node structures: each blank node may
  run it at most `max_node_calls` times (default max(N, 64) for N blank
  nodes sharing hashes), so a run makes at most O(N^2) calls by default,
  each costing its node's quads and permutations; `max_calls` caps the
  total. Past a limit the result is `DS_ERR_RANGE`.
- **Persistent LSM tree.** A sync writes the pending log records (about
  one page per page of records, plus the partly filled page) and syncs
  once; a flush also writes the new tables and a manifest and syncs twice.
  Recovery replays the log, then flushes what it replayed. In-memory mode
  loads the tables; `out_of_core` retains their indexes and filters and reads
  data blocks through the pager (see below).
- **Write-ahead-logged store.** A write costs one log page (plus a header
  page per (page size - 48) / 16 entries) and a sync one header write and
  one log sync. Reads cost an index lookup and one page read; logged pages
  are checked against their CRC. Recovery reads the whole log once. A
  checkpoint copies each logged page home once, syncs the data store, then
  writes and syncs one superblock.
- **qsort.** Bentley and McIlroy's `ds_q_sort` / `ds_qsort` take O(n log n)
  comparisons on typical inputs and O(n) on inputs with few distinct keys,
  with O(log n) stack; like every library qsort an adversary can force
  O(n^2) comparisons (`ds_intro_sort` bounds them).
- **Text indexes.** SA-IS builds a suffix array in O(n). The r-index counts
  in O(m log r) and locates each occurrence in O(log r), in 6r + 1 words
  for r BWT runs; building it needs the full suffix array for a while.
- **Fairness.** CTL checking with k fairness sets costs O(|f| (n + m + n k))
  instead of O(|f| (n + m)), and only the `_fair` witness helpers follow
  the fair semantics.

## Algebraic laws

Augmented structures state the laws their callbacks must satisfy:

- **Combine.** Associativity, whether an identity is needed, and whether the
  output may alias an input. The finger tree calls `combine(out, out, b)`;
  the treaps, link-cut, Euler-tour and topology trees never alias.
- **Reversal.** How the reversed aggregate is kept.
- **Tags.** Lazy tags are uniform per-element actions whose summary update
  agrees with per-element application.
- **Order.** Which order an aggregate follows. Euler-tour aggregates follow
  the current tour, so only order-independent combines are stable across
  link and cut.
- **Roots.** Which operations reroot. Link-cut path operations, link and cut
  all reroot; so do the top tree's link, cut, expose, subtree and path
  search, while `ds_top_evert` reroots on purpose and `ds_top_find_root`
  reports the current root.
- **Clusters.** Top-tree callbacks: compress is associative (not
  necessarily commutative), rake associative and commutative, and reverse
  an involution with reverse(compress(a, b)) = compress(reverse(b),
  reverse(a)); outputs never alias inputs. vertex receives NULL for an
  empty point cluster, so no identity element is needed.

## Outcomes

Statuses distinguish operational outcomes:

- `DS_OK`, `DS_EXISTS` (an insert that replaced an existing value),
  `DS_NOT_FOUND`, `DS_EMPTY` and `DS_STOP` are successes.
- Errors are negative.
- `DS_SUCCEEDED` only says the status is not an error. It does not say that a
  payload exists, so lookups that can miss report `DS_NOT_FOUND` explicitly.

`dsa/core/result.h` carries an explicitly tagged domain value/error, independent
of NULL and diagnostic severity, with owned or borrowed payloads and an ordered
immutable log. Its operational return status remains separate from that domain
outcome. Lightweight status/value pairs are also available. See
[result-i18n.md](result-i18n.md) for lifetimes and composition. The distinctions
below remain the responsibility of the domain payload; the Result layer does
not guess them from diagnostics.

- **Found, absent, could not finish.** Finger-tree lookup returns `DS_OK`,
  `DS_NOT_FOUND` or `DS_ERR_ALLOC` instead of NULL for both of the last two.
  Map iteration separates its end from a cursor overflow.
- **Committed or not.** Prepare-then-commit operations either change
  everything or nothing. Operations that cannot promise that say what
  remains (see the table above).
- **Borrowed, cloned or transferred.**
  - Finger-tree pops return a clone for owned elements.
  - SSTable values and LSM values point into the structure and are borrowed.
  - `ds_pq_pop` copies the item out.
  - A native meld transfers nodes and their handles; a copying meld
    invalidates the absorbed queue's handles.
- **Why a run stopped.** Annealing reports its stop reason (iterations,
  temperature, target, callback) and the largest energy correction made
  when move-mode deltas are resynchronized.
- **Certified or claimed.** A planarity answer comes with its proof: an
  embedding that `ds_planar_embedding_check` verifies (Euler's formula per
  component), or a K5 or K3,3 subdivision that `ds_kuratowski_classify`
  verifies by tracing its paths.
- **Exact, numerically qualified, heuristic.**
  - Robust predicates give exact signs within stated input ranges.
  - Weighted matching is exact while integer-mode duals stay in range.
    Its `_ex` entry points report the mode and can require exactness instead
    of silently falling back. Floating mode uses a comparison tolerance,
    not a certified objective-error bound.
  - Markov hitting times are numerical.
  - The TSP heuristics are approximations with stated bounds where they
    exist.

## Addressable array heaps

`ds_min_max_heap_set_move_callback` and `ds_shadow_heap_set_move_callback`
report item placement and movement. Index `(size_t)-1` reports removal,
replacement, clear or destruction. Callbacks update external metadata only;
they must not reenter the heap. Indices alone are not stable handles.

`ds_pq_min_max_ops()` and `ds_pq_shadow_ops()` store pointers to stable,
context-allocated payload nodes in those same heaps. Handles and item pointers
survive priority changes, growth and unrelated removals. Update and removal
use tracked indices, without a linear search. Removed handles are invalid.
Neither backend supplies native meld; the capability's copying fallback applies.
The min-max backend has constant-time top and logarithmic repair/removal.
The shadow backend scans its logarithmic shadow for top; its cost metadata
conservatively allows logarithmic-squared folding during removal. Array growth
can make a push linear. See `examples/addressable_schedulers.c`.

## Callback-backed SSTables and LSM reads

`ds_sstable_open_reader` accepts an immutable byte source with an exact-read
callback. Opening verifies the existing checksum in chunks, then retains only
the index and filter region. Each active iterator owns a reusable block buffer
and a decoded key; the table owns a separate iterator for point lookups.
Metadata and block limits are independently configurable. Zero means the format
limit, not a promise of constant memory. A single oversized record can require
a large block; metadata scales with the number of blocks and filter size.
Checksum verification still reads the entire table during open.

A callback lookup's value lasts until that table's next get or close. Iterator
views last until that iterator's next positioning call. Positioning stages its
input key, so a current key/value may be supplied as a new search key. Read
failures propagate; positive callback statuses become `DS_ERR_CALLBACK`.
Independent iterators do not share block buffers. A callback table may move
by value only when no external iterator refers to its old address; shallow
copies borrow its reader and must not close it.

`ds_lsm_config_t.out_of_core` selects this path and requires a page store. Each
table owns a pager with configurable frame count, page-map backend and eviction
policy. Retiring the table retires its cache, preventing stale pages when an
extent is reused. Newly flushed and compacted tables also switch to callback
reads after their bytes are stored. An LSM lookup's value is conservatively
borrowed until the next get, mutation or destruction. In-memory mode
is the default. Flush and compaction output are still assembled in memory;
this option bounds retained table data during reads, not write-side building.
See
`examples/sstable_reader.c` and `examples/paged_catalog.c`.

## Persistent trie storage

`ds_ptrie_config_t.storage` selects byte edges or compressed radix labels.
Both share immutable, reference-counted subtrees and copy an update's path;
the radix backend stores a suffix on each incoming edge and splits/coalesces
labels as keys diverge or disappear. Empty and binary keys remain distinct.
Prefix count, visit, removal, copy and move accept prefixes ending inside labels.
Traversal and release are iterative. Radix path scratch scales with branching
depth; decoded traversal keys still require space for the full key. A node's
ordered child list has at most 256 entries.

Checked creation and mutation APIs distinguish missing removals, invalid version
owners, allocation/size failures and clone failures. `clone_value_ex` can return
`DS_OK` with a NULL value, independently of failure. Owned values require a clone
callback; configure only one clone form. All updates publish a new version only
after successful construction; the base remains usable. Versions must be released
through their creating trie before destroying it. Cross-trie prefix copy reads the
source and uses the destination's clone policy. Reference counts are externally
synchronized; use checked retain to detect exhaustion. See
`examples/versioned_routes.c` for the same workflow with both backends.

## History archive and merge contracts

History storage accepts a context, and checked branch/transaction/edit APIs
report allocation failure and ID/time exhaustion. Checked portable import stages
all branches and payloads, validates parent/ID/order relationships, enforces
optional resource limits, and uses configurable ordered maps for ID indexes.
Payload readers can distinguish a successful NULL value from an error.

`history_adapters` provides FILE serialization with sticky I/O errors and transfer
budgets. `fflush` is not a durability barrier; atomic file replacement and fsync
remain the caller's storage policy. The memory archive also accepts a context,
checks allocator pairs, exposes sticky status, and safely appends its own data.

The keyed merge adapter projects semantic keys into a configurable map, supports
four conflict policies or a resolver callback, and reports partial progress.
It merges operations; sequence rebasing and domain-specific three-way snapshots
remain explicit application policies. Retained payload clones are released on
skipped/failed operations even when the clone returns the original address.

`history_catalog` uses three different map/ordered backends to merge inventory
edits, keep a newer target change, serialize, and reopen a NULL payload marker.
