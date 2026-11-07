#include "p_tree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PT_NONE (~(size_t)0)
#define PT_E(t) ((t)->max_entries + 1)

static double *pt_slab(const ds_p_tree_t *t, size_t v, size_t i) {
  return t->slab + (v * PT_E(t) + i) * 2 * t->k;
}

static double *pt_box(const ds_p_tree_t *t, size_t v, size_t i) {
  return t->box + (v * PT_E(t) + i) * 2 * t->dim;
}

static size_t *pt_ref(const ds_p_tree_t *t, size_t v, size_t i) {
  return t->ref + v * PT_E(t) + i;
}

/* the slabs of a box: the range of u_j.x over its corners */
static void pt_box_slab(const ds_p_tree_t *t, const double *box, double *s) {
  size_t j;
  size_t a;
  size_t d = t->dim;
  for (j = 0; j < t->k; j++) {
    const double *u = t->dirs + j * d;
    double lo = 0.0;
    double hi = 0.0;
    for (a = 0; a < d; a++) {
      double p = u[a] * box[a];
      double q = u[a] * box[d + a];
      lo += p < q ? p : q;
      hi += p < q ? q : p;
    }
    s[j] = lo;
    s[t->k + j] = hi;
  }
}

static void pt_union(size_t k, const double *x, const double *y, double *out) {
  size_t j;
  for (j = 0; j < k; j++) {
    out[j] = x[j] < y[j] ? x[j] : y[j];
    out[k + j] = x[k + j] > y[k + j] ? x[k + j] : y[k + j];
  }
}

static double pt_size(size_t k, const double *s) {
  size_t j;
  double w = 0.0;
  for (j = 0; j < k; j++) {
    w += s[k + j] - s[j];
  }
  return w;
}

static double pt_enlarge(size_t k, const double *x, const double *y) {
  double u[2 * DS_P_TREE_MAX_DIRS];
  pt_union(k, x, y, u);
  return pt_size(k, u) - pt_size(k, x);
}

static bool pt_slab_hit(size_t k, const double *x, const double *y) {
  size_t j;
  for (j = 0; j < k; j++) {
    if (x[k + j] < y[j] || y[k + j] < x[j]) {
      return false;
    }
  }
  return true;
}

static bool pt_slab_in(size_t k, const double *outer, const double *inner) {
  size_t j;
  for (j = 0; j < k; j++) {
    if (inner[j] < outer[j] || inner[k + j] > outer[k + j]) {
      return false;
    }
  }
  return true;
}

static ds_status_t pt_node(ds_p_tree_t *t, bool leaf, size_t *out) {
  size_t v;
  if (t->free_list != PT_NONE) {
    v = t->free_list;
    t->free_list = t->count[v];
  } else {
    if (t->nodes == t->capacity) {
      size_t nc = t->capacity < 8 ? 8 : t->capacity * 2;
      void *p;
#define PT_RE(field, type, per)                                                \
  p = ds_context_realloc(t->context, t->field, nc * (per) * sizeof(type));     \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      PT_RE(count, size_t, 1)
      PT_RE(leaf, unsigned char, 1)
      PT_RE(parent, size_t, 1)
      PT_RE(slab, double, PT_E(t) * 2 * t->k)
      PT_RE(box, double, PT_E(t) * 2 * t->dim)
      PT_RE(ref, size_t, PT_E(t))
#undef PT_RE
      t->capacity = nc;
    }
    v = t->nodes++;
  }
  t->count[v] = 0;
  t->leaf[v] = (unsigned char)leaf;
  t->parent[v] = PT_NONE;
  *out = v;
  return DS_OK;
}

static void pt_free(ds_p_tree_t *t, size_t v) {
  t->count[v] = t->free_list;
  t->free_list = v;
}

