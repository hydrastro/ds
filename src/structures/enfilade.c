#include <dsa/core/stage.h>
#include <dsa/structures/enfilade.h>
#include <string.h>

#define EN_NONE (~(size_t)0)

static size_t en_cnt(const ds_enfilade_t *e, size_t v) {
  return v == EN_NONE ? 0 : e->cnt[v];
}

static size_t en_sum(const ds_enfilade_t *e, size_t v) {
  return v == EN_NONE ? 0 : e->sum[v];
}

static size_t en_add(size_t a, size_t b) {
  return a > ((size_t)-1) - b ? (size_t)-1 : a + b;
}

static void en_fix(ds_enfilade_t *e, size_t v) {
  e->cnt[v] = en_cnt(e, e->left[v]) + en_cnt(e, e->right[v]) + 1;
  e->sum[v] =
      en_add(en_add(en_sum(e, e->left[v]), e->wid[v]), en_sum(e, e->right[v]));
}

ds_status_t ds_enfilade_init(ds_enfilade_t *e, size_t elem_size,
                             ds_context_t *context) {
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(e, 0, sizeof(*e));
  e->root = EN_NONE;
  e->free_list = EN_NONE;
  e->elem_size = elem_size;
  e->seed = 1234567UL;
  e->context = context != NULL ? context : ds_default_context();
  return DS_OK;
}

void ds_enfilade_destroy(ds_enfilade_t *e) {
  if (e == NULL) {
    return;
  }
  ds_context_free(e->context, e->data);
  ds_context_free(e->context, e->wid);
  ds_context_free(e->context, e->sum);
  ds_context_free(e->context, e->cnt);
  ds_context_free(e->context, e->left);
  ds_context_free(e->context, e->right);
  ds_context_free(e->context, e->parent);
  ds_context_free(e->context, e->prio);
  ds_enfilade_init(e, e->elem_size, e->context);
}

size_t ds_enfilade_count(const ds_enfilade_t *e) {
  return e == NULL ? 0 : en_cnt(e, e->root);
}

size_t ds_enfilade_width(const ds_enfilade_t *e) {
  return e == NULL ? 0 : en_sum(e, e->root);
}

/* Split/join build spines and repair them through parent indexes. */
static void en_split(ds_enfilade_t *e, size_t v, size_t k, size_t *a,
                     size_t *b) {
  size_t left = EN_NONE, right = EN_NONE, *ls = a, *rs = b;
  while (v != EN_NONE) {
    size_t next, count = en_cnt(e, e->left[v]);
    if (k > count) {
      k -= count + 1;
      next = e->right[v];
      *ls = v;
      e->parent[v] = left;
      left = v;
      ls = &e->right[v];
    } else {
      next = e->left[v];
      *rs = v;
      e->parent[v] = right;
      right = v;
      rs = &e->left[v];
    }
    v = next;
  }
  *ls = *rs = EN_NONE;
  for (; left != EN_NONE; left = e->parent[left])
    en_fix(e, left);
  for (; right != EN_NONE; right = e->parent[right])
    en_fix(e, right);
}

static size_t en_join(ds_enfilade_t *e, size_t a, size_t b) {
  size_t root = EN_NONE, tail = EN_NONE, *slot = &root;
  while (a != EN_NONE && b != EN_NONE) {
    size_t chosen;
    if (e->prio[a] > e->prio[b]) {
      chosen = a;
      a = e->right[a];
      *slot = chosen;
      slot = &e->right[chosen];
    } else {
      chosen = b;
      b = e->left[b];
      *slot = chosen;
      slot = &e->left[chosen];
    }
    e->parent[chosen] = tail;
    tail = chosen;
  }
  *slot = a == EN_NONE ? b : a;
  if (*slot != EN_NONE)
    e->parent[*slot] = tail;
  for (; tail != EN_NONE; tail = e->parent[tail])
    en_fix(e, tail);
  return root;
}

static unsigned long en_rand(ds_enfilade_t *e) {
  unsigned long x = e->seed;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  e->seed = x & 0xFFFFFFFFUL;
  return e->seed;
}

