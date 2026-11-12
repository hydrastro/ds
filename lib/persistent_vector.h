#ifndef DS_PERSISTENT_VECTOR_H
#define DS_PERSISTENT_VECTOR_H

/*
 * Persistent vector: a bit-partitioned trie (branching 2^bits, 32 by
 * default) with a tail buffer, as in Clojure's PersistentVector (Hickey;
 * Bagwell's ideal hash trees). Elements are stored by value.
 *
 *   get, set               O(log_W n)
 *   push_back, pop_back    O(1) amortized for the tail, O(log_W n) when a
 *                          full tail moves into (or out of) the trie
 *
 * Versions are cheap handles over reference-counted nodes; updates path-
 * copy into a new handle and leave the old version intact. Passing the
 * same handle as input and output edits the nodes only that version
 * holds in place (a transient), so building a vector that nobody else
 * shares costs no copying: the tail is always allocated at full width.
 *
 * Element policies:
 *   plain bytes        neither callback (values, or borrowed pointers the
 *                      caller keeps alive)
 *   owned resources    copy_elem and destroy_elem: a path copy clones each
 *                      element of a copied leaf, every leaf destroys its
 *                      own copies, and pop_back hands out a clone
 *   copy only          copy_elem without destroy_elem (e.g. refcount bumps
 *                      balanced elsewhere)
 * destroy_elem without copy_elem is rejected (DS_ERR_INVALID): copied
 * leaves would share and doubly destroy the resources.
 *
 * ds_pvec_get returns a borrowed pointer, valid while that version lives
 * unchanged. On failure nothing changes and nothing is copied. Memory
 * comes from the context; no locking.
 */

#include "context.h"
#include "schema.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_pvec_config {
  size_t elem_size;
  unsigned bits; /* 1..8: branching 2^bits (config_init sets 5) */
  /* dst is uninitialised storage; return false on failure */
  bool (*copy_elem)(void *dst, const void *src, void *user);
  void (*destroy_elem)(void *elem, void *user);
  void *user;
  ds_context_t *context;
} ds_pvec_config_t;

typedef struct ds_pvec_node {
  size_t refs;
  size_t count; /* live elements (leaf) or children (interior) */
  /* followed by 2^bits child pointers or elements */
} ds_pvec_node_t;

typedef struct ds_pvec {
  ds_pvec_node_t *root; /* NULL while everything fits in the tail */
  ds_pvec_node_t *tail;
  size_t size;
  unsigned shift; /* bits * (trie levels - 1) */
  const ds_pvec_config_t *config;
} ds_pvec_t;

typedef struct ds_pvec_iter {
  const ds_pvec_t *vec;
  size_t index;
  size_t leaf_base;
  const unsigned char *leaf;
} ds_pvec_iter_t;

void ds_pvec_config_init(ds_pvec_config_t *config, size_t elem_size);
/* config_init from an element schema (size, copy, destroy, user); other
 * fields keep their defaults. DS_ERR_INVALID for an invalid schema. */
ds_status_t ds_pvec_config_from_schema(ds_pvec_config_t *config,
                                       const ds_schema_t *schema);
/* the config must outlive every version built from it */
ds_status_t ds_pvec_init(ds_pvec_t *vec, const ds_pvec_config_t *config);
void ds_pvec_copy(const ds_pvec_t *vec, ds_pvec_t *out);
void ds_pvec_release(ds_pvec_t *vec);
size_t ds_pvec_size(const ds_pvec_t *vec);
/* pointer into the version's storage, valid while it lives unchanged */
const void *ds_pvec_get(const ds_pvec_t *vec, size_t index);
/* out may be vec (in place) or another handle */
ds_status_t ds_pvec_push_back(const ds_pvec_t *vec, const void *elem,
                              ds_pvec_t *out);
ds_status_t ds_pvec_pop_back(const ds_pvec_t *vec, void *out_elem,
                             ds_pvec_t *out);
ds_status_t ds_pvec_set(const ds_pvec_t *vec, size_t index, const void *elem,
                        ds_pvec_t *out);
/* keep the first n elements */
ds_status_t ds_pvec_truncate(const ds_pvec_t *vec, size_t n, ds_pvec_t *out);
void ds_pvec_iter_init(ds_pvec_iter_t *it, const ds_pvec_t *vec, size_t start);
const void *ds_pvec_iter_next(ds_pvec_iter_t *it);

#ifdef __cplusplus
}
#endif

#endif /* DS_PERSISTENT_VECTOR_H */
