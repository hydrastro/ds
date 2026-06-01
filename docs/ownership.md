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

Clone callbacks follow the same rule: if no clone callback is supplied, cloned
containers share the pointer. If a clone callback is supplied, cloned containers
own the cloned pointer according to the matching destructor.

`ds_hash_table_insert_take_key` exists for wrappers that already allocated a key
and need the table to dispose of that key atomically when an update finds an
existing equal key. `ds_string_map_put` uses this to avoid contains-then-insert
races in safe builds.

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

The library treats allocator results as infallible: `*_create`,
`*_create_alloc`, and every operation that grows a structure assume the supplied
allocator returns a valid, non-NULL pointer. There are deliberately no
out-of-memory error returns to thread through call sites. A program that must
survive memory exhaustion supplies an allocator that does not return NULL — one
that aborts, `longjmp`s to a recovery point, or otherwise handles the failure
itself. The default `malloc`/`free` pair follows the platform's behavior; wrap
it if you need a stricter policy. This choice keeps intrusive return values
unambiguous: a returned node pointer is always a real node, never an
allocation-failure sentinel.

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
