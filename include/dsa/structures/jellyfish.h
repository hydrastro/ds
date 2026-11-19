#ifndef DS_JELLYFISH_H
#define DS_JELLYFISH_H

/* A reference implementation of Snyder's theoretical jelly-fish shape.
 * floor(sqrt(n)) items form a balanced binary-search-tree body; its external
 * gaps lead to sorted singly-linked tentacles containing the remaining items.
 * Each tentacle has at most ceil((n-body)/(body+1)) items. Lookups use the
 * body then one tentacle: O(log n + sqrt(n)). A caller-selected ordered
 * backend can replace the body; its lookup/build costs then replace the
 * binary-tree costs. The default body is deterministic and balanced, with no
 * random priorities.
 *
 * This is an inspectable reference, not a new performance claim for updates:
 * inserting/deleting rebuilds the entire shape in O(n), plus the chosen body's
 * build cost. Bulk assignment sorts in O(n log n). Rebuilds stage all memory
 * and preserve contents on failure. They invalidate all borrowed pointers on
 * success. Replacing a value does not rebuild. Items have unsigned-long keys
 * and copied fixed-size values; referenced resources remain caller-owned.
 * All allocations use context. No internal synchronization.
 */
#include <dsa/structures/ordered_iface.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_jellyfish_config {
  size_t value_size;
  const ds_ordered_ops_t *body_ops; /* NULL: balanced binary tree */
  ds_context_t *context;
} ds_jellyfish_config_t;
typedef struct ds_jellyfish {
  void *impl;
  size_t value_size, value_offset, stride;
  const ds_ordered_ops_t *body_ops;
  ds_context_t *context;
} ds_jellyfish_t;
typedef struct ds_jellyfish_shape {
  size_t items, body_items, tentacles, longest_tentacle;
  size_t binary_body_height; /* 0 for empty or a caller-selected backend */
} ds_jellyfish_shape_t;

void ds_jellyfish_config_init(ds_jellyfish_config_t *config);
ds_status_t ds_jellyfish_init(ds_jellyfish_t *tree,
                              const ds_jellyfish_config_t *config);
void ds_jellyfish_destroy(ds_jellyfish_t *tree);
/* Unsorted keys; duplicate keys reject the assignment with DS_ERR_INVALID.
 * values is count*value_size bytes; NULL zeroes values. Input may borrow from
 * the old tree. Only a successful assignment invalidates old borrowed views.
 */
ds_status_t ds_jellyfish_assign(ds_jellyfish_t *tree,
                                const unsigned long *keys, const void *values,
                                size_t count);
/* DS_OK inserts; DS_EXISTS replaces. NULL value zeroes the payload. */
ds_status_t ds_jellyfish_put(ds_jellyfish_t *tree, unsigned long key,
                             const void *value);
ds_status_t ds_jellyfish_remove(ds_jellyfish_t *tree, unsigned long key);
/* For zero-size values, get returns a pointer to the stored key. */
ds_status_t ds_jellyfish_get(const ds_jellyfish_t *tree, unsigned long key,
                             void **out_value);
size_t ds_jellyfish_size(const ds_jellyfish_t *tree);
ds_status_t ds_jellyfish_shape(const ds_jellyfish_t *tree,
                               ds_jellyfish_shape_t *out_shape);
/* Full traversal is in key order. Tentacle traversal omits body items.
 * The first non-DS_OK visitor status stops/propagates. Visitors must not
 * mutate this tree, including value replacement, while the walk is active. */
ds_status_t ds_jellyfish_for_each(const ds_jellyfish_t *tree,
                                  ds_ordered_visit_fn visit, void *user);
ds_status_t ds_jellyfish_tentacle(const ds_jellyfish_t *tree, size_t index,
                                  ds_ordered_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif
#endif /* DS_JELLYFISH_H */
