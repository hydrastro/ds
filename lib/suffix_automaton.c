#include "suffix_automaton.h"
#include <string.h>

#define SAM_NONE (~(size_t)0)

static void *sam_realloc(ds_context_t *ctx, void *p, size_t count,
                         size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_realloc(ctx, p, count * size);
}

static size_t sam_key_hash(size_t state, unsigned char c, size_t mask) {
  size_t k = state * 257u + c;
  k ^= k >> 15;
  k *= 2246822519UL;
  k ^= k >> 13;
  return k & mask;
}

static ds_status_t sam_new_state(ds_suffix_automaton_t *sa, size_t len,
                                 size_t firstpos, unsigned char cloned,
                                 size_t *out) {
  size_t cap;
  if (sa->states == sa->state_capacity) {
    size_t *g;
    unsigned char *u;
    cap = sa->state_capacity < 16 ? 16 : sa->state_capacity * 2;
    g = (size_t *)sam_realloc(sa->context, sa->len, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->len = g;
    g = (size_t *)sam_realloc(sa->context, sa->link, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->link = g;
    g = (size_t *)sam_realloc(sa->context, sa->firstpos, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->firstpos = g;
    g = (size_t *)sam_realloc(sa->context, sa->edge_head, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->edge_head = g;
    u = (unsigned char *)sam_realloc(sa->context, sa->cloned, cap, 1);
    if (u == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->cloned = u;
    sa->state_capacity = cap;
  }
  sa->len[sa->states] = len;
  sa->link[sa->states] = SAM_NONE;
  sa->firstpos[sa->states] = firstpos;
  sa->edge_head[sa->states] = SAM_NONE;
  sa->cloned[sa->states] = cloned;
  *out = sa->states++;
  return DS_OK;
}

static ds_status_t sam_rehash(ds_suffix_automaton_t *sa, size_t size) {
  size_t *h;
  size_t i;
  size_t s;
  size_t e;
  h = (size_t *)sam_realloc(sa->context, NULL, size, sizeof(size_t));
  if (h == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < size; i++) {
    h[i] = SAM_NONE;
  }
  for (s = 0; s < sa->states; s++) {
    for (e = sa->edge_head[s]; e != SAM_NONE; e = sa->edge_next[e]) {
      i = sam_key_hash(s, sa->edge_symbol[e], size - 1);
      while (h[i] != SAM_NONE) {
        i = (i + 1) & (size - 1);
      }
      h[i] = e;
    }
  }
  ds_context_free(sa->context, sa->hash);
  sa->hash = h;
  sa->hash_size = size;
  return DS_OK;
}

/* Each edge's source state lives right after edge_to[] in the same block,
 * so hash probes can check the full (state, symbol) key. */
static size_t *sam_edge_src(const ds_suffix_automaton_t *sa) {
  return sa->edge_to + sa->edge_capacity;
}

/* Edge id of (state, c), or SAM_NONE; *slot receives the probe position
 * where it is or would go. */
static size_t sam_find(const ds_suffix_automaton_t *sa, size_t state,
                       unsigned char c, size_t *slot) {
  size_t i;
  size_t e;
  const size_t *src = sam_edge_src(sa);
  if (sa->hash_size == 0) {
    if (slot != NULL) {
      *slot = SAM_NONE;
    }
    return SAM_NONE;
  }
  i = sam_key_hash(state, c, sa->hash_size - 1);
  while ((e = sa->hash[i]) != SAM_NONE) {
    if (src[e] == state && sa->edge_symbol[e] == c) {
      break;
    }
    i = (i + 1) & (sa->hash_size - 1);
  }
  if (slot != NULL) {
    *slot = i;
  }
  return e;
}

static ds_status_t sam_add_edge(ds_suffix_automaton_t *sa, size_t from,
                                unsigned char c, size_t to) {
  size_t cap;
  size_t slot;
  ds_status_t st;
  if (sa->edges == sa->edge_capacity) {
    size_t *g;
    unsigned char *u;
    cap = sa->edge_capacity < 32 ? 32 : sa->edge_capacity * 2;
    /* edge_to holds to[cap] then src[cap] */
    g = (size_t *)sam_realloc(sa->context, NULL, 2 * cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    if (sa->edges > 0) {
      memcpy(g, sa->edge_to, sa->edges * sizeof(size_t));
      memcpy(g + cap, sam_edge_src(sa), sa->edges * sizeof(size_t));
    }
    ds_context_free(sa->context, sa->edge_to);
    sa->edge_to = g;
    g = (size_t *)sam_realloc(sa->context, sa->edge_next, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->edge_next = g;
    u = (unsigned char *)sam_realloc(sa->context, sa->edge_symbol, cap, 1);
    if (u == NULL) {
      return DS_ERR_ALLOC;
    }
    sa->edge_symbol = u;
    sa->edge_capacity = cap;
  }
  if (2 * (sa->edges + 1) > sa->hash_size) {
    st = sam_rehash(sa, sa->hash_size < 64 ? 64 : sa->hash_size * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  sa->edge_to[sa->edges] = to;
  sam_edge_src(sa)[sa->edges] = from;
  sa->edge_symbol[sa->edges] = c;
  sa->edge_next[sa->edges] = sa->edge_head[from];
  sa->edge_head[from] = sa->edges;
  sam_find(sa, from, c, &slot);
  sa->hash[slot] = sa->edges;
  sa->edges++;
  return DS_OK;
}

ds_status_t ds_suffix_automaton_init(ds_suffix_automaton_t *sa,
                                     ds_context_t *context) {
  size_t root;
  if (sa == NULL) {
    return DS_ERR_NULL;
  }
  memset(sa, 0, sizeof(*sa));
  sa->context = context;
  sa->last = 0;
  return sam_new_state(sa, 0, SAM_NONE, 0, &root);
}

void ds_suffix_automaton_destroy(ds_suffix_automaton_t *sa) {
  if (sa == NULL) {
    return;
  }
  ds_context_free(sa->context, sa->len);
  ds_context_free(sa->context, sa->link);
  ds_context_free(sa->context, sa->firstpos);
  ds_context_free(sa->context, sa->edge_head);
  ds_context_free(sa->context, sa->cloned);
  ds_context_free(sa->context, sa->edge_to);
  ds_context_free(sa->context, sa->edge_next);
  ds_context_free(sa->context, sa->edge_symbol);
  ds_context_free(sa->context, sa->hash);
  ds_context_free(sa->context, sa->occurrences);
  memset(sa, 0, sizeof(*sa));
}

size_t ds_suffix_automaton_next(const ds_suffix_automaton_t *sa, size_t state,
                                unsigned char c) {
  size_t e;
  if (sa == NULL || state >= sa->states) {
    return SAM_NONE;
  }
  e = sam_find(sa, state, c, NULL);
  return e == SAM_NONE ? SAM_NONE : sa->edge_to[e];
}

ds_status_t ds_suffix_automaton_extend(ds_suffix_automaton_t *sa,
                                       unsigned char c) {
  size_t cur;
  size_t p;
  size_t q;
  size_t clone;
  size_t e;
  size_t eq;
  ds_status_t st;
  if (sa == NULL) {
    return DS_ERR_NULL;
  }
  if (sa->occurrences != NULL) {
    ds_context_free(sa->context, sa->occurrences);
    sa->occurrences = NULL;
  }
  st = sam_new_state(sa, sa->len[sa->last] + 1, sa->length, 0, &cur);
  if (st != DS_OK) {
    return st;
  }
  p = sa->last;
  while (p != SAM_NONE && sam_find(sa, p, c, NULL) == SAM_NONE) {
    st = sam_add_edge(sa, p, c, cur);
    if (st != DS_OK) {
      return st;
    }
    p = sa->link[p];
  }
  if (p == SAM_NONE) {
    sa->link[cur] = 0;
  } else {
    q = sa->edge_to[sam_find(sa, p, c, NULL)];
    if (sa->len[p] + 1 == sa->len[q]) {
      sa->link[cur] = q;
    } else {
      st = sam_new_state(sa, sa->len[p] + 1, sa->firstpos[q], 1, &clone);
      if (st != DS_OK) {
        return st;
      }
      for (e = sa->edge_head[q]; e != SAM_NONE; e = sa->edge_next[e]) {
        st = sam_add_edge(sa, clone, sa->edge_symbol[e], sa->edge_to[e]);
        if (st != DS_OK) {
          return st;
        }
      }
      sa->link[clone] = sa->link[q];
      while (p != SAM_NONE) {
        eq = sam_find(sa, p, c, NULL);
        if (eq == SAM_NONE || sa->edge_to[eq] != q) {
          break;
        }
        sa->edge_to[eq] = clone;
        p = sa->link[p];
      }
      sa->link[q] = clone;
      sa->link[cur] = clone;
    }
  }
  sa->last = cur;
  sa->length++;
  return DS_OK;
}

ds_status_t ds_suffix_automaton_build(ds_suffix_automaton_t *sa,
                                      const unsigned char *s, size_t n,
                                      ds_context_t *context) {
  size_t i;
  ds_status_t st;
  if (sa == NULL || (s == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  st = ds_suffix_automaton_init(sa, context);
  for (i = 0; i < n && st == DS_OK; i++) {
    st = ds_suffix_automaton_extend(sa, s[i]);
  }
  if (st != DS_OK) {
    ds_suffix_automaton_destroy(sa);
  }
  return st;
}

static size_t sam_walk(const ds_suffix_automaton_t *sa, const unsigned char *p,
                       size_t m) {
  size_t v = 0;
  size_t i;
  for (i = 0; i < m && v != SAM_NONE; i++) {
    v = ds_suffix_automaton_next(sa, v, p[i]);
  }
  return v;
}

bool ds_suffix_automaton_contains(const ds_suffix_automaton_t *sa,
                                  const unsigned char *p, size_t m) {
  if (sa == NULL || (p == NULL && m > 0)) {
    return false;
  }
  return sam_walk(sa, p, m) != SAM_NONE;
}

bool ds_suffix_automaton_is_suffix(const ds_suffix_automaton_t *sa,
                                   const unsigned char *p, size_t m) {
  size_t v;
  size_t u;
  if (sa == NULL || (p == NULL && m > 0)) {
    return false;
  }
  v = sam_walk(sa, p, m);
  if (v == SAM_NONE) {
    return false;
  }
  for (u = sa->last; u != SAM_NONE; u = sa->link[u]) {
    if (u == v) {
      return true;
    }
  }
  return false;
}

static ds_status_t sam_prepare_counts(ds_suffix_automaton_t *sa) {
  size_t *cnt;
  size_t *order;
  size_t *bucket;
  size_t i;
  size_t v;
  if (sa->occurrences != NULL) {
    return DS_OK;
  }
  cnt = (size_t *)sam_realloc(sa->context, NULL, sa->states, sizeof(size_t));
  order = (size_t *)sam_realloc(sa->context, NULL, sa->states + sa->length + 2,
                                sizeof(size_t));
  if (cnt == NULL || order == NULL) {
    ds_context_free(sa->context, cnt);
    ds_context_free(sa->context, order);
    return DS_ERR_ALLOC;
  }
  bucket = order + sa->states;
  memset(bucket, 0, (sa->length + 2) * sizeof(size_t));
  for (v = 0; v < sa->states; v++) {
    cnt[v] = (v != 0 && !sa->cloned[v]) ? 1 : 0;
    bucket[sa->len[v] + 1]++;
  }
  for (i = 0; i <= sa->length; i++) {
    bucket[i + 1] += bucket[i];
  }
  for (v = 0; v < sa->states; v++) {
    order[bucket[sa->len[v]]++] = v;
  }
  for (i = sa->states; i-- > 1;) {
    v = order[i];
    if (sa->link[v] != SAM_NONE) {
      cnt[sa->link[v]] += cnt[v];
    }
  }
  ds_context_free(sa->context, order);
  sa->occurrences = cnt;
  return DS_OK;
}

ds_status_t ds_suffix_automaton_count(ds_suffix_automaton_t *sa,
                                      const unsigned char *p, size_t m,
                                      size_t *out_count) {
  size_t v;
  ds_status_t st;
  if (sa == NULL || out_count == NULL || (p == NULL && m > 0)) {
    return DS_ERR_NULL;
  }
  if (m == 0) {
    *out_count = sa->length + 1;
    return DS_OK;
  }
  v = sam_walk(sa, p, m);
  if (v == SAM_NONE) {
    *out_count = 0;
    return DS_OK;
  }
  st = sam_prepare_counts(sa);
  if (st != DS_OK) {
    return st;
  }
  *out_count = sa->occurrences[v];
  return DS_OK;
}

ds_status_t ds_suffix_automaton_first(const ds_suffix_automaton_t *sa,
                                      const unsigned char *p, size_t m,
                                      size_t *out_position) {
  size_t v;
  if (sa == NULL || out_position == NULL || (p == NULL && m > 0)) {
    return DS_ERR_NULL;
  }
  if (m == 0) {
    *out_position = 0;
    return DS_OK;
  }
  v = sam_walk(sa, p, m);
  if (v == SAM_NONE) {
    return DS_NOT_FOUND;
  }
  *out_position = sa->firstpos[v] + 1 - m;
  return DS_OK;
}

size_t ds_suffix_automaton_distinct(const ds_suffix_automaton_t *sa) {
  size_t v;
  size_t total = 0;
  if (sa == NULL) {
    return 0;
  }
  for (v = 1; v < sa->states; v++) {
    total += sa->len[v] - sa->len[sa->link[v]];
  }
  return total;
}

void ds_suffix_automaton_lcs(const ds_suffix_automaton_t *sa,
                             const unsigned char *t, size_t m,
                             size_t *out_t_pos, size_t *out_length) {
  size_t v = 0;
  size_t l = 0;
  size_t best = 0;
  size_t bpos = 0;
  size_t i;
  size_t nx;
  if (sa != NULL && (t != NULL || m == 0)) {
    for (i = 0; i < m; i++) {
      while (v != 0 && ds_suffix_automaton_next(sa, v, t[i]) == SAM_NONE) {
        v = sa->link[v];
        l = sa->len[v];
      }
      nx = ds_suffix_automaton_next(sa, v, t[i]);
      if (nx != SAM_NONE) {
        v = nx;
        l++;
      }
      if (l > best) {
        best = l;
        bpos = i + 1 - l;
      }
    }
  }
  if (out_t_pos != NULL) {
    *out_t_pos = bpos;
  }
  if (out_length != NULL) {
    *out_length = best;
  }
}

/* ---- compact DAWG --------------------------------------------------- */

ds_status_t ds_cdawg_build(ds_cdawg_t *cd, const ds_suffix_automaton_t *sa,
                           const unsigned char *text, size_t n,
                           ds_context_t *context) {
  size_t *node_of;
  size_t *outdeg;
  unsigned char *final;
  size_t v;
  size_t e;
  size_t f;
  size_t k;
  size_t len;
  size_t u;
  if (cd == NULL || sa == NULL || (text == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  if (n != sa->length) {
    return DS_ERR_INVALID;
  }
  memset(cd, 0, sizeof(*cd));
  cd->text = text;
  cd->length = n;
  cd->context = context;
  node_of =
      (size_t *)sam_realloc(context, NULL, 2 * sa->states, sizeof(size_t));
  final = (unsigned char *)ds_context_calloc(context, sa->states + 1, 1);
  if (node_of == NULL || final == NULL) {
    ds_context_free(context, node_of);
    ds_context_free(context, final);
    return DS_ERR_ALLOC;
  }
  outdeg = node_of + sa->states;
  for (u = sa->last; u != SAM_NONE; u = sa->link[u]) {
    final[u] = 1;
  }
  for (v = 0; v < sa->states; v++) {
    outdeg[v] = 0;
    for (e = sa->edge_head[v]; e != SAM_NONE; e = sa->edge_next[e]) {
      outdeg[v]++;
    }
  }
  for (v = 0; v < sa->states; v++) {
    if (v == 0 || outdeg[v] != 1 || final[v]) {
      node_of[v] = cd->nodes++;
    } else {
      node_of[v] = SAM_NONE;
    }
  }
  cd->edge_head =
      (size_t *)sam_realloc(context, NULL, cd->nodes, sizeof(size_t));
  cd->final = (unsigned char *)sam_realloc(context, NULL, cd->nodes, 1);
  cd->edge_to =
      (size_t *)sam_realloc(context, NULL, 4 * (sa->edges + 1), sizeof(size_t));
  if (cd->edge_head == NULL || cd->final == NULL || cd->edge_to == NULL) {
    ds_context_free(context, node_of);
    ds_context_free(context, final);
    ds_cdawg_destroy(cd);
    return DS_ERR_ALLOC;
  }
  cd->edge_start = cd->edge_to + (sa->edges + 1);
  cd->edge_len = cd->edge_start + (sa->edges + 1);
  cd->edge_next = cd->edge_len + (sa->edges + 1);
  cd->root = node_of[0];
  for (v = 0; v < sa->states; v++) {
    if (node_of[v] == SAM_NONE) {
      continue;
    }
    cd->final[node_of[v]] = final[v];
    cd->edge_head[node_of[v]] = SAM_NONE;
  }
  for (v = 0; v < sa->states; v++) {
    if (node_of[v] == SAM_NONE) {
      continue;
    }
    for (e = sa->edge_head[v]; e != SAM_NONE; e = sa->edge_next[e]) {
      /* follow the chain of contracted states */
      u = sa->edge_to[e];
      len = 1;
      while (node_of[u] == SAM_NONE) {
        f = sa->edge_head[u];
        u = sa->edge_to[f];
        len++;
      }
      k = cd->edges++;
      cd->edge_to[k] = node_of[u];
      cd->edge_len[k] = len;
      cd->edge_start[k] = sa->firstpos[u] + 1 - len;
      cd->edge_next[k] = cd->edge_head[node_of[v]];
      cd->edge_head[node_of[v]] = k;
    }
  }
  ds_context_free(context, node_of);
  ds_context_free(context, final);
  return DS_OK;
}

void ds_cdawg_destroy(ds_cdawg_t *cd) {
  if (cd == NULL) {
    return;
  }
  ds_context_free(cd->context, cd->edge_head);
  ds_context_free(cd->context, cd->final);
  ds_context_free(cd->context, cd->edge_to);
  cd->edge_head = NULL;
  cd->final = NULL;
  cd->edge_to = NULL;
  cd->nodes = 0;
  cd->edges = 0;
}

bool ds_cdawg_contains(const ds_cdawg_t *cd, const unsigned char *p, size_t m) {
  size_t node;
  size_t i = 0;
  size_t e;
  size_t j;
  if (cd == NULL || (p == NULL && m > 0) || cd->nodes == 0) {
    return false;
  }
  node = cd->root;
  while (i < m) {
    for (e = cd->edge_head[node]; e != SAM_NONE; e = cd->edge_next[e]) {
      if (cd->text[cd->edge_start[e]] == p[i]) {
        break;
      }
    }
    if (e == SAM_NONE) {
      return false;
    }
    for (j = 0; j < cd->edge_len[e] && i < m; j++, i++) {
      if (cd->text[cd->edge_start[e] + j] != p[i]) {
        return false;
      }
    }
    node = cd->edge_to[e];
  }
  return true;
}
