#ifndef DS_ROPE_H
#define DS_ROPE_H

#include "common.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rope: a binary tree of string fragments supporting efficient concatenation,
 * splitting, insertion, and deletion on large strings. Internal nodes carry a
 * weight equal to the total length of their left subtree, giving O(log n)
 * index/charAt. Leaves hold up to 64 bytes. Every edit maintains AVL balance
 * by preparing new paths over reference-counted immutable subtrees, then
 * publishing the root; failure leaves the rope unchanged. Indexing is
 * O(log leaves), joining O(log leaves), building inserted text O(bytes).
 * Split-based edits have an O(log^2 leaves) conservative bound in addition
 * to copying inserted text. All recursion is bounded by balanced height.
 * Indices and lengths are byte offsets. Node fields are read-only to callers.
 */

typedef struct rope_node {
  struct rope_node *left;
  struct rope_node *right;
  char *data;    /* leaf buffer (NULL for internal nodes) */
  size_t weight; /* length of left subtree (internal) or leaf length (leaf) */
  size_t length; /* total length of this subtree */
  size_t depth;  /* levels in this subtree, 1 for a leaf */
  size_t leaves; /* leaves in this subtree */
  bool is_leaf;
  size_t refs;
} ds_rope_node_t;

typedef struct rope {
  ds_rope_node_t *root;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_rope_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_rope_t *ds_rope_create(void);
ds_rope_t *ds_rope_create_alloc(void *(*allocator)(size_t),
                                void (*deallocator)(void *));
ds_rope_t *ds_rope_from_string(const char *str, size_t length);
size_t ds_rope_length(ds_rope_t *rope);
/* character at index, or '\0' if out of range */
char ds_rope_char_at(ds_rope_t *rope, size_t index);
/* Checked edits; all inputs remain unchanged on failure. Text may be borrowed
 * from this rope. Indices past the end append; oversized deletions clamp.
 * Legacy void wrappers leave the rope unchanged if the checked edit fails. */
ds_status_t ds_rope_append_ex(ds_rope_t *rope, const char *str, size_t length);
ds_status_t ds_rope_insert_ex(ds_rope_t *rope, size_t index, const char *str,
                              size_t length);
ds_status_t ds_rope_delete_ex(ds_rope_t *rope, size_t index, size_t count);
ds_status_t ds_rope_concat_ex(ds_rope_t *rope, ds_rope_t *other);
ds_status_t ds_rope_rebalance_ex(ds_rope_t *rope);
/* append text to the end */
void ds_rope_append(ds_rope_t *rope, const char *str, size_t length);
/* insert text at byte offset index */
void ds_rope_insert(ds_rope_t *rope, size_t index, const char *str,
                    size_t length);
/* delete `count` bytes starting at index */
void ds_rope_delete(ds_rope_t *rope, size_t index, size_t count);
/* copy [index, index+count) into out (caller-provided), returns bytes copied */
size_t ds_rope_substring(ds_rope_t *rope, size_t index, size_t count,
                         char *out);
/* materialize the whole rope into a freshly allocated NUL-terminated buffer */
char *ds_rope_to_string(ds_rope_t *rope);
/* concatenate other onto the end of rope; other is emptied on success.
 * Self-concatenation and different allocator pairs are DS_ERR_INVALID. */
void ds_rope_concat(ds_rope_t *rope, ds_rope_t *other);
void ds_rope_rebalance(ds_rope_t *rope);
void ds_rope_destroy(ds_rope_t *rope);

#ifdef __cplusplus
}
#endif

#endif /* DS_ROPE_H */