ds_status_t ds_p_tree_init(ds_p_tree_t *t, size_t dim, const double *dirs,
                           size_t k, size_t max_entries,
                           ds_context_t *context) {
  size_t a;
  size_t b;
  size_t j;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (max_entries == 0) {
    max_entries = 8;
  }
  if (dirs == NULL) {
    k = dim * dim;
  }
  if (dim == 0 || dim > DS_P_TREE_MAX_DIM || k == 0 || k > DS_P_TREE_MAX_DIRS ||
      max_entries < 4 || max_entries > 1024) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->k = k;
  t->max_entries = max_entries;
  t->min_entries = max_entries * 2 / 5 > 2 ? max_entries * 2 / 5 : 2;
  t->free_list = PT_NONE;
  t->context = context;
  t->dirs = (double *)ds_context_alloc(context, k * dim * sizeof(double));
  if (t->dirs == NULL) {
    return DS_ERR_ALLOC;
  }
  if (dirs != NULL) {
    for (j = 0; j < k * dim; j++) {
      if (!(dirs[j] > -HUGE_VAL && dirs[j] < HUGE_VAL)) {
        ds_p_tree_destroy(t);
        return DS_ERR_INVALID;
      }
    }
    memcpy(t->dirs, dirs, k * dim * sizeof(double));
  } else {
    memset(t->dirs, 0, k * dim * sizeof(double));
    j = 0;
    for (a = 0; a < dim; a++) {
      t->dirs[j++ * dim + a] = 1.0;
    }
    for (a = 0; a < dim; a++) {
      for (b = a + 1; b < dim; b++) {
        t->dirs[j * dim + a] = 1.0;
        t->dirs[j++ * dim + b] = 1.0;
        t->dirs[j * dim + a] = 1.0;
        t->dirs[j++ * dim + b] = -1.0;
      }
    }
  }
  st = pt_node(t, true, &t->root);
  if (st != DS_OK) {
    ds_p_tree_destroy(t);
    return st;
  }
  t->height = 1;
  return DS_OK;
}

void ds_p_tree_destroy(ds_p_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->dirs);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->slab);
  ds_context_free(t->context, t->box);
  ds_context_free(t->context, t->ref);
  memset(t, 0, sizeof(*t));
}

