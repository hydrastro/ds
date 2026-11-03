#ifndef DS_HAMT_H
#define DS_HAMT_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hash array mapped trie (HAMT): a map from byte-string keys to void* values.
 * Each internal node covers 5 bits of the key's hash and stores a 32-bit
 * occupancy bitmap plus a compact array holding only the children that exist.
 * To find the child for slot i: test bit i of the bitmap; if set, its array
 * index is popcount(bitmap & ((1u << i) - 1)). This gives shallow trees with
 * no wasted slots -- the defining HAMT trick. Hash collisions beyond the hash
 * length are resolved by a linear collision list at the leaf.
 */

typedef struct hamt_node ds_hamt_node_t;

typedef struct hamt {
  ds_hamt_node_t *root;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_hamt_t;

ds_hamt_t *FUNC(ds_hamt_create)(void);
ds_hamt_t *FUNC(ds_hamt_create_alloc)(void *(*allocator)(size_t),
                                      void (*deallocator)(void *));
/* insert or update; returns true if a new key was added, false on update */
bool FUNC(ds_hamt_insert)(ds_hamt_t *hamt, const void *key, size_t key_length,
                          void *value);
bool FUNC(ds_hamt_contains)(ds_hamt_t *hamt, const void *key,
                            size_t key_length);
void *FUNC(ds_hamt_get)(ds_hamt_t *hamt, const void *key, size_t key_length);
bool FUNC(ds_hamt_remove)(ds_hamt_t *hamt, const void *key, size_t key_length,
                          void **out_value);
size_t FUNC(ds_hamt_size)(ds_hamt_t *hamt);
bool FUNC(ds_hamt_is_empty)(ds_hamt_t *hamt);
void FUNC(ds_hamt_walk)(ds_hamt_t *hamt,
                        void (*callback)(const void *, size_t, void *, void *),
                        void *user);
void FUNC(ds_hamt_destroy)(ds_hamt_t *hamt, void (*destroy_value)(void *));

#ifdef __cplusplus
}
#endif

#endif /* DS_HAMT_H */
