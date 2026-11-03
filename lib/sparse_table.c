#include "sparse_table.h"
#include <string.h>

#define ST_SIZE_MAX (~(size_t)0)

static size_t st_floor_log2(size_t x) {
  size_t r = 0;
  while (x >= 256) {
    x >>= 8;
    r += 8;
  }
  while (x > 1) {
    x >>= 1;
    r++;
  }
  return r;
}

static size_t st_levels(ds_sparse_table_kind_t kind, size_t count) {
  if (count == 0) {
    return 0;
  }
  if (kind == DS_SPARSE_TABLE_IDEMPOTENT) {
    return st_floor_log2(count) + 1;
  }
  /* disjoint: ceil(log2 count) + 1 */
  return count == 1 ? 1 : st_floor_log2(count - 1) + 2;
}

static unsigned char *st_cell(const ds_sparse_table_t *table, size_t level,
                              size_t index) {
  return table->table + (level * table->size + index) * table->elem_size;
}

static void st_build_idempotent(ds_sparse_table_t *table) {
  size_t level;
  size_t i;
  size_t half;
  for (level = 1; level < table->levels; level++) {
    half = (size_t)1 << (level - 1);
    for (i = 0; i + 2 * half <= table->size; i++) {
      table->op(st_cell(table, level, i), st_cell(table, level - 1, i),
                st_cell(table, level - 1, i + half), table->user);
    }
  }
}

static void st_build_disjoint(ds_sparse_table_t *table) {
  size_t level;
  size_t half;
  size_t mid;
  size_t i;
  size_t es = table->elem_size;
  for (level = 1; level < table->levels; level++) {
    half = (size_t)1 << (level - 1);
    for (mid = half; mid < table->size; mid += 2 * half) {
      /* Suffix aggregates of [mid - half, mid), left to right. */
      memcpy(st_cell(table, level, mid - 1), st_cell(table, 0, mid - 1), es);
      for (i = mid - 1; i > mid - half; i--) {
        table->op(st_cell(table, level, i - 1), st_cell(table, 0, i - 1),
                  st_cell(table, level, i), table->user);
      }
      /* Prefix aggregates of [mid, mid + half). */
      memcpy(st_cell(table, level, mid), st_cell(table, 0, mid), es);
      for (i = mid + 1; i < mid + half && i < table->size; i++) {
        table->op(st_cell(table, level, i), st_cell(table, level, i - 1),
                  st_cell(table, 0, i), table->user);
      }
      if (mid > ST_SIZE_MAX - 2 * half) {
        break;
      }
    }
  }
}

static ds_status_t st_setup(ds_sparse_table_t *table,
                            const ds_sparse_table_config_t *config,
                            const void *elems, size_t count) {
  if (table == NULL || config == NULL || config->op == NULL ||
      (elems == NULL && count > 0)) {
    return DS_ERR_NULL;
  }
  if (config->elem_size == 0 || (config->kind != DS_SPARSE_TABLE_IDEMPOTENT &&
                                 config->kind != DS_SPARSE_TABLE_DISJOINT)) {
    return DS_ERR_INVALID;
  }
  if (count > 0 && ds_sparse_table_buffer_size(config->kind, count,
                                               config->elem_size) == 0) {
    return DS_ERR_OVERFLOW;
  }
  table->table = NULL;
  table->size = count;
  table->levels = st_levels(config->kind, count);
  table->elem_size = config->elem_size;
  table->op = config->op;
  table->user = config->user;
  table->kind = config->kind;
  return DS_OK;
}

static void st_build(ds_sparse_table_t *table, const void *elems) {
  if (table->size == 0) {
    return;
  }
  memcpy(table->table, elems, table->size * table->elem_size);
  if (table->kind == DS_SPARSE_TABLE_IDEMPOTENT) {
    st_build_idempotent(table);
  } else {
    st_build_disjoint(table);
  }
}

