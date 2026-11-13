#include "strict_fibonacci_heap.h"
#include "checked.h"
#include <string.h>

/*
 * Representation. Children form a ring (left, right) with child the
 * leftmost; Q is a ring (qprev, qnext) with queue its front. An active node
 * points to its heap's record while the record's flag is set; nodes that a
 * meld made passive still point to the dead record until they are next
 * looked at (sf_active drops the reference, the rank and any stale
 * filing). Filed nodes (active roots, nodes of loss one, nodes of loss two
 * or more) sit in lists through fprev and fnext; a node is unfiled before
 * its rank changes, so the list it leaves is the one of its rank.
 */

enum { SF_UNFILED = 0, SF_ROOT = 1, SF_LOSS1 = 2, SF_LOSS2 = 3 };
enum { SF_ACTIVE = 0, SF_HEAVY = 1, SF_LINKABLE = 2 };

struct ds_sf_record {
  int active;
  int owned; /* by a heap; otherwise freed with its last reference */
  size_t refs;
};

struct ds_sf_rank {
  ds_sf_rank_t *up;      /* rank + 1; the garbage list links through it too */
  ds_sf_rank_t *down;    /* rank - 1 */
  ds_sf_node_t *list[2]; /* active roots, nodes of loss one */
  size_t count[2];
  ds_sf_rank_t *next[2]; /* in the heap's pairs[i] while count[i] >= 2 */
  ds_sf_rank_t *prev[2];
  size_t value;
};

struct ds_sf_cell {
  ds_sf_node_t *node;
};

struct ds_sf_node {
  ds_sf_node_t *parent;
  ds_sf_node_t *left;
  ds_sf_node_t *right;
  ds_sf_node_t *child; /* leftmost */
  ds_sf_node_t *qprev;
  ds_sf_node_t *qnext;
  ds_sf_node_t *fprev;
  ds_sf_node_t *fnext;
  ds_sf_record_t *record;
  ds_sf_rank_t *rank; /* active nodes */
  ds_sf_cell_t *cell;
  size_t loss;
  size_t degree;
  unsigned char fix;
};

