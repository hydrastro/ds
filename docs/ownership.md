# Ownership model

`ds` deliberately separates container storage from user data ownership.

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
- pool frees reject pointers outside the pool and double-free attempts;
- debug allocator statistics are intended for diagnostics and test visibility.

Use the default context or a debug allocator when a module requires general
`realloc` behavior.

## Allocation failure

Two allocation profiles coexist. Every module states which one its API
follows; an adapter over a module keeps that module's profile and must not
claim the stronger one unless it checks for failure itself.

**Fallible profile** (status-returning APIs, and every module added or
reworked since the capability interfaces). An operation that returns
`ds_status_t` reports allocator exhaustion as `DS_ERR_ALLOC` and size
arithmetic that does not fit as `DS_ERR_OVERFLOW`. Unless its header says
otherwise, a failed operation leaves the structure unchanged and usable.
Where that cannot hold, the header states exactly what remains, for example:

- `lib/topology_tree` reserves its work lists before committing; if a
  commit still fails it marks the tree broken, queries report it, and
  `ds_topology_tree_rebuild` restores it from the adjacency;
- `lib/paged_btree` prepares every page a split needs before changing any;
- `lib/pq_iface`'s copying meld stops at the failing item with both queues
  valid and every item in exactly one of them;
- `lib/pager`'s `ds_pager_close` keeps the pager and its dirty pages alive
  when a write fails, so the close can be retried, and `ds_pager_discard`
  drops them explicitly.

Release paths are written not to need fresh memory: the trie child stores,
for instance, fall back to visiting children in place when they cannot
take a snapshot. A release path that still allocates says what it does on
failure.

**Infallible profile** (older APIs that return a node pointer or `void` and
whose header documents no failure value, such as the original intrusive
containers' `*_create` helpers). These assume the allocator returns a valid,
non-NULL pointer; the returned pointer is never a failure sentinel. Use them
with an allocator that never returns NULL: one that aborts, `longjmp`s to a
recovery point, or otherwise handles exhaustion itself. With such an
allocator the two profiles behave the same.

**Allocator capability limits are not memory exhaustion.** A fixed pool
running out of blocks and an arena refusing to `realloc` an existing
pointer are limits of those allocators. Fallible APIs report both as
`DS_ERR_ALLOC`, indistinguishable from exhaustion by status alone, so pick
an allocator whose limits suit the structure (anything that grows by
`realloc` needs a context with general `realloc`).

**Other failures are separate.** I/O errors (`DS_ERR_STATE`, as in
`algo/external_sort` and `lib/pager`), integer overflow
(`DS_ERR_OVERFLOW`), ownership mismatches (`DS_ERR_INVALID` or
`DS_ERR_UNSUPPORTED`, e.g. an element destructor without a copy function
where values are shared) and unsupported operations are reported in their
own right, whichever allocation profile applies.

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
  `string_map`) return the stored `void *` value, or `NULL` when the key is
  absent — `NULL` is the value's natural "not present" marker here.
- Index-returning succinct structures (`bitset`, `bitvector`, `wavelet_tree`)
  return a published `DS_*_NPOS` constant when no position satisfies the query.