size_t ds_p_tree_size(const ds_p_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_p_tree_height(const ds_p_tree_t *t) {
  return t == NULL ? 0 : t->height;
}

size_t ds_p_tree_directions(const ds_p_tree_t *t) {
  return t == NULL ? 0 : t->k;
}

static void pt_cover(const ds_p_tree_t *t, size_t v, double *out) {
  size_t i;
  memcpy(out, pt_slab(t, v, 0), 2 * t->k * sizeof(double));
  for (i = 1; i < t->count[v]; i++) {
    pt_union(t->k, out, pt_slab(t, v, i), out);
  }
}

static size_t pt_slot(const ds_p_tree_t *t, size_t v) {
  size_t p = t->parent[v];
  size_t i;
  for (i = 0; i < t->count[p]; i++) {
    if (*pt_ref(t, p, i) == v) {
      return i;
    }
  }
  return PT_NONE;
}

static void pt_adjust(ds_p_tree_t *t, size_t v) {
  while (t->parent[v] != PT_NONE) {
    pt_cover(t, v, pt_slab(t, t->parent[v], pt_slot(t, v)));
    v = t->parent[v];
  }
}

static void pt_copy_entry(ds_p_tree_t *t, size_t dv, size_t di, size_t sv,
                          size_t si) {
  memmove(pt_slab(t, dv, di), pt_slab(t, sv, si), 2 * t->k * sizeof(double));
  memmove(pt_box(t, dv, di), pt_box(t, sv, si), 2 * t->dim * sizeof(double));
  *pt_ref(t, dv, di) = *pt_ref(t, sv, si);
}

/* Quadratic split of v (max + 1 entries) into v and a new node. */
static ds_status_t pt_split(ds_p_tree_t *t, size_t v, size_t *out_w) {
  size_t k = t->k;
  size_t n = t->count[v];
  size_t i;
  size_t j;
  size_t s1 = 0;
  size_t s2 = 1;
  size_t w;
  size_t n1 = 1;
  size_t n2 = 1;
  double worst = -HUGE_VAL;
  double c1[2 * DS_P_TREE_MAX_DIRS];
  double c2[2 * DS_P_TREE_MAX_DIRS];
  unsigned char group[1025];
  size_t left;
  ds_status_t st = pt_node(t, t->leaf[v] != 0, &w);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < n; i++) {
    for (j = i + 1; j < n; j++) {
      double u[2 * DS_P_TREE_MAX_DIRS];
      double waste;
      pt_union(k, pt_slab(t, v, i), pt_slab(t, v, j), u);
      waste = pt_size(k, u) - pt_size(k, pt_slab(t, v, i)) -
              pt_size(k, pt_slab(t, v, j));
      if (waste > worst) {
        worst = waste;
        s1 = i;
        s2 = j;
      }
    }
  }
  memset(group, 0, n);
  group[s1] = 1;
  group[s2] = 2;
  memcpy(c1, pt_slab(t, v, s1), 2 * k * sizeof(double));
  memcpy(c2, pt_slab(t, v, s2), 2 * k * sizeof(double));
  for (left = n - 2; left > 0; left--) {
    size_t pick = 0;
    double pd = -1.0;
    double e1 = 0.0;
    double e2 = 0.0;
    if (n1 + left == t->min_entries || n2 + left == t->min_entries) {
      unsigned char g = n1 + left == t->min_entries ? 1 : 2;
      for (i = 0; i < n; i++) {
        if (group[i] == 0) {
          group[i] = g;
        }
      }
      break;
    }
    for (i = 0; i < n; i++) {
      double a1;
      double a2;
      if (group[i] != 0) {
        continue;
      }
      a1 = pt_enlarge(k, c1, pt_slab(t, v, i));
      a2 = pt_enlarge(k, c2, pt_slab(t, v, i));
      if (fabs(a1 - a2) > pd) {
        pd = fabs(a1 - a2);
        pick = i;
        e1 = a1;
        e2 = a2;
      }
    }
    if (e1 < e2 || (e1 <= e2 && n1 <= n2)) {
      group[pick] = 1;
      pt_union(k, c1, pt_slab(t, v, pick), c1);
      n1++;
    } else {
      group[pick] = 2;
      pt_union(k, c2, pt_slab(t, v, pick), c2);
      n2++;
    }
  }
  /* move group 2 to w, compact group 1 in v */
  {
    size_t a = 0;
    size_t b = 0;
    for (i = 0; i < n; i++) {
      if (group[i] == 2) {
        pt_copy_entry(t, w, b++, v, i);
        if (!t->leaf[v]) {
          t->parent[*pt_ref(t, w, b - 1)] = w;
        }
      }
    }
    for (i = 0; i < n; i++) {
      if (group[i] == 1) {
        if (a != i) {
          pt_copy_entry(t, v, a, v, i);
        }
        a++;
      }
    }
    t->count[v] = a;
    t->count[w] = b;
  }
  *out_w = w;
  return DS_OK;
}