static size_t sf_item_at(void) {
  return (sizeof(ds_sf_cell_t) + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN *
         DS_MAX_ALIGN;
}

static unsigned char *sf_item(ds_sf_cell_t *c) {
  return (unsigned char *)(void *)c + sf_item_at();
}

static const unsigned char *sf_citem(const ds_sf_cell_t *c) {
  return (const unsigned char *)(const void *)c + sf_item_at();
}

/* the strict order: items, then cell addresses as bytes */
static bool sf_less(const ds_sf_heap_t *h, const ds_sf_node_t *a,
                    const ds_sf_node_t *b) {
  int c = h->compare(sf_citem(a->cell), sf_citem(b->cell), h->user);
  if (c != 0) {
    return c < 0;
  }
  return memcmp(&a->cell, &b->cell, sizeof(a->cell)) < 0;
}

/* ---- activity ------------------------------------------------------------ */

static void sf_record_drop(ds_sf_heap_t *h, ds_sf_record_t *r) {
  r->refs--;
  if (r->refs == 0 && !r->owned) {
    ds_context_free(h->context, r);
  }
}

static bool sf_active(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (x->record == NULL) {
    return false;
  }
  if (x->record->active) {
    return true;
  }
  sf_record_drop(h, x->record); /* made passive by a meld */
  x->record = NULL;
  x->rank = NULL;
  x->loss = 0;
  x->fix = SF_UNFILED;
  return false;
}

static bool sf_cactive(const ds_sf_node_t *x) {
  return x->record != NULL && x->record->active != 0;
}

static void sf_make_active(ds_sf_heap_t *h, ds_sf_node_t *x,
                           ds_sf_rank_t *rank) {
  x->record = h->record;
  h->record->refs++;
  x->rank = rank;
  x->loss = 0;
  x->fix = SF_UNFILED;
}

static void sf_make_passive(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (x->record != NULL) {
    sf_record_drop(h, x->record);
    x->record = NULL;
  }
  x->rank = NULL;
  x->loss = 0;
  x->fix = SF_UNFILED;
}

/* ---- filing -------------------------------------------------------------- */

static void sf_list_add(ds_sf_node_t **head, ds_sf_node_t *x) {
  x->fprev = NULL;
  x->fnext = *head;
  if (*head != NULL) {
    (*head)->fprev = x;
  }
  *head = x;
}

static void sf_list_remove(ds_sf_node_t **head, ds_sf_node_t *x) {
  if (x->fprev != NULL) {
    x->fprev->fnext = x->fnext;
  } else {
    *head = x->fnext;
  }
  if (x->fnext != NULL) {
    x->fnext->fprev = x->fprev;
  }
}

static void sf_pair_add(ds_sf_heap_t *h, ds_sf_rank_t *k, int i) {
  k->prev[i] = NULL;
  k->next[i] = h->pairs[i];
  if (h->pairs[i] != NULL) {
    h->pairs[i]->prev[i] = k;
  }
  h->pairs[i] = k;
}

static void sf_pair_remove(ds_sf_heap_t *h, ds_sf_rank_t *k, int i) {
  if (k->prev[i] != NULL) {
    k->prev[i]->next[i] = k->next[i];
  } else {
    h->pairs[i] = k->next[i];
  }
  if (k->next[i] != NULL) {
    k->next[i]->prev[i] = k->prev[i];
  }
}

static void sf_unfile(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (x->fix == SF_ROOT || x->fix == SF_LOSS1) {
    int i = x->fix == SF_ROOT ? 0 : 1;
    ds_sf_rank_t *k = x->rank;
    sf_list_remove(&k->list[i], x);
    k->count[i]--;
    if (k->count[i] == 1) {
      sf_pair_remove(h, k, i);
    }
  } else if (x->fix == SF_LOSS2) {
    sf_list_remove(&h->loss2, x);
  }
  x->fix = SF_UNFILED;
}

/* file an unfiled active node by its state */
static void sf_file(ds_sf_heap_t *h, ds_sf_node_t *x) {
  int i;
  ds_sf_rank_t *k = x->rank;
  if (!sf_active(h, x->parent)) {
    i = 0;
    x->fix = SF_ROOT;
  } else if (x->loss == 1) {
    i = 1;
    x->fix = SF_LOSS1;
  } else {
    if (x->loss >= 2) {
      sf_list_add(&h->loss2, x);
      x->fix = SF_LOSS2;
    }
    return;
  }
  sf_list_add(&k->list[i], x);
  k->count[i]++;
  if (k->count[i] == 2) {
    sf_pair_add(h, k, i);
  }
}

/* ---- the rank list ------------------------------------------------------- */

static size_t sf_ceil_lg(size_t n) {
  size_t l = 0;
  size_t p = 1;
  while (p < n) {
    l++;
    if (p > ((size_t)-1) / 2) {
      break;
    }
    p <<= 1;
  }
  return l;
}

/* the paper's R = 2 lg n + 6, with lg n rounded up */
static size_t sf_bound(size_t n) { return 2 * sf_ceil_lg(n) + 6; }

/* rank records for n items: ranks up to min(R, n - 1), and a spare */
static size_t sf_ranks_needed(size_t n) {
  size_t r = sf_bound(n);
  if (n == 0) {
    return 0;
  }
  if (n - 1 < r) {
    r = n - 1;
  }
  return r + 2;
}

static size_t sf_ranks(const ds_sf_heap_t *h) {
  return h->top == NULL ? 0 : h->top->value + 1;
}

static void sf_rank_clear(ds_sf_rank_t *k) {
  k->up = NULL;
  k->list[0] = NULL;
  k->list[1] = NULL;
  k->count[0] = 0;
  k->count[1] = 0;
  k->next[0] = NULL;
  k->next[1] = NULL;
  k->prev[0] = NULL;
  k->prev[1] = NULL;
}

/* lengthen the rank list for n items, from the garbage first */
static ds_status_t sf_grow(ds_sf_heap_t *h, size_t n) {
  size_t need = sf_ranks_needed(n);
  while (sf_ranks(h) < need) {
    ds_sf_rank_t *k = h->garbage;
    if (k != NULL) {
      h->garbage = k->up;
      if (h->garbage == NULL) {
        h->garbage_tail = NULL;
      }
    } else {
      k = (ds_sf_rank_t *)ds_context_alloc(h->context, sizeof(*k));
      if (k == NULL) {
        return DS_ERR_ALLOC;
      }
    }
    sf_rank_clear(k);
    k->down = h->top;
    if (h->top == NULL) {
      k->value = 0;
      h->rank0 = k;
    } else {
      k->value = h->top->value + 1;
      h->top->up = k;
    }
    h->top = k;
  }
  return DS_OK;
}

/* a list of rank records linked by up joins the garbage */
static void sf_discard(ds_sf_heap_t *h, ds_sf_rank_t *first,
                       ds_sf_rank_t *last) {
  if (first == NULL) {
    return;
  }
  last->up = NULL;
  if (h->garbage_tail == NULL) {
    h->garbage = first;
  } else {
    h->garbage_tail->up = first;
  }
  h->garbage_tail = last;
}

static void sf_collect(ds_sf_heap_t *h, size_t count) {
  while (count > 0 && h->garbage != NULL) {
    ds_sf_rank_t *k = h->garbage;
    h->garbage = k->up;
    ds_context_free(h->context, k);
    count--;
  }
  if (h->garbage == NULL) {
    h->garbage_tail = NULL;
  }
}

/* ---- the queue ----------------------------------------------------------- */

static void sf_q_front(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (h->queue == NULL) {
    x->qprev = x;
    x->qnext = x;
  } else {
    ds_sf_node_t *last = h->queue->qprev;
    x->qprev = last;
    x->qnext = h->queue;
    last->qnext = x;
    h->queue->qprev = x;
  }
  h->queue = x;
}

static void sf_q_remove(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (x->qnext == x) {
    h->queue = NULL;
  } else {
    x->qprev->qnext = x->qnext;
    x->qnext->qprev = x->qprev;
    if (h->queue == x) {
      h->queue = x->qnext;
    }
  }
  x->qprev = x;
  x->qnext = x;
}

/* the ring a followed by the ring b (either may be NULL) */
static ds_sf_node_t *sf_q_concat(ds_sf_node_t *a, ds_sf_node_t *b) {
  ds_sf_node_t *alast;
  ds_sf_node_t *blast;
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  alast = a->qprev;
  blast = b->qprev;
  alast->qnext = b;
  b->qprev = alast;
  blast->qnext = a;
  a->qprev = blast;
  return a;
}

/* ---- child lists --------------------------------------------------------- */

static int sf_kind(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (sf_active(h, x)) {
    return SF_ACTIVE;
  }
  return x->child != NULL && sf_active(h, x->child) ? SF_HEAVY : SF_LINKABLE;
}

static void sf_ring_after(ds_sf_node_t *a, ds_sf_node_t *x) {
  x->left = a;
  x->right = a->right;
  a->right->left = x;
  a->right = x;
}

/* x becomes p's leftmost (front) or rightmost child */
static void sf_put(ds_sf_node_t *p, ds_sf_node_t *x, bool front) {
  if (p->child == NULL) {
    x->left = x;
    x->right = x;
    p->child = x;
  } else {
    sf_ring_after(p->child->left, x);
    if (front) {
      p->child = x;
    }
  }
  x->parent = p;
  p->degree++;
}

static void sf_unlink(ds_sf_node_t *p, ds_sf_node_t *x) {
  if (x->right == x) {
    p->child = NULL;
  } else {
    x->left->right = x->right;
    x->right->left = x->left;
    if (p->child == x) {
      p->child = x->right;
    }
  }
  p->degree--;
  x->parent = NULL;
  x->left = x;
  x->right = x;
}

/* before x leaves the root's children: keep the boundary pointer */
static void sf_boundary_leave(ds_sf_heap_t *h, ds_sf_node_t *x) {
  ds_sf_node_t *first = h->root->child;
  if (x != h->boundary) {
    return;
  }
  if (sf_kind(h, x) != SF_ACTIVE && x->right != first &&
      sf_kind(h, x->right) == SF_HEAVY) {
    h->boundary = x->right; /* the next heavy child */
  } else if (x != first && sf_kind(h, x->left) == SF_ACTIVE) {
    h->boundary = x->left; /* the rightmost active child */
  } else {
    h->boundary = NULL;
  }
}

static void sf_cut(ds_sf_heap_t *h, ds_sf_node_t *x) {
  ds_sf_node_t *p = x->parent;
  if (p == h->root) {
    sf_boundary_leave(h, x);
  }
  sf_unlink(p, x);
}

/* a new child of the root: active first, then heavy, then linkable */
static void sf_root_add(ds_sf_heap_t *h, ds_sf_node_t *x) {
  ds_sf_node_t *root = h->root;
  ds_sf_node_t *b = h->boundary;
  switch (sf_kind(h, x)) {
  case SF_ACTIVE:
    sf_put(root, x, true);
    if (b == NULL) {
      h->boundary = x;
    }
    break;
  case SF_HEAVY:
    if (b == NULL) {
      sf_put(root, x, true);
    } else {
      if (sf_kind(h, b) == SF_ACTIVE) {
        sf_ring_after(b, x);
      } else {
        sf_ring_after(b->left, x);
        if (root->child == b) {
          root->child = x;
        }
      }
      x->parent = root;
      root->degree++;
    }
    h->boundary = x;
    break;
  default:
    sf_put(root, x, false);
    break;
  }
}

/* a passive child of the root that lost its last active child moves to
 * the linkable end */
static void sf_settle(ds_sf_heap_t *h, ds_sf_node_t *p) {
  if (p->parent == h->root && sf_kind(h, p) == SF_LINKABLE) {
    sf_cut(h, p);
    sf_root_add(h, p);
  }
}

/* ---- transformations ----------------------------------------------------- */

static bool sf_root_reduction(ds_sf_heap_t *h) {
  ds_sf_rank_t *k = h->pairs[0];
  ds_sf_node_t *x;
  ds_sf_node_t *y;
  ds_sf_node_t *p;
  ds_sf_node_t *z;
  if (k == NULL || k->up == NULL) {
    return false;
  }
  x = k->list[0];
  y = x->fnext;
  if (sf_less(h, y, x)) {
    z = x;
    x = y;
    y = z;
  }
  p = y->parent;
  sf_unfile(h, x);
  sf_unfile(h, y);
  h->active_roots--;
  sf_cut(h, y);
  sf_put(x, y, true);
  x->rank = x->rank->up;
  sf_file(h, x);
  if (p != h->root) {
    sf_settle(h, p);
  }
  z = x->child->left; /* x's rightmost child */
  if (!sf_active(h, z)) {
    sf_cut(h, z);
    sf_root_add(h, z);
  }
  return true;
}

static bool sf_degree_reduction(ds_sf_heap_t *h) {
  ds_sf_node_t *root = h->root;
  ds_sf_node_t *c[3];
  ds_sf_node_t *t;
  int i;
  if (root->degree < 3) {
    return false;
  }
  c[0] = root->child->left;
  c[1] = c[0]->left;
  c[2] = c[1]->left;
  for (i = 0; i < 3; i++) {
    if (sf_kind(h, c[i]) != SF_LINKABLE) {
      return false;
    }
  }
  if (sf_less(h, c[1], c[0])) {
    t = c[0];
    c[0] = c[1];
    c[1] = t;
  }
  if (sf_less(h, c[2], c[1])) {
    t = c[1];
    c[1] = c[2];
    c[2] = t;
  }
  if (sf_less(h, c[1], c[0])) {
    t = c[0];
    c[0] = c[1];
    c[1] = t;
  }
  for (i = 0; i < 3; i++) {
    sf_unlink(root, c[i]); /* linkable: never the boundary */
  }
  sf_make_active(h, c[0], h->rank0->up);
  sf_make_active(h, c[1], h->rank0);
  sf_put(c[1], c[2], false);
  sf_put(c[0], c[1], true);
  sf_root_add(h, c[0]);
  sf_file(h, c[0]);
  h->active_roots++;
  return true;
}

/* a one-node loss reduction if a node has loss two or more, else a
 * two-node loss reduction if two nodes of loss one share a rank */
static bool sf_loss_reduction(ds_sf_heap_t *h) {
  ds_sf_node_t *x = h->loss2;
  ds_sf_node_t *y;
  ds_sf_node_t *z;
  ds_sf_rank_t *k;
  if (x != NULL) {
    y = x->parent;
    sf_unfile(h, x);
    sf_unfile(h, y);
    h->total_loss -= x->loss;
    x->loss = 0;
    sf_cut(h, x);
    sf_root_add(h, x);
    sf_file(h, x);
    h->active_roots++;
    y->rank = y->rank->down;
    if (sf_active(h, y->parent)) {
      y->loss++;
      h->total_loss++;
    }
    sf_file(h, y);
    return true;
  }
  k = h->pairs[1];
  if (k == NULL || k->up == NULL) {
    return false;
  }
  x = k->list[1];
  y = x->fnext;
  if (sf_less(h, y, x)) {
    z = x;
    x = y;
    y = z;
  }
  z = y->parent;
  sf_unfile(h, x);
  sf_unfile(h, y);
  if (z != x) {
    sf_unfile(h, z);
  }
  h->total_loss -= 2;
  x->loss = 0;
  y->loss = 0;
  sf_cut(h, y);
  sf_put(x, y, true);
  x->rank = x->rank->up;
  z->rank = z->rank->down;
  if (sf_active(h, z->parent)) {
    z->loss++;
    h->total_loss++;
  }
  sf_file(h, x);
  if (z != x) {
    sf_file(h, z);
  }
  return true;
}

/* up to roots active root and degrees root degree reductions, an active
 * root reduction first whenever one applies */
static void sf_reduce(ds_sf_heap_t *h, size_t roots, size_t degrees) {
  for (;;) {
    if (roots > 0 && sf_root_reduction(h)) {
      roots--;
    } else if (degrees > 0 && sf_degree_reduction(h)) {
      degrees--;
    } else {
      break;
    }
  }
}

/* ---- the operations ------------------------------------------------------ */

static void sf_clear(ds_sf_heap_t *h) {
  h->root = NULL;
  h->queue = NULL;
  h->size = 0;
  h->record = NULL;
  h->boundary = NULL;
  h->active_roots = 0;
  h->total_loss = 0;
  h->rank0 = NULL;
  h->top = NULL;
  h->pairs[0] = NULL;
  h->pairs[1] = NULL;
  h->loss2 = NULL;
  h->garbage = NULL;
  h->garbage_tail = NULL;
}

/* the structure of src (not its settings) */
static void sf_take(ds_sf_heap_t *dst, const ds_sf_heap_t *src) {
  dst->root = src->root;
  dst->queue = src->queue;
  dst->size = src->size;
  dst->record = src->record;
  dst->boundary = src->boundary;
  dst->active_roots = src->active_roots;
  dst->total_loss = src->total_loss;
  dst->rank0 = src->rank0;
  dst->top = src->top;
  dst->pairs[0] = src->pairs[0];
  dst->pairs[1] = src->pairs[1];
  dst->loss2 = src->loss2;
  dst->garbage = src->garbage;
  dst->garbage_tail = src->garbage_tail;
}

ds_status_t ds_sf_heap_init(ds_sf_heap_t *h, size_t item_size,
                            ds_sf_compare_fn compare, void *user,
                            ds_context_t *context) {
  if (h == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  memset(h, 0, sizeof(*h));
  sf_clear(h);
  if (item_size == 0 || item_size > (size_t)-1 - sf_item_at()) {
    return DS_ERR_INVALID;
  }
  h->item_size = item_size;
  h->compare = compare;
  h->user = user;
  h->context = context;
  return DS_OK;
}

static void sf_free_node(ds_sf_heap_t *h, ds_sf_node_t *x) {
  if (x->record != NULL) {
    sf_record_drop(h, x->record);
  }
  ds_context_free(h->context, x->cell);
  ds_context_free(h->context, x);
}

void ds_sf_heap_destroy(ds_sf_heap_t *h) {
  if (h == NULL) {
    return;
  }
  while (h->queue != NULL) {
    ds_sf_node_t *x = h->queue;
    sf_q_remove(h, x);
    sf_free_node(h, x);
  }
  if (h->root != NULL) {
    sf_free_node(h, h->root);
  }
  if (h->record != NULL) {
    h->record->owned = 0;
    if (h->record->refs == 0) {
      ds_context_free(h->context, h->record);
    }
  }
  sf_discard(h, h->rank0, h->top);
  sf_collect(h, (size_t)-1);
  sf_clear(h);
}

/* a node with its cell joins the heap: the meld of a one-item heap */
static void sf_insert(ds_sf_heap_t *h, ds_sf_node_t *n) {
  n->parent = NULL;
  n->child = NULL;
  n->left = n;
  n->right = n;
  n->qprev = n;
  n->qnext = n;
  n->record = NULL;
  n->rank = NULL;
  n->loss = 0;
  n->degree = 0;
  n->fix = SF_UNFILED;
  h->size++;
  if (h->root == NULL) {
    h->root = n;
    h->boundary = NULL;
    return;
  }
  if (sf_less(h, n, h->root)) {
    ds_sf_node_t *old = h->root;
    h->root = n;
    h->boundary = NULL;
    sf_root_add(h, old);
    sf_q_front(h, old);
  } else {
    sf_root_add(h, n);
    sf_q_front(h, n);
  }
  sf_reduce(h, 1, 1);
}

ds_status_t ds_sf_heap_push(ds_sf_heap_t *h, const void *item,
                            ds_sf_cell_t **out_handle) {
  ds_sf_node_t *n;
  ds_sf_cell_t *c;
  if (h == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  if (h->record == NULL) {
    h->record =
        (ds_sf_record_t *)ds_context_alloc(h->context, sizeof(ds_sf_record_t));
    if (h->record == NULL) {
      return DS_ERR_ALLOC;
    }
    h->record->active = 1;
    h->record->owned = 1;
    h->record->refs = 0;
  }
  if (sf_grow(h, h->size + 1) != DS_OK) {
    return DS_ERR_ALLOC;
  }
  n = (ds_sf_node_t *)ds_context_alloc(h->context, sizeof(ds_sf_node_t));
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  c = (ds_sf_cell_t *)ds_context_alloc(h->context, sf_item_at() + h->item_size);
  if (c == NULL) {
    ds_context_free(h->context, n);
    return DS_ERR_ALLOC;
  }
  memcpy(sf_item(c), item, h->item_size);
  c->node = n;
  n->cell = c;
  sf_insert(h, n);
  sf_collect(h, 2);
  if (out_handle != NULL) {
    *out_handle = c;
  }
  return DS_OK;
}

const void *ds_sf_heap_top(const ds_sf_heap_t *h) {
  return h == NULL || h->root == NULL ? NULL : sf_citem(h->root->cell);
}

size_t ds_sf_heap_size(const ds_sf_heap_t *h) {
  return h == NULL ? 0 : h->size;
}

const void *ds_sf_heap_item(const ds_sf_heap_t *h, const ds_sf_cell_t *handle) {
  (void)h;
  return handle == NULL ? NULL : sf_citem(handle);
}

/* the root leaves the tree (delete-min without freeing it) */
static ds_sf_node_t *sf_take_root(ds_sf_heap_t *h) {
  ds_sf_node_t *z = h->root;
  ds_sf_node_t *x;
  ds_sf_node_t *c;
  ds_sf_node_t *next;
  size_t n;
  size_t k;
  int i;
  h->size--;
  if (z->child == NULL) {
    h->root = NULL;
    h->boundary = NULL;
    return z;
  }
  x = z->child; /* the least child */
  for (c = x->right; c != z->child; c = c->right) {
    if (sf_less(h, c, x)) {
      x = c;
    }
  }
  if (sf_active(h, x)) { /* x turns passive: its active children are roots */
    sf_unfile(h, x);
    h->active_roots--;
    sf_make_passive(h, x);
    c = x->child;
    if (c != NULL) {
      do {
        if (!sf_active(h, c)) {
          break; /* the active children come first */
        }
        sf_unfile(h, c);
        h->total_loss -= c->loss;
        c->loss = 0;
        sf_file(h, c);
        h->active_roots++;
        c = c->right;
      } while (c != x->child);
    }
  }
  /* x becomes the root and takes the old root's other children; the
   * children are laid out again: active, heavy, linkable */
  sf_unlink(z, x);
  h->root = x;
  h->boundary = NULL;
  c = x->child;
  n = x->degree;
  x->child = NULL;
  x->degree = 0;
  for (k = 0; k < n; k++) {
    next = c->right;
    sf_root_add(h, c);
    c = next;
  }
  while (z->child != NULL) {
    c = z->child;
    sf_unlink(z, c);
    sf_root_add(h, c);
  }
  sf_q_remove(h, x);
  /* twice: the front of Q goes to the back, giving its two last children
   * to the root if they are passive */
  for (i = 0; i < 2 && h->queue != NULL; i++) {
    ds_sf_node_t *w = h->queue;
    h->queue = w->qnext;
    for (k = 0; k < 2 && w->child != NULL; k++) {
      c = w->child->left;
      if (sf_active(h, c)) {
        break;
      }
      sf_cut(h, c);
      sf_root_add(h, c);
    }
  }
  (void)sf_loss_reduction(h);
  sf_reduce(h, (size_t)-1, (size_t)-1);
  return z;
}

ds_status_t ds_sf_heap_pop(ds_sf_heap_t *h, void *out_item) {
  ds_sf_node_t *z;
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (h->root == NULL) {
    return DS_EMPTY;
  }
  z = sf_take_root(h);
  if (out_item != NULL) {
    memcpy(out_item, sf_item(z->cell), h->item_size);
  }
  sf_free_node(h, z);
  sf_collect(h, 2);
  return DS_OK;
}

/* the item at x went down (or is to leave the heap: force) */
static void sf_decrease(ds_sf_heap_t *h, ds_sf_node_t *x, bool force) {
  ds_sf_node_t *root = h->root;
  ds_sf_node_t *y;
  if (x == root) {
    return;
  }
  if (force || sf_less(h, x, root)) { /* the items trade nodes */
    ds_sf_cell_t *t = x->cell;
    x->cell = root->cell;
    root->cell = t;
    x->cell->node = x;
    root->cell->node = root;
  }
  y = x->parent;
  if (y != root) {
    bool x_active = sf_active(h, x);
    bool y_active = sf_active(h, y);
    sf_cut(h, x);
    sf_root_add(h, x);
    if (x_active && y_active) { /* x becomes an active root */
      sf_unfile(h, x);
      sf_unfile(h, y);
      h->total_loss -= x->loss;
      x->loss = 0;
      sf_file(h, x);
      h->active_roots++;
      y->rank = y->rank->down;
      if (sf_active(h, y->parent)) {
        y->loss++;
        h->total_loss++;
      }
      sf_file(h, y);
    } else if (x_active) {
      sf_settle(h, y); /* y may have lost its last active child */
    }
  }
  (void)sf_loss_reduction(h);
  sf_reduce(h, 6, 4);
}

ds_status_t ds_sf_heap_update(ds_sf_heap_t *h, ds_sf_cell_t *handle,
                              const void *item) {
  int c;
  if (h == NULL || handle == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  c = h->compare(item, sf_citem(handle), h->user);
  memcpy(sf_item(handle), item, h->item_size);
  if (c < 0) {
    sf_decrease(h, handle->node, false);
  } else if (c > 0) { /* out and back in, reusing the node */
    ds_sf_node_t *z;
    sf_decrease(h, handle->node, true);
    z = sf_take_root(h);
    sf_insert(h, z);
    sf_collect(h, 2);
  }
  return DS_OK;
}

ds_status_t ds_sf_heap_remove(ds_sf_heap_t *h, ds_sf_cell_t *handle,
                              void *out_item) {
  ds_sf_node_t *z;
  if (h == NULL || handle == NULL) {
    return DS_ERR_NULL;
  }
  sf_decrease(h, handle->node, true);
  z = sf_take_root(h);
  if (out_item != NULL) {
    memcpy(out_item, sf_item(z->cell), h->item_size);
  }
  sf_free_node(h, z);
  sf_collect(h, 2);
  return DS_OK;
}

void ds_sf_heap_meld(ds_sf_heap_t *h, ds_sf_heap_t *other) {
  ds_sf_heap_t a;
  ds_sf_heap_t b;
  ds_sf_heap_t *small;
  ds_sf_heap_t *big;
  ds_sf_node_t *u;
  ds_sf_node_t *v;
  if (h == NULL || other == NULL || other == h || other->root == NULL) {
    return;
  }
  a = *h;
  b = *other;
  sf_clear(other);
  if (a.root == NULL) { /* h takes other's structure */
    sf_take(h, &b);
    sf_discard(h, a.rank0, a.top);
    sf_discard(h, a.garbage, a.garbage_tail);
    if (a.record != NULL) { /* no nodes: no references */
      ds_context_free(h->context, a.record);
    }
    sf_collect(h, 2);
    return;
  }
  small = a.size <= b.size ? &a : &b;
  big = small == &a ? &b : &a;
  small->record->active = 0; /* every node of the smaller heap turns passive */
  small->record->owned = 0;
  if (small->record->refs == 0) {
    ds_context_free(h->context, small->record);
  }
  sf_take(h, big);
  h->size = a.size + b.size;
  sf_discard(h, small->rank0, small->top);
  sf_discard(h, small->garbage, small->garbage_tail);
  if (sf_less(h, small->root, big->root)) {
    u = small->root;
    v = big->root;
    h->boundary = NULL; /* u's children are all linkable now */
  } else {
    u = big->root;
    v = small->root;
  }
  h->root = u;
  v->qprev = v;
  v->qnext = v;
  h->queue = sf_q_concat(sf_q_concat(small->queue, v), big->queue);
  sf_root_add(h, v);
  (void)sf_grow(h, h->size); /* from the garbage: never allocates */
  sf_reduce(h, 1, 1);
  sf_collect(h, 2);
}

/* ---- inspection ---------------------------------------------------------- */

static size_t sf_mix(const void *p) {
  unsigned char b[sizeof(const void *)];
  size_t v = 0;
  size_t i;
  memcpy(b, &p, sizeof(p));
  for (i = 0; i < sizeof(p); i++) {
    v = v * 257u + b[i];
  }
  v ^= v >> 13;
  v *= (size_t)0x5bd1e995ul;
  v ^= v >> 15;
  return v;
}

typedef struct sf_tally {
  size_t aroots;
  size_t loss;
  size_t hash;
} sf_tally_t;

static int sf_ckind(const ds_sf_node_t *x) {
  if (sf_cactive(x)) {
    return SF_ACTIVE;
  }
  return x->child != NULL && sf_cactive(x->child) ? SF_HEAVY : SF_LINKABLE;
}

static bool sf_check_node(const ds_sf_heap_t *h, const ds_sf_node_t *n,
                          sf_tally_t *t) {
  const ds_sf_node_t *c;
  size_t deg = 0;
  size_t act = 0;
  int section = SF_ACTIVE;
  if (n->cell == NULL || n->cell->node != n) {
    return false;
  }
  if (n->record == NULL && n->fix != SF_UNFILED) {
    return false;
  }
  c = n->child;
  if (c != NULL) {
    do {
      int kind = sf_ckind(c);
      if (deg >= h->size || c->parent != n || c->right->left != c ||
          c->left->right != c || !sf_less(h, n, c)) {
        return false; /* links, heap order */
      }
      if (kind == SF_ACTIVE) {
        if (section != SF_ACTIVE) {
          return false; /* active children first */
        }
        act++;
      } else if (n == h->root) {
        if (kind < section) {
          return false; /* the root: active, heavy, linkable */
        }
        section = kind;
      } else {
        section = SF_LINKABLE;
      }
      deg++;
      c = c->right;
    } while (c != n->child);
  }
  if (deg != n->degree ||
      (n != h->root && deg > 2 * sf_ceil_lg(h->size) + 12)) {
    return false;
  }
  if (sf_cactive(n)) {
    size_t j = 0;
    if (n->parent == NULL || n->rank == NULL || n->rank->value != act ||
        act > sf_bound(h->size)) {
      return false;
    }
    /* the i-th active child from the right has rank + loss >= i - 1 */
    for (c = n->child; j < act; c = c->right) {
      j++;
      if (c->rank->value + c->loss < act - j) {
        return false;
      }
    }
    if (!sf_cactive(n->parent)) {
      if (n->loss != 0 || n->fix != SF_ROOT) {
        return false;
      }
      t->aroots++;
    } else if (n->fix != (n->loss == 0   ? SF_UNFILED
                          : n->loss == 1 ? SF_LOSS1
                                         : SF_LOSS2)) {
      return false;
    }
    t->loss += n->loss;
  }
  return true;
}

static bool sf_check_ranks(const ds_sf_heap_t *h) {
  const ds_sf_rank_t *k;
  const ds_sf_node_t *f;
  size_t value = 0;
  size_t filed[2];
  size_t paired[2];
  int i;
  filed[0] = 0;
  filed[1] = 0;
  paired[0] = 0;
  paired[1] = 0;
  if ((h->rank0 == NULL) != (h->top == NULL) ||
      (h->rank0 != NULL && h->rank0->down != NULL) ||
      sf_ranks(h) < sf_ranks_needed(h->size)) {
    return false;
  }
  for (k = h->rank0; k != NULL; k = k->up) {
    if (k->value != value || (k->up == NULL) != (k == h->top) ||
        (k->up != NULL && k->up->down != k)) {
      return false;
    }
    for (i = 0; i < 2; i++) {
      size_t m = 0;
      for (f = k->list[i]; f != NULL; f = f->fnext) {
        if (!sf_cactive(f) || f->rank != k ||
            f->fix != (i == 0 ? SF_ROOT : SF_LOSS1) ||
            (f->fnext != NULL && f->fnext->fprev != f) || ++m > h->size) {
          return false;
        }
      }
      if (m != k->count[i]) {
        return false;
      }
      filed[i] += m;
      if (m >= 2) {
        paired[i]++;
      }
    }
    value++;
  }
  for (i = 0; i < 2; i++) {
    size_t m = 0;
    for (k = h->pairs[i]; k != NULL; k = k->next[i]) {
      if (k->count[i] < 2 || (k->next[i] != NULL && k->next[i]->prev[i] != k) ||
          ++m > value) {
        return false;
      }
    }
    if (m != paired[i]) {
      return false;
    }
  }
  if (filed[0] != h->active_roots) {
    return false;
  }
  for (f = h->loss2; f != NULL; f = f->fnext) {
    if (!sf_cactive(f) || f->loss < 2 || f->fix != SF_LOSS2 ||
        (f->fnext != NULL && f->fnext->fprev != f)) {
      return false;
    }
  }
  return true;
}

/* every node once by the tree links, every non-root node once in Q */
static bool sf_walk(const ds_sf_heap_t *h, sf_tally_t *t) {
  const ds_sf_node_t *n = h->root;
  const ds_sf_node_t *q;
  size_t visited = 0;
  size_t queued = 0;
  size_t qhash = 0;
  for (;;) {
    if (++visited > h->size || !sf_check_node(h, n, t)) {
      return false;
    }
    if (n != h->root) {
      t->hash += sf_mix(n);
    }
    if (n->child != NULL) {
      n = n->child;
      continue;
    }
    while (n != h->root && n->right == n->parent->child) {
      n = n->parent;
    }
    if (n == h->root) {
      break;
    }
    n = n->right;
  }
  if (visited != h->size) {
    return false;
  }
  q = h->queue;
  if (q != NULL) {
    do {
      if (++queued >= h->size || q == h->root || q->parent == NULL ||
          q->qnext->qprev != q) {
        return false;
      }
      qhash += sf_mix(q);
      q = q->qnext;
    } while (q != h->queue);
  }
  return queued + 1 == h->size && qhash == t->hash;
}

bool ds_sf_heap_check(const ds_sf_heap_t *h) {
  sf_tally_t t;
  size_t r;
  const ds_sf_node_t *c;
  const ds_sf_node_t *want = NULL;
  if (h == NULL) {
    return false;
  }
  if (h->root == NULL) {
    return h->size == 0 && h->queue == NULL && h->active_roots == 0 &&
           h->total_loss == 0 && h->loss2 == NULL && h->pairs[0] == NULL &&
           h->pairs[1] == NULL && h->boundary == NULL && sf_check_ranks(h);
  }
  if (sf_cactive(h->root) || h->root->parent != NULL || h->record == NULL ||
      !h->record->active) {
    return false;
  }
  t.aroots = 0;
  t.loss = 0;
  t.hash = 0;
  if (!sf_walk(h, &t) || t.aroots != h->active_roots ||
      t.loss != h->total_loss || !sf_check_ranks(h)) {
    return false;
  }
  r = sf_bound(h->size);
  if (h->active_roots > r + 1 || h->total_loss > r + 1 ||
      h->root->degree > r + 3) {
    return false;
  }
  c = h->root->child; /* the boundary pointer */
  if (c != NULL) {
    do {
      int kind = sf_ckind(c);
      if (kind == SF_ACTIVE) {
        want = c;
      } else if (kind == SF_HEAVY) {
        want = c;
        break;
      }
      c = c->right;
    } while (c != h->root->child);
  }
  return want == h->boundary;
}

void ds_sf_heap_measures(const ds_sf_heap_t *h, size_t *out_active_roots,
                         size_t *out_total_loss, size_t *out_root_degree,
                         size_t *out_max_degree) {
  size_t maxd = 0;
  const ds_sf_node_t *x;
  if (out_active_roots != NULL) {
    *out_active_roots = h->active_roots;
  }
  if (out_total_loss != NULL) {
    *out_total_loss = h->total_loss;
  }
  if (out_root_degree != NULL) {
    *out_root_degree = h->root != NULL ? h->root->degree : 0;
  }
  x = h->queue;
  if (x != NULL) {
    do {
      if (x->degree > maxd) {
        maxd = x->degree;
      }
      x = x->qnext;
    } while (x != h->queue);
  }
  if (out_max_degree != NULL) {
    *out_max_degree = maxd;
  }
}
