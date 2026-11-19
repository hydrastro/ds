#ifndef DS_RADIX_TRIE_H
#define DS_RADIX_TRIE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
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
  ds_context_t *context; /* borrowed allocation owner */
} ds_radix_trie_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_radix_trie_create_context(ds_context_t *context,
                                         ds_radix_trie_t **out);

ds_radix_trie_t *ds_radix_trie_create(ds_context_t *context);
/* DS_OK/new or DS_EXISTS/update. Allocation failure leaves the tree unchanged.
 */
ds_status_t ds_radix_trie_insert_ex(ds_radix_trie_t *trie, const void *key,
                                    size_t length, void *value);
/* true only for a new key; false on update or failure. */
bool ds_radix_trie_insert(ds_radix_trie_t *trie, const void *key,
                          size_t length, void *value);
bool ds_radix_trie_contains(ds_radix_trie_t *trie, const void *key,
                            size_t length);
void *ds_radix_trie_get(ds_radix_trie_t *trie, const void *key, size_t length);
bool ds_radix_trie_remove(ds_radix_trie_t *trie, const void *key,
                          size_t length, void **out_value);
/* Removal has no key-depth limit and succeeds if optional edge compression
 * cannot allocate; a valid unary node then remains. */
size_t ds_radix_trie_size(ds_radix_trie_t *trie);
bool ds_radix_trie_is_empty(ds_radix_trie_t *trie);
void ds_radix_trie_walk(ds_radix_trie_t *trie,
                        void (*callback)(const unsigned char *, size_t,
                                         void *));
void ds_radix_trie_destroy(ds_radix_trie_t *trie,
                           void (*destroy_value)(void *));

typedef ds_status_t (*ds_radix_trie_visit_fn)(const unsigned char *key,
                                              size_t length, void *value,
                                              void *user);
/* Find distinguishes a stored NULL value from absence. Error/DS_NOT_FOUND
 * preserves *out. NULL key is valid only for zero length. */
ds_status_t ds_radix_trie_find_ex(ds_radix_trie_t *tree,
                                  const unsigned char *key, size_t length,
                                  void **out);
/* Stops on the visitor's first non-DS_OK status. Visits already performed are
 * not rolled back; borrowed key/value views may not be retained or mutated.
 * Radix traversal also reports allocation/overflow failures in its scratch. */
ds_status_t ds_radix_trie_walk_with(ds_radix_trie_t *tree,
                                    ds_radix_trie_visit_fn visit, void *user);
/* Optional destroy receives each stored value and the supplied user data;
 * it must not inspect/traverse/mutate this container during destruction. */
void ds_radix_trie_destroy_with(ds_radix_trie_t *tree,
                                void (*destroy)(void *, void *), void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_RADIX_TRIE_H */
