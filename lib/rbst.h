#ifndef DS_RBST_H
#define DS_RBST_H

/*
 * Randomized binary search trees (Martinez and Roura): a new key becomes
 * the root of a subtree of size n with probability 1/(n + 1) (by
 * splitting), and deletion joins the two subtrees choosing each root with
 * probability proportional to its size. The tree is a random BST whatever
 * the order of operations, so every operation takes O(log n) expected
 * time. Subtree sizes give select and rank; split and join of whole trees
 * are exposed too.
 *
 * The random source is pluggable (NULL: an internal xorshift generator
 * seeded per tree). Elements are stored by value in node arrays.
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

#define DS_RBST_NONE (~(size_t)0)

typedef int (*ds_rbst_compare_func_t)(const void *a, const void *b, void *user);
/* Uniform random bits (at least 31 of them). */
typedef unsigned long (*ds_rbst_random_func_t)(void *state);

typedef struct ds_rbst {
  unsigned char *data;
  size_t *left;
  size_t *right;
  size_t *size;
  size_t root;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  ds_rbst_compare_func_t compare;
  void *user;
  ds_rbst_random_func_t random;
  void *random_state;
  unsigned long seed;
  ds_context_t *context;
} ds_rbst_t;

ds_status_t ds_rbst_init(ds_rbst_t *t, size_t elem_size,
                         ds_rbst_compare_func_t compare, void *user,
                         ds_rbst_random_func_t random, void *random_state,
                         ds_context_t *context);
void ds_rbst_destroy(ds_rbst_t *t);
size_t ds_rbst_size(const ds_rbst_t *t);
/* DS_EXISTS for a duplicate key. */
ds_status_t ds_rbst_insert(ds_rbst_t *t, const void *elem);
ds_status_t ds_rbst_remove(ds_rbst_t *t, const void *key, void *out);
ds_status_t ds_rbst_find(const ds_rbst_t *t, const void *key, void *out);
ds_status_t ds_rbst_select(const ds_rbst_t *t, size_t k, void *out);
size_t ds_rbst_rank(const ds_rbst_t *t, const void *key);
size_t ds_rbst_height(const ds_rbst_t *t);
/* Moves every element >= key from t into out (an empty tree initialised
 * with the same element size; nodes are copied). */
ds_status_t ds_rbst_split(ds_rbst_t *t, const void *key, ds_rbst_t *out);
/* Moves every element of other (all greater than t's) into t; other is
 * left empty. DS_ERR_INVALID if the ranges overlap. */
ds_status_t ds_rbst_join(ds_rbst_t *t, ds_rbst_t *other);
void ds_rbst_foreach(const ds_rbst_t *t,
                     bool (*visit)(const void *elem, void *user), void *user);
bool ds_rbst_valid(const ds_rbst_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_RBST_H */
