#include "lca.h"
#include <string.h>

#define LC_NONE DS_LCA_NONE

/* Children in CSR form (off: n + 1, kids: n) and a validity check that
 * the parent array is a forest. order receives a preorder of all nodes,
 * depth and root filled. */
static ds_status_t lc_forest(const size_t *parent, size_t n, size_t *off,
                             size_t *kids, size_t *order, size_t *depth,
                             size_t *root) {
  size_t i;
  size_t top;
  size_t cnt = 0;
  size_t v;
  size_t k;
  for (i = 0; i <= n; i++) {
    off[i] = 0;
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != LC_NONE) {
      if (parent[i] >= n || parent[i] == i) {
        return DS_ERR_INVALID;
      }
      off[parent[i] + 1]++;
    }
  }
  for (i = 0; i < n; i++) {
    off[i + 1] += off[i];
  }
  /* fill with a moving cursor kept in depth[] temporarily */
  for (i = 0; i < n; i++) {
    depth[i] = off[i];
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != LC_NONE) {
      kids[depth[parent[i]]++] = i;
    }
  }
  /* preorder from every root; order doubles as the stack */
  for (i = 0; i < n; i++) {
    if (parent[i] != LC_NONE) {
      continue;
    }
    top = cnt;
    order[cnt++] = i;
    depth[i] = 0;
    root[i] = i;
    while (top < cnt) {
      v = order[top++];
      for (k = off[v]; k < off[v + 1]; k++) {
        depth[kids[k]] = depth[v] + 1;
        root[kids[k]] = root[v];
        order[cnt++] = kids[k];
      }
    }
  }
  return cnt == n ? DS_OK : DS_ERR_INVALID; /* nodes on cycles unreached */
}

/* ---- binary lifting --------------------------------------------------------
 */

ds_status_t ds_lca_lifting_build(ds_lca_lifting_t *l, const size_t *parent,
                                 size_t n, ds_context_t *context) {
  size_t *off;
  size_t *kids;
  size_t *order;
  size_t i;
  size_t k;
  size_t v;
  ds_status_t st;
  if (l == NULL || (n > 0 && parent == NULL)) {
    return DS_ERR_NULL;
  }
  memset(l, 0, sizeof(*l));
  l->n = n;
  l->context = context;
  l->levels = 1;
  while (((size_t)1 << l->levels) < n) {
    l->levels++;
  }
  if (n > ((size_t)-1) / sizeof(size_t) / (l->levels + 6)) {
    return DS_ERR_OVERFLOW;
  }
  l->up = (size_t *)ds_context_alloc(
      context, (n > 0 ? n : 1) * (l->levels + 2) * sizeof(size_t));
  off = (size_t *)ds_context_alloc(context, (3 * n + 1) * sizeof(size_t));
  if (l->up == NULL || off == NULL) {
    ds_context_free(context, l->up);
    ds_context_free(context, off);
    l->up = NULL;
    return DS_ERR_ALLOC;
  }
  l->depth = l->up + n * l->levels;
  l->root = l->depth + n;
  kids = off + n + 1;
  order = kids + n;
  st = lc_forest(parent, n, off, kids, order, l->depth, l->root);
  ds_context_free(context, off);
  if (st != DS_OK) {
    ds_lca_lifting_destroy(l);
    return st;
  }
  for (i = 0; i < n; i++) {
    l->up[i] = parent[i];
  }
  for (k = 1; k < l->levels; k++) {
    for (v = 0; v < n; v++) {
      size_t mid = l->up[(k - 1) * n + v];
      l->up[k * n + v] = mid == LC_NONE ? LC_NONE : l->up[(k - 1) * n + mid];
    }
  }
  return DS_OK;
}

void ds_lca_lifting_destroy(ds_lca_lifting_t *l) {
  if (l == NULL) {
    return;
  }
  ds_context_free(l->context, l->up);
  l->up = l->depth = l->root = NULL;
  l->n = 0;
}

size_t ds_lca_lifting_ancestor(const ds_lca_lifting_t *l, size_t v, size_t k) {
  size_t b;
  if (l == NULL || v >= l->n) {
    return LC_NONE;
  }
  if (k > l->depth[v]) {
    return LC_NONE;
  }
  for (b = 0; k != 0 && v != LC_NONE; b++, k >>= 1) {
    if (k & 1u) {
      v = l->up[b * l->n + v];
    }
  }
  return v;
}

