# Legacy API migration

The legacy families now have checked/context-aware paths alongside their
compatibility entry points. Cheap operational statuses remain separate from
Result domain outcomes. This does not rename every old function or change every
legacy convenience return type: callers opt into the checked paths below.

| Family | Ownership and policies | Checked path |
|---|---|---|
| BST, AVL, red-black | Context-owned internal nodes; caller-owned intrusive records | Comparison user data, checked insert/find, three traversal orders, shape-preserving clone with rollback, destruction user data |
| List, dlist, ring list, stack, queue, deque | Context-owned headers/sentinels; clone inherits or selects context | Checked insertion/pop/unlink, user-data search, forward/reverse stoppable walks, owning clones with rollback |
| Circular buffer, binary heap | Context-owned arrays; intrusive records | Checked push/eviction/pop; heap comparison user data and addressable update/remove; stoppable slot walks and owning clones |
| Splay, skip list, B+ tree, order-statistic tree | Stored comparison function/user policy and context | Checked insert/find/remove; stoppable walks and clones; B+ range walk; order-statistic rank/select |
| Pairing and intrusive Fibonacci heaps | Stored comparison/user policy and context; records stay caller-owned | Checked insert/extract/meld, iterative visitors, owning clones and user-data destruction; Fibonacci checked decrease-key |
| ART, HAMT, radix trie | Context owns nodes, keys and scratch; values borrowed until explicit cleanup | Atomic insertion, lookup distinguishing stored NULL, stoppable visits and destruction user data; radix scratch failures reported |
| Aho–Corasick | Context owns nodes and compilation scratch | Checked add/compile/search; stoppable search publishes the number of delivered matches |
| Rope | Context owns shared nodes and returned text | Atomic checked edits, context-compatible concat, checked character/materialization access |
| Disjoint set | Context owns header; intrusive nodes caller-owned | Checked make/find/union; membership remains a caller precondition |
| Bitset, bitvector, wavelet tree | Context owns storage; nested wavelet bitvectors use the same context | Strict index/rank/select/quantile status paths; bitvector freeze/build state enforced by checked operations |
| Bloom filter | Context owns words; clones inherit or select owner | Validated sizing/rates, checked add/query/union/clone; nullable byte keys only at length zero |
| Fenwick tree | Context-owned array | Checked half-open prefix/range queries and atomic update/set, with checked signed arithmetic |
| Segment tree | Context-owned arrays and optional operation scratch | Inclusive range operations, checked arithmetic, per-element extrema and rollback journal; no partial edits on failure |
| String | Context owns object and buffers, including builder scratch | Checked byte edits, reserve/pop/clone; atomic cross-owner swap preserves object ownership |
| B-tree, treap | Delivered earlier in this pass | Context, checked insertion/lookup/cloning and callback user data |
| Hash table | Existing configurable hash/equality, bucket store and context retained | Checked config creation, statuses/cursor, stoppable walk and checked nullable payload cloning with rollback |
| Generic trie | Existing selectable child stores retained; checked clone may select another store | Typed fallible slice callbacks, checked config, unambiguous get/remove, iterative stoppable walks and checked payload cloning |
| History, diagnostics | Existing context/payload policies retained | Checked archives, insertion/merge, immutable diagnostic snapshots and checked collection; void sinks expose sticky failure status |

## Ownership and compatibility

A context-created container keeps its context alive by contract, not by taking
ownership of it. The context and its allocator policy must remain valid until
all associated storage is released. NULL context at creation selects
`ds_default_context()`; NULL on the new clone operations generally inherits the
source owner. The trie clone uses a config pointer instead: NULL inherits both
owner and child store. An explicit context/config selects destination storage.

Legacy allocator constructors keep their function pair. `legacy_alloc.h`
shares internal allocation dispatch without global or thread-local bridges.
Raw allocator fields on a context-created object are compatibility metadata,
not its storage owner. For example, release materialized rope text through its
context when that field is non-NULL. Strings provide `ds_str_free_external` for
this purpose while their original owner is alive.

