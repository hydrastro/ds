# API overview

`ds` currently exposes both legacy intrusive APIs and newer `ds_`-prefixed APIs.
The newer APIs are preferred for new code because they carry clearer ownership,
status, context, and error semantics.

## Status-first functions

New APIs generally return `ds_status_t` and write results through out-parameters:

```c
void *out;
ds_status_t status;

status = ds_hash_table_get(table, key, &out);
if (status == DS_OK) {
  /* found */
} else if (status == DS_NOT_FOUND) {
  /* missing */
} else if (DS_FAILED(status)) {
  /* actual failure */
}
```

`DS_NOT_FOUND`, `DS_EXISTS`, `DS_EMPTY`, and `DS_STOP` are nonnegative
conditions. `DS_ERR_*` values are failures.

## Contexts

A `ds_context_t` carries:

- allocator callbacks
- a diagnostic sink
- the last primary error
- user data for allocator callbacks

Passing `NULL` for a context uses the default process-global fallback. Serious
multi-threaded code should prefer explicit contexts, especially when reading
`last_error` or collecting diagnostics.

## Config constructors

Complex containers should be configured once and then used with simple calls.
For example, the config-based hash table stores its hash, comparator,
ownership, and context callbacks inside the table:

```c
ds_hash_table_config_t config;
ds_hash_table_config_init(&config);
config.capacity = 128;
config.hash = my_hash;
config.compare = my_compare;
config.destroy_key = my_destroy_key;
config.destroy_value = my_destroy_value;

table = ds_hash_table_create_config(&config);
```

Then:

```c
ds_hash_table_insert(table, key, value);
ds_hash_table_get(table, key, &out);
ds_hash_table_remove(table, key);
```

## Legacy APIs

Legacy APIs remain available for compatibility. Many expose intrusive nodes and
return a container-private sentinel for missing values. Those sentinels are not
part of the public interface, so new code should prefer the `ds_status_t` APIs
where available.

Examples of legacy functions that remain compatibility-oriented:

```txt
hash_table_lookup
bst_search
btree_search
avl_search
rbt_search
stack_search
queue_search
list_search
dlist_search
```

## Iteration

`ds_iter_t` is the generic iterator shell used by newer modules. Iterator
thread-safety depends on the container. If a container may be mutated while an
iteration is active, use external synchronization unless the specific iterator
API documents stronger guarantees.

## Convenience wrappers

`ds_string_map_t` and `ds_string_set_t` are high-level wrappers over the new
hash-table API. They own duplicated string keys and borrow stored values.

## Persistent containers

Persistent containers are immutable by version:

- `ds_ptrie_t` / `ds_ptrie_version_t`
- `ds_prbt_t` / `ds_prbt_version_t`

An update returns a new version. Old versions remain valid until released or
until the owning container is destroyed.
