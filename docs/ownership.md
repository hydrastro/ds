# Ownership model

`dsa` separates container storage from user data ownership.

## Default rule

A container owns only the internal memory it allocates for itself. It does not
own keys, values, payloads, or intrusive nodes unless the API/config says so.

## Configured ownership

Config-based containers may receive destructor callbacks. If a destructor is
`NULL`, that object is borrowed. If a destructor is set, the container owns that
object and will destroy it when replacing/removing entries or destroying the
container.

For config-based hash tables:

- `destroy_key == NULL`: keys are borrowed.
- `destroy_key != NULL`: keys are owned by the table.
- `destroy_value == NULL`: values are borrowed.
- `destroy_value != NULL`: values are owned by the table.

Clone callbacks follow the same rule, without ever duplicating destruction
rights. A part (keys or values) that the table does not destroy is shared by
a clone. A part that the table destroys must have a clone callback: the clone
then owns its own copies. If it does not, `ds_hash_table_clone` refuses
(`DS_ERR_UNSUPPORTED` on the context) instead of letting two tables free the
same pointer; `ds_hash_table_clone_borrowed` makes a clone that owns nothing
and must not outlive the source's entries.

Replacing a value with the same pointer, or passing the already stored key
pointer to `ds_hash_table_insert_take_key`, destroys nothing.

`ds_hash_table_insert_take_key` exists for wrappers that already allocated a key
and need the table to dispose of that key atomically when an update finds an
existing equal key. `ds_string_map_put` uses this so an update needs one
lookup instead of contains-then-insert.

## String map and string set

`ds_string_map_t` owns duplicated string keys and borrows values.

`ds_string_set_t` owns duplicated string keys.

Updating an existing string-map key does not leak the newly duplicated key and
does not store caller-owned key memory.

## Intrusive structures

Intrusive structures store links inside user-provided node structs. The user owns
intrusive node memory unless a destroy callback is passed to the relevant destroy
function.

Destroy callbacks must match the structure's ownership policy. Passing a destroy
callback for stack-allocated or borrowed nodes is a caller bug.

## Persistent/versioned structures

Persistent versions are immutable handles. Releasing one version must not affect
other versions. Structural sharing is an internal optimization; callers should
reason in terms of version ownership.

For persistent trie/RBT values and keys:

- If clone callbacks are configured, inserted data is cloned before entering the
  persistent structure.
- If destroy callbacks are configured, released versions eventually destroy data
  when no shared node still references it.
- If clone/destroy callbacks are `NULL`, pointers are borrowed/shared according
  to normal C pointer lifetime rules.

## History timelines

`ds_history_branch_insert_at` takes ownership of the operation payload. The
payload is released through `destroy_payload` when the operation is deleted or
when the history is destroyed.

Snapshots returned by `snapshot_at` are owned by the caller and must be released
with `ds_history_snapshot_destroy`.

Serialized/deserialized payload ownership remains adapter-defined: the history
module owns operation objects, but payload construction/destruction is delegated
to the adapter callbacks.

## Context-backed allocators

`ds_context_t` controls memory allocation for context-aware modules. Arena and
pool allocators have stricter behavior than `malloc`:

- arena realloc of an existing pointer is unsupported and returns `NULL`;
- pool allocation only supports fixed-size blocks;
- pool frees reject pointers outside the pool and double-free attempts (in
  O(1) unless the block's data happen to look like a free block's mark);
- debug allocator statistics expose allocation counts and sizes.

Use the default context or a debug allocator when a module requires general
`realloc` behavior.

Allocator-pair constructors take a pair of allocator functions instead of a context
(`void *(*)(size_t)` and `void (*)(void *)`, as the bitset, the hash table
and the trie do). `ds_context_use_allocator_functions` makes a context over
such a pair, with a size header per block so that it can reallocate; a
structure built through the pair can hand it to the context-based modules
inside it. The trie's Robin Hood and sorted-vector stores do this for their
arrays.

## Allocation failure

Failure reporting and rollback guarantees belong to each operation. An
adapter inherits the guarantees of its backend; check its header before
assuming that a failed call leaves every part unchanged.

**Status-returning APIs.** An operation that returns
`ds_status_t` reports allocator exhaustion as `DS_ERR_ALLOC` and size
arithmetic that does not fit as `DS_ERR_OVERFLOW`. Unless its header says
otherwise, a failed operation leaves the structure unchanged and usable.
Where that cannot hold, the header states exactly what remains, for example:

