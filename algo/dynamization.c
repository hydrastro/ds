#include "dynamization.h"
#include <string.h>

static size_t dy_limit(const ds_dynamization_t *d, size_t level) {
  size_t cap = d->base - 1;
  size_t i;
  for (i = 0; i < level; i++) {
    if (cap > (size_t)-1 / d->base) {
      return (size_t)-1;
    }
    cap *= d->base;
  }
  return cap;
}

ds_status_t ds_dynamization_init(ds_dynamization_t *dyn, size_t elem_size,
                                 size_t base, double rebuild_ratio,
                                 const ds_dynamization_ops_t *ops,
                                 ds_context_t *context) {
  if (dyn == NULL || ops == NULL) {
    return DS_ERR_NULL;
  }
  memset(dyn, 0, sizeof(*dyn));
  if (elem_size == 0 || base < 2 || !(rebuild_ratio > 0.0) ||
      ops->build == NULL || ops->destroy == NULL || ops->query == NULL) {
    return DS_ERR_INVALID;
  }
  dyn->elem_size = elem_size;
  dyn->base = base;
  dyn->ops = *ops;
  dyn->rebuild_ratio = rebuild_ratio;
  dyn->context = context;
  return DS_OK;
}

static void dy_clear_level(ds_dynamization_t *d, ds_dynamization_level_t *l) {
  if (l->structure != NULL) {
    d->ops.destroy(l->structure, d->ops.user);
  }
  ds_context_free(d->context, l->items);
  ds_context_free(d->context, l->ids);
  memset(l, 0, sizeof(*l));
}

void ds_dynamization_destroy(ds_dynamization_t *dyn) {
  size_t i;
  if (dyn == NULL) {
    return;
  }
  for (i = 0; i < dyn->level_count; i++) {
    dy_clear_level(dyn, &dyn->levels[i]);
  }
  ds_context_free(dyn->context, dyn->levels);
  ds_context_free(dyn->context, dyn->dead);
  memset(dyn, 0, sizeof(*dyn));
}

static ds_status_t dy_levels(ds_dynamization_t *d, size_t n) {
  ds_dynamization_level_t *nl;
  size_t nc;
  if (n <= d->level_capacity) {
    while (d->level_count < n) {
      memset(&d->levels[d->level_count++], 0, sizeof(ds_dynamization_level_t));
    }
    return DS_OK;
  }
  nc = d->level_capacity < 8 ? 16 : d->level_capacity * 2;
  while (nc < n) {
    nc *= 2;
  }
  nl = (ds_dynamization_level_t *)ds_context_realloc(
      d->context, d->levels, nc * sizeof(ds_dynamization_level_t));
  if (nl == NULL) {
    return DS_ERR_ALLOC;
  }
  d->levels = nl;
  d->level_capacity = nc;
  return dy_levels(d, n);
}

/* a level holding items[0..count) */
static ds_status_t dy_make(ds_dynamization_t *d, unsigned char *items,
                           size_t *ids, size_t count,
                           ds_dynamization_level_t *out) {
  void *s = d->ops.build(items, ids, count, d->ops.user);
  if (s == NULL) {
    return DS_ERR_CALLBACK;
  }
  out->structure = s;
  out->items = items;
  out->ids = ids;
  out->count = count;
  out->capacity = count;
  return DS_OK;
}

