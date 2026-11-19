#include <dsa/core/checked.h>
#include <dsa/structures/ab_tree.h>
#include <string.h>

/* ---- nodes ---- */

static size_t ab_round(size_t x) {
  return (x + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN * DS_MAX_ALIGN;
}

static int ab_cmp(const ds_ab_tree_t *t, const void *a, const void *b) {
  return t->compare(a, b, t->user);
}

static unsigned char *ab_item(const ds_ab_tree_t *t, const ds_ab_node_t *x,
                              size_t i) {
  return x->items + i * t->item_size;
}

static void ab_setup(const ds_ab_tree_t *t, ds_ab_node_t *x) {
  unsigned char *base = (unsigned char *)(void *)x;
  size_t off = ab_round(sizeof(ds_ab_node_t));
  x->child = (ds_ab_node_t **)(void *)(base + off);
  off += ab_round((t->b + 2) * sizeof(ds_ab_node_t *));
  x->items = base + off;
  off += ab_round((t->b + 1) * t->item_size);
  x->lo = base + off;
}

/* a node from the spare pool (which ab_reserve filled) */
static ds_ab_node_t *ab_take(ds_ab_tree_t *t, bool leaf) {
  ds_ab_node_t *x = t->spare;
  t->spare = x->parent;
  t->spares--;
  x->parent = NULL;
  x->prev = NULL;
  x->next = NULL;
  x->count = 0;
  x->size = 0;
  x->leaf = (unsigned char)leaf;
  return x;
}

static void ab_give(ds_ab_tree_t *t, ds_ab_node_t *x) {
  x->parent = t->spare;
  t->spare = x;
  t->spares++;
}

/* at least n spare nodes; nothing else changes */
static ds_status_t ab_reserve(ds_ab_tree_t *t, size_t n) {
  while (t->spares < n) {
    ds_ab_node_t *x =
        (ds_ab_node_t *)ds_context_alloc(t->context, t->node_bytes);
    if (x == NULL) {
      return DS_ERR_ALLOC;
    }
    ab_setup(t, x);
    ab_give(t, x);
  }
  return DS_OK;
}

/* keeps a few spare nodes, frees the rest */
static void ab_trim(ds_ab_tree_t *t) {
  size_t keep = 2 * t->height + 4;
  while (t->spares > keep) {
    ds_ab_node_t *x = t->spare;
    t->spare = x->parent;
    t->spares--;
    ds_context_free(t->context, x);
  }
}

ds_status_t ds_ab_init(ds_ab_tree_t *t, size_t a, size_t b, size_t item_size,
                       ds_ab_compare_fn compare, void *user,
                       ds_context_t *context) {
  size_t bytes;
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (a < 2 || b < 2 * a - 1 || item_size == 0) {
    return DS_ERR_INVALID;
  }
  if (b > ((size_t)-1 / 4) / (item_size + sizeof(void *)) - 4) {
    return DS_ERR_OVERFLOW;
  }
  bytes = ab_round(sizeof(ds_ab_node_t)) +
          ab_round((b + 2) * sizeof(ds_ab_node_t *)) +
          ab_round((b + 1) * item_size) + ab_round(item_size);
  t->scratch = (unsigned char *)ds_context_alloc(context, item_size);
  if (t->scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  t->a = a;
  t->b = b;
  t->item_size = item_size;
  t->compare = compare;
  t->user = user;
  t->context = context != NULL ? context : ds_default_context();
  t->node_bytes = bytes;
  return DS_OK;
}

/* every node below x into the spare pool, without recursion: the levels
 * are linked, so each level is a list */
static void ab_release_all(ds_ab_tree_t *t) {
  ds_ab_node_t *row = t->root;
  while (row != NULL) {
    ds_ab_node_t *below = row->leaf ? NULL : row->child[0];
    ds_ab_node_t *x = row;
    while (x->prev != NULL) { /* the start of the level */
      x = x->prev;
    }
    while (x != NULL) {
      ds_ab_node_t *n = x->next;
      ab_give(t, x);
      x = n;
    }
    row = below;
    while (row != NULL && row->prev != NULL) {
      row = row->prev;
    }
  }
  t->root = NULL;
  t->first = NULL;
  t->last = NULL;
  t->size = 0;
  t->height = 0;
}

void ds_ab_clear(ds_ab_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ab_release_all(t);
  ab_trim(t);
}

void ds_ab_destroy(ds_ab_tree_t *t) {
  if (t == NULL || t->scratch == NULL) {
    return;
  }
  ab_release_all(t);
  while (t->spare != NULL) {
    ds_ab_node_t *x = t->spare;
    t->spare = x->parent;
    ds_context_free(t->context, x);
  }
  ds_context_free(t->context, t->scratch);
  memset(t, 0, sizeof(*t));
}

/* ---- searching ---- */

/* the first index with item >= key; *eq if it is equal */
static size_t ab_leaf_lower(const ds_ab_tree_t *t, const ds_ab_node_t *x,
                            const void *key, bool *eq) {
  size_t lo = 0;
  size_t hi = x->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (ab_cmp(t, ab_item(t, x, mid), key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  *eq = lo < x->count && ab_cmp(t, ab_item(t, x, lo), key) == 0;
  return lo;
}

/* the child of an internal node whose range holds key: the number of
 * separators <= key */
static size_t ab_route(const ds_ab_tree_t *t, const ds_ab_node_t *x,
                       const void *key) {
  size_t lo = 0;
  size_t hi = x->count - 1;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (ab_cmp(t, ab_item(t, x, mid), key) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static ds_ab_node_t *ab_descend(const ds_ab_tree_t *t, ds_ab_node_t *x,
                                const void *key) {
  while (!x->leaf) {
    x = x->child[ab_route(t, x, key)];
  }
  return x;
}

static size_t ab_index(const ds_ab_node_t *p, const ds_ab_node_t *x) {
  size_t i = 0;
  while (p->child[i] != x) {
    i++;
  }
  return i;
}

static void ab_add_sizes(ds_ab_node_t *x, size_t add, size_t sub) {
  for (; x != NULL; x = x->parent) {
    x->size = x->size + add - sub;
  }
}

static size_t ab_child_sizes(const ds_ab_node_t *x) {
  size_t s = 0;
  size_t i;
  if (x->leaf) {
    return x->count;
  }
  for (i = 0; i < x->count; i++) {
    s += x->child[i]->size;
  }
  return s;
}

/* ---- restructuring ---- */

/* a tree in the making: split and join assemble pieces before they are
 * the tree's own */
typedef struct ab_part {
  ds_ab_node_t *root;
  size_t height;
  size_t size;
} ab_part_t;

/* x holds b + 1 entries: a right half moves to a new node after it; up
 * the tree while parents overflow. The pool holds the nodes needed. */
static void ab_split_up(ds_ab_tree_t *t, ab_part_t *part, ds_ab_node_t *x) {
  while (x->count > t->b) {
    size_t total = x->count;
    size_t left = total - total / 2;
    size_t right = total / 2;
    ds_ab_node_t *y = ab_take(t, x->leaf != 0);
    ds_ab_node_t *p = x->parent;
    const unsigned char *m;
    size_t i;
    if (x->leaf) {
      memcpy(y->items, ab_item(t, x, left), right * t->item_size);
      m = y->items; /* the separator: y's first item */
    } else {
      /* children left .. total-1, separators left .. total-2 */
      for (i = 0; i < right; i++) {
        y->child[i] = x->child[left + i];
        y->child[i]->parent = y;
      }
      memcpy(y->items, ab_item(t, x, left), (right - 1) * t->item_size);
      m = ab_item(t, x, left - 1); /* moves up */
    }
    y->count = right;
    x->count = left;
    memcpy(y->lo, m, t->item_size);
    y->prev = x;
    y->next = x->next;
    if (x->next != NULL) {
      x->next->prev = y;
    }
    x->next = y;
    if (x->leaf && t->last == x) {
      t->last = y;
    }
    y->size = ab_child_sizes(y);
    x->size -= y->size;
    t->splits++;
    if (p == NULL) {
      ds_ab_node_t *r = ab_take(t, false);
      r->child[0] = x;
      r->child[1] = y;
      memcpy(r->items, y->lo, t->item_size);
      r->count = 2;
      r->size = x->size + y->size;
      x->parent = r;
      y->parent = r;
      part->root = r;
      part->height++;
      return;
    }
    i = ab_index(p, x);
    memmove(&p->child[i + 2], &p->child[i + 1],
            (p->count - i - 1) * sizeof(ds_ab_node_t *));
    p->child[i + 1] = y;
    memmove(ab_item(t, p, i + 1), ab_item(t, p, i),
            (p->count - 1 - i) * t->item_size);
    memcpy(ab_item(t, p, i), y->lo, t->item_size);
    p->count++;
    y->parent = p;
    x = p;
  }
}

/* moves one entry from the left sibling u into v (separator k of p
 * between them) */
static void ab_shift_right(ds_ab_tree_t *t, ds_ab_node_t *p, size_t k) {
  ds_ab_node_t *u = p->child[k];
  ds_ab_node_t *v = p->child[k + 1];
  size_t is = t->item_size;
  if (v->leaf) {
    memmove(ab_item(t, v, 1), ab_item(t, v, 0), v->count * is);
    memcpy(ab_item(t, v, 0), ab_item(t, u, u->count - 1), is);
    u->count--;
    v->count++;
    memcpy(ab_item(t, p, k), ab_item(t, v, 0), is);
    memcpy(v->lo, ab_item(t, v, 0), is);
    u->size--;
    v->size++;
  } else {
    ds_ab_node_t *c = u->child[u->count - 1];
    memmove(&v->child[1], &v->child[0], v->count * sizeof(ds_ab_node_t *));
    v->child[0] = c;
    c->parent = v;
    memmove(ab_item(t, v, 1), ab_item(t, v, 0), (v->count - 1) * is);
    memcpy(ab_item(t, v, 0), ab_item(t, p, k), is);            /* comes down */
    memcpy(ab_item(t, p, k), ab_item(t, u, u->count - 2), is); /* goes up */
    memcpy(v->lo, ab_item(t, p, k), is);
    u->count--;
    v->count++;
    u->size -= c->size;
    v->size += c->size;
  }
}

/* moves one entry from the right sibling v into u */
static void ab_shift_left(ds_ab_tree_t *t, ds_ab_node_t *p, size_t k) {
  ds_ab_node_t *u = p->child[k];
  ds_ab_node_t *v = p->child[k + 1];
  size_t is = t->item_size;
  if (u->leaf) {
    memcpy(ab_item(t, u, u->count), ab_item(t, v, 0), is);
    memmove(ab_item(t, v, 0), ab_item(t, v, 1), (v->count - 1) * is);
    u->count++;
    v->count--;
    memcpy(ab_item(t, p, k), ab_item(t, v, 0), is);
    memcpy(v->lo, ab_item(t, v, 0), is);
    u->size++;
    v->size--;
  } else {
    ds_ab_node_t *c = v->child[0];
    u->child[u->count] = c;
    c->parent = u;
    memcpy(ab_item(t, u, u->count - 1), ab_item(t, p, k), is);
    memcpy(ab_item(t, p, k), ab_item(t, v, 0), is);
    memcpy(v->lo, ab_item(t, v, 0), is);
    memmove(&v->child[0], &v->child[1],
            (v->count - 1) * sizeof(ds_ab_node_t *));
    memmove(ab_item(t, v, 0), ab_item(t, v, 1), (v->count - 2) * is);
    u->count++;
    v->count--;
    u->size += c->size;
    v->size -= c->size;
  }
}

/* v merges into its left sibling u (separator k of p between them) */
static void ab_fuse(ds_ab_tree_t *t, ds_ab_node_t *p, size_t k) {
  ds_ab_node_t *u = p->child[k];
  ds_ab_node_t *v = p->child[k + 1];
  size_t is = t->item_size;
  size_t i;
  if (u->leaf) {
    memcpy(ab_item(t, u, u->count), ab_item(t, v, 0), v->count * is);
  } else {
    memcpy(ab_item(t, u, u->count - 1), ab_item(t, p, k), is);
    memcpy(ab_item(t, u, u->count), ab_item(t, v, 0), (v->count - 1) * is);
    for (i = 0; i < v->count; i++) {
      u->child[u->count + i] = v->child[i];
      v->child[i]->parent = u;
    }
  }
  u->count += v->count;
  u->size += v->size;
  u->next = v->next;
  if (v->next != NULL) {
    v->next->prev = u;
  }
  if (v->leaf && t->last == v) {
    t->last = u;
  }
  memmove(&p->child[k + 1], &p->child[k + 2],
          (p->count - k - 2) * sizeof(ds_ab_node_t *));
  memmove(ab_item(t, p, k), ab_item(t, p, k + 1), (p->count - k - 2) * is);
  p->count--;
  ab_give(t, v);
  t->fusions++;
}

/* x (not the root) may hold fewer than a entries: shares with or fuses
 * into a sibling, up the tree while parents underflow */
static void ab_fix_under(ds_ab_tree_t *t, ab_part_t *part, ds_ab_node_t *x) {
  while (x->parent != NULL && x->count < t->a) {
    ds_ab_node_t *p = x->parent;
    size_t j = ab_index(p, x);
    size_t k = j > 0 ? j - 1 : j; /* the pair (child k, child k + 1) */
    ds_ab_node_t *u = p->child[k];
    ds_ab_node_t *v = p->child[k + 1];
    /* the sibling shares while it has more than a; whatever x still lacks
     * then fits in one fusion: fewer than a plus a, at most 2a - 1 <= b */
    bool shared = false;
    while (x->count < t->a && (x == u ? v : u)->count > t->a) {
      if (x == u) {
        ab_shift_left(t, p, k);
      } else {
        ab_shift_right(t, p, k);
      }
      shared = true;
    }
    if (x->count >= t->a) {
      t->shares += shared;
      return;
    }
    {
      ab_fuse(t, p, k);
      if (p->parent == NULL && p->count == 1) { /* the root shrinks */
        part->root = u;
        u->parent = NULL;
        u->prev = NULL;
        u->next = NULL;
        ab_give(t, p);
        part->height--;
        return;
      }
      x = p;
    }
  }
}

/* ---- updates ---- */

static ds_status_t ab_insert(ds_ab_tree_t *t, const void *item, bool put) {
  ds_ab_node_t *x;
  ds_ab_node_t *v;
  size_t need = 0;
  size_t i;
  bool eq = false;
  ds_status_t st;
  if (t == NULL || item == NULL || t->scratch == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == NULL) {
    st = ab_reserve(t, 1);
    if (st != DS_OK) {
      return st;
    }
    x = ab_take(t, true);
    memcpy(x->items, item, t->item_size);
    x->count = 1;
    x->size = 1;
    t->root = x;
    t->first = x;
    t->last = x;
    t->height = 1;
    t->size = 1;
    return DS_OK;
  }
  x = ab_descend(t, t->root, item);
  i = ab_leaf_lower(t, x, item, &eq);
  if (eq) {
    if (put) {
      memcpy(ab_item(t, x, i), item, t->item_size);
    }
    return DS_EXISTS;
  }
  for (v = x; v != NULL && v->count == t->b; v = v->parent) {
    need++;
  }
  if (v == NULL) {
    need++; /* a new root */
  }
  st = ab_reserve(t, need);
  if (st != DS_OK) {
    return st;
  }
  memmove(ab_item(t, x, i + 1), ab_item(t, x, i),
          (x->count - i) * t->item_size);
  memcpy(ab_item(t, x, i), item, t->item_size);
  x->count++;
  t->size++;
  ab_add_sizes(x, 1, 0);
  {
    ab_part_t whole;
    whole.root = t->root;
    whole.height = t->height;
    ab_split_up(t, &whole, x);
    t->root = whole.root;
    t->height = whole.height;
  }
  return DS_OK;
}

ds_status_t ds_ab_insert(ds_ab_tree_t *t, const void *item) {
  return ab_insert(t, item, false);
}

ds_status_t ds_ab_put(ds_ab_tree_t *t, const void *item) {
  return ab_insert(t, item, true);
}

ds_status_t ds_ab_remove(ds_ab_tree_t *t, const void *key, void *out) {
  ds_ab_node_t *x;
  size_t i;
  bool eq = false;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == NULL) {
    return DS_NOT_FOUND;
  }
  x = ab_descend(t, t->root, key);
  i = ab_leaf_lower(t, x, key, &eq);
  if (!eq) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, ab_item(t, x, i), t->item_size);
  }
  memmove(ab_item(t, x, i), ab_item(t, x, i + 1),
          (x->count - i - 1) * t->item_size);
  x->count--;
  t->size--;
  ab_add_sizes(x, 0, 1);
  if (x == t->root) {
    if (x->count == 0) {
      ab_give(t, x);
      t->root = NULL;
      t->first = NULL;
      t->last = NULL;
      t->height = 0;
    }
  } else {
    ab_part_t whole;
    whole.root = t->root;
    whole.height = t->height;
    ab_fix_under(t, &whole, x);
    t->root = whole.root;
    t->height = whole.height;
  }
  ab_trim(t);
  return DS_OK;
}

/* ---- bulk loading ---- */

ds_status_t ds_ab_build(ds_ab_tree_t *t, const void *items, size_t n) {
  const unsigned char *src = (const unsigned char *)items;
  size_t per;
  size_t leaves;
  size_t nodes = 0;
  size_t level;
  size_t i;
  ds_ab_node_t *row = NULL;
  ds_status_t st;
  if (t == NULL || (n > 0 && items == NULL)) {
    return DS_ERR_NULL;
  }
  if (t->root != NULL) {
    return DS_ERR_STATE;
  }
  for (i = 1; i < n; i++) {
    if (ab_cmp(t, src + (i - 1) * t->item_size, src + i * t->item_size) >= 0) {
      return DS_ERR_INVALID;
    }
  }
  if (n == 0) {
    return DS_OK;
  }
  /* nodes about (a + b) / 2 full; at least 2a - 1 per node keeps every
   * node of ceil(n / per) near-equal ones at a or more */
  per = (t->a + t->b + 1) / 2;
  if (per < 2 * t->a - 1) {
    per = 2 * t->a - 1;
  }
  for (level = n; level > 1; level = (level + per - 1) / per) {
    nodes += (level + per - 1) / per;
  }
  st = ab_reserve(t, nodes + 2);
  if (st != DS_OK) {
    return st;
  }
  /* the leaves: n items in ceil(n / per) runs of near-equal length */
  leaves = (n + per - 1) / per;
  {
    ds_ab_node_t *prev = NULL;
    size_t at = 0;
    for (i = 0; i < leaves; i++) {
      size_t len = n / leaves + (i < n % leaves ? 1 : 0);
      ds_ab_node_t *x = ab_take(t, true);
      memcpy(x->items, src + at * t->item_size, len * t->item_size);
      x->count = len;
      x->size = len;
      memcpy(x->lo, x->items, t->item_size);
      x->prev = prev;
      if (prev != NULL) {
        prev->next = x;
      } else {
        row = x;
      }
      prev = x;
      at += len;
    }
    t->first = row;
    t->last = prev;
  }
  t->height = 1;
  /* level by level: groups of near-equal size */
  while (leaves > 1) {
    size_t groups = (leaves + per - 1) / per;
    ds_ab_node_t *prev = NULL;
    ds_ab_node_t *c = row;
    ds_ab_node_t *next_row = NULL;
    for (i = 0; i < groups; i++) {
      size_t len = leaves / groups + (i < leaves % groups ? 1 : 0);
      ds_ab_node_t *x = ab_take(t, false);
      size_t j;
      for (j = 0; j < len; j++) {
        x->child[j] = c;
        c->parent = x;
        if (j > 0) {
          memcpy(ab_item(t, x, j - 1), c->lo, t->item_size);
        }
        x->size += c->size;
        c = c->next;
      }
      x->count = len;
      memcpy(x->lo, x->child[0]->lo, t->item_size);
      x->prev = prev;
      if (prev != NULL) {
        prev->next = x;
      } else {
        next_row = x;
      }
      prev = x;
    }
    row = next_row;
    leaves = groups;
    t->height++;
  }
  t->root = row;
  t->size = n;
  return DS_OK;
}

/* ---- queries ---- */

void *ds_ab_find(const ds_ab_tree_t *t, const void *key) {
  ds_ab_node_t *x;
  size_t i;
  bool eq = false;
  if (t == NULL || key == NULL || t->root == NULL) {
    return NULL;
  }
  x = ab_descend(t, t->root, key);
  i = ab_leaf_lower(t, x, key, &eq);
  return eq ? ab_item(t, x, i) : NULL;
}

void *ds_ab_ceiling(const ds_ab_tree_t *t, const void *key) {
  ds_ab_cursor_t c;
  ds_ab_cursor_seek(t, key, &c);
  return ds_ab_cursor_item(&c);
}

void *ds_ab_floor(const ds_ab_tree_t *t, const void *key) {
  ds_ab_cursor_t c;
  void *at;
  ds_ab_cursor_seek(t, key, &c);
  at = ds_ab_cursor_item(&c);
  if (at != NULL && ab_cmp(t, at, key) == 0) {
    return at;
  }
  if (c.leaf == NULL) {
    ds_ab_cursor_last(t, &c);
    return ds_ab_cursor_item(&c);
  }
  return ds_ab_cursor_prev(&c) ? ds_ab_cursor_item(&c) : NULL;
}

void *ds_ab_min(const ds_ab_tree_t *t) {
  return t != NULL && t->first != NULL ? ab_item(t, t->first, 0) : NULL;
}

void *ds_ab_max(const ds_ab_tree_t *t) {
  return t != NULL && t->last != NULL ? ab_item(t, t->last, t->last->count - 1)
                                      : NULL;
}

size_t ds_ab_size(const ds_ab_tree_t *t) { return t != NULL ? t->size : 0; }

size_t ds_ab_height(const ds_ab_tree_t *t) { return t != NULL ? t->height : 0; }

ds_status_t ds_ab_rank(const ds_ab_tree_t *t, const void *key,
                       size_t *out_rank) {
  ds_ab_node_t *x;
  size_t r = 0;
  bool eq = false;
  if (t == NULL || key == NULL || out_rank == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  while (x != NULL && !x->leaf) {
    size_t j = ab_route(t, x, key);
    size_t i;
    for (i = 0; i < j; i++) {
      r += x->child[i]->size;
    }
    x = x->child[j];
  }
  if (x != NULL) {
    r += ab_leaf_lower(t, x, key, &eq);
  }
  *out_rank = r;
  return DS_OK;
}

void *ds_ab_select(const ds_ab_tree_t *t, size_t k) {
  ds_ab_node_t *x;
  if (t == NULL || k >= t->size) {
    return NULL;
  }
  x = t->root;
  while (!x->leaf) {
    size_t i = 0;
    while (k >= x->child[i]->size) {
      k -= x->child[i]->size;
      i++;
    }
    x = x->child[i];
  }
  return ab_item(t, x, k);
}

/* ---- cursors and fingers ---- */

void ds_ab_cursor_first(const ds_ab_tree_t *t, ds_ab_cursor_t *c) {
  c->leaf = t != NULL ? t->first : NULL;
  c->index = 0;
  c->item_size = t != NULL ? t->item_size : 0;
}

void ds_ab_cursor_last(const ds_ab_tree_t *t, ds_ab_cursor_t *c) {
  c->leaf = t != NULL ? t->last : NULL;
  c->index = c->leaf != NULL ? c->leaf->count - 1 : 0;
  c->item_size = t != NULL ? t->item_size : 0;
}

/* from a leaf position that may be past the leaf's end */
static void ab_settle(ds_ab_cursor_t *c) {
  if (c->leaf != NULL && c->index >= c->leaf->count) {
    c->leaf = c->leaf->next;
    c->index = 0;
  }
}

void ds_ab_cursor_seek(const ds_ab_tree_t *t, const void *key,
                       ds_ab_cursor_t *c) {
  bool eq = false;
  c->leaf = NULL;
  c->index = 0;
  c->item_size = t != NULL ? t->item_size : 0;
  if (t == NULL || key == NULL || t->root == NULL) {
    return;
  }
  c->leaf = ab_descend(t, t->root, key);
  c->index = ab_leaf_lower(t, c->leaf, key, &eq);
  ab_settle(c);
}

bool ds_ab_cursor_next(ds_ab_cursor_t *c) {
  if (c->leaf == NULL) {
    return false;
  }
  c->index++;
  ab_settle(c);
  return c->leaf != NULL;
}

bool ds_ab_cursor_prev(ds_ab_cursor_t *c) {
  if (c->leaf == NULL) {
    return false;
  }
  if (c->index > 0) {
    c->index--;
    return true;
  }
  if (c->leaf->prev == NULL) {
    return false;
  }
  c->leaf = c->leaf->prev;
  c->index = c->leaf->count - 1;
  return true;
}

void *ds_ab_cursor_item(const ds_ab_cursor_t *c) {
  return c != NULL && c->leaf != NULL ? c->leaf->items + c->index * c->item_size
                                      : NULL;
}

/* the key lies in x's range: from the separator before x up to the one
 * before its right neighbour */
static bool ab_in_range(const ds_ab_tree_t *t, const ds_ab_node_t *x,
                        const void *key) {
  return (x->prev == NULL || ab_cmp(t, x->lo, key) <= 0) &&
         (x->next == NULL || ab_cmp(t, key, x->next->lo) < 0);
}

void ds_ab_finger_seek(const ds_ab_tree_t *t, const void *key,
                       ds_ab_cursor_t *c, size_t *out_levels) {
  ds_ab_node_t *v;
  size_t levels = 0;
  bool eq = false;
  if (out_levels != NULL) {
    *out_levels = 0;
  }
  if (c == NULL) {
    return;
  }
  c->item_size = t != NULL ? t->item_size : 0;
  if (t == NULL || key == NULL || t->root == NULL) {
    c->leaf = NULL;
    c->index = 0;
    return;
  }
  v = c->leaf != NULL ? c->leaf : t->last;
  while (!ab_in_range(t, v, key)) {
    ds_ab_node_t *w =
        v->prev != NULL && ab_cmp(t, key, v->lo) < 0 ? v->prev : v->next;
    if (w != NULL && ab_in_range(t, w, key)) {
      v = w;
      break;
    }
    v = v->parent;
    levels++;
  }
  v = ab_descend(t, v, key);
  c->leaf = v;
  c->index = ab_leaf_lower(t, v, key, &eq);
  ab_settle(c);
  if (out_levels != NULL) {
    *out_levels = levels;
  }
}

int ds_ab_foreach(const ds_ab_tree_t *t, int (*visit)(void *item, void *user),
                  void *user) {
  ds_ab_node_t *x;
  if (t == NULL || visit == NULL) {
    return 0;
  }
  for (x = t->first; x != NULL; x = x->next) {
    size_t i;
    for (i = 0; i < x->count; i++) {
      int rc = visit(ab_item(t, x, i), user);
      if (rc != 0) {
        return rc;
      }
    }
  }
  return 0;
}

/* ---- join and split ---- */

static void ab_ends(ds_ab_tree_t *t) {
  ds_ab_node_t *x = t->root;
  t->first = NULL;
  t->last = NULL;
  if (x == NULL) {
    return;
  }
  while (!x->leaf) {
    x = x->child[0];
  }
  t->first = x;
  x = t->root;
  while (!x->leaf) {
    x = x->child[x->count - 1];
  }
  t->last = x;
}

/* L, then R (all of L < sep <= all of R), as one part. With link, the
 * levels the two share are linked here, the separators kept by R's first
 * nodes set to sep; split's pieces come linked already. Nodes come from
 * the pool. */
static void ab_join_parts(ds_ab_tree_t *t, const ab_part_t *left,
                          const ab_part_t *right, const void *sep, bool link,
                          ab_part_t *out) {
  ab_part_t L = *left;
  ab_part_t R = *right;
  ds_ab_node_t *v;
  size_t d;
  if (L.root == NULL) {
    *out = R;
    return;
  }
  if (R.root == NULL) {
    *out = L;
    return;
  }
  if (link) {
    ds_ab_node_t *ln = L.root;
    ds_ab_node_t *rn = R.root;
    size_t level = L.height < R.height ? L.height : R.height;
    for (d = L.height; d > level; d--) {
      ln = ln->child[ln->count - 1];
    }
    for (d = R.height; d > level; d--) {
      rn = rn->child[0];
    }
    for (;;) {
      ln->next = rn;
      rn->prev = ln;
      memcpy(rn->lo, sep, t->item_size);
      if (ln->leaf) {
        break;
      }
      ln = ln->child[ln->count - 1];
      rn = rn->child[0];
    }
  }
  if (L.height == R.height) {
    ab_part_t whole;
    ds_ab_node_t *r = ab_take(t, false);
    ds_ab_node_t *lr = L.root;
    ds_ab_node_t *rr = R.root;
    r->child[0] = lr;
    r->child[1] = rr;
    memcpy(r->items, sep, t->item_size);
    r->count = 2;
    r->size = L.size + R.size;
    lr->parent = r;
    rr->parent = r;
    whole.root = r;
    whole.height = L.height + 1;
    whole.size = L.size + R.size;
    if (lr->count < t->a) {
      ab_fix_under(t, &whole, lr);
    }
    if (whole.root == r && r->count == 2 && rr->count < t->a) {
      ab_fix_under(t, &whole, rr);
    }
    *out = whole;
    return;
  }
  if (L.height > R.height) {
    v = L.root;
    for (d = L.height; d > R.height + 1; d--) {
      v = v->child[v->count - 1];
    }
    v->child[v->count] = R.root;
    memcpy(ab_item(t, v, v->count - 1), sep, t->item_size);
    v->count++;
    R.root->parent = v;
    ab_add_sizes(v, R.size, 0);
    L.size += R.size;
    if (R.root->count < t->a) {
      ab_fix_under(t, &L, R.root);
    }
    ab_split_up(t, &L, v);
    *out = L;
    return;
  }
  v = R.root;
  for (d = R.height; d > L.height + 1; d--) {
    v = v->child[0];
  }
  memmove(&v->child[1], &v->child[0], v->count * sizeof(ds_ab_node_t *));
  memmove(ab_item(t, v, 1), ab_item(t, v, 0), (v->count - 1) * t->item_size);
  v->child[0] = L.root;
  memcpy(ab_item(t, v, 0), sep, t->item_size);
  v->count++;
  L.root->parent = v;
  ab_add_sizes(v, L.size, 0);
  R.size += L.size;
  if (L.root->count < t->a) {
    ab_fix_under(t, &R, L.root);
  }
  ab_split_up(t, &R, v);
  *out = R;
}

static bool ab_compatible(const ds_ab_tree_t *t, const ds_ab_tree_t *o) {
  return t->a == o->a && t->b == o->b && t->item_size == o->item_size &&
         t->compare == o->compare && t->user == o->user;
}

ds_status_t ds_ab_join(ds_ab_tree_t *t, ds_ab_tree_t *other) {
  ab_part_t L;
  ab_part_t R;
  ab_part_t J;
  ds_status_t st;
  if (t == NULL || other == NULL || t->scratch == NULL ||
      other->scratch == NULL) {
    return DS_ERR_NULL;
  }
  if (t == other || !ab_compatible(t, other)) {
    return DS_ERR_INVALID;
  }
  if (other->root == NULL) {
    return DS_OK;
  }
  if (t->root != NULL && ab_cmp(t, ds_ab_max(t), ds_ab_min(other)) >= 0) {
    return DS_ERR_INVALID;
  }
  st = ab_reserve(t,
                  (t->height > other->height ? t->height : other->height) + 2);
  if (st != DS_OK) {
    return st;
  }
  memcpy(t->scratch, ds_ab_min(other), t->item_size);
  L.root = t->root;
  L.height = t->height;
  L.size = t->size;
  R.root = other->root;
  R.height = other->height;
  R.size = other->size;
  ab_join_parts(t, &L, &R, t->scratch, true, &J);
  t->root = J.root;
  t->height = J.height;
  t->size = J.size;
  ab_ends(t);
  other->root = NULL;
  other->first = NULL;
  other->last = NULL;
  other->size = 0;
  other->height = 0;
  ab_trim(t);
  return DS_OK;
}

#define AB_MAX_HEIGHT 72

/* the level links and kept separators around one spine (the left one of
 * the upper tree a split leaves, the right one of the lower), from the
 * parents down: nodes made by the joins of a split sit on that spine and
 * were linked to nothing yet */
static void ab_relink(ds_ab_tree_t *t, bool left) {
  ds_ab_node_t *s = t->root;
  if (s == NULL) {
    return;
  }
  s->prev = NULL;
  s->next = NULL;
  while (!s->leaf) {
    size_t n = s->count;
    size_t i;
    for (i = 0; i < n; i++) {
      ds_ab_node_t *c = s->child[i];
      c->prev = i > 0 ? s->child[i - 1] : NULL;
      c->next = i + 1 < n ? s->child[i + 1] : NULL;
      if (i > 0) {
        memcpy(c->lo, ab_item(t, s, i - 1), t->item_size);
      }
    }
    if (left && s->next != NULL) { /* the first node of the next parent */
      ds_ab_node_t *x = s->next->child[0];
      s->child[n - 1]->next = x;
      x->prev = s->child[n - 1];
      memcpy(x->lo, s->next->lo, t->item_size);
    }
    if (!left && s->prev != NULL) { /* the last node of the previous one */
      ds_ab_node_t *x = s->prev->child[s->prev->count - 1];
      s->child[0]->prev = x;
      x->next = s->child[0];
      memcpy(s->child[0]->lo, s->lo, t->item_size);
    }
    s = left ? s->child[0] : s->child[n - 1];
  }
}

ds_status_t ds_ab_split(ds_ab_tree_t *t, const void *key, ds_ab_tree_t *upper) {
  ds_ab_node_t *path[AB_MAX_HEIGHT];
  size_t route[AB_MAX_HEIGHT];
  unsigned char *sepl;
  unsigned char *sepr;
  ab_part_t lacc;
  ab_part_t racc;
  ds_ab_node_t *x;
  size_t h;
  size_t level;
  size_t i;
  size_t c;
  bool eq = false;
  ds_status_t st;
  if (t == NULL || key == NULL || upper == NULL || t->scratch == NULL ||
      upper->scratch == NULL) {
    return DS_ERR_NULL;
  }
  if (t == upper || !ab_compatible(t, upper) || upper->root != NULL) {
    return DS_ERR_INVALID;
  }
  if (t->root == NULL) {
    return DS_OK;
  }
  if (t->height > AB_MAX_HEIGHT) {
    return DS_ERR_STATE;
  }
  st = ab_reserve(t, 8 * t->height + 8);
  if (st != DS_OK) {
    return st;
  }
  sepl = (unsigned char *)ds_context_alloc(t->context, 2 * t->item_size);
  if (sepl == NULL) {
    return DS_ERR_ALLOC;
  }
  sepr = sepl + t->item_size;
  /* the path: path[level] at level (1 = the leaves) */
  x = t->root;
  for (level = t->height; level > 1; level--) {
    path[level - 1] = x;
    route[level - 1] = ab_route(t, x, key);
    x = x->child[route[level - 1]];
  }
  path[0] = x;
  /* the leaf: items < key stay, the rest move to a new leaf */
  h = t->height;
  i = ab_leaf_lower(t, x, key, &eq);
  c = x->count;
  {
    ds_ab_node_t *prev = x->prev;
    ds_ab_node_t *next = x->next;
    ds_ab_node_t *w = NULL;
    if (i < c) {
      w = ab_take(t, true);
      memcpy(w->items, ab_item(t, x, i), (c - i) * t->item_size);
      w->count = c - i;
      w->size = c - i;
      w->next = next;
      if (next != NULL) {
        next->prev = w;
      }
    } else if (next != NULL) {
      next->prev = NULL;
    }
    x->count = i;
    x->size = i;
    x->next = NULL;
    lacc.root = NULL;
    lacc.height = 0;
    lacc.size = 0;
    if (i > 0) {
      lacc.root = x;
      lacc.height = 1;
      lacc.size = i;
    } else {
      if (prev != NULL) {
        prev->next = NULL;
      }
      ab_give(t, x);
    }
    racc.root = w;
    racc.height = w != NULL ? 1 : 0;
    racc.size = c - i;
    if (lacc.root != NULL) {
      lacc.root->parent = NULL;
    }
  }
  /* level by level upwards: the path node's children left of the path
   * join the left part, those right of it the right part */
  for (level = 2; level <= h; level++) {
    ds_ab_node_t *v = path[level - 1];
    size_t j = route[level - 1];
    size_t n = v->count;
    ds_ab_node_t *prev = v->prev;
    ds_ab_node_t *next = v->next;
    ab_part_t lp;
    ab_part_t rp;
    ds_ab_node_t *w = NULL;
    if (j > 0) {
      memcpy(sepl, ab_item(t, v, j - 1), t->item_size);
    }
    if (j + 1 < n) {
      memcpy(sepr, ab_item(t, v, j), t->item_size);
    }
    rp.root = NULL;
    rp.height = 0;
    rp.size = 0;
    if (j + 1 < n) {
      size_t k;
      w = ab_take(t, false);
      for (k = j + 1; k < n; k++) {
        w->child[k - j - 1] = v->child[k];
        v->child[k]->parent = w;
      }
      memcpy(w->items, ab_item(t, v, j + 1), (n - j - 2) * t->item_size);
      w->count = n - j - 1;
      w->size = ab_child_sizes(w);
      rp.size = w->size;
      if (w->count == 1) {
        rp.root = w->child[0];
        rp.height = level - 1;
        ab_give(t, w);
        w = NULL;
        if (next != NULL) {
          next->prev = NULL;
        }
      } else {
        rp.root = w;
        rp.height = level;
        w->next = next;
        if (next != NULL) {
          next->prev = w;
        }
      }
    } else if (next != NULL) {
      next->prev = NULL;
    }
    v->count = j;
    v->next = NULL;
    lp.root = NULL;
    lp.height = 0;
    lp.size = 0;
    if (j >= 2) {
      v->size = ab_child_sizes(v);
      lp.root = v;
      lp.height = level;
      lp.size = v->size;
    } else {
      if (j == 1) {
        lp.root = v->child[0];
        lp.height = level - 1;
        lp.size = lp.root->size;
      }
      if (prev != NULL) {
        prev->next = NULL;
      }
      ab_give(t, v);
    }
    if (lp.root != NULL) {
      lp.root->parent = NULL;
    }
    if (rp.root != NULL) {
      rp.root->parent = NULL;
    }
    ab_join_parts(t, &lp, &lacc, sepl, false, &lacc);
    ab_join_parts(t, &racc, &rp, sepr, false, &racc);
  }
  ds_context_free(t->context, sepl);
  t->root = lacc.root;
  t->height = lacc.height;
  t->size = lacc.size;
  upper->root = racc.root;
  upper->height = racc.height;
  upper->size = racc.size;
  ab_relink(t, false);
  ab_relink(upper, true);
  ab_ends(t);
  ab_ends(upper);
  ab_trim(t);
  return DS_OK;
}

/* ---- checking ---- */

ds_status_t ds_ab_check(const ds_ab_tree_t *t, bool *out_valid) {
  ds_ab_node_t *row;
  size_t level;
  size_t items = 0;
  bool ok = true;
  if (t == NULL || out_valid == NULL) {
    return DS_ERR_NULL;
  }
  *out_valid = false;
  if (t->root == NULL) {
    *out_valid =
        t->size == 0 && t->height == 0 && t->first == NULL && t->last == NULL;
    return DS_OK;
  }
  if (t->root->parent != NULL || t->root->prev != NULL ||
      t->root->next != NULL) {
    return DS_OK;
  }
  row = t->root;
  for (level = t->height; level >= 1 && ok; level--) {
    ds_ab_node_t *x;
    ds_ab_node_t *below = row->leaf ? NULL : row->child[0];
    if (row->prev != NULL || (level == 1) != (row->leaf != 0)) {
      return DS_OK;
    }
    if (level == 1 && row != t->first) {
      return DS_OK;
    }
    for (x = row; x != NULL && ok; x = x->next) {
      size_t i;
      bool root = x == t->root;
      if ((x->leaf != 0) != (level == 1)) {
        return DS_OK;
      }
      if (x->next != NULL && x->next->prev != x) {
        return DS_OK;
      }
      if (x->leaf) {
        if (x->count > t->b || x->count < (root ? 1 : t->a)) {
          return DS_OK;
        }
        for (i = 0; i < x->count; i++) {
          const unsigned char *it = ab_item(t, x, i);
          if (i > 0 && ab_cmp(t, ab_item(t, x, i - 1), it) >= 0) {
            return DS_OK;
          }
          if (x->prev != NULL && ab_cmp(t, x->lo, it) > 0) {
            return DS_OK;
          }
          if (x->next != NULL && ab_cmp(t, it, x->next->lo) >= 0) {
            return DS_OK;
          }
        }
        if (x->size != x->count) {
          return DS_OK;
        }
        items += x->count;
        if (x->next == NULL && x != t->last) {
          return DS_OK;
        }
      } else {
        size_t sum = 0;
        if (x->count > t->b || x->count < (root ? 2 : t->a)) {
          return DS_OK;
        }
        for (i = 0; i < x->count; i++) {
          ds_ab_node_t *ch = x->child[i];
          if (ch->parent != x) {
            return DS_OK;
          }
          if (i + 1 < x->count && ch->next != x->child[i + 1]) {
            return DS_OK;
          }
          if (i + 1 == x->count &&
              ch->next != (x->next != NULL ? x->next->child[0] : NULL)) {
            return DS_OK;
          }
          if (i == 0) {
            if ((ch->prev == NULL) != (x->prev == NULL)) {
              return DS_OK;
            }
            if (x->prev != NULL && ab_cmp(t, ch->lo, x->lo) != 0) {
              return DS_OK;
            }
          } else if (ab_cmp(t, ch->lo, ab_item(t, x, i - 1)) != 0) {
            return DS_OK;
          }
          if (i > 0 && i + 1 < x->count &&
              ab_cmp(t, ab_item(t, x, i - 1), ab_item(t, x, i)) >= 0) {
            return DS_OK;
          }
          sum += ch->size;
        }
        if (x->size != sum) {
          return DS_OK;
        }
      }
    }
    row = below;
  }
  ok = items == t->size && row == NULL;
  *out_valid = ok;
  return DS_OK;
}
