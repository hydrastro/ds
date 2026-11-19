#ifndef DS_DISJOINT_SET_H
#define DS_DISJOINT_SET_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct disjoint_set_node {
  struct disjoint_set_node *parent;
  size_t rank;
} ds_disjoint_set_node_t;

typedef struct disjoint_set {
  size_t count;
  ds_context_t *context; /* borrowed allocation owner */
} ds_disjoint_set_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_disjoint_set_create_context(ds_context_t *context,
                                           ds_disjoint_set_t **out);

ds_disjoint_set_t *ds_disjoint_set_create(ds_context_t *context);
/* Nodes must be registered exactly once and belong to this set. Intrusive
 * nodes carry no ownership tag. Checked operations validate arguments/counts,
 * not membership or arbitrary link corruption. Find may compress paths.
 * DS_EXISTS from union means already connected. Errors preserve outputs. */
ds_status_t ds_disjoint_set_make_ex(ds_disjoint_set_t *set,
                                    ds_disjoint_set_node_t *node);
ds_status_t ds_disjoint_set_find_ex(ds_disjoint_set_t *set,
                                    ds_disjoint_set_node_t *node,
                                    ds_disjoint_set_node_t **out);
ds_status_t ds_disjoint_set_union_ex(ds_disjoint_set_t *set,
                                     ds_disjoint_set_node_t *a,
                                     ds_disjoint_set_node_t *b);
void ds_disjoint_set_make(ds_disjoint_set_t *set,
                          ds_disjoint_set_node_t *node);
ds_disjoint_set_node_t *ds_disjoint_set_find(ds_disjoint_set_t *set,
                                             ds_disjoint_set_node_t *node);
bool ds_disjoint_set_union(ds_disjoint_set_t *set, ds_disjoint_set_node_t *a,
                           ds_disjoint_set_node_t *b);
bool ds_disjoint_set_connected(ds_disjoint_set_t *set,
                               ds_disjoint_set_node_t *a,
                               ds_disjoint_set_node_t *b);
size_t ds_disjoint_set_count(ds_disjoint_set_t *set);
void ds_disjoint_set_destroy(ds_disjoint_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* DS_DISJOINT_SET_H */
