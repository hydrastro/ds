#ifndef DS_SPARSE_MATRIX_H
#define DS_SPARSE_MATRIX_H

/*
 * Sparse and irregular arrays.
 *
 * ds_sparse_matrix_t  a sparse matrix of doubles as orthogonal lists
 *                     (Knuth): every stored entry is linked into its row
 *                     list (by increasing column) and its column list (by
 *                     increasing row), so rows and columns are both cheap
 *                     to walk. Set / get / remove, row and column
 *                     iteration, transpose, sum, product (sparse times
 *                     sparse, row by row with a dense accumulator), matrix
 *                     times vector, and export to compressed sparse row
 *                     (CSR) arrays.
 * ds_ragged_matrix_t  a ragged (jagged) matrix: rows of different lengths
 *                     stored back to back with row offsets; elements by
 *                     value.
 * ds_huge_array_t     a huge sparse array: indices over the whole size_t
 *                     range, storage only for the pages actually written
 *                     (a hash of fixed-size pages with per-page presence
 *                     bitmaps), unwritten entries read as absent; pages
 *                     stay allocated until the array is destroyed.
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

#define DS_SPARSE_NONE (~(size_t)0)

typedef struct ds_sparse_matrix {
  size_t rows;
  size_t cols;
  size_t *row_head;
  size_t *col_head;
  size_t *row; /* per node */
  size_t *col;
  double *value;
  size_t *right; /* next in the row */
  size_t *down;  /* next in the column */
  size_t nnz;
  size_t slots;
  size_t capacity;
  size_t free_list;
  ds_context_t *context;
} ds_sparse_matrix_t;

typedef bool (*ds_sparse_visit_func_t)(size_t row, size_t col, double value,
                                       void *user);

ds_status_t ds_sparse_matrix_init(ds_sparse_matrix_t *a, size_t rows,
                                  size_t cols, ds_context_t *context);
void ds_sparse_matrix_destroy(ds_sparse_matrix_t *a);
void ds_sparse_matrix_clear(ds_sparse_matrix_t *a);
size_t ds_sparse_matrix_nnz(const ds_sparse_matrix_t *a);
/* Stores the value (zero included: an explicit entry). */
ds_status_t ds_sparse_matrix_set(ds_sparse_matrix_t *a, size_t i, size_t j,
                                 double value);
/* Adds to the entry (created if absent). */
ds_status_t ds_sparse_matrix_add_to(ds_sparse_matrix_t *a, size_t i, size_t j,
                                    double value);
/* 0.0 and DS_NOT_FOUND for an absent entry. */
ds_status_t ds_sparse_matrix_get(const ds_sparse_matrix_t *a, size_t i,
                                 size_t j, double *out);
ds_status_t ds_sparse_matrix_remove(ds_sparse_matrix_t *a, size_t i, size_t j);
void ds_sparse_matrix_row(const ds_sparse_matrix_t *a, size_t i,
                          ds_sparse_visit_func_t f, void *user);
void ds_sparse_matrix_col(const ds_sparse_matrix_t *a, size_t j,
                          ds_sparse_visit_func_t f, void *user);
/* out: an initialised matrix, cleared and resized as needed. */
ds_status_t ds_sparse_matrix_transpose(const ds_sparse_matrix_t *a,
                                       ds_sparse_matrix_t *out);
ds_status_t ds_sparse_matrix_sum(const ds_sparse_matrix_t *a,
                                 const ds_sparse_matrix_t *b,
                                 ds_sparse_matrix_t *out);
ds_status_t ds_sparse_matrix_product(const ds_sparse_matrix_t *a,
                                     const ds_sparse_matrix_t *b,
                                     ds_sparse_matrix_t *out);
/* y = A x (x: cols entries, y: rows entries). */
ds_status_t ds_sparse_matrix_mul_vec(const ds_sparse_matrix_t *a,
                                     const double *x, double *y);
/* row_ptr: rows + 1 entries; col_idx and values: nnz entries. */
ds_status_t ds_sparse_matrix_to_csr(const ds_sparse_matrix_t *a,
                                    size_t *row_ptr, size_t *col_idx,
                                    double *values);

typedef struct ds_ragged_matrix {
  unsigned char *data;
  size_t *offset; /* rows + 1 entries */
  size_t rows;
  size_t row_capacity;
  size_t bytes_capacity;
  size_t elem_size;
  ds_context_t *context;
} ds_ragged_matrix_t;

ds_status_t ds_ragged_matrix_init(ds_ragged_matrix_t *r, size_t elem_size,
                                  ds_context_t *context);
void ds_ragged_matrix_destroy(ds_ragged_matrix_t *r);
/* Appends a row of len elements (elems may be NULL: zero bytes). */
ds_status_t ds_ragged_matrix_append_row(ds_ragged_matrix_t *r,
                                        const void *elems, size_t len);
size_t ds_ragged_matrix_rows(const ds_ragged_matrix_t *r);
size_t ds_ragged_matrix_row_length(const ds_ragged_matrix_t *r, size_t i);
/* Pointer to element (i, j), or NULL out of range. */
void *ds_ragged_matrix_at(const ds_ragged_matrix_t *r, size_t i, size_t j);

#define DS_HUGE_PAGE 64

typedef struct ds_huge_array {
  size_t *page_key; /* page number + 1, 0 = empty */
  unsigned char **page;
  size_t slots; /* power of two */
  size_t pages;
  size_t count; /* entries present */
  size_t elem_size;
  ds_context_t *context;
} ds_huge_array_t;

ds_status_t ds_huge_array_init(ds_huge_array_t *h, size_t elem_size,
                               ds_context_t *context);
void ds_huge_array_destroy(ds_huge_array_t *h);
ds_status_t ds_huge_array_set(ds_huge_array_t *h, size_t index,
                              const void *elem);
/* Pointer to the entry, or NULL when it was never set (or unset). */
void *ds_huge_array_get(const ds_huge_array_t *h, size_t index);
ds_status_t ds_huge_array_unset(ds_huge_array_t *h, size_t index);
size_t ds_huge_array_count(const ds_huge_array_t *h);
size_t ds_huge_array_pages(const ds_huge_array_t *h);

#ifdef __cplusplus
}
#endif

#endif /* DS_SPARSE_MATRIX_H */