size_t ds_lca_lifting_query(const ds_lca_lifting_t *l, size_t u, size_t v) {
  size_t k;
  if (l == NULL || u >= l->n || v >= l->n || l->root[u] != l->root[v]) {
    return LC_NONE;
  }
  if (l->depth[u] < l->depth[v]) {
    k = u;
    u = v;
    v = k;
  }
  u = ds_lca_lifting_ancestor(l, u, l->depth[u] - l->depth[v]);
  if (u == v) {
    return u;
  }
  for (k = l->levels; k-- > 0;) {
    if (l->up[k * l->n + u] != l->up[k * l->n + v]) {
      u = l->up[k * l->n + u];
      v = l->up[k * l->n + v];
    }
  }
  return l->up[u];
}

size_t ds_lca_lifting_distance(const ds_lca_lifting_t *l, size_t u, size_t v) {
  size_t a = ds_lca_lifting_query(l, u, v);
  if (a == LC_NONE) {
    return LC_NONE;
  }
  return l->depth[u] + l->depth[v] - 2 * l->depth[a];
}

/* ---- Euler tour + sparse table ---------------------------------------------
 */

ds_status_t ds_lca_euler_build(ds_lca_euler_t *e, const size_t *parent,
                               size_t n, ds_context_t *context) {
  size_t *off;
  size_t *kids;
  size_t *order;
  size_t *stack;
  size_t *it;
  size_t i;
  size_t k;
  size_t top;
  size_t v;
  size_t len;
  ds_status_t st;
  if (e == NULL || (n > 0 && parent == NULL)) {
    return DS_ERR_NULL;
  }
  memset(e, 0, sizeof(*e));
  e->n = n;
  e->context = context;
  if (n == 0) {
    return DS_OK;
  }
  len = 2 * n; /* at most 2n - 1 positions per forest */
  e->levels = 1;
  while (((size_t)1 << e->levels) <= len) {
    e->levels++;
  }
  if (len > ((size_t)-1) / sizeof(size_t) / (e->levels + 8)) {
    return DS_ERR_OVERFLOW;
  }
  e->tour = (size_t *)ds_context_alloc(
      context, (len * (e->levels + 2) + 3 * n) * sizeof(size_t));
  off = (size_t *)ds_context_alloc(context, (5 * n + 1) * sizeof(size_t));
  if (e->tour == NULL || off == NULL) {
    ds_context_free(context, e->tour);
    ds_context_free(context, off);
    e->tour = NULL;
    return DS_ERR_ALLOC;
  }
  e->lg = e->tour + len;
  e->table = e->lg + len;
  e->depth = e->table + len * e->levels;
  e->first = e->depth + n;
  e->root = e->first + n;
  kids = off + n + 1;
  order = kids + n;
  stack = order + n;
  it = stack + n;
  st = lc_forest(parent, n, off, kids, order, e->depth, e->root);
  if (st != DS_OK) {
    ds_context_free(context, off);
    ds_lca_euler_destroy(e);
    return st;
  }
  /* iterative Euler tours of every tree, concatenated */
  e->len = 0;
  for (i = 0; i < n; i++) {
    if (parent[i] != LC_NONE) {
      continue;
    }
    top = 0;
    stack[top++] = i;
    it[i] = off[i];
    e->first[i] = e->len;
    e->tour[e->len++] = i;
    while (top > 0) {
      v = stack[top - 1];
      if (it[v] < off[v + 1]) {
        k = kids[it[v]++];
        it[k] = off[k];
        e->first[k] = e->len;
        e->tour[e->len++] = k;
        stack[top++] = k;
      } else {
        top--;
        if (top > 0) {
          e->tour[e->len++] = stack[top - 1];
        }
      }
    }
  }
  ds_context_free(context, off);
  e->lg[0] = 0;
  e->lg[1] = 0;
  for (i = 2; i < e->len + 1 && i < len; i++) {
    e->lg[i] = e->lg[i / 2] + 1;
  }
  for (i = 0; i < e->len; i++) {
    e->table[i] = i;
  }
  for (k = 1; k < e->levels; k++) {
    for (i = 0; i + ((size_t)1 << k) <= e->len; i++) {
      size_t a = e->table[(k - 1) * len + i];
      size_t b = e->table[(k - 1) * len + i + ((size_t)1 << (k - 1))];
      e->table[k * len + i] =
          e->depth[e->tour[a]] <= e->depth[e->tour[b]] ? a : b;
    }
  }
  return DS_OK;
}

void ds_lca_euler_destroy(ds_lca_euler_t *e) {
  if (e == NULL) {
    return;
  }
  ds_context_free(e->context, e->tour);
  e->tour = NULL;
  e->n = e->len = 0;
}