ds_status_t ds_dynamization_insert(ds_dynamization_t *dyn, const void *item,
                                   size_t *out_id) {
  size_t below = 0;
  size_t k;
  size_t total;
  size_t i;
  size_t at;
  size_t id;
  unsigned char *items;
  size_t *ids;
  ds_dynamization_level_t made;
  ds_status_t st;
  if (dyn == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  if (dyn->ids == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  if (dyn->ids >= dyn->dead_capacity) {
    size_t nc = dyn->dead_capacity < 32 ? 64 : dyn->dead_capacity * 2;
    unsigned char *nd =
        (unsigned char *)ds_context_realloc(dyn->context, dyn->dead, nc);
    if (nd == NULL) {
      return DS_ERR_ALLOC;
    }
    memset(nd + dyn->dead_capacity, 0, nc - dyn->dead_capacity);
    dyn->dead = nd;
    dyn->dead_capacity = nc;
  }
  /* the first level that can absorb every level below it plus the item */
  for (k = 0;; k++) {
    size_t here = k < dyn->level_count ? dyn->levels[k].count : 0;
    if (below + 1 + here <= dy_limit(dyn, k)) {
      break;
    }
    below += here;
  }
  st = dy_levels(dyn, k + 1);
  if (st != DS_OK) {
    return st;
  }
  total = below + 1 + dyn->levels[k].count;
  if (total > (size_t)-1 / dyn->elem_size) {
    return DS_ERR_OVERFLOW;
  }
  items =
      (unsigned char *)ds_context_alloc(dyn->context, total * dyn->elem_size);
  ids = (size_t *)ds_context_alloc(dyn->context, total * sizeof(size_t));
  if (items == NULL || ids == NULL) {
    ds_context_free(dyn->context, items);
    ds_context_free(dyn->context, ids);
    return DS_ERR_ALLOC;
  }
  at = 0;
  for (i = 0; i <= k; i++) {
    ds_dynamization_level_t *l = &dyn->levels[i];
    if (l->count > 0) {
      memcpy(items + at * dyn->elem_size, l->items, l->count * dyn->elem_size);
      memcpy(ids + at, l->ids, l->count * sizeof(size_t));
      at += l->count;
    }
  }
  id = dyn->ids;
  memcpy(items + at * dyn->elem_size, item, dyn->elem_size);
  ids[at] = id;
  st = dy_make(dyn, items, ids, total, &made);
  if (st != DS_OK) {
    ds_context_free(dyn->context, items);
    ds_context_free(dyn->context, ids);
    return st;
  }
  for (i = 0; i <= k; i++) {
    dy_clear_level(dyn, &dyn->levels[i]);
  }
  dyn->levels[k] = made;
  dyn->ids++;
  dyn->live++;
  if (out_id != NULL) {
    *out_id = id;
  }
  return DS_OK;
}

bool ds_dynamization_is_alive(const ds_dynamization_t *dyn, size_t id) {
  return dyn != NULL && id < dyn->ids && dyn->dead[id] == 0;
}

ds_status_t ds_dynamization_rebuild(ds_dynamization_t *dyn) {
  ds_dynamization_level_t *fresh;
  unsigned char *items;
  size_t *ids;
  size_t n;
  size_t levels = 0;
  size_t i;
  size_t at = 0;
  size_t rest;
  size_t taken = 0;
  ds_status_t st = DS_OK;
  if (dyn == NULL) {
    return DS_ERR_NULL;
  }
  n = dyn->live;
  for (rest = n; rest > 0; rest /= dyn->base) {
    levels++;
  }
  fresh = (ds_dynamization_level_t *)ds_context_calloc(
      dyn->context, levels + 1, sizeof(ds_dynamization_level_t));
  items =
      (unsigned char *)ds_context_alloc(dyn->context, (n + 1) * dyn->elem_size);
  ids = (size_t *)ds_context_alloc(dyn->context, (n + 1) * sizeof(size_t));
  if (fresh == NULL || items == NULL || ids == NULL) {
    ds_context_free(dyn->context, fresh);
    ds_context_free(dyn->context, items);
    ds_context_free(dyn->context, ids);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < dyn->level_count; i++) {
    ds_dynamization_level_t *l = &dyn->levels[i];
    size_t j;
    for (j = 0; j < l->count; j++) {
      if (dyn->dead[l->ids[j]] == 0) {
        memcpy(items + at * dyn->elem_size, l->items + j * dyn->elem_size,
               dyn->elem_size);
        ids[at++] = l->ids[j];
      }
    }
  }
  /* one level per nonzero digit of n in base `base` */
  rest = n;
  for (i = 0; st == DS_OK && i < levels; i++) {
    size_t unit = dy_limit(dyn, i) / (dyn->base - 1);
    size_t digit = rest % dyn->base;
    size_t cnt = digit * unit;
    rest /= dyn->base;
    if (cnt == 0) {
      continue;
    }
    {
      unsigned char *li =
          (unsigned char *)ds_context_alloc(dyn->context, cnt * dyn->elem_size);
      size_t *lids =
          (size_t *)ds_context_alloc(dyn->context, cnt * sizeof(size_t));
      if (li == NULL || lids == NULL) {
        ds_context_free(dyn->context, li);
        ds_context_free(dyn->context, lids);
        st = DS_ERR_ALLOC;
        break;
      }
      memcpy(li, items + taken * dyn->elem_size, cnt * dyn->elem_size);
      memcpy(lids, ids + taken, cnt * sizeof(size_t));
      st = dy_make(dyn, li, lids, cnt, &fresh[i]);
      if (st != DS_OK) {
        ds_context_free(dyn->context, li);
        ds_context_free(dyn->context, lids);
        break;
      }
      taken += cnt;
    }
  }
  ds_context_free(dyn->context, items);
  ds_context_free(dyn->context, ids);
  if (st == DS_OK) {
    st = dy_levels(dyn, levels);
  }
  if (st != DS_OK) {
    for (i = 0; i < levels; i++) {
      dy_clear_level(dyn, &fresh[i]);
    }
    ds_context_free(dyn->context, fresh);
    return st;
  }
  for (i = 0; i < dyn->level_count; i++) {
    dy_clear_level(dyn, &dyn->levels[i]);
  }
  for (i = 0; i < levels; i++) {
    dyn->levels[i] = fresh[i];
  }
  dyn->level_count = levels;
  ds_context_free(dyn->context, fresh);
  dyn->dead_count = 0;
  dyn->rebuilds++;
  return DS_OK;
}

ds_status_t ds_dynamization_remove(ds_dynamization_t *dyn, size_t id) {
  if (dyn == NULL) {
    return DS_ERR_NULL;
  }
  if (id >= dyn->ids || dyn->dead[id] != 0) {
    return DS_ERR_RANGE;
  }
  dyn->dead[id] = 1;
  dyn->live--;
  dyn->dead_count++;
  if ((double)dyn->dead_count > dyn->rebuild_ratio * (double)dyn->live) {
    return ds_dynamization_rebuild(dyn);
  }
  return DS_OK;
}

ds_status_t ds_dynamization_query(const ds_dynamization_t *dyn,
                                  const void *query, void *result) {
  size_t i;
  if (dyn == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < dyn->level_count; i++) {
    const ds_dynamization_level_t *l = &dyn->levels[i];
    if (l->count > 0) {
      ds_status_t st =
          dyn->ops.query(l->structure, query, result, dyn, dyn->ops.user);
      if (st != DS_OK) {
        return st;
      }
    }
  }
  return DS_OK;
}

size_t ds_dynamization_size(const ds_dynamization_t *dyn) {
  return dyn == NULL ? 0 : dyn->live;
}

size_t ds_dynamization_structures(const ds_dynamization_t *dyn) {
  size_t i;
  size_t n = 0;
  if (dyn == NULL) {
    return 0;
  }
  for (i = 0; i < dyn->level_count; i++) {
    if (dyn->levels[i].count > 0) {
      n++;
    }
  }
  return n;
}
