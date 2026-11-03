#ifndef DS_RADIX_TRIE_H
#define DS_RADIX_TRIE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radix_trie_node {
  unsigned char *edge;
  size_t edge_length;
  struct radix_trie_node **children;
  size_t num_children;
  bool is_terminal;
  void *value;
} ds_radix_trie_node_t;

typedef struct radix_trie {
  ds_radix_trie_node_t *root;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_radix_trie_t;

ds_radix_trie_t *ds_radix_trie_create(void);
ds_radix_trie_t *ds_radix_trie_create_alloc(void *(*allocator)(size_t),
                                            void (*deallocator)(void *));
/* Inserts key/value. If the key already exists its value is updated (upsert)
 * and false is returned; returns true only when a new key is added. */
bool ds_radix_trie_insert(ds_radix_trie_t *trie, const void *key, size_t length,
                          void *value);
bool ds_radix_trie_contains(ds_radix_trie_t *trie, const void *key,
                            size_t length);
void *ds_radix_trie_get(ds_radix_trie_t *trie, const void *key, size_t length);
bool ds_radix_trie_remove(ds_radix_trie_t *trie, const void *key, size_t length,
                          void **out_value);
size_t ds_radix_trie_size(ds_radix_trie_t *trie);
bool ds_radix_trie_is_empty(ds_radix_trie_t *trie);
void ds_radix_trie_walk(ds_radix_trie_t *trie,
                        void (*callback)(const unsigned char *, size_t,
                                         void *));
void ds_radix_trie_destroy(ds_radix_trie_t *trie,
                           void (*destroy_value)(void *));

#ifdef __cplusplus
}
#endif

#endif /* DS_RADIX_TRIE_H */
