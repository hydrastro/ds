#ifndef DS_TST_H
#define DS_TST_H

/*
 * Ternary search trees (Bentley and Sedgewick): a trie whose nodes are
 * binary search tree nodes on one byte, with low, equal and high children.
 * They use far less memory than array tries for sparse alphabets and
 * support the searches tries are good at.
 *
 * Keys are byte strings (any bytes, explicit lengths, the empty key
 * included); each key carries a size_t value. Operations: insert (value
 * replaced on a duplicate), find, delete with pruning of dead nodes,
 * ordered prefix enumeration, partial-match search (a chosen byte is a
 * wildcard matching any one byte) and near-neighbour search (keys of the
 * same length within Hamming distance d). Traversals use an explicit
 * stack, never recursion.
 *
 * Concurrency: a tree may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_tst {
  unsigned char *split;
  size_t *lo;
  size_t *eq;
  size_t *hi;
  size_t *value;
  unsigned char *terminal;
  size_t root;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t keys;
  bool has_empty; /* the empty key */
  size_t empty_value;
  size_t *stack; /* scratch */
  size_t stack_capacity;
  ds_context_t *context;
} ds_tst_t;

typedef bool (*ds_tst_visit_func_t)(const unsigned char *key, size_t len,
                                    size_t value, void *user);

ds_status_t ds_tst_init(ds_tst_t *t, ds_context_t *context);
void ds_tst_destroy(ds_tst_t *t);
size_t ds_tst_size(const ds_tst_t *t);
/* DS_EXISTS (value replaced) for a present key. */
ds_status_t ds_tst_insert(ds_tst_t *t, const unsigned char *key, size_t len,
                          size_t value);
ds_status_t ds_tst_find(const ds_tst_t *t, const unsigned char *key, size_t len,
                        size_t *out_value);
ds_status_t ds_tst_remove(ds_tst_t *t, const unsigned char *key, size_t len);
/* Keys starting with prefix, in lexicographic order. buf (buf_len bytes)
 * receives each key: DS_ERR_RANGE if a key does not fit. */
ds_status_t ds_tst_prefix(ds_tst_t *t, const unsigned char *prefix, size_t len,
                          unsigned char *buf, size_t buf_len,
                          ds_tst_visit_func_t visit, void *user);
/* Keys of length len matching pattern, `wildcard` matching any byte. */
ds_status_t ds_tst_partial_match(ds_tst_t *t, const unsigned char *pattern,
                                 size_t len, unsigned char wildcard,
                                 unsigned char *buf, ds_tst_visit_func_t visit,
                                 void *user);
/* Keys of length len differing from key in at most d positions. */
ds_status_t ds_tst_near(ds_tst_t *t, const unsigned char *key, size_t len,
                        size_t d, unsigned char *buf, ds_tst_visit_func_t visit,
                        void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_TST_H */
