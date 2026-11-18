/* Legacy exported functions delegate to the canonical implementations. */
#include "hash_table.h"
#include "heap.h"
#include "trie.h"
#include "unicode_runtime.h"

#ifndef DS_NO_LEGACY_NAMES
size_t hash_func_string_djb2(void *key) {
  return ds_hash_func_string_djb2(key);
}

size_t hash_func_int(void *key) { return ds_hash_func_int(key); }

size_t hash_func_size_t(void *key) { return ds_hash_func_size_t(key); }

size_t hash_func_pointer(void *key) { return ds_hash_func_pointer(key); }

size_t hash_func_double(void *key) { return ds_hash_func_double(key); }

size_t hash_func_default(void *key) { return ds_hash_func_default(key); }

ds_hash_node_t *hash_node_create(ds_hash_table_t *table, void *key,
                                 void *value) {
  return ds_hash_node_create(table, key, value);
}

size_t next_prime_capacity(size_t current_capacity) {
  return ds_hash_next_prime_capacity(current_capacity);
}

size_t linear_probing(size_t base_index, size_t iteration, size_t capacity) {
  return ds_hash_linear_probing(base_index, iteration, capacity);
}

size_t quadratic_probing(size_t base_index, size_t iteration, size_t capacity) {
  return ds_hash_quadratic_probing(base_index, iteration, capacity);
}

void hash_table_insert(ds_hash_table_t *table, void *key, void *value,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *)) {
  ds_hash_table_insert_with(table, key, value, hash_func, compare);
}

void hash_table_remove(ds_hash_table_t *table, void *key,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *),
                       void (*destroy)(ds_hash_node_t *)) {
  ds_hash_table_remove_with(table, key, hash_func, compare, destroy);
}

void hash_table_destroy(ds_hash_table_t *table,
                        void (*destroy)(ds_hash_node_t *)) {
  ds_hash_table_destroy_with(table, destroy);
}

ds_hash_table_t *hash_table_clone(ds_hash_table_t *table,
                                  void *(*clone_key)(void *),
                                  void *(*clone_value)(void *)) {
  return ds_hash_table_clone_callbacks(table, clone_key, clone_value);
}

void swap(ds_heap_node_t **a, ds_heap_node_t **b) { ds_heap_swap(a, b); }

void heapify_up(ds_heap_t *heap, size_t index,
                int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  ds_heapify_up(heap, index, compare);
}

void heapify_down(ds_heap_t *heap, size_t index,
                  int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  ds_heapify_down(heap, index, compare);
}

int u32vec_push(u32vec_like *v, unsigned long x) {
  return ds_u32vec_push(v, x);
}

void u32vec_free(u32vec_like *v) { ds_u32vec_free(v); }

int ucd_do_init(void) { return ds_ucd_do_init(); }

int ucd_init_once(void) { return ds_ucd_init_once(); }

void ensure_ucd_once_lock(void) { ds_ensure_ucd_once_lock(); }

int u8_next(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp) {
  return ds_u8_next(buf, len, ioff, out_cp);
}

int u8_push(ds_str_t *s, unsigned long cp) { return ds_u8_push(s, cp); }

int is_Hangul_S(unsigned long cp) { return ds_is_Hangul_S(cp); }

int is_Hangul_L(unsigned long cp) { return ds_is_Hangul_L(cp); }

int is_Hangul_V(unsigned long cp) { return ds_is_Hangul_V(cp); }

int is_Hangul_T(unsigned long cp) { return ds_is_Hangul_T(cp); }

void decompose_Hangul(unsigned long s, void *out_u32vec) {
  ds_decompose_Hangul(s, out_u32vec);
}

int compose_Hangul(unsigned long *LVT, unsigned long next, size_t *consumed) {
  return ds_compose_Hangul(LVT, next, consumed);
}

unsigned long get_ccc(unsigned long cp) { return ds_get_ccc(cp); }

int decompose_cp(unsigned long cp, int compat, void *out_u32vec) {
  return ds_decompose_cp(cp, compat, out_u32vec);
}

void reorder_ccc(void *u32vec) { ds_reorder_ccc(u32vec); }

void compose_vec(void *u32vec) { ds_compose_vec(u32vec); }

int fold_cp(unsigned long cp, void *out_u32vec) {
  return ds_fold_cp(cp, out_u32vec);
}

unsigned long gb_get(unsigned long cp) { return ds_gb_get(cp); }

int is_EP(unsigned long cp) { return ds_is_EP(cp); }

ds_trie_t *trie_create_hash(size_t num_splits) {
  return ds_trie_create_hash_alloc(num_splits, malloc, free);
}

void trie_insert(ds_trie_t *trie, void *data,
                 size_t (*get_slice)(void *, size_t),
                 bool (*has_slice)(void *, size_t)) {
  (void)ds_trie_insert(trie, data, get_slice, has_slice);
}

#endif
