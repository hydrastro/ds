#ifndef DS_RADIX_TRIE_H
#define DS_RADIX_TRIE_H

#include "common.h"
#include "status.h"
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
  struct radix_trie_node *parent;
} ds_radix_trie_node_t;

typedef struct radix_trie {
  ds_radix_trie_node_t *root;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_radix_trie_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_radix_trie_t *ds_radix_trie_create(void);
ds_radix_trie_t *ds_radix_trie_create_alloc(void *(*allocator)(size_t),
                                            void (*deallocator)(void *));
/* DS_OK/new or DS_EXISTS/update. Allocation failure leaves the tree unchanged.
 */
ds_status_t ds_radix_trie_insert_ex(ds_radix_trie_t *trie, const void *key,
                                    size_t length, void *value);
/* true only for a new key; false on update or failure. */
bool ds_radix_trie_insert(ds_radix_trie_t *trie, const void *key, size_t length,
                          void *value);
bool ds_radix_trie_contains(ds_radix_trie_t *trie, const void *key,
                            size_t length);
void *ds_radix_trie_get(ds_radix_trie_t *trie, const void *key, size_t length);
bool ds_radix_trie_remove(ds_radix_trie_t *trie, const void *key, size_t length,
                          void **out_value);
/* Removal has no key-depth limit and succeeds if optional edge compression
 * cannot allocate; a valid unary node then remains. */
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
