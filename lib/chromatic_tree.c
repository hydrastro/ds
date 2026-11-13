#include "chromatic_tree.h"
#include "checked.h"
#include <string.h>

#define CR_LEAF(n) ((n)->child[0] == NULL)
#define CR_PATH_START 128

typedef ds_chromatic_node_t cr_node_t;

static unsigned char *cr_elem(const ds_chromatic_tree_t *t, cr_node_t *n) {
  return (unsigned char *)n + (t->leaf_bytes - t->elem_size);
}

static int cr_cmp(const ds_chromatic_tree_t *t, const void *key, cr_node_t *n) {
  return t->compare(key, cr_elem(t, n), t->user);
}

/* the router key of internal node n */
static unsigned char *cr_key_of(const ds_chromatic_tree_t *t, cr_node_t *n) {
  return cr_elem(t, t->inline_routers ? n : n->router);
}

/* the child a search for key takes at internal node n */
static unsigned cr_dir(const ds_chromatic_tree_t *t, const void *key,
                       cr_node_t *n) {
  return t->compare(key, cr_key_of(t, n), t->user) >= 0 ? 1u : 0u;
}

static bool cr_reserve_path(ds_chromatic_tree_t *t, size_t need) {
  size_t cap;
  size_t bytes;
  cr_node_t **p;
  if (need <= t->path_capacity) {
    return true;
  }
  cap = t->path_capacity * 2;
  if (cap < need) {
    cap = need;
  }
  if (!ds_size_mul(cap, sizeof(cr_node_t *), &bytes)) {
    return false;
  }
  p = (cr_node_t **)ds_context_realloc(t->context, t->path, bytes);
  if (p == NULL) {
    return false;
  }
  t->path = p;
  t->path_capacity = cap;
  return true;
}

/* room for one more pending slot */
static bool cr_reserve_pending(ds_chromatic_tree_t *t) {
  size_t cap;
  size_t bytes;
  cr_node_t **p;
  if (t->pending_count < t->pending_capacity) {
    return true;
  }
  cap = t->pending_capacity == 0 ? 8 : 2 * t->pending_capacity;
  if (cap <= t->pending_capacity ||
      !ds_size_mul(cap, sizeof(cr_node_t *), &bytes)) {
    return false;
  }
  p = (cr_node_t **)ds_context_realloc(t->context, t->pending, bytes);
  if (p == NULL) {
    return false;
  }
  t->pending = p;
  t->pending_capacity = cap;
  return true;
}

/* leaf becomes pending (a slot was reserved) */
static void cr_mark(ds_chromatic_tree_t *t, cr_node_t *leaf) {
  if (leaf->pending == 0) {
    t->pending[t->pending_count++] = leaf;
    leaf->pending = t->pending_count;
    t->pending_live++;
  }
}

/* a pending leaf leaves the tree: its slot passes to heir */
static void cr_pass(ds_chromatic_tree_t *t, cr_node_t *leaf, cr_node_t *heir) {
  size_t slot;
  if (leaf->pending == 0) {
    return;
  }
  slot = leaf->pending - 1;
  leaf->pending = 0;
  if (heir->pending == 0) {
    t->pending[slot] = heir;
    heir->pending = slot + 1;
  } else {
    t->pending[slot] = NULL;
    t->pending_live--;
  }
}

