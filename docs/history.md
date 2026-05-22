# ds history architecture

The history module is a generic versioning and retroactivity layer for existing
`ds` containers. It intentionally does not make every mutable container aware of
time. Normal containers stay fast and simple; history wraps them through an
operation table.

## Model

A `ds_history_t` owns one object family and one operation interface. A
`ds_history_branch_t` is a branch in an acyclic timeline. Each branch stores only
its own operations and inherits state from its parent at `fork_time`.

The core invariant is:

```text
every branch has finite acyclic ancestry
```

That gives deterministic snapshots and avoids the semantic problems created by
cyclic time dependencies. Loop-like history can be added later as a separate
fixed-point or simulation layer, but it should not be part of the normal branch
DAG.

## Persistence vs retroactivity

Persistence keeps old versions accessible. Retroactivity edits the operation log
in the past and lets the change propagate to later snapshots.

This module supports both through one replay/checkpoint engine:

```text
snapshot_at(branch, t):
  find the nearest checkpoint <= t
  otherwise materialize the parent at fork_time
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
branch, and query.

## Intended tiers

```text
Tier 1: generic clone/replay backend
  Works for most existing mutable containers.

Tier 2: persistent backends
  Structure-specific implementations with structural sharing.

Tier 3: optimized retroactive backends
  Structure-specific algorithms for faster past edits and full retroactivity.
```

The current implementation is Tier 1. It is deliberately general and should be
used as the semantic foundation before adding specialized persistent tries,
persistent red-black trees, HAMT-style maps, or optimized retroactive queues.

## Ownership

When an operation is inserted into a branch, ownership of its payload moves to the
history object. The payload is destroyed when the operation is deleted or the
history is destroyed, if `destroy_payload` is provided.

Snapshots returned by `ds_history_branch_snapshot_at` and
`ds_history_branch_snapshot_head` are owned by the caller and must be destroyed
with `ds_history_snapshot_destroy`.

Query result ownership is adapter-specific. The history layer simply returns the
value produced by the adapter's `query` callback.