Intrusive records and byte-map values remain caller-owned unless the caller
explicitly supplies cleanup. A successful owning clone callback produces a
fresh equivalent record; completed records are rolled back on later failure.
An unsuccessful callback owns any unfinished allocation. Negative errors
propagate; unexpected positive clone statuses become `DS_ERR_CALLBACK`.
Checked hash/trie payload callbacks can return DS_OK with NULL as actual data.
Pointer-returning legacy hash clone callbacks use NULL for failure on the
checked cloning path, and every cloned part needs a matching cleanup policy.

Rope concatenation transfers internal allocations and checks context
compatibility (or matching legacy pairs). Pairing/Fibonacci meld transfers
only caller-owned intrusive nodes: each heap retains its sentinel, so allocator
contexts may differ, but comparison function/user pointers must match. Self-meld
and count overflow are rejected before mutation.

String swap now keeps each object with its allocation owner. Compatible owners
exchange buffers in O(1); incompatible owners stage two copies before changing
either string. Legacy void swap ignores a failure. Detach transfers a buffer
with its owner policy. Adopt requires compatible external storage, allows exact
self-adoption, and forbids interior aliases. The no-NUL-room safe-adopt branch
copies instead, leaving its input buffer with the caller.

Public structures gained fields: **recompile all consumers**. Source entry
points remain, but this is not binary compatibility with precompiled objects.
Callbacks must obey each header's mutation, ownership and lifetime rules.
Legacy clone APIs without cleanup callbacks keep their documented limitations;
use the checked alternatives for fallible payload construction.

## Costs and boundary contracts

- Singly linked/ring reverse walks use temporary link reversal and O(1) scratch;
  all links are restored even on visitor stop/error. Their callbacks must not
  inspect the container, traverse it or free records. Doubly linked walks use
  predecessor links. Identity-checked list unlink scans O(n); legacy intrusive
  unlink keeps its original membership preconditions/cost.
- Splay operations may change shape on absence or duplicate status. General
  clones preserve ordering and data, not necessarily the original shape; the
  BST/AVL/red-black clone engines preserve shape and balance metadata.
- Generic trie checked walk/clone/destruction requires `store.next`. The legacy
  custom-store fallback still delegates recursion to that store. Typed slice
  failures roll back newly created trie nodes; finite key length and deterministic
  extraction are callback contracts. Successful replacement returns DS_EXISTS.
- Bitvector checked writes reject a built/frozen vector. Rank/select queries
  require a built directory. Wavelet creation validates alphabet membership;
  its checked queries distinguish range errors and absent occurrences.
- Fenwick checked queries use half-open ranges, stage arithmetic and preserve
  outputs on failure. Updates stage all changed cells. Legacy fast arithmetic
  entry points still require representable intermediate values.
- Segment operations use inclusive ranges and O(log n) journaled changes. A small
  local journal expands through the selected owner when needed. Extrema catch
  overflowing individual elements even for MIN/MAX trees. Node sums and lazy
  tags must fit long; materializing a deferred child sum can report overflow
  even when another requested aggregate fits after cancellation. Rejected
  updates restore all arrays/tags; queries restore their lazy-cache changes on
  success and failure. Legacy modifiers ignore failures; legacy queries return
  the aggregate identity on error.
- Bloom matches remain probabilistic; counts are additions, not distinct keys.
  Aho–Corasick callbacks are streaming side effects: an early stop/error reports
  delivered matches including the stopping match, without undoing callbacks.

## Validation

Examples demonstrate selected ownership, cleanup, cancellation/range boundaries,
stored NULL values, early visitor stops, comparator policies, addressable heaps,
child-store conversion and cloning. The examples are `legacy_contexts`,
`intrusive_policies`, `bounded_ranges`, `byte_visitors`,
`legacy_sequence_policies`, `ordered_policies`, `owned_buffers` and
`composed_callbacks`. The latter converts a Robin Hood child store into a sorted
vector store and clones nullable values in a configured probing hash table.

The validation record is in `completion-notes.md`. No tests are added or run.
Builds, examples, sanitizers and static analysis are evidence of the paths
examined, not an exhaustive proof of allocator failure behavior, callback
implementations, input sequences or platform portability.