ds_status_t ds_chromatic_init(ds_chromatic_tree_t *t, size_t elem_size,
                              ds_chromatic_compare_fn compare, void *user,
                              ds_context_t *context) {
  size_t head;
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_align(sizeof(cr_node_t), DS_MAX_ALIGN, &head) ||
      !ds_size_add(head, elem_size, &t->leaf_bytes)) {
    return DS_ERR_OVERFLOW;
  }
  t->elem_size = elem_size;
  t->compare = compare;
  t->user = user;
  t->context = context;
  /* eager mode never needs more: a red-black tree that deep has more
   * than 2^63 leaves */
  if (!cr_reserve_path(t, CR_PATH_START)) {
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_chromatic_destroy(ds_chromatic_tree_t *t) {
  cr_node_t *n;
  if (t == NULL) {
    return;
  }
  /* flatten: rotate left children up, free along the right spine */
  n = t->root;
  while (n != NULL) {
    cr_node_t *l = n->child[0];
    if (l == NULL) {
      cr_node_t *next = n->child[1];
      ds_context_free(t->context, n);
      n = next;
    } else if (CR_LEAF(l)) {
      ds_context_free(t->context, l);
      n->child[0] = NULL;
    } else {
      n->child[0] = l->child[1];
      l->child[1] = n;
      n = l;
    }
  }
  ds_context_free(t->context, t->path);
  ds_context_free(t->context, t->pending);
  t->root = NULL;
  t->path = NULL;
  t->pending = NULL;
  t->size = 0;
  t->pending_count = 0;
  t->pending_live = 0;
  t->pending_capacity = 0;
  t->path_capacity = 0;
}

ds_status_t ds_chromatic_set_inline_routers(ds_chromatic_tree_t *t, bool on) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root != NULL) {
    return DS_ERR_STATE;
  }
  t->inline_routers = on;
  return DS_OK;
}