size_t ds_lca_euler_query(const ds_lca_euler_t *e, size_t u, size_t v) {
  size_t a;
  size_t b;
  size_t k;
  size_t x;
  size_t y;
  size_t stride;
  if (e == NULL || u >= e->n || v >= e->n || e->root[u] != e->root[v]) {
    return LC_NONE;
  }
  a = e->first[u];
  b = e->first[v];
  if (a > b) {
    k = a;
    a = b;
    b = k;
  }
  stride = 2 * e->n;
  k = e->lg[b - a + 1];
  x = e->table[k * stride + a];
  y = e->table[k * stride + b + 1 - ((size_t)1 << k)];
  return e->depth[e->tour[x]] <= e->depth[e->tour[y]] ? e->tour[x] : e->tour[y];
}

/* ---- Tarjan's offline LCA ---------------------------------------------------
 */

static size_t lc_find(size_t *uf, size_t x) {
  size_t r = x;
  size_t t;
  while (uf[r] != r) {
    r = uf[r];
  }
  while (uf[x] != r) {
    t = uf[x];
    uf[x] = r;
    x = t;
  }
  return r;
}

ds_status_t ds_lca_offline(const size_t *parent, size_t n, const size_t *qu,
                           const size_t *qv, size_t q, size_t *answers,
                           ds_context_t *context) {
  size_t *off;
  size_t *kids;
  size_t *order;
  size_t *depth;
  size_t *root;
  size_t *uf;
  size_t *anc;
  size_t *stack;
  size_t *it;
  size_t *qoff;
  size_t *qlist;
  unsigned char *done;
  size_t i;
  size_t k;
  size_t top;
  size_t v;
  size_t w;
  ds_status_t st;
  if ((n > 0 && parent == NULL) ||
      (q > 0 && (qu == NULL || qv == NULL || answers == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < q; i++) {
    if (qu[i] >= n || qv[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  if (n > ((size_t)-1) / sizeof(size_t) / 12 ||
      q > ((size_t)-1) / sizeof(size_t) / 4) {
    return DS_ERR_OVERFLOW;
  }
  off = (size_t *)ds_context_alloc(context,
                                   (10 * n + 2 + 2 * q) * sizeof(size_t));
  done = (unsigned char *)ds_context_calloc(context, n > 0 ? n : 1, 1);
  if (off == NULL || done == NULL) {
    ds_context_free(context, off);
    ds_context_free(context, done);
    return DS_ERR_ALLOC;
  }
  kids = off + n + 1;
  order = kids + n;
  depth = order + n;
  root = depth + n;
  uf = root + n;
  anc = uf + n;
  stack = anc + n;
  it = stack + n;
  qoff = it + n;        /* n + 1 */
  qlist = qoff + n + 1; /* 2q: query ids by endpoint */
  st = lc_forest(parent, n, off, kids, order, depth, root);
  if (st != DS_OK) {
    ds_context_free(context, off);
    ds_context_free(context, done);
    return st;
  }
  /* queries bucketed by endpoint */
  for (i = 0; i <= n; i++) {
    qoff[i] = 0;
  }
  for (i = 0; i < q; i++) {
    qoff[qu[i] + 1]++;
    qoff[qv[i] + 1]++;
    answers[i] = LC_NONE;
  }
  for (i = 0; i < n; i++) {
    qoff[i + 1] += qoff[i];
  }
  for (i = 0; i < n; i++) {
    it[i] = qoff[i];
  }
  for (i = 0; i < q; i++) {
    qlist[it[qu[i]]++] = i;
    qlist[it[qv[i]]++] = i;
  }
  for (i = 0; i < n; i++) {
    uf[i] = i;
    anc[i] = i;
  }
  /* iterative DFS; a node is finished after all its children */
  for (i = 0; i < n; i++) {
    if (parent[i] != LC_NONE) {
      continue;
    }
    top = 0;
    stack[top++] = i;
    it[i] = off[i];
    while (top > 0) {
      v = stack[top - 1];
      if (it[v] < off[v + 1]) {
        w = kids[it[v]++];
        it[w] = off[w];
        stack[top++] = w;
        continue;
      }
      top--;
      /* v finished: answer queries whose other end is done */
      done[v] = 1;
      for (k = qoff[v]; k < qoff[v + 1]; k++) {
        size_t id = qlist[k];
        size_t other = qu[id] == v ? qv[id] : qu[id];
        if (done[other] && root[other] == root[v]) {
          answers[id] = anc[lc_find(uf, other)];
        }
      }
      if (top > 0) {
        size_t p = stack[top - 1];
        size_t rv = lc_find(uf, v);
        size_t rp = lc_find(uf, p);
        uf[rv] = rp;
        anc[rp] = p;
      }
    }
  }
  ds_context_free(context, off);
  ds_context_free(context, done);
  return DS_OK;
}