/* Insert an entry (an object at level 0, a subtree above). */
static ds_status_t pt_insert_at(ds_p_tree_t *t, const double *slab,
                                const double *box, size_t ref, size_t level) {
  size_t v = t->root;
  size_t depth = t->height - 1;
  size_t i;
  ds_status_t st;
  while (depth > level) {
    size_t best = 0;
    double be = HUGE_VAL;
    double bs = HUGE_VAL;
    for (i = 0; i < t->count[v]; i++) {
      double e = pt_enlarge(t->k, pt_slab(t, v, i), slab);
      double s = pt_size(t->k, pt_slab(t, v, i));
      if (e < be || (e <= be && s < bs)) {
        be = e;
        bs = s;
        best = i;
      }
    }
    v = *pt_ref(t, v, best);
    depth--;
  }
  i = t->count[v]++;
  memcpy(pt_slab(t, v, i), slab, 2 * t->k * sizeof(double));
  if (box != NULL) {
    memcpy(pt_box(t, v, i), box, 2 * t->dim * sizeof(double));
  }
  *pt_ref(t, v, i) = ref;
  if (!t->leaf[v]) {
    t->parent[ref] = v;
  }
  pt_adjust(t, v);
  while (t->count[v] > t->max_entries) {
    size_t w;
    st = pt_split(t, v, &w);
    if (st != DS_OK) {
      return st;
    }
    if (v == t->root) {
      size_t r;
      st = pt_node(t, false, &r);
      if (st != DS_OK) {
        return st;
      }
      pt_cover(t, v, pt_slab(t, r, 0));
      *pt_ref(t, r, 0) = v;
      pt_cover(t, w, pt_slab(t, r, 1));
      *pt_ref(t, r, 1) = w;
      t->count[r] = 2;
      t->parent[v] = t->parent[w] = r;
      t->root = r;
      t->height++;
      return DS_OK;
    }
    {
      size_t p = t->parent[v];
      pt_cover(t, v, pt_slab(t, p, pt_slot(t, v)));
      i = t->count[p]++;
      pt_cover(t, w, pt_slab(t, p, i));
      *pt_ref(t, p, i) = w;
      t->parent[w] = p;
      pt_adjust(t, p);
      v = p;
    }
  }
  return DS_OK;
}

ds_status_t ds_p_tree_insert(ds_p_tree_t *t, const double *box, size_t value) {
  double s[2 * DS_P_TREE_MAX_DIRS];
  size_t a;
  ds_status_t st;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  for (a = 0; a < t->dim; a++) {
    if (!(box[a] > -HUGE_VAL && box[t->dim + a] < HUGE_VAL &&
          box[a] <= box[t->dim + a])) {
      return DS_ERR_INVALID;
    }
  }
  pt_box_slab(t, box, s);
  st = pt_insert_at(t, s, box, value, 0);
  if (st == DS_OK) {
    t->size++;
  }
  return st;
}

ds_status_t ds_p_tree_remove(ds_p_tree_t *t, const double *box, size_t value) {
  double s[2 * DS_P_TREE_MAX_DIRS];
  size_t *stack;
  size_t top = 0;
  size_t leaf = PT_NONE;
  size_t slot = 0;
  size_t v;
  size_t i;
  double *obox = NULL;
  size_t *oval = NULL;
  size_t on = 0;
  size_t ocap = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  pt_box_slab(t, box, s);
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = t->root;
  while (top > 0 && leaf == PT_NONE) {
    v = stack[--top];
    for (i = 0; i < t->count[v]; i++) {
      if (t->leaf[v]) {
        if (*pt_ref(t, v, i) == value &&
            memcmp(pt_box(t, v, i), box, 2 * t->dim * sizeof(double)) == 0) {
          leaf = v;
          slot = i;
          break;
        }
      } else if (pt_slab_in(t->k, pt_slab(t, v, i), s)) {
        stack[top++] = *pt_ref(t, v, i);
      }
    }
  }
  if (leaf == PT_NONE) {
    ds_context_free(t->context, stack);
    return DS_NOT_FOUND;
  }
  v = leaf;
  t->count[v]--;
  if (slot != t->count[v]) {
    pt_copy_entry(t, v, slot, v, t->count[v]);
  }
  t->size--;
  /* underfull nodes leave; their objects are inserted again */
  while (v != t->root) {
    size_t p = t->parent[v];
    if (t->count[v] < t->min_entries) {
      size_t ps = pt_slot(t, v);
      top = 0;
      stack[top++] = v;
      while (top > 0 && st == DS_OK) {
        size_t u = stack[--top];
        for (i = 0; i < t->count[u]; i++) {
          if (!t->leaf[u]) {
            stack[top++] = *pt_ref(t, u, i);
            continue;
          }
          if (on == ocap) {
            size_t nc = ocap < 8 ? 16 : ocap * 2;
            void *q = ds_context_realloc(t->context, obox,
                                         nc * 2 * t->dim * sizeof(double));
            if (q == NULL) {
              st = DS_ERR_ALLOC;
              break;
            }
            obox = (double *)q;
            q = ds_context_realloc(t->context, oval, nc * sizeof(size_t));
            if (q == NULL) {
              st = DS_ERR_ALLOC;
              break;
            }
            oval = (size_t *)q;
            ocap = nc;
          }
          memcpy(obox + on * 2 * t->dim, pt_box(t, u, i),
                 2 * t->dim * sizeof(double));
          oval[on++] = *pt_ref(t, u, i);
        }
        pt_free(t, u);
      }
      t->count[p]--;
      if (ps != t->count[p]) {
        pt_copy_entry(t, p, ps, p, t->count[p]);
      }
    } else {
      pt_cover(t, v, pt_slab(t, p, pt_slot(t, v)));
    }
    v = p;
  }
  ds_context_free(t->context, stack);
  while (!t->leaf[t->root] && t->count[t->root] == 1) {
    size_t old = t->root;
    t->root = *pt_ref(t, old, 0);
    t->parent[t->root] = PT_NONE;
    pt_free(t, old);
    t->height--;
  }
  if (!t->leaf[t->root] && t->count[t->root] == 0) {
    t->leaf[t->root] = 1;
    t->height = 1;
  }
  for (i = 0; i < on && st == DS_OK; i++) {
    double s2[2 * DS_P_TREE_MAX_DIRS];
    pt_box_slab(t, obox + i * 2 * t->dim, s2);
    st = pt_insert_at(t, s2, obox + i * 2 * t->dim, oval[i], 0);
  }
  ds_context_free(t->context, obox);
  ds_context_free(t->context, oval);
  return st;
}

