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

For hash tables:

- `destroy_key == NULL`: keys are borrowed.
- `destroy_key != NULL`: keys are owned by the table.
- `destroy_value == NULL`: values are borrowed.
- `destroy_value != NULL`: values are owned by the table.

Clone callbacks follow the same rule: if no clone callback is supplied, cloned
containers share the pointer. If a clone callback is supplied, cloned containers
own the cloned pointer according to the matching destructor.

## Intrusive structures

Intrusive structures store links inside user-provided node structs. The user owns
intrusive node memory unless a destroy callback is passed to the relevant destroy
function.

## Persistent/versioned structures

Persistent versions are immutable handles. Releasing one version must not affect
other versions. Tier-1 persistent wrappers may clone whole internal structures;
structural sharing is an implementation detail and not a public ownership rule.

## History timelines

`ds_history_branch_insert_at` takes ownership of the operation payload. The
payload is released through `destroy_payload` when the operation is deleted or
when the history is destroyed. Snapshots returned by `snapshot_at` are owned by
the caller and must be released with `ds_history_snapshot_destroy`.
