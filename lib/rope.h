#ifndef DS_ROPE_H
#define DS_ROPE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rope: a binary tree of string fragments supporting efficient concatenation,
 * splitting, insertion, and deletion on large strings. Internal nodes carry a
 * weight equal to the total length of their left subtree, giving O(log n)
 * index/charAt. Leaves hold contiguous character buffers. The rope rebalances
 * when it becomes too deep relative to its length, keeping operations
 * logarithmic. Indices and lengths are byte offsets.
 */

typedef struct rope_node {
  struct rope_node *left;
  struct rope_node *right;
  char *data;    /* leaf buffer (NULL for internal nodes) */
  size_t weight; /* length of left subtree (internal) or leaf length (leaf) */
  size_t length; /* total length of this subtree */
  bool is_leaf;
} ds_rope_node_t;

typedef struct rope {
  ds_rope_node_t *root;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_rope_t;

ds_rope_t *FUNC(ds_rope_create)(void);
ds_rope_t *FUNC(ds_rope_create_alloc)(void *(*allocator)(size_t),
                                      void (*deallocator)(void *));
ds_rope_t *FUNC(ds_rope_from_string)(const char *str, size_t length);
size_t FUNC(ds_rope_length)(ds_rope_t *rope);
/* character at index, or '\0' if out of range */
char FUNC(ds_rope_char_at)(ds_rope_t *rope, size_t index);
/* append text to the end */
void FUNC(ds_rope_append)(ds_rope_t *rope, const char *str, size_t length);
/* insert text at byte offset index */
void FUNC(ds_rope_insert)(ds_rope_t *rope, size_t index, const char *str,
                          size_t length);
/* delete `count` bytes starting at index */
void FUNC(ds_rope_delete)(ds_rope_t *rope, size_t index, size_t count);
/* copy [index, index+count) into out (caller-provided), returns bytes copied */
size_t FUNC(ds_rope_substring)(ds_rope_t *rope, size_t index, size_t count,
                               char *out);
/* materialize the whole rope into a freshly allocated NUL-terminated buffer */
char *FUNC(ds_rope_to_string)(ds_rope_t *rope);
/* concatenate other onto the end of rope; other is emptied (not destroyed) */
void FUNC(ds_rope_concat)(ds_rope_t *rope, ds_rope_t *other);
void FUNC(ds_rope_rebalance)(ds_rope_t *rope);
void FUNC(ds_rope_destroy)(ds_rope_t *rope);

#ifdef __cplusplus
}
#endif

#endif /* DS_ROPE_H */
