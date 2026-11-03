#include "perfect_hash.h"
#include "hash.h"

static void ph_edge(const ds_perfect_hash_t *h, const void *data, size_t length,
                    size_t *u, size_t *v) {
  size_t half = h->vertices / 2;
  *u = (size_t)(ds_hash_murmur3_32(data, length, h->seed1) % half);
  *v = half + (size_t)(ds_hash_murmur3_32(data, length, h->seed2) % half);
}

size_t ds_perfect_hash_lookup(const ds_perfect_hash_t *hash, const void *data,
                              size_t length) {
  size_t u;
  size_t v;
  if (hash->count == 0) {
    return 0;
  }
  ph_edge(hash, data, length, &u, &v);
  return (size_t)((hash->g[u] + hash->g[v]) % (unsigned long)hash->count);
}

void ds_perfect_hash_destroy(ds_perfect_hash_t *hash) {
  if (hash != NULL) {
    ds_context_free(hash->context, hash->g);
    hash->g = NULL;
    hash->count = 0;
    hash->vertices = 0;
  }
}

static unsigned long ph_next_seed(unsigned long *state) {
  unsigned long x = (*state += 0x9E3779B9UL) & 0xFFFFFFFFUL;
  x ^= x >> 16;
  x = (x * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
  x ^= x >> 13;
  x = (x * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
  x ^= x >> 16;
  return x;
}

ds_status_t ds_perfect_hash_build(ds_perfect_hash_t *hash, size_t count,
                                  ds_perfect_hash_key_func_t key, void *user,
                                  double ratio, unsigned long seed,
                                  ds_context_t *context) {
  size_t m;
  size_t *eu = NULL;
  size_t *ev = NULL;
  size_t *xr = NULL;
  size_t *deg = NULL;
  size_t *stack = NULL;
  size_t *order = NULL; /* peeled (edge, vertex) pairs */
  size_t sp;
  size_t peeled;
  size_t i;
  size_t e;
  size_t x;
  size_t w;
  const void *data;
  size_t length;
  unsigned int attempt;
  ds_status_t status = DS_ERR_STATE;
  unsigned long n;

  if (hash == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  hash->g = NULL;
  hash->count = count;
  hash->context = context;
  if (!(ratio > 0)) {
    ratio = 2.09;
  }
  if (ratio < 1.0) {
    return DS_ERR_INVALID;
  }
  m = (size_t)((double)count * ratio) + 2;
  m += m & 1U;
  hash->vertices = m;
  if (count == 0) {
    hash->g =
        (unsigned long *)ds_context_calloc(context, m, sizeof(unsigned long));
    return hash->g == NULL ? DS_ERR_ALLOC : DS_OK;
  }
  if (count > ((size_t)-1) / 4 / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  eu = (size_t *)ds_context_alloc(context, count * sizeof(size_t));
  ev = (size_t *)ds_context_alloc(context, count * sizeof(size_t));
  xr = (size_t *)ds_context_alloc(context, m * sizeof(size_t));
  deg = (size_t *)ds_context_alloc(context, m * sizeof(size_t));
  stack = (size_t *)ds_context_alloc(context, m * sizeof(size_t));
  order = (size_t *)ds_context_alloc(context, 2 * count * sizeof(size_t));
  hash->g =
      (unsigned long *)ds_context_alloc(context, m * sizeof(unsigned long));
  if (eu == NULL || ev == NULL || xr == NULL || deg == NULL || stack == NULL ||
      order == NULL || hash->g == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  n = (unsigned long)count;
  for (attempt = 0; attempt < 64; attempt++) {
    hash->seed1 = ph_next_seed(&seed);
    hash->seed2 = ph_next_seed(&seed);
    for (i = 0; i < m; i++) {
      xr[i] = 0;
      deg[i] = 0;
    }
    for (e = 0; e < count; e++) {
      key(e, &data, &length, user);
      ph_edge(hash, data, length, &eu[e], &ev[e]);
      xr[eu[e]] ^= e;
      xr[ev[e]] ^= e;
      deg[eu[e]]++;
      deg[ev[e]]++;
    }
    /* Peel vertices of degree 1. */
    sp = 0;
    for (i = 0; i < m; i++) {
      if (deg[i] == 1) {
        stack[sp++] = i;
      }
    }
    peeled = 0;
    while (sp > 0) {
      x = stack[--sp];
      if (deg[x] != 1) {
        continue;
      }
      e = xr[x];
      order[2 * peeled] = e;
      order[2 * peeled + 1] = x;
      peeled++;
      w = eu[e] == x ? ev[e] : eu[e];
      deg[x] = 0;
      xr[w] ^= e;
      deg[w]--;
      if (deg[w] == 1) {
        stack[sp++] = w;
      }
    }
    if (peeled != count) {
      continue; /* a cycle (or a duplicated edge): new seeds */
    }
    for (i = 0; i < m; i++) {
      hash->g[i] = 0;
    }
    for (i = count; i-- > 0;) {
      e = order[2 * i];
      x = order[2 * i + 1];
      w = eu[e] == x ? ev[e] : eu[e];
      hash->g[x] = ((unsigned long)e + n - hash->g[w] % n) % n;
    }
    status = DS_OK;
    break;
  }
  if (status == DS_OK) {
    /* Cheap self-check of the construction. */
    for (e = 0; e < count; e++) {
      key(e, &data, &length, user);
      if (ds_perfect_hash_lookup(hash, data, length) != e) {
        status = DS_ERR_INVALID;
        break;
      }
    }
  }
out:
  ds_context_free(context, eu);
  ds_context_free(context, ev);
  ds_context_free(context, xr);
  ds_context_free(context, deg);
  ds_context_free(context, stack);
  ds_context_free(context, order);
  if (status != DS_OK) {
    ds_context_free(context, hash->g);
    hash->g = NULL;
  }
  return status;
}
