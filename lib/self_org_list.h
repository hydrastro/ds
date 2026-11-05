#ifndef DS_SELF_ORG_LIST_H
#define DS_SELF_ORG_LIST_H

/*
 * Self-organizing sequential search: a list searched front to back that
 * reorders itself on every successful search so frequently requested
 * elements drift to the front.
 *
 * ds_self_org_list_t stores elements by value in one contiguous array
 * (dynamic through a ds_context_t, or a caller buffer). The reordering
 * policy is pluggable:
 *   DS_SOL_NONE           plain sequential search
 *   DS_SOL_MOVE_TO_FRONT  the found element goes to the front
 *   DS_SOL_TRANSPOSE      it swaps with its predecessor
 *   DS_SOL_COUNT          frequency count: access counts kept, the list
 *                         kept in decreasing count order (stable)
 *   DS_SOL_MOVE_AHEAD_K   it moves k places forward
 *   DS_SOL_CUSTOM         a callback chooses the new position (set
 *                         through ds_self_org_list_set_policy)
 * Every probe is counted (for comparing policies).
 *
 * ds_mtr_tree_t is the tree counterpart: a binary search tree with the
 * move-to-root heuristic (Allen and Munro), where every accessed node is
 * rotated to the root, or the simple-exchange heuristic (one rotation
 * towards the root). Nodes live in arrays; elements are stored by value.
 *
 * Concurrency: a structure may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_self_org_compare_func_t)(const void *a, const void *b,
                                          void *user);

typedef enum ds_sol_policy {
  DS_SOL_NONE = 0,
  DS_SOL_MOVE_TO_FRONT,
  DS_SOL_TRANSPOSE,
  DS_SOL_COUNT,
  DS_SOL_MOVE_AHEAD_K,
  DS_SOL_CUSTOM
} ds_sol_policy_t;

/* New position (at most index) for the element found at index; counts[i]
 * is the access count of the element at i (already incremented). */
typedef size_t (*ds_sol_policy_func_t)(size_t index, const size_t *counts,
                                       size_t size, void *user);

typedef struct ds_self_org_list {
  unsigned char *data;
  size_t *counts;
  size_t size;
  size_t capacity;
  size_t elem_size;
  ds_sol_policy_t policy;
  size_t k;
  ds_sol_policy_func_t custom;
  void *custom_user;
  ds_self_org_compare_func_t compare; /* 0 = equal */
  void *user;
  size_t probes;
  unsigned char *swap; /* one element of scratch */
  bool fixed;
  ds_context_t *context;
} ds_self_org_list_t;

ds_status_t ds_self_org_list_init(ds_self_org_list_t *list, size_t elem_size,
                                  ds_self_org_compare_func_t compare,
                                  void *user, ds_sol_policy_t policy,
                                  ds_context_t *context);
/* buffer: (capacity + 1) * elem_size bytes; counts: capacity entries. */
ds_status_t ds_self_org_list_init_buffer(ds_self_org_list_t *list,
                                         size_t elem_size,
                                         ds_self_org_compare_func_t compare,
                                         void *user, ds_sol_policy_t policy,
                                         void *buffer, size_t *counts,
                                         size_t capacity);
void ds_self_org_list_destroy(ds_self_org_list_t *list);
void ds_self_org_list_clear(ds_self_org_list_t *list);
/* k for DS_SOL_MOVE_AHEAD_K, func and user for DS_SOL_CUSTOM. */
ds_status_t ds_self_org_list_set_policy(ds_self_org_list_t *list,
                                        ds_sol_policy_t policy, size_t k,
                                        ds_sol_policy_func_t func,
                                        void *func_user);
size_t ds_self_org_list_size(const ds_self_org_list_t *list);
/* Appended at the back with count 0 (duplicates are allowed). */
ds_status_t ds_self_org_list_insert(ds_self_org_list_t *list, const void *elem);
/* Sequential search, then reorganisation: the element is copied to out
 * (may be NULL) and its new index stored (may be NULL). */
ds_status_t ds_self_org_list_search(ds_self_org_list_t *list, const void *key,
                                    void *out, size_t *out_index);
/* Search without reorganising or counting. */
ds_status_t ds_self_org_list_find(const ds_self_org_list_t *list,
                                  const void *key, size_t *out_index);
ds_status_t ds_self_org_list_remove(ds_self_org_list_t *list, const void *key,
                                    void *out);
const void *ds_self_org_list_at(const ds_self_org_list_t *list, size_t index);
size_t ds_self_org_list_count_at(const ds_self_org_list_t *list, size_t index);
size_t ds_self_org_list_probes(const ds_self_org_list_t *list);

#define DS_MTR_NONE (~(size_t)0)

typedef enum ds_mtr_policy {
  DS_MTR_MOVE_TO_ROOT = 0,
  DS_MTR_SIMPLE_EXCHANGE,
  DS_MTR_STATIC
} ds_mtr_policy_t;

typedef struct ds_mtr_tree {
  unsigned char *data;
  size_t *left;
  size_t *right;
  size_t *parent;
  size_t root;
  size_t size;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  ds_mtr_policy_t policy;
  ds_self_org_compare_func_t compare; /* ordering */
  void *user;
  size_t probes;
  ds_context_t *context;
} ds_mtr_tree_t;

ds_status_t ds_mtr_tree_init(ds_mtr_tree_t *t, size_t elem_size,
                             ds_self_org_compare_func_t compare, void *user,
                             ds_mtr_policy_t policy, ds_context_t *context);
void ds_mtr_tree_destroy(ds_mtr_tree_t *t);
size_t ds_mtr_tree_size(const ds_mtr_tree_t *t);
/* DS_EXISTS (and the existing node accessed) for a duplicate key. */
ds_status_t ds_mtr_tree_insert(ds_mtr_tree_t *t, const void *elem);
ds_status_t ds_mtr_tree_search(ds_mtr_tree_t *t, const void *key, void *out);
ds_status_t ds_mtr_tree_remove(ds_mtr_tree_t *t, const void *key, void *out);
/* Depth of the node holding key (root 0), without reorganising. */
ds_status_t ds_mtr_tree_depth(const ds_mtr_tree_t *t, const void *key,
                              size_t *out_depth);
/* In-order visit; return false to stop. */
void ds_mtr_tree_foreach(const ds_mtr_tree_t *t,
                         bool (*visit)(const void *elem, void *user),
                         void *user);
size_t ds_mtr_tree_probes(const ds_mtr_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_SELF_ORG_LIST_H */