size_t ds_chromatic_size(const ds_chromatic_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_chromatic_pending(const ds_chromatic_tree_t *t) {
  return t == NULL ? 0 : t->pending_live;
}

/* ---- transformations ------------------------------------------------------
 *
 * P[0] is the root and P[k] the node with the violation; P[i - 1] is the
 * parent of P[i]. A rotation's new subtree root takes the place of P[i]
 * through cr_link, which keeps the root black.
 */

static cr_node_t *cr_rotate(cr_node_t *a, unsigned i) {
  cr_node_t *b = a->child[i];
  a->child[i] = b->child[1u - i];
  b->child[1u - i] = a;
  a->size = a->child[0]->size + a->child[1]->size;
  b->size = b->child[0]->size + b->child[1]->size;
  return b;
}

static void cr_link(ds_chromatic_tree_t *t, cr_node_t **P, size_t i,
                    cr_node_t *top) {
  if (i == 0) {
    t->root = top;
    top->weight = 1;
  } else {
    cr_node_t *q = P[i - 1];
    q->child[q->child[0] == P[i] ? 0 : 1] = top;
  }
}

/* a red P[k] under a red parent, whose own parent is black */
static int cr_redred(ds_chromatic_tree_t *t, cr_node_t **P, size_t k) {
  cr_node_t *u = P[k];
  cr_node_t *v = P[k - 1];
  cr_node_t *x = P[k - 2];
  unsigned dv = x->child[1] == v ? 1u : 0u;
  unsigned du = v->child[1] == u ? 1u : 0u;
  size_t wx = x->weight;
  cr_node_t *y = x->child[1u - dv];
  cr_node_t *top;
  if (wx == 0) {
    return -2;
  }
  if (y->weight == 0) { /* blacking */
    v->weight = 1;
    y->weight = 1;
    x->weight = k - 2 == 0 ? 1 : wx - 1;
    t->transformations[DS_CHROMATIC_BLACKING]++;
  } else if (du == dv) { /* v rotates up */
    top = cr_rotate(x, dv);
    v->weight = wx;
    x->weight = 0;
    cr_link(t, P, k - 2, top);
    t->transformations[DS_CHROMATIC_ROTATE1]++;
  } else { /* u rotates up twice */
    x->child[dv] = cr_rotate(v, du);
    top = cr_rotate(x, dv);
    u->weight = wx;
    v->weight = 0;
    x->weight = 0;
    cr_link(t, P, k - 2, top);
    t->transformations[DS_CHROMATIC_ROTATE2]++;
  }
  return 1;
}

/* an overweight P[k] whose ancestors have no violation */
static int cr_overweight(ds_chromatic_tree_t *t, cr_node_t **P, size_t k) {
  cr_node_t *u = P[k];
  cr_node_t *v = P[k - 1];
  unsigned du = v->child[1] == u ? 1u : 0u;
  cr_node_t *s = v->child[1u - du];
  cr_node_t *top;
  size_t wv = v->weight;
  if (s->weight >= 2) { /* push */
    u->weight--;
    s->weight--;
    v->weight = k - 1 == 0 ? 1 : wv + 1;
    t->transformations[DS_CHROMATIC_PUSH]++;
    return 1;
  }
  if (CR_LEAF(s)) {
    return -2; /* the path weights rule a light leaf sibling out */
  }
  if (s->weight == 1) {
    cr_node_t *sn = s->child[du];
    cr_node_t *sf = s->child[1u - du];
    if (sf->weight == 0) { /* s rotates up; u, sf and v share one unit */
      top = cr_rotate(v, 1u - du);
      s->weight = wv;
      v->weight = 1;
      u->weight--;
      sf->weight = 1;
      cr_link(t, P, k - 1, top);
      t->transformations[DS_CHROMATIC_ABSORB1]++;
    } else if (sn->weight == 0) { /* sn rotates up twice */
      v->child[1u - du] = cr_rotate(s, du);
      top = cr_rotate(v, 1u - du);
      sn->weight = wv;
      v->weight = 1;
      s->weight = 1;
      u->weight--;
      cr_link(t, P, k - 1, top);
      t->transformations[DS_CHROMATIC_ABSORB2]++;
    } else { /* push: s turns red */
      u->weight--;
      s->weight = 0;
      v->weight = k - 1 == 0 ? 1 : wv + 1;
      t->transformations[DS_CHROMATIC_PUSH]++;
    }
    return 1;
  }
  /* s is red: first any red-red violation around it */
  if (wv == 0) {
    P[k] = s;
    return cr_redred(t, P, k);
  }
  if (s->child[0]->weight == 0 || s->child[1]->weight == 0) {
    P[k] = s;
    P[k + 1] = s->child[0]->weight == 0 ? s->child[0] : s->child[1];
    return cr_redred(t, P, k + 1);
  }
  top = cr_rotate(v, 1u - du); /* s rotates up, v turns red */
  s->weight = wv;
  v->weight = 0;
  cr_link(t, P, k - 1, top);
  t->transformations[DS_CHROMATIC_RED_SIB]++;
  return 1;
}

/* one transformation at the topmost violation on key's path: 1 done, 0
 * the path is clean, -1 the path cannot grow, -2 broken invariants */
static int cr_step(ds_chromatic_tree_t *t, const void *key) {
  cr_node_t **P;
  size_t i = 0;
  if (t->root == NULL) {
    return 0;
  }
  t->root->weight = 1;
  if (!cr_reserve_path(t, 4)) {
    return -1;
  }
  P = t->path;
  P[0] = t->root;
  for (;;) {
    cr_node_t *n = P[i];
    if (i > 0 && n->weight >= 2) {
      return cr_overweight(t, P, i);
    }
    if (i > 0 && n->weight == 0 && P[i - 1]->weight == 0) {
      return cr_redred(t, P, i);
    }
    if (CR_LEAF(n)) {
      return 0;
    }
    if (!cr_reserve_path(t, i + 4)) {
      return -1;
    }
    P = t->path;
    P[i + 1] = n->child[cr_dir(t, key, n)];
    i++;
  }
}

/* the leaf a search for key ends at */
static cr_node_t *cr_search(const ds_chromatic_tree_t *t, const void *key) {
  cr_node_t *n = t->root;
  while (!CR_LEAF(n)) {
    n = n->child[cr_dir(t, key, n)];
  }
  return n;
}

/* after an update whose violation lies on key's search path: rebalance
 * that path now, or record the leaf it ends at (a slot was reserved) */
static ds_status_t cr_after_update(ds_chromatic_tree_t *t, const void *key) {
  int r;
  if (t->deferred) {
    cr_mark(t, cr_search(t, key));
    return DS_OK;
  }
  do {
    r = cr_step(t, key);
  } while (r == 1);
  if (r == -1) { /* left for later: the tree stays a valid search tree */
    cr_mark(t, cr_search(t, key));
    return DS_OK;
  }
  return r == 0 ? DS_OK : DS_ERR_INTERNAL;
}

ds_status_t ds_chromatic_rebalance(ds_chromatic_tree_t *t, size_t max_steps,
                                   size_t *out_steps) {
  size_t steps = 0;
  ds_status_t st = DS_OK;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  while (t->pending_count > 0 && steps < max_steps) {
    cr_node_t *leaf = t->pending[t->pending_count - 1];
    int r;
    if (leaf == NULL) {
      t->pending_count--;
      continue;
    }
    r = cr_step(t, cr_elem(t, leaf));
    if (r == 1) {
      steps++;
    } else if (r == 0) {
      t->pending_count--;
      t->pending_live--;
      leaf->pending = 0;
    } else {
      st = r == -1 ? DS_ERR_ALLOC : DS_ERR_INTERNAL;
      break;
    }
  }
  while (t->pending_count > 0 && t->pending[t->pending_count - 1] == NULL) {
    t->pending_count--;
  }
  if (out_steps != NULL) {
    *out_steps = steps;
  }
  return st;
}

ds_status_t ds_chromatic_set_deferred(ds_chromatic_tree_t *t, bool deferred) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (deferred) {
    t->deferred = true;
    return DS_OK;
  }
  st = ds_chromatic_rebalance(t, (size_t)-1, NULL);
  if (st == DS_OK) {
    t->deferred = false;
  }
  return st;
}