static ds_status_t en_node(ds_enfilade_t *e, size_t *out) {
  size_t cap;
  void *p;
  if (e->free_list != EN_NONE) {
    *out = e->free_list;
    e->free_list = e->left[*out];
    return DS_OK;
  }
  if (e->slots == e->capacity) {
    cap = e->capacity < 8 ? 8 : e->capacity * 2;
    if (cap <= e->capacity || cap > ((size_t)-1) / e->elem_size ||
        cap > ((size_t)-1) / sizeof(size_t) ||
        cap > ((size_t)-1) / sizeof(unsigned long)) {
      return DS_ERR_OVERFLOW;
    }
#define EN_GROW(field, type)                                                   \
  p = ds_context_realloc(e->context, e->field, cap * sizeof(type));            \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  e->field = (type *)p;
    EN_GROW(wid, size_t)
    EN_GROW(sum, size_t)
    EN_GROW(cnt, size_t)
    EN_GROW(left, size_t)
    EN_GROW(right, size_t)
    EN_GROW(parent, size_t)
    EN_GROW(prio, unsigned long)
#undef EN_GROW
    p = ds_context_realloc(e->context, e->data, cap * e->elem_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    e->data = (unsigned char *)p;
    e->capacity = cap;
  }
  *out = e->slots++;
  return DS_OK;
}

ds_status_t ds_enfilade_insert(ds_enfilade_t *e, size_t index, const void *elem,
                               size_t wid) {
  size_t n;
  size_t a;
  size_t b;
  ds_status_t st;
  ds_stage_t staged;
  if (e == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (index > ds_enfilade_count(e)) {
    return DS_ERR_RANGE;
  }
  st = ds_stage_copy(&staged, e->context, true, elem, e->elem_size, NULL, 0);
  if (st != DS_OK)
    return st;
  st = en_node(e, &n);
  if (st != DS_OK) {
    ds_stage_release(&staged);
    return st;
  }
  memcpy(e->data + n * e->elem_size, staged.key, e->elem_size);
  ds_stage_release(&staged);
  e->wid[n] = wid;
  e->left[n] = e->right[n] = e->parent[n] = EN_NONE;
  e->prio[n] = en_rand(e);
  en_fix(e, n);
  en_split(e, e->root, index, &a, &b);
  e->root = en_join(e, en_join(e, a, n), b);
  return DS_OK;
}

ds_status_t ds_enfilade_remove(ds_enfilade_t *e, size_t index, void *out) {
  size_t a;
  size_t m;
  size_t b;
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= ds_enfilade_count(e)) {
    return DS_ERR_RANGE;
  }
  en_split(e, e->root, index, &a, &m);
  en_split(e, m, 1, &m, &b);
  if (out != NULL) {
    memmove(out, e->data + m * e->elem_size, e->elem_size);
  }
  e->left[m] = e->free_list;
  e->free_list = m;
  e->root = en_join(e, a, b);
  return DS_OK;
}

/* Node at index with its displacement. */
static size_t en_find(const ds_enfilade_t *e, size_t index, size_t *dsp) {
  size_t v = e->root;
  size_t d = 0;
  size_t lc;
  while (v != EN_NONE) {
    lc = en_cnt(e, e->left[v]);
    if (index < lc) {
      v = e->left[v];
    } else {
      d = en_add(d, en_sum(e, e->left[v]));
      if (index == lc) {
        break;
      }
      d = en_add(d, e->wid[v]);
      index -= lc + 1;
      v = e->right[v];
    }
  }
  *dsp = d;
  return v;
}

ds_status_t ds_enfilade_at(const ds_enfilade_t *e, size_t index,
                           void **out_elem, size_t *out_wid, size_t *out_dsp) {
  size_t v;
  size_t d;
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= ds_enfilade_count(e)) {
    return DS_ERR_RANGE;
  }
  v = en_find(e, index, &d);
  if (out_elem != NULL) {
    *out_elem = e->data + v * e->elem_size;
  }
  if (out_wid != NULL) {
    *out_wid = e->wid[v];
  }
  if (out_dsp != NULL) {
    *out_dsp = d;
  }
  return DS_OK;
}

ds_status_t ds_enfilade_retrieve(const ds_enfilade_t *e, size_t address,
                                 size_t *out_index, size_t *out_dsp) {
  size_t v;
  size_t d = 0;
  size_t idx = 0;
  size_t ls;
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  if (address >= ds_enfilade_width(e)) {
    return DS_NOT_FOUND;
  }
  v = e->root;
  while (v != EN_NONE) {
    ls = en_sum(e, e->left[v]);
    if (address < ls) {
      v = e->left[v];
    } else if (address - ls < e->wid[v]) {
      idx += en_cnt(e, e->left[v]);
      d += ls;
      break;
    } else {
      address -= ls + e->wid[v];
      idx += en_cnt(e, e->left[v]) + 1;
      d += ls + e->wid[v];
      v = e->right[v];
    }
  }
  if (v == EN_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_index != NULL) {
    *out_index = idx;
  }
  if (out_dsp != NULL) {
    *out_dsp = d;
  }
  return DS_OK;
}

ds_status_t ds_enfilade_set_width(ds_enfilade_t *e, size_t index, size_t wid) {
  size_t a;
  size_t m;
  size_t b;
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= ds_enfilade_count(e)) {
    return DS_ERR_RANGE;
  }
  en_split(e, e->root, index, &a, &m);
  en_split(e, m, 1, &m, &b);
  e->wid[m] = wid;
  en_fix(e, m);
  e->root = en_join(e, en_join(e, a, m), b);
  return DS_OK;
}

ds_status_t ds_enfilade_rearrange(ds_enfilade_t *e, size_t from, size_t to,
                                  size_t before) {
  size_t a;
  size_t m;
  size_t b;
  size_t x;
  size_t y;
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  if (from > to || to > ds_enfilade_count(e) || before > ds_enfilade_count(e) ||
      (before > from && before < to)) {
    return DS_ERR_RANGE;
  }
  en_split(e, e->root, from, &a, &m);
  en_split(e, m, to - from, &m, &b);
  if (before <= from) {
    en_split(e, a, before, &x, &y);
    e->root = en_join(e, en_join(e, x, m), en_join(e, y, b));
  } else {
    en_split(e, b, before - to, &x, &y);
    e->root = en_join(e, en_join(e, a, x), en_join(e, m, y));
  }
  return DS_OK;
}

void ds_enfilade_foreach(const ds_enfilade_t *e, ds_enfilade_visit_func_t f,
                         void *user) {
  size_t v, dsp = 0;
  if (e == NULL || f == NULL)
    return;
  v = e->root;
  if (v != EN_NONE)
    while (e->left[v] != EN_NONE)
      v = e->left[v];
  while (v != EN_NONE) {
    if (!f(e->data + v * e->elem_size, e->wid[v], dsp, user))
      return;
    dsp = en_add(dsp, e->wid[v]);
    if (e->right[v] != EN_NONE) {
      v = e->right[v];
      while (e->left[v] != EN_NONE)
        v = e->left[v];
    } else {
      while (e->parent[v] != EN_NONE && v == e->right[e->parent[v]])
        v = e->parent[v];
      v = e->parent[v];
    }
  }
}
