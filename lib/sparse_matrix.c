#include "sparse_matrix.h"
#include <stdlib.h>
#include <string.h>

#define SM_NONE DS_SPARSE_NONE

/* ---- orthogonal-list sparse matrix --------------------------------------- */

static ds_status_t sm_heads(ds_sparse_matrix_t *a, size_t rows, size_t cols) {
  size_t i;
  size_t *rh;
  size_t *ch;
  if (rows > ((size_t)-1) / sizeof(size_t) ||
      cols > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  rh = (size_t *)ds_context_alloc(a->context,
                                  (rows > 0 ? rows : 1) * sizeof(size_t));
  ch = (size_t *)ds_context_alloc(a->context,
                                  (cols > 0 ? cols : 1) * sizeof(size_t));
  if (rh == NULL || ch == NULL) {
    ds_context_free(a->context, rh);
    ds_context_free(a->context, ch);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < rows; i++) {
    rh[i] = SM_NONE;
  }
  for (i = 0; i < cols; i++) {
    ch[i] = SM_NONE;
  }
  ds_context_free(a->context, a->row_head);
  ds_context_free(a->context, a->col_head);
  a->row_head = rh;
  a->col_head = ch;
  a->rows = rows;
  a->cols = cols;
  a->nnz = a->slots = 0;
  a->free_list = SM_NONE;
  return DS_OK;
}

ds_status_t ds_sparse_matrix_init(ds_sparse_matrix_t *a, size_t rows,
                                  size_t cols, ds_context_t *context) {
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  memset(a, 0, sizeof(*a));
  a->context = context;
  a->free_list = SM_NONE;
  return sm_heads(a, rows, cols);
}

void ds_sparse_matrix_destroy(ds_sparse_matrix_t *a) {
  if (a == NULL) {
    return;
  }
  ds_context_free(a->context, a->row_head);
  ds_context_free(a->context, a->col_head);
  ds_context_free(a->context, a->row);
  ds_context_free(a->context, a->col);
  ds_context_free(a->context, a->value);
  ds_context_free(a->context, a->right);
  ds_context_free(a->context, a->down);
  memset(a, 0, sizeof(*a));
  a->free_list = SM_NONE;
}

void ds_sparse_matrix_clear(ds_sparse_matrix_t *a) {
  size_t i;
  if (a == NULL) {
    return;
  }
  for (i = 0; i < a->rows; i++) {
    a->row_head[i] = SM_NONE;
  }
  for (i = 0; i < a->cols; i++) {
    a->col_head[i] = SM_NONE;
  }
  a->nnz = a->slots = 0;
  a->free_list = SM_NONE;
}

size_t ds_sparse_matrix_nnz(const ds_sparse_matrix_t *a) {
  return a == NULL ? 0 : a->nnz;
}

static ds_status_t sm_node(ds_sparse_matrix_t *a, size_t *out) {
  size_t cap;
  void *p;
  if (a->free_list != SM_NONE) {
    *out = a->free_list;
    a->free_list = a->right[*out];
    return DS_OK;
  }
  if (a->slots == a->capacity) {
    cap = a->capacity < 16 ? 16 : a->capacity * 2;
    if (cap <= a->capacity || cap > ((size_t)-1) / sizeof(double)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(a->context, a->row, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    a->row = (size_t *)p;
    p = ds_context_realloc(a->context, a->col, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    a->col = (size_t *)p;
    p = ds_context_realloc(a->context, a->value, cap * sizeof(double));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    a->value = (double *)p;
    p = ds_context_realloc(a->context, a->right, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    a->right = (size_t *)p;
    p = ds_context_realloc(a->context, a->down, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    a->down = (size_t *)p;
    a->capacity = cap;
  }
  *out = a->slots++;
  return DS_OK;
}

/* Node (i, j) or SM_NONE; *prev_row receives its row predecessor (or the
 * node after which it would go). */
static size_t sm_find_row(const ds_sparse_matrix_t *a, size_t i, size_t j,
                          size_t *prev_row) {
  size_t prev = SM_NONE;
  size_t v;
  for (v = a->row_head[i]; v != SM_NONE && a->col[v] < j; v = a->right[v]) {
    prev = v;
  }
  *prev_row = prev;
  return v != SM_NONE && a->col[v] == j ? v : SM_NONE;
}

static size_t sm_col_prev(const ds_sparse_matrix_t *a, size_t i, size_t j) {
  size_t prev = SM_NONE;
  size_t v;
  for (v = a->col_head[j]; v != SM_NONE && a->row[v] < i; v = a->down[v]) {
    prev = v;
  }
  return prev;
}

static ds_status_t sm_insert(ds_sparse_matrix_t *a, size_t i, size_t j,
                             double value, size_t prow) {
  size_t n;
  size_t pcol;
  ds_status_t st = sm_node(a, &n);
  if (st != DS_OK) {
    return st;
  }
  a->row[n] = i;
  a->col[n] = j;
  a->value[n] = value;
  if (prow == SM_NONE) {
    a->right[n] = a->row_head[i];
    a->row_head[i] = n;
  } else {
    a->right[n] = a->right[prow];
    a->right[prow] = n;
  }
  pcol = sm_col_prev(a, i, j);
  if (pcol == SM_NONE) {
    a->down[n] = a->col_head[j];
    a->col_head[j] = n;
  } else {
    a->down[n] = a->down[pcol];
    a->down[pcol] = n;
  }
  a->nnz++;
  return DS_OK;
}

ds_status_t ds_sparse_matrix_set(ds_sparse_matrix_t *a, size_t i, size_t j,
                                 double value) {
  size_t prow;
  size_t v;
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= a->rows || j >= a->cols) {
    return DS_ERR_RANGE;
  }
  v = sm_find_row(a, i, j, &prow);
  if (v != SM_NONE) {
    a->value[v] = value;
    return DS_OK;
  }
  return sm_insert(a, i, j, value, prow);
}

ds_status_t ds_sparse_matrix_add_to(ds_sparse_matrix_t *a, size_t i, size_t j,
                                    double value) {
  size_t prow;
  size_t v;
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= a->rows || j >= a->cols) {
    return DS_ERR_RANGE;
  }
  v = sm_find_row(a, i, j, &prow);
  if (v != SM_NONE) {
    a->value[v] += value;
    return DS_OK;
  }
  return sm_insert(a, i, j, value, prow);
}

ds_status_t ds_sparse_matrix_get(const ds_sparse_matrix_t *a, size_t i,
                                 size_t j, double *out) {
  size_t prow;
  size_t v;
  if (a == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= a->rows || j >= a->cols) {
    return DS_ERR_RANGE;
  }
  v = sm_find_row(a, i, j, &prow);
  *out = v == SM_NONE ? 0.0 : a->value[v];
  return v == SM_NONE ? DS_NOT_FOUND : DS_OK;
}

ds_status_t ds_sparse_matrix_remove(ds_sparse_matrix_t *a, size_t i, size_t j) {
  size_t prow;
  size_t pcol;
  size_t v;
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= a->rows || j >= a->cols) {
    return DS_ERR_RANGE;
  }
  v = sm_find_row(a, i, j, &prow);
  if (v == SM_NONE) {
    return DS_NOT_FOUND;
  }
  if (prow == SM_NONE) {
    a->row_head[i] = a->right[v];
  } else {
    a->right[prow] = a->right[v];
  }
  pcol = sm_col_prev(a, i, j);
  if (pcol == SM_NONE) {
    a->col_head[j] = a->down[v];
  } else {
    a->down[pcol] = a->down[v];
  }
  a->right[v] = a->free_list;
  a->free_list = v;
  a->nnz--;
  return DS_OK;
}

void ds_sparse_matrix_row(const ds_sparse_matrix_t *a, size_t i,
                          ds_sparse_visit_func_t f, void *user) {
  size_t v;
  if (a == NULL || f == NULL || i >= a->rows) {
    return;
  }
  for (v = a->row_head[i]; v != SM_NONE; v = a->right[v]) {
    if (!f(i, a->col[v], a->value[v], user)) {
      return;
    }
  }
}

void ds_sparse_matrix_col(const ds_sparse_matrix_t *a, size_t j,
                          ds_sparse_visit_func_t f, void *user) {
  size_t v;
  if (a == NULL || f == NULL || j >= a->cols) {
    return;
  }
  for (v = a->col_head[j]; v != SM_NONE; v = a->down[v]) {
    if (!f(a->row[v], j, a->value[v], user)) {
      return;
    }
  }
}

/* Bulk building in increasing (row, col) order: O(1) per entry. */
typedef struct sm_builder {
  ds_sparse_matrix_t *m;
  size_t *col_tail;
  size_t row_tail;
  size_t cur_row;
} sm_builder_t;

static ds_status_t sm_begin(sm_builder_t *b, ds_sparse_matrix_t *out,
                            size_t rows, size_t cols) {
  size_t j;
  ds_status_t st;
  b->m = out;
  b->col_tail = NULL;
  if (out->rows != rows || out->cols != cols) {
    st = sm_heads(out, rows, cols);
    if (st != DS_OK) {
      return st;
    }
  } else {
    ds_sparse_matrix_clear(out);
  }
  b->m = out;
  b->row_tail = SM_NONE;
  b->cur_row = SM_NONE;
  b->col_tail = (size_t *)ds_context_alloc(out->context, (cols > 0 ? cols : 1) *
                                                             sizeof(size_t));
  if (b->col_tail == NULL) {
    return DS_ERR_ALLOC;
  }
  for (j = 0; j < cols; j++) {
    b->col_tail[j] = SM_NONE;
  }
  return DS_OK;
}

static ds_status_t sm_append(sm_builder_t *b, size_t i, size_t j, double v) {
  ds_sparse_matrix_t *m = b->m;
  size_t n;
  ds_status_t st = sm_node(m, &n);
  if (st != DS_OK) {
    return st;
  }
  m->row[n] = i;
  m->col[n] = j;
  m->value[n] = v;
  m->right[n] = SM_NONE;
  m->down[n] = SM_NONE;
  if (b->cur_row != i) {
    b->cur_row = i;
    b->row_tail = SM_NONE;
  }
  if (b->row_tail == SM_NONE) {
    m->row_head[i] = n;
  } else {
    m->right[b->row_tail] = n;
  }
  b->row_tail = n;
  if (b->col_tail[j] == SM_NONE) {
    m->col_head[j] = n;
  } else {
    m->down[b->col_tail[j]] = n;
  }
  b->col_tail[j] = n;
  m->nnz++;
  return DS_OK;
}

static void sm_end(sm_builder_t *b) {
  if (b->col_tail != NULL) {
    ds_context_free(b->m->context, b->col_tail);
  }
}

ds_status_t ds_sparse_matrix_transpose(const ds_sparse_matrix_t *a,
                                       ds_sparse_matrix_t *out) {
  sm_builder_t b;
  size_t j;
  size_t v;
  ds_status_t st;
  if (a == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (a == out) {
    return DS_ERR_INVALID;
  }
  st = sm_begin(&b, out, a->cols, a->rows);
  for (j = 0; st == DS_OK && j < a->cols; j++) {
    for (v = a->col_head[j]; st == DS_OK && v != SM_NONE; v = a->down[v]) {
      st = sm_append(&b, j, a->row[v], a->value[v]);
    }
  }
  sm_end(&b);
  return st;
}

ds_status_t ds_sparse_matrix_sum(const ds_sparse_matrix_t *a,
                                 const ds_sparse_matrix_t *b,
                                 ds_sparse_matrix_t *out) {
  sm_builder_t bl;
  size_t i;
  size_t x;
  size_t y;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (a->rows != b->rows || a->cols != b->cols) {
    return DS_ERR_INVALID;
  }
  if (out == a || out == b) {
    return DS_ERR_INVALID;
  }
  st = sm_begin(&bl, out, a->rows, a->cols);
  for (i = 0; st == DS_OK && i < a->rows; i++) {
    x = a->row_head[i];
    y = b->row_head[i];
    while (st == DS_OK && (x != SM_NONE || y != SM_NONE)) {
      if (y == SM_NONE || (x != SM_NONE && a->col[x] < b->col[y])) {
        st = sm_append(&bl, i, a->col[x], a->value[x]);
        x = a->right[x];
      } else if (x == SM_NONE || b->col[y] < a->col[x]) {
        st = sm_append(&bl, i, b->col[y], b->value[y]);
        y = b->right[y];
      } else {
        st = sm_append(&bl, i, a->col[x], a->value[x] + b->value[y]);
        x = a->right[x];
        y = b->right[y];
      }
    }
  }
  sm_end(&bl);
  return st;
}

static int sm_cmp_size(const void *p, const void *q) {
  size_t x = *(const size_t *)p;
  size_t y = *(const size_t *)q;
  return x < y ? -1 : x > y;
}

ds_status_t ds_sparse_matrix_product(const ds_sparse_matrix_t *a,
                                     const ds_sparse_matrix_t *b,
                                     ds_sparse_matrix_t *out) {
  sm_builder_t bl;
  double *acc;
  size_t *touched;
  unsigned char *mark;
  size_t nt;
  size_t i;
  size_t k;
  size_t x;
  size_t y;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (a->cols != b->rows || out == a || out == b) {
    return DS_ERR_INVALID;
  }
  if (b->cols > ((size_t)-1) / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  acc = (double *)ds_context_alloc(out->context, (b->cols > 0 ? b->cols : 1) *
                                                     sizeof(double));
  touched = (size_t *)ds_context_alloc(
      out->context, (b->cols > 0 ? b->cols : 1) * sizeof(size_t));
  mark = (unsigned char *)ds_context_calloc(out->context,
                                            b->cols > 0 ? b->cols : 1, 1);
  if (acc == NULL || touched == NULL || mark == NULL) {
    ds_context_free(out->context, acc);
    ds_context_free(out->context, touched);
    ds_context_free(out->context, mark);
    return DS_ERR_ALLOC;
  }
  st = sm_begin(&bl, out, a->rows, b->cols);
  for (i = 0; st == DS_OK && i < a->rows; i++) {
    nt = 0;
    for (x = a->row_head[i]; x != SM_NONE; x = a->right[x]) {
      for (y = b->row_head[a->col[x]]; y != SM_NONE; y = b->right[y]) {
        if (!mark[b->col[y]]) {
          mark[b->col[y]] = 1;
          acc[b->col[y]] = 0.0;
          touched[nt++] = b->col[y];
        }
        acc[b->col[y]] += a->value[x] * b->value[y];
      }
    }
    qsort(touched, nt, sizeof(size_t), sm_cmp_size);
    for (k = 0; k < nt; k++) {
      if (st == DS_OK) {
        st = sm_append(&bl, i, touched[k], acc[touched[k]]);
      }
      mark[touched[k]] = 0;
    }
  }
  sm_end(&bl);
  ds_context_free(out->context, acc);
  ds_context_free(out->context, touched);
  ds_context_free(out->context, mark);
  return st;
}

ds_status_t ds_sparse_matrix_mul_vec(const ds_sparse_matrix_t *a,
                                     const double *x, double *y) {
  size_t i;
  size_t v;
  double s;
  if (a == NULL || (x == NULL && a->cols > 0) || (y == NULL && a->rows > 0)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < a->rows; i++) {
    s = 0.0;
    for (v = a->row_head[i]; v != SM_NONE; v = a->right[v]) {
      s += a->value[v] * x[a->col[v]];
    }
    y[i] = s;
  }
  return DS_OK;
}

ds_status_t ds_sparse_matrix_to_csr(const ds_sparse_matrix_t *a,
                                    size_t *row_ptr, size_t *col_idx,
                                    double *values) {
  size_t i;
  size_t v;
  size_t k = 0;
  if (a == NULL || row_ptr == NULL ||
      (a->nnz > 0 && (col_idx == NULL || values == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < a->rows; i++) {
    row_ptr[i] = k;
    for (v = a->row_head[i]; v != SM_NONE; v = a->right[v]) {
      col_idx[k] = a->col[v];
      values[k] = a->value[v];
      k++;
    }
  }
  row_ptr[a->rows] = k;
  return DS_OK;
}

/* ---- ragged matrix ------------------------------------------------------- */

ds_status_t ds_ragged_matrix_init(ds_ragged_matrix_t *r, size_t elem_size,
                                  ds_context_t *context) {
  if (r == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(r, 0, sizeof(*r));
  r->elem_size = elem_size;
  r->context = context;
  r->offset = (size_t *)ds_context_alloc(context, sizeof(size_t));
  if (r->offset == NULL) {
    return DS_ERR_ALLOC;
  }
  r->offset[0] = 0;
  r->row_capacity = 0;
  return DS_OK;
}

void ds_ragged_matrix_destroy(ds_ragged_matrix_t *r) {
  if (r == NULL) {
    return;
  }
  ds_context_free(r->context, r->data);
  ds_context_free(r->context, r->offset);
  r->data = NULL;
  r->offset = NULL;
  r->rows = r->row_capacity = r->bytes_capacity = 0;
}

ds_status_t ds_ragged_matrix_append_row(ds_ragged_matrix_t *r,
                                        const void *elems, size_t len) {
  size_t used;
  size_t bytes;
  size_t cap;
  void *p;
  if (r == NULL || r->offset == NULL) {
    return DS_ERR_NULL;
  }
  if (len > ((size_t)-1) / r->elem_size) {
    return DS_ERR_OVERFLOW;
  }
  used = r->offset[r->rows] * r->elem_size;
  bytes = len * r->elem_size;
  if (bytes > ((size_t)-1) - used) {
    return DS_ERR_OVERFLOW;
  }
  if (r->rows == r->row_capacity) {
    cap = r->row_capacity < 8 ? 8 : r->row_capacity * 2;
    if (cap <= r->row_capacity || cap > ((size_t)-1) / sizeof(size_t) - 1) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(r->context, r->offset, (cap + 1) * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    r->offset = (size_t *)p;
    r->row_capacity = cap;
  }
  if (used + bytes > r->bytes_capacity) {
    cap = r->bytes_capacity < 64 ? 64 : r->bytes_capacity;
    while (cap < used + bytes) {
      if (cap > ((size_t)-1) / 2) {
        return DS_ERR_OVERFLOW;
      }
      cap *= 2;
    }
    p = ds_context_realloc(r->context, r->data, cap);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    r->data = (unsigned char *)p;
    r->bytes_capacity = cap;
  }
  if (bytes > 0) {
    if (elems != NULL) {
      memcpy(r->data + used, elems, bytes);
    } else {
      memset(r->data + used, 0, bytes);
    }
  }
  r->offset[r->rows + 1] = r->offset[r->rows] + len;
  r->rows++;
  return DS_OK;
}

size_t ds_ragged_matrix_rows(const ds_ragged_matrix_t *r) {
  return r == NULL ? 0 : r->rows;
}

size_t ds_ragged_matrix_row_length(const ds_ragged_matrix_t *r, size_t i) {
  if (r == NULL || i >= r->rows) {
    return 0;
  }
  return r->offset[i + 1] - r->offset[i];
}

void *ds_ragged_matrix_at(const ds_ragged_matrix_t *r, size_t i, size_t j) {
  if (r == NULL || i >= r->rows || j >= r->offset[i + 1] - r->offset[i]) {
    return NULL;
  }
  return r->data + (r->offset[i] + j) * r->elem_size;
}

/* ---- huge sparse array --------------------------------------------------- */

#define HA_BITMAP (DS_HUGE_PAGE / 8)

ds_status_t ds_huge_array_init(ds_huge_array_t *h, size_t elem_size,
                               ds_context_t *context) {
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || elem_size > ((size_t)-1) / DS_HUGE_PAGE - HA_BITMAP) {
    return DS_ERR_INVALID;
  }
  memset(h, 0, sizeof(*h));
  h->elem_size = elem_size;
  h->context = context;
  return DS_OK;
}

void ds_huge_array_destroy(ds_huge_array_t *h) {
  size_t i;
  if (h == NULL) {
    return;
  }
  for (i = 0; i < h->slots; i++) {
    if (h->page_key[i] != 0) {
      ds_context_free(h->context, h->page[i]);
    }
  }
  ds_context_free(h->context, h->page_key);
  ds_context_free(h->context, (void *)h->page);
  h->page_key = NULL;
  h->page = NULL;
  h->slots = h->pages = h->count = 0;
}

static size_t ha_slot(const ds_huge_array_t *h, size_t key) {
  size_t i = key ^ (key >> 16);
  i = (i * (size_t)0x45d9f3bUL) & (h->slots - 1);
  while (h->page_key[i] != 0 && h->page_key[i] != key) {
    i = (i + 1) & (h->slots - 1);
  }
  return i;
}

static ds_status_t ha_grow(ds_huge_array_t *h) {
  size_t ns = h->slots == 0 ? 16 : h->slots * 2;
  size_t *ok = h->page_key;
  unsigned char **op = h->page;
  size_t os = h->slots;
  size_t i;
  size_t j;
  if (ns <= h->slots || ns > ((size_t)-1) / sizeof(void *)) {
    return DS_ERR_OVERFLOW;
  }
  h->page_key = (size_t *)ds_context_calloc(h->context, ns, sizeof(size_t));
  h->page = (unsigned char **)ds_context_alloc(h->context,
                                               ns * sizeof(unsigned char *));
  if (h->page_key == NULL || h->page == NULL) {
    ds_context_free(h->context, h->page_key);
    ds_context_free(h->context, (void *)h->page);
    h->page_key = ok;
    h->page = op;
    return DS_ERR_ALLOC;
  }
  h->slots = ns;
  for (i = 0; i < os; i++) {
    if (ok[i] != 0) {
      j = ha_slot(h, ok[i]);
      h->page_key[j] = ok[i];
      h->page[j] = op[i];
    }
  }
  ds_context_free(h->context, ok);
  ds_context_free(h->context, (void *)op);
  return DS_OK;
}

ds_status_t ds_huge_array_set(ds_huge_array_t *h, size_t index,
                              const void *elem) {
  size_t key;
  size_t s;
  size_t off;
  unsigned char *pg;
  ds_status_t st;
  if (h == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  key = index / DS_HUGE_PAGE + 1;
  off = index % DS_HUGE_PAGE;
  if (h->slots == 0 || (h->pages + 1) * 4 > h->slots * 3) {
    st = ha_grow(h);
    if (st != DS_OK) {
      return st;
    }
  }
  s = ha_slot(h, key);
  if (h->page_key[s] == 0) {
    pg = (unsigned char *)ds_context_calloc(
        h->context, HA_BITMAP + DS_HUGE_PAGE * h->elem_size, 1);
    if (pg == NULL) {
      return DS_ERR_ALLOC;
    }
    h->page_key[s] = key;
    h->page[s] = pg;
    h->pages++;
  }
  pg = h->page[s];
  if (!(pg[off / 8] & (1u << (off % 8)))) {
    pg[off / 8] = (unsigned char)(pg[off / 8] | (1u << (off % 8)));
    h->count++;
  }
  memcpy(pg + HA_BITMAP + off * h->elem_size, elem, h->elem_size);
  return DS_OK;
}

void *ds_huge_array_get(const ds_huge_array_t *h, size_t index) {
  size_t s;
  size_t off;
  unsigned char *pg;
  if (h == NULL || h->slots == 0) {
    return NULL;
  }
  s = ha_slot(h, index / DS_HUGE_PAGE + 1);
  if (h->page_key[s] == 0) {
    return NULL;
  }
  off = index % DS_HUGE_PAGE;
  pg = h->page[s];
  if (!(pg[off / 8] & (1u << (off % 8)))) {
    return NULL;
  }
  return pg + HA_BITMAP + off * h->elem_size;
}

ds_status_t ds_huge_array_unset(ds_huge_array_t *h, size_t index) {
  size_t s;
  size_t off;
  unsigned char *pg;
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (h->slots == 0) {
    return DS_NOT_FOUND;
  }
  s = ha_slot(h, index / DS_HUGE_PAGE + 1);
  if (h->page_key[s] == 0) {
    return DS_NOT_FOUND;
  }
  off = index % DS_HUGE_PAGE;
  pg = h->page[s];
  if (!(pg[off / 8] & (1u << (off % 8)))) {
    return DS_NOT_FOUND;
  }
  pg[off / 8] = (unsigned char)(pg[off / 8] & ~(1u << (off % 8)));
  h->count--;
  return DS_OK;
}

size_t ds_huge_array_count(const ds_huge_array_t *h) {
  return h == NULL ? 0 : h->count;
}

size_t ds_huge_array_pages(const ds_huge_array_t *h) {
  return h == NULL ? 0 : h->pages;
}
