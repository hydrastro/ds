#include "sparse_set.h"
#include <string.h>

#define SPARSE_SET_SIZE_MAX (~(size_t)0)

ds_status_t ds_sparse_set_init(ds_sparse_set_t *set, size_t universe,
                               ds_context_t *context) {
  if (set == NULL) {
    return DS_ERR_NULL;
  }
  set->dense = NULL;
  set->sparse = NULL;
  set->size = 0;
  set->universe = 0;
  set->context = context != NULL ? context : ds_default_context();
  set->fixed = false;
  return ds_sparse_set_set_universe(set, universe);
}

ds_status_t ds_sparse_set_init_buffer(ds_sparse_set_t *set, size_t *dense,
                                      size_t *sparse, size_t universe) {
  if (set == NULL || (universe > 0 && (dense == NULL || sparse == NULL))) {
    return DS_ERR_NULL;
  }
  set->dense = dense;
  set->sparse = sparse;
  set->size = 0;
  set->universe = universe;
  set->context = NULL;
  set->fixed = true;
  /* Not needed for correctness, but keeps every read of sparse[] defined. */
  if (universe > 0) {
    memset(sparse, 0, universe * sizeof(*sparse));
  }
  return DS_OK;
}

void ds_sparse_set_destroy(ds_sparse_set_t *set) {
  if (set == NULL) {
    return;
  }
  if (!set->fixed) {
    ds_context_free(set->context, set->dense);
    ds_context_free(set->context, set->sparse);
    set->dense = NULL;
    set->sparse = NULL;
    set->universe = 0;
  }
  set->size = 0;
}

bool ds_sparse_set_contains(const ds_sparse_set_t *set, size_t value) {
  size_t index;
  if (value >= set->universe) {
    return false;
  }
  index = set->sparse[value];
  return index < set->size && set->dense[index] == value;
}

ds_status_t ds_sparse_set_insert(ds_sparse_set_t *set, size_t value) {
  if (value >= set->universe) {
    return DS_ERR_RANGE;
  }
  if (ds_sparse_set_contains(set, value)) {
    return DS_EXISTS;
  }
  set->dense[set->size] = value;
  set->sparse[value] = set->size;
  set->size++;
  return DS_OK;
}

ds_status_t ds_sparse_set_remove(ds_sparse_set_t *set, size_t value) {
  size_t index;
  size_t last;
  if (!ds_sparse_set_contains(set, value)) {
    return DS_NOT_FOUND;
  }
  index = set->sparse[value];
  set->size--;
  last = set->dense[set->size];
  set->dense[index] = last;
  set->sparse[last] = index;
  return DS_OK;
}

ds_status_t ds_sparse_set_index_of(const ds_sparse_set_t *set, size_t value,
                                   size_t *out_index) {
  if (!ds_sparse_set_contains(set, value)) {
    return DS_NOT_FOUND;
  }
  if (out_index != NULL) {
    *out_index = set->sparse[value];
  }
  return DS_OK;
}

void ds_sparse_set_clear(ds_sparse_set_t *set) { set->size = 0; }

size_t ds_sparse_set_size(const ds_sparse_set_t *set) { return set->size; }

size_t ds_sparse_set_universe(const ds_sparse_set_t *set) {
  return set->universe;
}

const size_t *ds_sparse_set_data(const ds_sparse_set_t *set) {
  return set->dense;
}

ds_status_t ds_sparse_set_at(const ds_sparse_set_t *set, size_t index,
                             size_t *out_value) {
  if (out_value == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= set->size) {
    return DS_ERR_RANGE;
  }
  *out_value = set->dense[index];
  return DS_OK;
}

ds_status_t ds_sparse_set_pop(ds_sparse_set_t *set, size_t *out_value) {
  if (set->size == 0) {
    return DS_EMPTY;
  }
  set->size--;
  if (out_value != NULL) {
    *out_value = set->dense[set->size];
  }
  return DS_OK;
}

ds_status_t ds_sparse_set_set_universe(ds_sparse_set_t *set, size_t universe) {
  size_t *dense;
  size_t *sparse;
  size_t i;
  if (universe <= set->universe) {
    /* Shrinking keeps the memory; every member must stay in range. */
    for (i = 0; i < set->size; i++) {
      if (set->dense[i] >= universe) {
        return DS_ERR_STATE;
      }
    }
    set->universe = universe;
    return DS_OK;
  }
  if (set->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (universe > SPARSE_SET_SIZE_MAX / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  dense = (size_t *)ds_context_alloc(set->context, universe * sizeof(size_t));
  if (dense == NULL) {
    return DS_ERR_ALLOC;
  }
  sparse = (size_t *)ds_context_calloc(set->context, universe, sizeof(size_t));
  if (sparse == NULL) {
    ds_context_free(set->context, dense);
    return DS_ERR_ALLOC;
  }
  if (set->size > 0) {
    memcpy(dense, set->dense, set->size * sizeof(size_t));
    for (i = 0; i < set->size; i++) {
      sparse[dense[i]] = i;
    }
  }
  ds_context_free(set->context, set->dense);
  ds_context_free(set->context, set->sparse);
  set->dense = dense;
  set->sparse = sparse;
  set->universe = universe;
  return DS_OK;
}
