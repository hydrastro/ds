#include "pagoda.h"
#include <string.h>

#define PG_NONE DS_PAGODA_EMPTY

ds_status_t ds_pagoda_pool_init(ds_pagoda_pool_t *pool, size_t elem_size,
                                ds_pagoda_compare_func_t compare, void *user,
                                ds_context_t *context) {
  if (pool == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(pool, 0, sizeof(*pool));
  pool->free_list = PG_NONE;
  pool->elem_size = elem_size;
  pool->compare = compare;
  pool->user = user;
  pool->context = context;
  return DS_OK;
}

void ds_pagoda_pool_destroy(ds_pagoda_pool_t *pool) {
  if (pool == NULL) {
    return;
  }
  ds_context_free(pool->context, pool->data);
  ds_context_free(pool->context, pool->left);
  ds_context_free(pool->context, pool->right);
  pool->data = NULL;
  pool->left = pool->right = NULL;
  pool->slots = pool->capacity = 0;
  pool->free_list = PG_NONE;
}

/* a has priority over b (a's key is smaller). */
static bool pg_before(const ds_pagoda_pool_t *p, size_t a, size_t b) {
  return p->compare(p->data + a * p->elem_size, p->data + b * p->elem_size,
                    p->user) < 0;
}

/* Meld two pagodas (roots a and b). */
static size_t pg_merge(ds_pagoda_pool_t *p, size_t a, size_t b) {
  size_t bota;
  size_t botb;
  size_t r = PG_NONE;
  size_t tmp;
  if (a == PG_NONE) {
    return b;
  }
  if (b == PG_NONE) {
    return a;
  }
  /* bottom of a's right branch and of b's left branch */
  bota = p->right[a];
  p->right[a] = PG_NONE;
  botb = p->left[b];
  p->left[b] = PG_NONE;
  /* merge the two branches from the bottom; r is the last node placed */
  while (bota != PG_NONE && botb != PG_NONE) {
    if (pg_before(p, botb, bota)) {
      /* bota is lower priority: it goes below on the right branch */
      tmp = p->right[bota];
      if (r == PG_NONE) {
        p->right[bota] = bota;
      } else {
        p->right[bota] = p->right[r];
        p->right[r] = bota;
      }
      r = bota;
      bota = tmp;
    } else {
      tmp = p->left[botb];
      if (r == PG_NONE) {
        p->left[botb] = botb;
      } else {
        p->left[botb] = p->left[r];
        p->left[r] = botb;
      }
      r = botb;
      botb = tmp;
    }
  }
  if (botb == PG_NONE) {
    p->right[a] = p->right[r];
    p->right[r] = bota;
    return a;
  }
  p->left[b] = p->left[r];
  p->left[r] = botb;
  return b;
}

ds_status_t ds_pagoda_push(ds_pagoda_pool_t *pool, ds_pagoda_t *q,
                           const void *elem) {
  size_t v;
  size_t cap;
  void *p;
  if (pool == NULL || q == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (pool->free_list != PG_NONE) {
    v = pool->free_list;
    pool->free_list = pool->left[v];
  } else {
    if (pool->slots == pool->capacity) {
      cap = pool->capacity < 8 ? 8 : pool->capacity * 2;
      if (cap <= pool->capacity || cap > ((size_t)-1) / pool->elem_size ||
          cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(pool->context, pool->data, cap * pool->elem_size);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      pool->data = (unsigned char *)p;
      p = ds_context_realloc(pool->context, pool->left, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      pool->left = (size_t *)p;
      p = ds_context_realloc(pool->context, pool->right, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      pool->right = (size_t *)p;
      pool->capacity = cap;
    }
    v = pool->slots++;
  }
  memcpy(pool->data + v * pool->elem_size, elem, pool->elem_size);
  pool->left[v] = v;
  pool->right[v] = v;
  *q = pg_merge(pool, *q, v);
  return DS_OK;
}

ds_status_t ds_pagoda_peek(const ds_pagoda_pool_t *pool, ds_pagoda_t q,
                           void *out) {
  if (pool == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (q == PG_NONE) {
    return DS_EMPTY;
  }
  memcpy(out, pool->data + q * pool->elem_size, pool->elem_size);
  return DS_OK;
}

ds_status_t ds_pagoda_pop(ds_pagoda_pool_t *pool, ds_pagoda_t *q, void *out) {
  size_t root;
  size_t le;
  size_t ri;
  if (pool == NULL || q == NULL) {
    return DS_ERR_NULL;
  }
  root = *q;
  if (root == PG_NONE) {
    return DS_EMPTY;
  }
  if (out != NULL) {
    memcpy(out, pool->data + root * pool->elem_size, pool->elem_size);
  }
  /* the left branch without the root: its top node becomes a root */
  if (pool->left[root] == root) {
    le = PG_NONE;
  } else {
    le = pool->left[root];
    while (pool->left[le] != root) {
      le = pool->left[le];
    }
    pool->left[le] = pool->left[root];
  }
  if (pool->right[root] == root) {
    ri = PG_NONE;
  } else {
    ri = pool->right[root];
    while (pool->right[ri] != root) {
      ri = pool->right[ri];
    }
    pool->right[ri] = pool->right[root];
  }
  *q = pg_merge(pool, le, ri);
  pool->left[root] = pool->free_list;
  pool->free_list = root;
  return DS_OK;
}

void ds_pagoda_meld(ds_pagoda_pool_t *pool, ds_pagoda_t *q,
                    ds_pagoda_t *other) {
  if (pool == NULL || q == NULL || other == NULL || q == other) {
    return;
  }
  *q = pg_merge(pool, *q, *other);
  *other = PG_NONE;
}
