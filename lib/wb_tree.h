#ifndef DS_WB_TREE_H
#define DS_WB_TREE_H

/*
 * Weight-balanced trees (trees of bounded balance, BB[alpha], Nievergelt
 * and Reingold): every node keeps its subtree size, and a node is in
 * balance when neither subtree outweighs the other by more than a factor
 * delta (weights are sizes + 1). Rebalancing uses single or double
 * rotations, chosen with the ratio gamma. Both are pluggable rationals;
 * the default (delta, gamma) = (3, 2) is the integer pair proven to keep
 * the invariant under insertion and deletion (Hirai and Yamamoto), and
 * (5/2, 3/2) is another pair inside the valid region. BB[alpha]
 * corresponds to delta = (1 - alpha) / alpha. Outside the valid region
 * the operations stay correct but the balance bound can be lost (which
 * ds_wb_tree_valid reports).
 *
 * Because sizes are kept, order statistics come for free: select (k-th
 * smallest) and rank. ds_wb_tree_root_balance is the root balance
 * (size(left) + 1) / (size + 1) of BB[alpha] trees.
 *
 * Elements are stored by value in node arrays; operations recurse only
 * along one root-to-leaf path (O(log n) deep).
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

typedef int (*ds_wb_compare_func_t)(const void *a, const void *b, void *user);

typedef struct ds_wb_tree {
  unsigned char *data;
  size_t *left;
  size_t *right;
  size_t *size;
  size_t root;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  size_t delta_num; /* delta = delta_num / delta_den */
  size_t delta_den;
  size_t gamma_num;
  size_t gamma_den;
  ds_wb_compare_func_t compare;
  void *user;
  ds_context_t *context;
} ds_wb_tree_t;

#define DS_WB_NONE (~(size_t)0)

/* All four 0 for the defaults (3, 2); otherwise 1 < delta, 1 <= gamma <
 * delta, numerators and denominators at most 1024. */
ds_status_t ds_wb_tree_init(ds_wb_tree_t *t, size_t elem_size,
                            ds_wb_compare_func_t compare, void *user,
                            size_t delta_num, size_t delta_den,
                            size_t gamma_num, size_t gamma_den,
                            ds_context_t *context);
void ds_wb_tree_destroy(ds_wb_tree_t *t);
size_t ds_wb_tree_size(const ds_wb_tree_t *t);
/* DS_EXISTS for a duplicate key. */
ds_status_t ds_wb_tree_insert(ds_wb_tree_t *t, const void *elem);
ds_status_t ds_wb_tree_remove(ds_wb_tree_t *t, const void *key, void *out);
ds_status_t ds_wb_tree_find(const ds_wb_tree_t *t, const void *key, void *out);
/* k-th smallest (0-based): DS_ERR_RANGE past the end. */
ds_status_t ds_wb_tree_select(const ds_wb_tree_t *t, size_t k, void *out);
/* Number of elements smaller than key. */
size_t ds_wb_tree_rank(const ds_wb_tree_t *t, const void *key);
double ds_wb_tree_root_balance(const ds_wb_tree_t *t);
size_t ds_wb_tree_height(const ds_wb_tree_t *t);
/* In-order visit; return false to stop. */
void ds_wb_tree_foreach(const ds_wb_tree_t *t,
                        bool (*visit)(const void *elem, void *user),
                        void *user);
/* Checks order, sizes and the balance invariant. */
bool ds_wb_tree_valid(const ds_wb_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_WB_TREE_H */