/* ---- updates ------------------------------------------------------------ */

/* walk to the leaf for key, recording the path in t->path when it can
 * grow (*out_recorded); returns the leaf and its depth */
static cr_node_t *cr_walk(ds_chromatic_tree_t *t, const void *key,
                          size_t *out_depth, bool *out_recorded) {
  cr_node_t *n = t->root;
  size_t d = 0;
  bool rec = true;
  t->path[0] = n;
  while (!CR_LEAF(n)) {
    n = n->child[cr_dir(t, key, n)];
    d++;
    if (rec && cr_reserve_path(t, d + 1)) {
      t->path[d] = n;
    } else {
      rec = false;
    }
  }
  *out_depth = d;
  *out_recorded = rec;
  return n;
}

ds_status_t ds_chromatic_insert(ds_chromatic_tree_t *t, const void *elem) {
  cr_node_t *n = NULL;
  cr_node_t *parent = NULL;
  cr_node_t *leaf;
  cr_node_t *p;
  size_t depth = 0;
  size_t i;
  bool rec = false;
  int c = 0;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root != NULL) {
    n = cr_walk(t, elem, &depth, &rec);
    c = cr_cmp(t, elem, n);
    if (c == 0) {
      return DS_EXISTS;
    }
  }
  if (!cr_reserve_pending(t)) {
    return DS_ERR_ALLOC;
  }
  leaf = (cr_node_t *)ds_context_alloc(t->context, t->leaf_bytes);
  if (leaf == NULL) {
    return DS_ERR_ALLOC;
  }
  leaf->child[0] = NULL;
  leaf->child[1] = NULL;
  leaf->router = NULL;
  leaf->size = 1;
  leaf->weight = 1;
  leaf->pending = 0;
  memcpy(cr_elem(t, leaf), elem, t->elem_size);
  if (t->root == NULL) {
    t->root = leaf;
    t->size = 1;
    return DS_OK;
  }
  p = (cr_node_t *)ds_context_alloc(
      t->context, t->inline_routers ? t->leaf_bytes : sizeof(cr_node_t));
  if (p == NULL) {
    ds_context_free(t->context, leaf);
    return DS_ERR_ALLOC;
  }
  p->pending = 0;
  if (rec) { /* count the new leaf along the recorded path */
    for (i = 0; i < depth; i++) {
      t->path[i]->size++;
    }
    parent = depth > 0 ? t->path[depth - 1] : NULL;
  } else { /* or along a second walk */
    n = t->root;
    while (!CR_LEAF(n)) {
      n->size++;
      parent = n;
      n = n->child[cr_dir(t, elem, n)];
    }
  }
  p->weight = n->weight - 1;
  p->size = 2;
  n->weight = 1;
  if (c < 0) {
    p->child[0] = leaf;
    p->child[1] = n;
  } else {
    p->child[0] = n;
    p->child[1] = leaf;
  }
  if (t->inline_routers) {
    p->router = NULL;
    memcpy(cr_elem(t, p), cr_elem(t, p->child[1]), t->elem_size);
  } else {
    p->router = p->child[1];
  }
  if (parent == NULL) {
    t->root = p;
    p->weight = 1;
  } else {
    parent->child[parent->child[0] == n ? 0 : 1] = p;
  }
  t->size++;
  return cr_after_update(t, elem);
}

