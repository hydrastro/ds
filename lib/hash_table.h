#ifndef DS_HASH_TABLE_H
#define DS_HASH_TABLE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifndef HASH_TABLE_RESIZE_FACTOR
#define HASH_TABLE_RESIZE_FACTOR 0.75
#endif

typedef enum {
  HASH_CHAINING,
  HASH_LINEAR_PROBING,
  HASH_QUADRATIC_PROBING,
  HASH_CUSTOM_PROBING
} ds_hash_table_mode_t;

typedef size_t (*ds_hash_probing_func_t)(size_t base_index, size_t iteration,
                                         size_t capacity);

typedef struct hash_node {
  void *key;
  void *value;
  struct hash_node *next;
  struct hash_node *list_next;
  struct hash_node *list_prev;
} ds_hash_node_t;

typedef struct hash_table {
  union {
    ds_hash_node_t **buckets;
    ds_hash_node_t *entries;
  } store;
  size_t size;
  size_t capacity;
  void *nil;
  void *tombstone;
  ds_hash_table_mode_t mode;
  ds_hash_probing_func_t probing_func;
  ds_hash_node_t *last_node;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef HASH_DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_hash_table_t;

ds_hash_node_t *hash_node_create(ds_hash_table_t *table, void *key,
                                 void *value);

size_t hash_func_string_djb2(void *key);
size_t hash_func_int(void *key);
size_t hash_func_pointer(void *key);
size_t hash_func_double(void *key);
size_t hash_func_default(void *key);

size_t next_prime_capacity(size_t current_capacity);

size_t quadratic_probing(size_t base_index, size_t iteration, size_t capacity);
size_t linear_probing(size_t base_index, size_t iteration, size_t capacity);
ds_hash_table_t *FUNC(hash_table_create)(size_t capacity,
                                         ds_hash_table_mode_t mode,
                                         ds_hash_probing_func_t probing_func);
ds_hash_table_t *
    FUNC(hash_table_create_alloc)(size_t capacity, ds_hash_table_mode_t mode,
                                  ds_hash_probing_func_t probing_func,
                                  void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
void FUNC(hash_table_insert)(ds_hash_table_t *table, void *key, void *value,
                             size_t (*hash_func)(void *),
                             int (*compare)(void *, void *));
void FUNC(hash_table_resize)(ds_hash_table_t *table, size_t new_capacity,
                             size_t (*hash_func)(void *),
                             int (*compare)(void *, void *));
void *FUNC(hash_table_lookup)(ds_hash_table_t *table, void *key,
                              size_t (*hash_func)(void *),
                              int (*compare)(void *, void *));
void FUNC(hash_table_remove)(ds_hash_table_t *table, void *key,
                             size_t (*hash_func)(void *),
                             int (*compare)(void *, void *),
                             void (*destroy)(ds_hash_node_t *));
bool FUNC(hash_table_is_empty)(ds_hash_table_t *table);
void FUNC(hash_table_destroy)(ds_hash_table_t *table,
                              void (*destroy)(ds_hash_node_t *));
ds_hash_table_t *FUNC(hash_table_clone)(ds_hash_table_t *table,
                                        void *(*clone_key)(void *),
                                        void *(*clone_value)(void *));

#endif /* DS_HASH_TABLE_H */
