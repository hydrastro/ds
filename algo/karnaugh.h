#ifndef DS_ALGO_KARNAUGH_H
#define DS_ALGO_KARNAUGH_H

/* Karnaugh maps for 0..6 variables: Gray-code coordinates, don't-care cells,
 * and cube covers of the true or false cells. For five/six variables a cube
 * can span separated Gray-code regions (the equivalent multiple-map groups).
 * A cube fixes the bits in mask to the corresponding bits in value.
 *
 * Default minimization enumerates prime cubes and minimizes term count, then
 * literal count. A custom nonnegative cube cost enumerates ALL valid cubes
 * and minimizes total cost, then terms, then literals. Exact cover search is
 * exponential; the node budget bounds work and returns a valid best-so-far
 * cover with DS_STOP when exhausted. DS_OK certifies search completion within
 * this bounded Boolean model. Search depth is at most 64 (one newly covered
 * required cell per level), independent of node budget.
 */
#include "../lib/context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DS_KMAP_MAX_VARIABLES 6
#define DS_KMAP_DONT_CARE 2

typedef struct ds_kmap {
  size_t variables, row_variables;
  /* Input variable bits: order[0..row_variables) belong to the row. */
  size_t order[DS_KMAP_MAX_VARIABLES];
  unsigned char values[64]; /* binary assignment order, values 0, 1 or 2 */
} ds_kmap_t;
typedef struct ds_kmap_cube {
  unsigned int mask, value;
  unsigned long cost;
} ds_kmap_cube_t;
typedef ds_status_t (*ds_kmap_cost_fn)(const ds_kmap_cube_t *cube,
                                       unsigned long *out_cost, void *user);
typedef struct ds_kmap_policy {
  size_t node_budget; /* init: 100,000; 0: unlimited */
  ds_kmap_cost_fn cost;
  void *user;
} ds_kmap_policy_t;
typedef struct ds_kmap_cover {
  ds_kmap_cube_t *cubes;
  size_t count, literals, variables, search_nodes;
  unsigned long cost;
  bool target, optimal;
  ds_context_t *context;
} ds_kmap_cover_t;

/* order is a permutation of 0..variables-1; NULL uses natural bit order.
 * values has 2^variables entries. Output is unchanged on error. */
ds_status_t ds_kmap_init(ds_kmap_t *map, size_t variables, size_t row_variables,
                         const size_t *order, const unsigned char *values);
ds_status_t ds_kmap_assignment(const ds_kmap_t *map, size_t row, size_t column,
                               size_t *out_assignment);
ds_status_t ds_kmap_coordinates(const ds_kmap_t *map, size_t assignment,
                                size_t *out_row, size_t *out_column);
void ds_kmap_policy_init(ds_kmap_policy_t *policy);
/* target=true: sum-of-products. target=false: complement of a cover of zeros
 * (equivalently a product-of-sums after De Morgan). Don't-care outputs are
 * free. Custom costs must be <= ULONG_MAX / (2^variables), so every cover sum
 * fits. All allocation precedes publication; callbacks must not mutate
 * map/policy. Output must be uninitialized; errors leave it unchanged. DS_OK
 * and DS_STOP both publish an owned cover, which must be destroyed. */
ds_status_t ds_kmap_minimize(const ds_kmap_t *map, bool target,
                             const ds_kmap_policy_t *policy,
                             ds_kmap_cover_t *out, ds_context_t *context);
ds_status_t ds_kmap_evaluate(const ds_kmap_cover_t *cover, size_t assignment,
                             bool *out_value);
void ds_kmap_cover_destroy(ds_kmap_cover_t *cover);

#ifdef __cplusplus
}
#endif
#endif /* DS_ALGO_KARNAUGH_H */
