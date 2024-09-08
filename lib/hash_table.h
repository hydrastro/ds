#ifndef DS_HASH_TABLE_H
#define DS_HASH_TABLE_H

#include <stddef.h>

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

#ifndef DS_HASH_TABLE_RESIZE_FACTOR
#define DS_HASH_TABLE_RESIZE_FACTOR 0.75
#endif

typedef enum {
  HASH_CHAINING,
  HASH_LINEAR_PROBING,
  HASH_QUADRATIC_PROBING,
  HASH_CUSTOM_PROBING
} hash_table_mode_t;

typedef size_t (*hash_probing_func_t)(size_t base_index, size_t iteration,
                                      size_t capacity);

typedef struct hash_node {
  void *key;
  void *value;
  struct hash_node *next;
} hash_node_t;

typedef struct hash_table {
  union {
    hash_node_t **buckets;
    hash_node_t *entries;
  };
  size_t size;
  size_t capacity;
  void *nil;
  void *tombstone;
  hash_table_mode_t mode;
  hash_probing_func_t probing_func;
} hash_table_t;

size_t hash_func_string_djb2(void *key);
size_t hash_func_int(void *key);
size_t hash_func_pointer(void *key);
size_t hash_func_double(void *key);
size_t hash_func_default(void *key);

size_t quadratic_probing(size_t base_index, size_t iteration, size_t capacity);
size_t linear_probing(size_t base_index, size_t iteration, size_t capacity);
hash_table_t *hash_table_create(size_t capacity, hash_table_mode_t mode,
                                hash_probing_func_t probing_func);
void hash_table_insert(hash_table_t *table, void *key, void *value,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *));
void hash_table_resize(hash_table_t *table, size_t new_capacity,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *));
void *hash_table_lookup(hash_table_t *table, void *key,
                        size_t (*hash_func)(void *),
                        int (*compare)(void *, void *));
void hash_table_remove(hash_table_t *table, void *key,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *),
                       void (*destroy_node)(hash_node_t *));
int hash_table_is_empty(hash_table_t *table);
void hash_table_destroy(hash_table_t *table,
                        void (*destroy_node)(hash_node_t *));

#endif // DS_HASH_TABLE_H