void ds_sparse_table_config_init(ds_sparse_table_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->elem_size = 0;
  config->op = NULL;
  config->user = NULL;
  config->kind = DS_SPARSE_TABLE_IDEMPOTENT;
  config->context = NULL;
}

size_t ds_sparse_table_buffer_size(ds_sparse_table_kind_t kind, size_t count,
                                   size_t elem_size) {
  size_t levels = st_levels(kind, count);
  if (count == 0 || elem_size == 0) {
    return 0;
  }
  if (count > ST_SIZE_MAX / levels ||
      count * levels > ST_SIZE_MAX / elem_size) {
    return 0;
  }
  return count * levels * elem_size;
}

ds_status_t ds_sparse_table_init(ds_sparse_table_t *table,
                                 const ds_sparse_table_config_t *config,
                                 const void *elems, size_t count) {
  ds_status_t status = st_setup(table, config, elems, count);
  size_t bytes;
  if (status != DS_OK) {
    return status;
  }
  table->context =
      config->context != NULL ? config->context : ds_default_context();
  table->fixed = false;
  if (count > 0) {
    bytes = ds_sparse_table_buffer_size(table->kind, count, table->elem_size);
    table->table = (unsigned char *)ds_context_alloc(table->context, bytes);
    if (table->table == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  st_build(table, elems);
  return DS_OK;
}

ds_status_t ds_sparse_table_init_buffer(ds_sparse_table_t *table,
                                        const ds_sparse_table_config_t *config,
                                        const void *elems, size_t count,
                                        void *buffer, size_t buffer_size) {
  ds_status_t status = st_setup(table, config, elems, count);
  if (status != DS_OK) {
    return status;
  }
  if (count > 0) {
    if (buffer == NULL) {
      return DS_ERR_NULL;
    }
    if (buffer_size <
        ds_sparse_table_buffer_size(table->kind, count, table->elem_size)) {
      return DS_ERR_OVERFLOW;
    }
  }
  table->table = (unsigned char *)buffer;
  table->context = NULL;
  table->fixed = true;
  st_build(table, elems);
  return DS_OK;
}

void ds_sparse_table_destroy(ds_sparse_table_t *table) {
  if (table == NULL) {
    return;
  }
  if (!table->fixed) {
    ds_context_free(table->context, table->table);
  }
  table->table = NULL;
  table->size = 0;
  table->levels = 0;
}

ds_status_t ds_sparse_table_query(const ds_sparse_table_t *table, size_t left,
                                  size_t right, void *out) {
  size_t level;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (left > right || right >= table->size) {
    return DS_ERR_RANGE;
  }
  if (left == right) {
    memcpy(out, st_cell(table, 0, left), table->elem_size);
    return DS_OK;
  }
  if (table->kind == DS_SPARSE_TABLE_IDEMPOTENT) {
    level = st_floor_log2(right - left + 1);
    table->op(out, st_cell(table, level, left),
              st_cell(table, level, right + 1 - ((size_t)1 << level)),
              table->user);
  } else {
    level = st_floor_log2(left ^ right) + 1;
    table->op(out, st_cell(table, level, left), st_cell(table, level, right),
              table->user);
  }
  return DS_OK;
}

size_t ds_sparse_table_size(const ds_sparse_table_t *table) {
  return table->size;
}

void ds_sparse_table_op_min_long(void *out, const void *a, const void *b,
                                 void *user) {
  long x = *(const long *)a;
  long y = *(const long *)b;
  (void)user;
  *(long *)out = x < y ? x : y;
}

void ds_sparse_table_op_max_long(void *out, const void *a, const void *b,
                                 void *user) {
  long x = *(const long *)a;
  long y = *(const long *)b;
  (void)user;
  *(long *)out = x > y ? x : y;
}

void ds_sparse_table_op_sum_long(void *out, const void *a, const void *b,
                                 void *user) {
  long x = *(const long *)a;
  long y = *(const long *)b;
  (void)user;
  *(long *)out = x + y;
}