ds_status_t ds_p_tree_query(const ds_p_tree_t *t, const double *box,
                            ds_p_tree_visit_func_t visit, void *user) {
  double s[2 * DS_P_TREE_MAX_DIRS];
  size_t *stack;
  size_t top = 0;
  size_t d;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  pt_box_slab(t, box, s);
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = t->root;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    size_t i;
    for (i = 0; i < t->count[v]; i++) {
      if (!pt_slab_hit(t->k, pt_slab(t, v, i), s)) {
        continue;
      }
      if (!t->leaf[v]) {
        stack[top++] = *pt_ref(t, v, i);
      } else {
        const double *b = pt_box(t, v, i);
        size_t a;
        for (a = 0; a < d && !(b[d + a] < box[a]) && !(box[d + a] < b[a]);
             a++) {
        }
        if (a == d && !visit(b, *pt_ref(t, v, i), user)) {
          st = DS_STOP;
          break;
        }
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

static bool pt_check(const ds_p_tree_t *t, size_t v, size_t level,
                     size_t *count) {
  size_t i;
  if (t->count[v] > t->max_entries ||
      (v != t->root && t->count[v] < t->min_entries) ||
      (t->leaf[v] != 0) != (level == 0)) {
    return false;
  }
  if (t->leaf[v]) {
    for (i = 0; i < t->count[v]; i++) {
      double s[2 * DS_P_TREE_MAX_DIRS];
      pt_box_slab(t, pt_box(t, v, i), s);
      if (memcmp(s, pt_slab(t, v, i), 2 * t->k * sizeof(double)) != 0) {
        return false;
      }
    }
    *count += t->count[v];
    return true;
  }
  for (i = 0; i < t->count[v]; i++) {
    size_t c = *pt_ref(t, v, i);
    double s[2 * DS_P_TREE_MAX_DIRS];
    if (t->parent[c] != v || t->count[c] == 0) {
      return false;
    }
    pt_cover(t, c, s);
    if (memcmp(s, pt_slab(t, v, i), 2 * t->k * sizeof(double)) != 0 ||
        !pt_check(t, c, level - 1, count)) {
      return false;
    }
  }
  return true;
}

bool ds_p_tree_valid(const ds_p_tree_t *t) {
  size_t count = 0;
  if (t == NULL || t->count == NULL) {
    return false;
  }
  return pt_check(t, t->root, t->height - 1, &count) && count == t->size;
}