ds_status_t ds_chromatic_remove(ds_chromatic_tree_t *t, const void *key,
                                void *out) {
  cr_node_t *n;
  cr_node_t *g = NULL;
  cr_node_t *p = NULL;
  cr_node_t *rnode = NULL; /* the last node where the walk went right */
  cr_node_t *s;
  const unsigned char *ckey;
  size_t depth;
  size_t i;
  bool rec;
  unsigned d = 0;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == NULL) {
    return DS_NOT_FOUND;
  }
  n = cr_walk(t, key, &depth, &rec);
  if (cr_cmp(t, key, n) != 0) {
    return DS_NOT_FOUND;
  }
  if (n != t->root && !cr_reserve_pending(t)) {
    return DS_ERR_ALLOC;
  }
  if (out != NULL) {
    memcpy(out, cr_elem(t, n), t->elem_size);
  }
  if (n == t->root) {
    ds_context_free(t->context, n);
    t->root = NULL;
    t->size = 0;
    t->pending_count = 0; /* only n could have been pending */
    t->pending_live = 0;
    return DS_OK;
  }
  if (rec) {
    for (i = 0; i < depth; i++) {
      t->path[i]->size--;
      if (t->path[i]->child[1] == t->path[i + 1]) {
        rnode = t->path[i];
      }
    }
    p = t->path[depth - 1];
    g = depth >= 2 ? t->path[depth - 2] : NULL;
    d = p->child[1] == n ? 1u : 0u;
  } else {
    n = t->root;
    while (!CR_LEAF(n)) {
      n->size--;
      g = p;
      p = n;
      d = cr_dir(t, key, n);
      if (d == 1) {
        rnode = n;
      }
      n = n->child[d];
    }
  }
  s = p->child[1u - d];
  if (d == 0 && rnode != NULL) { /* rnode's router was n: now s's least */
    if (t->inline_routers) {
      memcpy(cr_elem(t, rnode), cr_elem(t, p), t->elem_size);
    } else {
      rnode->router = p->router;
    }
  }
  /* a key whose search path runs through s afterwards: the deleted key's
   * path turns away from s at rnode when rnode's router moved */
  ckey = d == 0 && rnode != NULL ? cr_key_of(t, rnode)
                                 : (const unsigned char *)key;
  s->weight += p->weight;
  if (g == NULL) {
    t->root = s;
    s->weight = 1;
  } else {
    g->child[g->child[0] == p ? 0 : 1] = s;
  }
  if (n->pending != 0) { /* its slot passes to a leaf on the same path */
    cr_pass(t, n, cr_search(t, ckey));
  }
  ds_context_free(t->context, n);
  ds_context_free(t->context, p);
  t->size--;
  return cr_after_update(t, ckey);
}

/* ---- queries ------------------------------------------------------------ */

