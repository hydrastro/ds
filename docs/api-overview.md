# Using the APIs

Include `<dsa.h>` to explore the library, `<dsad.h>` or `<dsaa.h>` for a family,
or an individual header such as `<dsa/structures/hash_table.h>`. Every public
header includes its dependencies. Function and type names use the `ds_` prefix.

## Choose the ownership model

There are three useful starting points:

- **Intrusive containers** embed links in your own records. You choose where the
  records live, and insertion usually links an existing node. Use
  `DS_CONTAINER_OF(node, record_type, member)` to recover the containing record;
  the node need not be its first field.
- **Value containers** store elements of a configured byte size. Some accept a
  fixed caller-owned buffer; others grow through an allocation context. Copying
  pointer bytes does not copy or take ownership of the pointed-to object.
- **Pointer containers** store keys and values under explicit copy/destroy
  policies. Configure those policies before inserting owned data.

[Ownership](ownership.md) describes destruction, cloning and persistent versions.
[Mutation and lifetimes](container-lifetimes.md) explains when borrowed pointers,
cursors and handles remain valid.

## Check operational statuses

Status-returning operations separate ordinary conditions from failures:

```c
void *value;
ds_status_t status;

status = ds_hash_table_get(table, key, &value);
if (status == DS_OK) {
  /* The key exists; value may itself be NULL. */
} else if (status == DS_NOT_FOUND) {
  /* The key is absent. */
} else if (DS_FAILED(status)) {
  /* An operational failure. */
}
```

`DS_NOT_FOUND`, `DS_EXISTS`, `DS_EMPTY` and `DS_STOP` are nonnegative conditions.
`DS_ERR_*` values are negative. `DS_SUCCEEDED` means only that the status is
nonnegative; it does not promise that a lookup produced a value.

Pointer, boolean and void convenience functions have module-specific contracts.
Intrusive search/min/max functions often return the container's `nil` sentinel
when empty or absent. The sentinel is not a user record: do not insert, destroy
or interpret it as one. Use status-returning operations when absence and
failure need different handling.

## Supply an allocation context

A `ds_context_t` carries allocator callbacks and user state, resource traits,
a diagnostic sink and the last primary error. The context is borrowed and must
outlive the objects allocated through it. NULL selects the process-global
default in APIs that accept an optional context.

An explicit context lets you use the C heap, an arena, a pool, a buddy allocator
or your own allocation policy. Match resource capabilities to the container:
a growable array that uses `realloc` needs a context that supports it.
Allocator-pair constructors are also available; their allocation and release
functions must match. [Component contracts](contracts.md) explains resource
traits and moving storage between owners.

Containers and reference counts do not supply synchronization. Protect shared
mutable containers, callback state and contexts as a whole, including lookup
followed by use of a borrowed pointer. Independent explicit contexts avoid
sharing the default context's diagnostic/error state.

## Configure complex structures once

Configuration objects collect the policy choices a structure needs. Always call
their initializer before overriding fields:

```c
ds_hash_table_config_t config;
ds_hash_table_t *table;

ds_hash_table_config_init(&config);
config.capacity = 128;
config.hash = my_hash;
config.compare = my_compare;
config.destroy_key = my_destroy_key;
config.destroy_value = my_destroy_value;
table = ds_hash_table_create_config(&config);
if (table == NULL) {
  /* Configuration or allocation failed. */
}
```

The resulting table uses that hash, equality and ownership policy for its
operations. `ds_string_map_t` and `ds_string_set_t` provide string-key wrappers;
they duplicate their keys, while the map borrows its stored values.

## Compose structures through capabilities

Interfaces such as `ds_map_ops_t`, `ds_pq_ops_t` and `ds_rs_ops_t` let a caller
select a backend or provide one. The interface states the operations, costs and
lifetime rules that its consumer can rely on. A custom backend must satisfy
those contracts, not just provide matching function signatures.

For example, a native priority-queue meld needs compatible ordering and memory
ownership. A copying meld can use a different destination backend but has
different costs and handle lifetimes. See [component contracts](contracts.md)
and the [backend examples](../examples/README.md).

## Keep domain Results separate from operation status

`ds_result_t` has an explicit success/error branch, a borrowed or owned payload,
and an immutable ordered diagnostic log. NULL is valid on either branch.
A Result operation returns `ds_status_t` to say whether construction or
composition succeeded; use `ds_result_is_ok` / `ds_result_is_err` to inspect the
domain branch.

Initialize owning handles, then use copy, move and destroy operations instead of
struct assignment. `map` and `and_then` operate on success; `map_error` and
`or_else` operate on failure. Binding preserves preceding diagnostics before
the callback's diagnostics. i18n renders the messages at the presentation
boundary and does not change the Result branch.

See [Results and localization](result-i18n.md), [diagnostics](diagnostics.md),
and [catalogs and locale policies](i18n-catalogs.md).

## Iterate and retain versions deliberately

`ds_iter_t` is a generic iterator shell, not a snapshot or an automatic mutation
guard. Follow the concrete container's invalidation rules, and destroy any
owned iterator state even when a walk ends early.

Persistent containers return immutable versions that share storage. Retain a
version while using its views and release every owned reference. A borrowed
payload also depends on its application's lifetime. The [history layer](history.md)
adds operation logs, branches, snapshots and retroactive edits through adapters.
