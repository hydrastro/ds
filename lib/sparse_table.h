#ifndef DS_SPARSE_TABLE_H
#define DS_SPARSE_TABLE_H

/*
 * Sparse table: a static array preprocessed for O(1) range queries.
 *
 * Two kinds, chosen in the config:
 *   - DS_SPARSE_TABLE_IDEMPOTENT: the classic table. The operation must be
 *     associative and idempotent (op(x, x) == x): min, max, gcd, bitwise
 *     and/or. A query combines two overlapping power-of-two windows.
 *     floor(log2 n) + 1 rows.
 *   - DS_SPARSE_TABLE_DISJOINT: the disjoint sparse table. The operation
 *     only needs to be associative (sum, product, xor, matrix product,
 *     concatenation...), and it may be non-commutative: queries always
 *     combine elements left to right. ceil(log2 n) + 1 rows.
 *
 * Elements are fixed-size values stored by value. The table is immutable
 * after construction; rebuild it to change the data.
 *
 * Storage: dynamic (ds_sparse_table_init, through a ds_context_t) or a
 * caller buffer of ds_sparse_table_buffer_size() bytes, suitably aligned for
 * the element type (ds_sparse_table_init_buffer, no allocation).
 *
 * Ranges are inclusive: [left, right].
 *
 * Complexity: build O(n log n) time and space; query O(1) (one call of the
 * operation, or none for a single element).
 * Concurrency: queries never modify the table, so any number of threads may
 * query it concurrently once it is built.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ds_sparse_table_op_func_t)(void *out, const void *a,
                                          const void *b, void *user);

typedef enum ds_sparse_table_kind {
  DS_SPARSE_TABLE_IDEMPOTENT = 0,
  DS_SPARSE_TABLE_DISJOINT = 1
} ds_sparse_table_kind_t;

typedef struct ds_sparse_table_config {
  size_t elem_size;
  ds_sparse_table_op_func_t op;
  void *user;
  ds_sparse_table_kind_t kind;
  ds_context_t *context;
} ds_sparse_table_config_t;

typedef struct ds_sparse_table {
  unsigned char *table;
  size_t size;
  size_t levels;
  size_t elem_size;
  ds_sparse_table_op_func_t op;
  void *user;
  ds_sparse_table_kind_t kind;
  ds_context_t *context;
  bool fixed;
} ds_sparse_table_t;

void FUNC(ds_sparse_table_config_init)(ds_sparse_table_config_t *config);
size_t FUNC(ds_sparse_table_buffer_size)(ds_sparse_table_kind_t kind,
                                         size_t count, size_t elem_size);
ds_status_t FUNC(ds_sparse_table_init)(ds_sparse_table_t *table,
                                       const ds_sparse_table_config_t *config,
                                       const void *elems, size_t count);
ds_status_t FUNC(ds_sparse_table_init_buffer)(
    ds_sparse_table_t *table, const ds_sparse_table_config_t *config,
    const void *elems, size_t count, void *buffer, size_t buffer_size);
void FUNC(ds_sparse_table_destroy)(ds_sparse_table_t *table);

ds_status_t FUNC(ds_sparse_table_query)(const ds_sparse_table_t *table,
                                        size_t left, size_t right, void *out);
size_t FUNC(ds_sparse_table_size)(const ds_sparse_table_t *table);

/* Ready-made operations on `long` elements. */
void FUNC(ds_sparse_table_op_min_long)(void *out, const void *a, const void *b,
                                       void *user);
void FUNC(ds_sparse_table_op_max_long)(void *out, const void *a, const void *b,
                                       void *user);
void FUNC(ds_sparse_table_op_sum_long)(void *out, const void *a, const void *b,
                                       void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_SPARSE_TABLE_H */