void *ds_chromatic_get(const ds_chromatic_tree_t *t, const void *key) {
  cr_node_t *n;
  if (t == NULL || key == NULL || t->root == NULL) {
    return NULL;
  }
  n = t->root;
  while (!CR_LEAF(n)) {
    n = n->child[cr_dir(t, key, n)];
  }
  return cr_cmp(t, key, n) == 0 ? cr_elem(t, n) : NULL;
}

void *ds_chromatic_select(const ds_chromatic_tree_t *t, size_t k) {
  cr_node_t *n;
  if (t == NULL || k >= t->size) {
    return NULL;
  }
  n = t->root;
  while (!CR_LEAF(n)) {
    if (k < n->child[0]->size) {
      n = n->child[0];
    } else {
      k -= n->child[0]->size;
      n = n->child[1];
    }
  }
  return cr_elem(t, n);
}

/* elements smaller than key (strict) or not greater (!strict) */
static size_t cr_rank(const ds_chromatic_tree_t *t, const void *key,
                      bool strict) {
  cr_node_t *n;
  size_t r = 0;
  int c;
  if (t == NULL || key == NULL || t->root == NULL) {
    return 0;
  }
  n = t->root;
  while (!CR_LEAF(n)) {
    if (cr_dir(t, key, n) == 1) {
      r += n->child[0]->size;
      n = n->child[1];
    } else {
      n = n->child[0];
    }
  }
  c = cr_cmp(t, key, n);
  if (c > 0 || (c == 0 && !strict)) {
    r++;
  }
  return r;
}

size_t ds_chromatic_rank(const ds_chromatic_tree_t *t, const void *key) {
  return cr_rank(t, key, true);
}

void *ds_chromatic_ceiling(const ds_chromatic_tree_t *t, const void *key) {
  return ds_chromatic_select(t, cr_rank(t, key, true));
}

void *ds_chromatic_floor(const ds_chromatic_tree_t *t, const void *key) {
  size_t r = cr_rank(t, key, false);
  return r == 0 ? NULL : ds_chromatic_select(t, r - 1);
}

/* ---- walks ---------------------------------------------------------------
 */

typedef struct cr_frame {
  cr_node_t *node;
  size_t depth;
  size_t above; /* total weight of the proper ancestors */
} cr_frame_t;

static bool cr_frames(ds_context_t *ctx, cr_frame_t **stack, size_t *cap,
                      size_t need) {
  size_t c;
  size_t bytes;
  cr_frame_t *p;
  if (need <= *cap) {
    return true;
  }
  c = *cap * 2 < need ? need : *cap * 2;
  if (!ds_size_mul(c, sizeof(cr_frame_t), &bytes)) {
    return false;
  }
  p = (cr_frame_t *)ds_context_realloc(ctx, *stack, bytes);
  if (p == NULL) {
    return false;
  }
  *stack = p;
  *cap = c;
  return true;
}

ds_status_t ds_chromatic_foreach(const ds_chromatic_tree_t *t,
                                 bool (*visit)(void *elem, void *user),
                                 void *user) {
  cr_frame_t *stack = NULL;
  size_t cap = 0;
  size_t sp = 0;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == NULL) {
    return DS_OK;
  }
  if (!cr_frames(t->context, &stack, &cap, 64)) {
    return DS_ERR_ALLOC;
  }
  stack[sp++].node = t->root;
  while (sp > 0) {
    cr_node_t *n = stack[--sp].node;
    if (CR_LEAF(n)) {
      if (!visit(cr_elem(t, n), user)) {
        break;
      }
      continue;
    }
    if (!cr_frames(t->context, &stack, &cap, sp + 2)) {
      ds_context_free(t->context, stack);
      return DS_ERR_ALLOC;
    }
    stack[sp++].node = n->child[1];
    stack[sp++].node = n->child[0];
  }
  ds_context_free(t->context, stack);
  return DS_OK;
}