- `dsa/structures/topology_tree.h` reserves cluster slots and work lists for the
  re-clustering bound before committing; if a commit still fails it marks
  the tree broken, queries report it, and `ds_topology_tree_rebuild`
  restores it from the adjacency;
- `dsa/structures/paged_btree.h` prepares every page a split needs before changing any;
- `dsa/structures/pq_iface.h`'s copying meld stops at the failing item with both queues
  valid and every item in exactly one of them;
- `dsa/structures/pager.h`'s `ds_pager_close` keeps the pager and its dirty pages alive
  when a write fails, so the close can be retried, and `ds_pager_discard`
  drops them explicitly.

Release paths are written not to need fresh memory: the trie child stores,
for instance, fall back to visiting children in place when they cannot
take a snapshot. A release path that still allocates says what it does on
failure.

**Pointer, boolean and void APIs.** Constructors return NULL on allocation
failure. A return type alone does not imply an infallible allocator. Checked insertion is available for the heap, skip list, B-tree, treap,
B+ tree (including B*), HAMT, radix trie and ART; Aho-Corasick also has checked
add, compile and query functions. Rope edits have checked variants. These
prepare allocations before changing existing content. Pointer, boolean and void forms
cannot distinguish all operational failures from ordinary conditions; use a
status-returning form when that distinction matters.

An operation whose header explicitly assumes allocation success
requires an allocator that handles exhaustion itself. Do not infer a stronger
failure guarantee for an adapter than its underlying operation provides.

ART and radix-trie deletion may keep a larger layout or unary node when
optional compression cannot allocate. The key is still removed. Rope edits
share immutable AVL subtrees while preparing a replacement root; a failed edit
leaves the old root intact. The rope's public node fields are read-only, and
concatenation requires matching allocator/deallocator functions.

**Allocator capability limits are not memory exhaustion.** A fixed pool
running out of blocks and an arena refusing to `realloc` an existing
pointer are limits of those allocators. Fallible APIs report both as
`DS_ERR_ALLOC`, indistinguishable from exhaustion by status alone, so pick
an allocator whose limits suit the structure (anything that grows by
`realloc` needs a context with general `realloc`).

**Other failures are separate.** I/O errors (`DS_ERR_STATE`, as in
`dsa/algorithms/external_sort.h` and `dsa/structures/pager.h`), integer overflow
(`DS_ERR_OVERFLOW`), ownership mismatches (`DS_ERR_INVALID` or
`DS_ERR_UNSUPPORTED`, e.g. an element destructor without a copy function
where values are shared) and unsupported operations are reported in their
own right, independently of allocation failure.

## Persistent structure versions

Persistent containers (`persistent_rbt`, `persistent_trie`) are immutable:
`insert`, `remove`, and similar operations do not modify their input version,
they return a brand-new version that structurally shares unchanged subtrees with
the old one. Versions are reference counted. Every version handed back to the
caller carries a reference the caller owns and must hand back with
`*_version_release` once it is no longer needed; `*_version_retain` adds a
reference when a version is stored in more than one place. Destroying the
container releases only the references the container itself holds (such as its
initial empty root), so leaking intermediate versions is a caller bug, not a
library one. A typical loop that keeps only the latest version releases each
previous one as soon as the next is produced.

## Lookup return values

- Intrusive containers with a `nil` sentinel return that sentinel for an absent
  result from search/min/max/peek queries. Test against the container's `nil`
  field rather than `NULL`.
- Value-mapping APIs (`hash_table`, `trie`, `radix_trie`, `hamt`, `art`,
  `string_map`) offer pointer-returning lookups. A NULL result can mean either an absent
  key or a stored NULL value; use a status-returning lookup to distinguish them.
- Index-returning succinct structures (`bitset`, `bitvector`, `wavelet_tree`)
  return a published `DS_*_NPOS` constant when no position satisfies the query.

## Unicode scratch storage

Normalization, case folding and case mapping allocate their temporary code-point
sequences through the destination string's allocation owner. Growth checks size
overflow and uses allocate/copy/release, so an allocator without realloc can
serve these operations. An arena retains released blocks until reset; budget
for intermediate buffers as well as the final text. See
[arena_text.c](../examples/arena_text.c).
