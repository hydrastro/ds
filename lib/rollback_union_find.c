#include "rollback_union_find.h"

#define RUF_SIZE_MAX (~(size_t)0)

static size_t ruf_root(const ds_rollback_uf_t *uf, size_t x) {
  while (uf->parent[x] != x) {
    x = uf->parent[x];
  }
  return x;
}

ds_status_t ds_rollback_uf_init(ds_rollback_uf_t *uf, size_t count,
                                ds_context_t *context) {
  size_t bytes;
  if (uf == NULL) {
    return DS_ERR_NULL;
  }
  uf->parent = NULL;
  uf->size = NULL;
  uf->history = NULL;
  uf->count = count;
  uf->history_len = 0;
  uf->components = count;
  uf->context = context != NULL ? context : ds_default_context();
  uf->fixed = false;
  if (count == 0) {
    return DS_OK;
  }
  if (count > RUF_SIZE_MAX / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  bytes = count * sizeof(size_t);
  uf->parent = (size_t *)ds_context_alloc(uf->context, bytes);
  uf->size = (size_t *)ds_context_alloc(uf->context, bytes);
  uf->history = (size_t *)ds_context_alloc(uf->context, bytes);
  if (uf->parent == NULL || uf->size == NULL || uf->history == NULL) {
    ds_rollback_uf_destroy(uf);
    return DS_ERR_ALLOC;
  }
  ds_rollback_uf_reset(uf);
  return DS_OK;
}

ds_status_t ds_rollback_uf_init_buffer(ds_rollback_uf_t *uf, size_t *parent,
                                       size_t *size, size_t *history,
                                       size_t count) {
  if (uf == NULL ||
      (count > 0 && (parent == NULL || size == NULL || history == NULL))) {
    return DS_ERR_NULL;
  }
  uf->parent = parent;
  uf->size = size;
  uf->history = history;
  uf->count = count;
  uf->context = NULL;
  uf->fixed = true;
  ds_rollback_uf_reset(uf);
  return DS_OK;
}

void ds_rollback_uf_destroy(ds_rollback_uf_t *uf) {
  if (uf == NULL) {
    return;
  }
  if (!uf->fixed) {
    ds_context_free(uf->context, uf->parent);
    ds_context_free(uf->context, uf->size);
    ds_context_free(uf->context, uf->history);
  }
  uf->parent = NULL;
  uf->size = NULL;
  uf->history = NULL;
  uf->count = 0;
  uf->history_len = 0;
  uf->components = 0;
}

ds_status_t ds_rollback_uf_find(const ds_rollback_uf_t *uf, size_t x,
                                size_t *out_root) {
  if (out_root == NULL) {
    return DS_ERR_NULL;
  }
  if (x >= uf->count) {
    return DS_ERR_RANGE;
  }
  *out_root = ruf_root(uf, x);
  return DS_OK;
}

ds_status_t ds_rollback_uf_union(ds_rollback_uf_t *uf, size_t a, size_t b,
                                 bool *out_merged) {
  size_t ra;
  size_t rb;
  size_t tmp;
  if (out_merged != NULL) {
    *out_merged = false;
  }
  if (a >= uf->count || b >= uf->count) {
    return DS_ERR_RANGE;
  }
  ra = ruf_root(uf, a);
  rb = ruf_root(uf, b);
  if (ra == rb) {
    return DS_OK;
  }
  if (uf->size[ra] < uf->size[rb]) {
    tmp = ra;
    ra = rb;
    rb = tmp;
  }
  uf->parent[rb] = ra;
  uf->size[ra] += uf->size[rb];
  uf->history[uf->history_len++] = rb;
  uf->components--;
  if (out_merged != NULL) {
    *out_merged = true;
  }
  return DS_OK;
}

bool ds_rollback_uf_same(const ds_rollback_uf_t *uf, size_t a, size_t b) {
  if (a >= uf->count || b >= uf->count) {
    return false;
  }
  return ruf_root(uf, a) == ruf_root(uf, b);
}

ds_status_t ds_rollback_uf_component_size(const ds_rollback_uf_t *uf, size_t x,
                                          size_t *out_size) {
  if (out_size == NULL) {
    return DS_ERR_NULL;
  }
  if (x >= uf->count) {
    return DS_ERR_RANGE;
  }
  *out_size = uf->size[ruf_root(uf, x)];
  return DS_OK;
}

size_t ds_rollback_uf_components(const ds_rollback_uf_t *uf) {
  return uf->components;
}

size_t ds_rollback_uf_count(const ds_rollback_uf_t *uf) { return uf->count; }

size_t ds_rollback_uf_snapshot(const ds_rollback_uf_t *uf) {
  return uf->history_len;
}

ds_status_t ds_rollback_uf_undo(ds_rollback_uf_t *uf) {
  size_t child;
  size_t root;
  if (uf->history_len == 0) {
    return DS_EMPTY;
  }
  child = uf->history[--uf->history_len];
  root = uf->parent[child];
  uf->size[root] -= uf->size[child];
  uf->parent[child] = child;
  uf->components++;
  return DS_OK;
}

ds_status_t ds_rollback_uf_rollback(ds_rollback_uf_t *uf, size_t snapshot) {
  if (snapshot > uf->history_len) {
    return DS_ERR_RANGE;
  }
  while (uf->history_len > snapshot) {
    ds_rollback_uf_undo(uf);
  }
  return DS_OK;
}

void ds_rollback_uf_reset(ds_rollback_uf_t *uf) {
  size_t i;
  for (i = 0; i < uf->count; i++) {
    uf->parent[i] = i;
    uf->size[i] = 1;
  }
  uf->history_len = 0;
  uf->components = uf->count;
}
