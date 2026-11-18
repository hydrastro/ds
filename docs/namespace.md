# Symbol namespace

All implementation entry points now have a `ds_` prefix. The default build
retains the old public function names as small wrappers in `legacy_names.c`.
Canonical code does not call those wrappers. Existing source callers can
migrate gradually using this table; the wrappers keep their original signatures.
The old void trie insertion delegates to the checked entry point and ignores
its status. Prefer the status-returning call in new code.

| Legacy function | Canonical function |
|---|---|
| `hash_func_string_djb2` | `ds_hash_func_string_djb2` |
| `hash_func_int` | `ds_hash_func_int` |
| `hash_func_size_t` | `ds_hash_func_size_t` |
| `hash_func_pointer` | `ds_hash_func_pointer` |
| `hash_func_double` | `ds_hash_func_double` |
| `hash_func_default` | `ds_hash_func_default` |
| `hash_node_create` | `ds_hash_node_create` |
| `next_prime_capacity` | `ds_hash_next_prime_capacity` |
| `linear_probing` | `ds_hash_linear_probing` |
| `quadratic_probing` | `ds_hash_quadratic_probing` |
| `hash_table_insert` | `ds_hash_table_insert_with` |
| `hash_table_remove` | `ds_hash_table_remove_with` |
| `hash_table_destroy` | `ds_hash_table_destroy_with` |
| `hash_table_clone` | `ds_hash_table_clone_callbacks` |
| `swap` | `ds_heap_swap` |
| `heapify_up` | `ds_heapify_up` |
| `heapify_down` | `ds_heapify_down` |
| `u32vec_push` | `ds_u32vec_push` |
| `u32vec_free` | `ds_u32vec_free` |
| `ucd_do_init` | `ds_ucd_do_init` |
| `ucd_init_once` | `ds_ucd_init_once` |
| `ensure_ucd_once_lock` | `ds_ensure_ucd_once_lock` |
| `u8_next` | `ds_u8_next` |
| `u8_push` | `ds_u8_push` |
| `is_Hangul_S` | `ds_is_Hangul_S` |
| `is_Hangul_L` | `ds_is_Hangul_L` |
| `is_Hangul_V` | `ds_is_Hangul_V` |
| `is_Hangul_T` | `ds_is_Hangul_T` |
| `decompose_Hangul` | `ds_decompose_Hangul` |
| `compose_Hangul` | `ds_compose_Hangul` |
| `get_ccc` | `ds_get_ccc` |
| `decompose_cp` | `ds_decompose_cp` |
| `reorder_ccc` | `ds_reorder_ccc` |
| `compose_vec` | `ds_compose_vec` |
| `fold_cp` | `ds_fold_cp` |
| `gb_get` | `ds_gb_get` |
| `is_EP` | `ds_is_EP` |
| `trie_create_hash` | `ds_trie_create_hash` |
| `trie_insert` | `ds_trie_insert` |

To omit the old declarations, define `DS_NO_LEGACY_NAMES` before including
headers. To omit their binary exports too, rebuild from clean objects:

```sh
make clean
make CPPFLAGS=-DDS_NO_LEGACY_NAMES all examples
```

Use the same macro when compiling clients. Changing compiler flags requires
a clean rebuild, as with other build flags. The source examples use canonical
names. `swap` and the other generic names therefore need not collide with an
embedding application's symbols.

Generated Unicode tables are private implementation data, excluded from the
installed headers. Their symbols now use `ds_ucd_`; internal source aliases
retain the old `U_*` spellings unless disabled by the same macro. Their former
binary names are not retained. Recompile the library and clients together; this
release also extends several public structs and makes no binary-layout promise.