ds_status_t ds_chromatic_height(const ds_chromatic_tree_t *t,
                                size_t *out_height) {
  cr_frame_t *stack = NULL;
  size_t cap = 0;
  size_t sp = 0;
  size_t h = 0;
  if (t == NULL || out_height == NULL) {
    return DS_ERR_NULL;
  }
  *out_height = 0;
  if (t->root == NULL) {
    return DS_OK;
  }
  if (!cr_frames(t->context, &stack, &cap, 64)) {
    return DS_ERR_ALLOC;
  }
  stack[sp].node = t->root;
  stack[sp++].depth = 0;
  while (sp > 0) {
    cr_frame_t f = stack[--sp];
    if (f.depth > h) {
      h = f.depth;
    }
    if (CR_LEAF(f.node)) {
      continue;
    }
    if (!cr_frames(t->context, &stack, &cap, sp + 2)) {
      ds_context_free(t->context, stack);
      return DS_ERR_ALLOC;
    }
    stack[sp].node = f.node->child[1];
    stack[sp++].depth = f.depth + 1;
    stack[sp].node = f.node->child[0];
    stack[sp++].depth = f.depth + 1;
  }
  ds_context_free(t->context, stack);
  *out_height = h;
  return DS_OK;
}

ds_status_t ds_chromatic_check(const ds_chromatic_tree_t *t, bool *out_valid,
                               size_t *out_violations) {
  cr_frame_t *stack = NULL;
  size_t cap = 0;
  size_t sp = 0;
  size_t leaves = 0;
  size_t bad = 0;
  size_t total = 0;
  bool have_total = false;
  bool ok = true;
  cr_node_t *prev = NULL;
  if (t == NULL || out_valid == NULL) {
    return DS_ERR_NULL;
  }
  *out_valid = false;
  if (out_violations != NULL) {
    *out_violations = 0;
  }
  if (t->root == NULL) {
    *out_valid = t->size == 0;
    return DS_OK;
  }
  if (!cr_frames(t->context, &stack, &cap, 64)) {
    return DS_ERR_ALLOC;
  }
  stack[sp].node = t->root;
  stack[sp].depth = 0;
  stack[sp++].above = 0;
  while (sp > 0 && ok) {
    cr_frame_t f = stack[--sp];
    cr_node_t *n = f.node;
    size_t here = f.above + n->weight;
    if (CR_LEAF(n)) {
      if (n->child[1] != NULL || n->size != 1 || n->weight < 1) {
        ok = false;
        break;
      }
      if (have_total && here != total) {
        ok = false;
        break;
      }
      total = here;
      have_total = true;
      if (prev != NULL &&
          t->compare(cr_elem(t, prev), cr_elem(t, n), t->user) >= 0) {
        ok = false;
        break;
      }
      prev = n;
      leaves++;
    } else {
      cr_node_t *m;
      unsigned i;
      if (n->child[1] == NULL ||
          n->size != n->child[0]->size + n->child[1]->size) {
        ok = false;
        break;
      }
      m = n->child[1];
      while (!CR_LEAF(m)) {
        m = m->child[0];
      }
      if (t->inline_routers
              ? t->compare(cr_elem(t, n), cr_elem(t, m), t->user) != 0
              : n->router != m) {
        ok = false;
        break;
      }
      for (i = 0; i < 2; i++) {
        cr_node_t *c = n->child[i];
        if (c->weight >= 2 || (c->weight == 0 && n->weight == 0)) {
          bad++;
        }
      }
      if (!cr_frames(t->context, &stack, &cap, sp + 2)) {
        ds_context_free(t->context, stack);
        return DS_ERR_ALLOC;
      }
      stack[sp].node = n->child[1];
      stack[sp].depth = f.depth + 1;
      stack[sp++].above = here;
      stack[sp].node = n->child[0];
      stack[sp].depth = f.depth + 1;
      stack[sp++].above = here;
    }
  }
  ds_context_free(t->context, stack);
  if (ok && leaves != t->size) {
    ok = false;
  }
  *out_valid = ok;
  if (out_violations != NULL) {
    *out_violations = bad;
  }
  return DS_OK;
}
