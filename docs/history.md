# History architecture

The history module is a generic versioning and retroactivity layer for existing
`ds` containers. It intentionally does not make every mutable container aware of
time. Normal containers stay fast and simple; history wraps them through an
operation table.

## Model

A `ds_history_t` owns one object family and one operation interface. A
`ds_history_branch_t` is a branch in an acyclic timeline. Each branch stores its
own operation log and inherits state from its parent at `fork_time`.

The core invariant is:

```txt
every branch has finite acyclic ancestry
```

That gives deterministic snapshots and avoids the semantic problems created by
cyclic time dependencies. Loop-like history can be explored later as a separate
fixed-point/simulation layer, but it should not be part of the normal branch
DAG.

## Persistence vs retroactivity

Persistence keeps old versions accessible. Retroactivity edits the operation log
in the past and lets the change propagate to later snapshots.

This module supports both through a replay/checkpoint engine:

```txt
snapshot_at(branch, t):
  find nearest checkpoint <= t
  otherwise materialize parent at fork_time
  replay branch-local operations up to t
```

Retroactive insertion/deletion invalidates affected checkpoints. If a parent is
edited before a child branch's fork point, descendant checkpoints are invalidated
as well.

## Operation interface

A container adapter supplies:

- `create_state`
- `clone_state`
- `destroy_state`
- `apply`
- `query`
- optional payload clone/destroy callbacks

The history engine knows nothing about hash tables, tries, lists, or trees. It
only knows how to order operations, materialize snapshots, cache checkpoints,
branch, serialize, merge, and query.

## Transactions

Transactions batch checkpoint invalidation while multiple history edits are
performed:

```c
ds_history_transaction_begin(history);
/* insert/delete/merge operations */
ds_history_transaction_commit(history);
```

This is useful for bulk retroactive edits or branch merges.

## Branch merge

`ds_history_branch_merge` performs deterministic operation-log merging.
`ds_history_branch_merge_with` adds a callback-driven merge policy hook.

The history layer can choose whether to take, skip, or stop on an operation, but
semantic payload conflicts belong to the wrapped container adapter.

## Ownership

When an operation is inserted into a branch, ownership of its payload moves to
the history object. The payload is destroyed when the operation is deleted or the
history is destroyed if `destroy_payload` is provided.

Snapshots returned by `ds_history_branch_snapshot_at` and
`ds_history_branch_snapshot_head` are owned by the caller and must be destroyed
with `ds_history_snapshot_destroy`.

Query result ownership is adapter-specific. The history layer simply returns the
value produced by the adapter's `query` callback.

## Serialization and archives

History serialization has several layers:

- branch-local operation export/import
- full multi-branch serialization/deserialization
- portable little-endian metadata helpers
- `ds_history_archive_t`, an owned in-memory byte archive

Full serialization preserves:

- branch IDs
- branch names
- parent relationships
- fork times
- operation IDs
- operation times
- operation kinds
- adapter-owned operation payloads

Payload encoding/decoding is callback-driven. The history engine writes generic
operation metadata and calls adapter-provided callbacks for payload bytes. This
keeps the core history module generic.

Use the portable helpers when archives need to survive machine-endianness or
integer-representation differences.
