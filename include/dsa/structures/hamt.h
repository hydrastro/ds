#ifndef DS_HAMT_H
#define DS_HAMT_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
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
  ds_context_t *context; /* borrowed allocation owner */
} ds_hamt_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_hamt_create_context(ds_context_t *context, ds_hamt_t **out);

ds_hamt_t *ds_hamt_create(ds_context_t *context);
/* DS_OK for a new key, DS_EXISTS for an update. Allocation/size failure
 * leaves all existing keys and values unchanged; values remain borrowed. */
ds_status_t ds_hamt_insert_ex(ds_hamt_t *hamt, const void *key,
                              size_t key_length, void *value);
/* true for a new key; false on update or failure (use _ex to distinguish). */
bool ds_hamt_insert(ds_hamt_t *hamt, const void *key, size_t key_length,
                    void *value);
bool ds_hamt_contains(ds_hamt_t *hamt, const void *key, size_t key_length);
void *ds_hamt_get(ds_hamt_t *hamt, const void *key, size_t key_length);
bool ds_hamt_remove(ds_hamt_t *hamt, const void *key, size_t key_length,
                    void **out_value);
size_t ds_hamt_size(ds_hamt_t *hamt);
bool ds_hamt_is_empty(ds_hamt_t *hamt);
void ds_hamt_walk(ds_hamt_t *hamt,
                  void (*callback)(const void *, size_t, void *, void *),
                  void *user);
void ds_hamt_destroy(ds_hamt_t *hamt, void (*destroy_value)(void *));

typedef ds_status_t (*ds_hamt_visit_fn)(const void *key, size_t length,
                                        void *value, void *user);
/* Find distinguishes a stored NULL value from absence. Error/DS_NOT_FOUND
 * preserves *out. NULL key is valid only for zero length. */
ds_status_t ds_hamt_find_ex(ds_hamt_t *tree, const void *key, size_t length,
                            void **out);
/* Stops on the visitor's first non-DS_OK status. Visits already performed are
 * not rolled back; borrowed key/value views may not be retained or mutated.
 * Radix traversal also reports allocation/overflow failures in its scratch. */
ds_status_t ds_hamt_walk_with(ds_hamt_t *tree, ds_hamt_visit_fn visit,
                              void *user);
/* Optional destroy receives each stored value and the supplied user data;
 * it must not inspect/traverse/mutate this container during destruction. */
void ds_hamt_destroy_with(ds_hamt_t *tree, void (*destroy)(void *, void *),
                          void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_HAMT_H */
