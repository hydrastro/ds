#include "rank_pairing_heap.h"
#include "checked.h"
#include <string.h>

#define RP_CHUNK 256

/* a node; the item follows at h->node_offset. In the binary form a
 * root's right pointer links the circular root list. */
struct ds_rp_node {
  ds_rp_node_t *left;
  ds_rp_node_t *right;
  ds_rp_node_t *parent; /* binary parent; NULL for roots and free nodes */
  int rank;
};

struct ds_rp_chunk {
  ds_rp_chunk_t *next;
};

static unsigned char *rp_item(const ds_rp_heap_t *h, ds_rp_node_t *n) {
  return (unsigned char *)n + h->node_offset;
}

static const unsigned char *rp_citem(const ds_rp_heap_t *h,
                                     const ds_rp_node_t *n) {
  return (const unsigned char *)n + h->node_offset;
}

static int rp_less(const ds_rp_heap_t *h, const ds_rp_node_t *a,
                   const ds_rp_node_t *b) {
  return h->compare(rp_citem(h, a), rp_citem(h, b), h->user) < 0;
}

static int rp_rank(const ds_rp_node_t *n) { return n == NULL ? -1 : n->rank; }

/* the rank a non-root node with children of ranks a and b must have */
static int rp_rule(int type, int a, int b) {
  int hi = a > b ? a : b;
  int lo = a > b ? b : a;
  if (type == 1) {
    return a == b ? a + 1 : hi;
  }
  return hi - lo <= 1 ? hi + 1 : hi;
}

/* ---- pool ---- */

static void rp_spare(ds_rp_heap_t *h, ds_rp_node_t *n) {
  n->parent = NULL;
  n->left = NULL;
  n->right = h->spare;
  if (h->spare == NULL) {
    h->spare_tail = n;
  }
  h->spare = n;
}

static ds_rp_node_t *rp_node_new(ds_rp_heap_t *h) {
  ds_rp_node_t *n;
  if (h->spare == NULL) {
    ds_rp_chunk_t *c = (ds_rp_chunk_t *)ds_context_alloc(
        h->context, DS_MAX_ALIGN + RP_CHUNK * h->node_size);
    unsigned char *p;
    size_t i;
    if (c == NULL) {
      return NULL;
    }
    c->next = NULL;
    if (h->chunks_tail == NULL) {
      h->chunks = c;
    } else {
      h->chunks_tail->next = c;
    }
    h->chunks_tail = c;
    p = (unsigned char *)c + DS_MAX_ALIGN;
    for (i = RP_CHUNK; i-- > 0;) {
      rp_spare(h, (ds_rp_node_t *)(void *)(p + i * h->node_size));
    }
  }
  n = h->spare;
  h->spare = n->right;
  if (h->spare == NULL) {
    h->spare_tail = NULL;
  }
  n->left = NULL;
  n->right = NULL;
  n->parent = NULL;
  n->rank = 0;
  return n;
}

/* ---- root list ---- */

/* add a root (right pointer free) to the circular list */
static void rp_add_root(ds_rp_heap_t *h, ds_rp_node_t *x) {
  x->parent = NULL;
  if (h->min == NULL) {
    x->right = x;
    h->min = x;
  } else {
    x->right = h->min->right;
    h->min->right = x;
    if (rp_less(h, x, h->min)) {
      h->min = x;
    }
  }
  h->roots++;
}

/* two roots of equal rank: the larger becomes the smaller's left child */
static ds_rp_node_t *rp_link(const ds_rp_heap_t *h, ds_rp_node_t *x,
                             ds_rp_node_t *y) {
  ds_rp_node_t *t;
  if (rp_less(h, y, x)) {
    t = x;
    x = y;
    y = t;
  }
  y->right = x->left;
  if (x->left != NULL) {
    x->left->parent = y;
  }
  y->parent = x;
  x->left = y;
  x->rank = y->rank + 1;
  return x;
}

ds_status_t ds_rp_heap_init(ds_rp_heap_t *h, size_t item_size,
                            ds_rp_compare_fn compare, void *user, int type,
                            ds_context_t *context) {
  size_t chunk;
  if (h == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  memset(h, 0, sizeof(*h));
  if (item_size == 0 || (type != 1 && type != 2)) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_align(sizeof(ds_rp_node_t), DS_MAX_ALIGN, &h->node_offset) ||
      !ds_size_add(h->node_offset, item_size, &h->node_size) ||
      !ds_size_align(h->node_size, DS_MAX_ALIGN, &h->node_size) ||
      !ds_size_mul(h->node_size, RP_CHUNK, &chunk) ||
      !ds_size_add(chunk, DS_MAX_ALIGN, &chunk)) {
    return DS_ERR_OVERFLOW;
  }
  h->type = type;
  h->item_size = item_size;
  h->compare = compare;
  h->user = user;
  h->context = context;
  return DS_OK;
}

void ds_rp_heap_destroy(ds_rp_heap_t *h) {
  ds_rp_chunk_t *c;
  if (h == NULL) {
    return;
  }
  c = h->chunks;
  while (c != NULL) {
    ds_rp_chunk_t *next = c->next;
    ds_context_free(h->context, c);
    c = next;
  }
  h->chunks = NULL;
  h->chunks_tail = NULL;
  h->spare = NULL;
  h->spare_tail = NULL;
  h->min = NULL;
  h->size = 0;
  h->roots = 0;
}

ds_status_t ds_rp_heap_push(ds_rp_heap_t *h, const void *item,
                            ds_rp_node_t **out_node) {
  ds_rp_node_t *n;
  if (h == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  n = rp_node_new(h);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(rp_item(h, n), item, h->item_size);
  rp_add_root(h, n);
  h->size++;
  if (out_node != NULL) {
    *out_node = n;
  }
  return DS_OK;
}

const void *ds_rp_heap_top(const ds_rp_heap_t *h) {
  return h == NULL || h->min == NULL ? NULL : rp_citem(h, h->min);
}

/* one-pass linking: each half tree takes part in at most one link */
static void rp_offer(ds_rp_heap_t *h, ds_rp_node_t *t, ds_rp_node_t **out,
                     int *top) {
  int r = t->rank;
  if (h->bucket[r] == NULL) {
    h->bucket[r] = t;
    if (r > *top) {
      *top = r;
    }
    return;
  }
  t = rp_link(h, h->bucket[r], t);
  h->bucket[r] = NULL;
  t->right = *out;
  *out = t;
}

/* take the minimum out of the heap (it is not freed) and relink */
static void rp_take_min(ds_rp_heap_t *h) {
  ds_rp_node_t *x = h->min;
  ds_rp_node_t *out = NULL;
  ds_rp_node_t *y;
  ds_rp_node_t *stop;
  int top = -1;
  int r;
  size_t roots = 0;
  /* the left spine of the minimum: each node with its left subtree
   * becomes a half tree */
  y = x->left;
  while (y != NULL) {
    ds_rp_node_t *next = y->right;
    y->right = NULL;
    y->parent = NULL;
    y->rank = rp_rank(y->left) + 1;
    rp_offer(h, y, &out, &top);
    y = next;
  }
  /* the other roots */
  stop = x;
  y = x->right;
  while (y != stop) {
    ds_rp_node_t *next = y->right;
    y->right = NULL;
    rp_offer(h, y, &out, &top);
    y = next;
  }
  for (r = 0; r <= top; r++) {
    if (h->bucket[r] != NULL) {
      h->bucket[r]->right = out;
      out = h->bucket[r];
      h->bucket[r] = NULL;
    }
  }
  /* the survivors form the new circular root list */
  h->min = NULL;
  if (out != NULL) {
    ds_rp_node_t *last = out;
    for (y = out; y != NULL; y = y->right) {
      y->parent = NULL;
      if (h->min == NULL || rp_less(h, y, h->min)) {
        h->min = y;
      }
      last = y;
      roots++;
    }
    last->right = out;
  }
  h->roots = roots;
  x->left = NULL;
  x->right = NULL;
  x->parent = NULL;
  x->rank = 0;
}

ds_status_t ds_rp_heap_pop(ds_rp_heap_t *h, void *out_item) {
  ds_rp_node_t *x;
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (h->min == NULL) {
    return DS_EMPTY;
  }
  x = h->min;
  if (out_item != NULL) {
    memcpy(out_item, rp_item(h, x), h->item_size);
  }
  rp_take_min(h);
  rp_spare(h, x);
  h->size--;
  return DS_OK;
}

/* cut a non-root node with its left subtree to the root list; its right
 * subtree takes its place, and ranks above are lowered while they break
 * the rule */
static void rp_cut(ds_rp_heap_t *h, ds_rp_node_t *x) {
  ds_rp_node_t *y = x->parent;
  ds_rp_node_t *z = x->right;
  ds_rp_node_t *u;
  if (y->left == x) {
    y->left = z;
  } else {
    y->right = z;
  }
  if (z != NULL) {
    z->parent = y;
  }
  x->right = NULL;
  x->rank = rp_rank(x->left) + 1;
  rp_add_root(h, x);
  for (u = y; u != NULL; u = u->parent) {
    int k;
    if (u->parent == NULL) { /* a root */
      u->rank = rp_rank(u->left) + 1;
      break;
    }
    k = rp_rule(h->type, rp_rank(u->left), rp_rank(u->right));
    if (k >= u->rank) {
      break;
    }
    u->rank = k;
  }
}

/* is x a root? roots have no parent; free nodes are never passed in */
static int rp_is_root(const ds_rp_node_t *x) { return x->parent == NULL; }

ds_status_t ds_rp_heap_update(ds_rp_heap_t *h, ds_rp_node_t *x,
                              const void *item) {
  int c;
  if (h == NULL || x == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  c = h->compare(item, rp_citem(h, x), h->user);
  if (c <= 0) {
    /* decrease (or equal): cut to the root list */
    memcpy(rp_item(h, x), item, h->item_size);
    if (!rp_is_root(x)) {
      rp_cut(h, x);
    }
    if (rp_less(h, x, h->min)) {
      h->min = x;
    }
    return DS_OK;
  }
  /* increase: take the node out as if it were the minimum, then push it
   * back with the new item */
  if (!rp_is_root(x)) {
    rp_cut(h, x);
  }
  h->min = x;
  rp_take_min(h);
  memcpy(rp_item(h, x), item, h->item_size);
  rp_add_root(h, x);
  return DS_OK;
}

ds_status_t ds_rp_heap_remove(ds_rp_heap_t *h, ds_rp_node_t *x,
                              void *out_item) {
  if (h == NULL || x == NULL) {
    return DS_ERR_NULL;
  }
  if (out_item != NULL) {
    memcpy(out_item, rp_item(h, x), h->item_size);
  }
  if (!rp_is_root(x)) {
    rp_cut(h, x);
  }
  h->min = x;
  rp_take_min(h);
  rp_spare(h, x);
  h->size--;
  return DS_OK;
}

const void *ds_rp_heap_item(const ds_rp_heap_t *h, const ds_rp_node_t *x) {
  return h == NULL || x == NULL ? NULL : rp_citem(h, x);
}

size_t ds_rp_heap_size(const ds_rp_heap_t *h) {
  return h == NULL ? 0 : h->size;
}

size_t ds_rp_heap_roots(const ds_rp_heap_t *h) {
  return h == NULL ? 0 : h->roots;
}

void ds_rp_heap_meld(ds_rp_heap_t *h, ds_rp_heap_t *other) {
  if (h == NULL || other == NULL || h == other) {
    return;
  }
  if (other->min != NULL) {
    if (h->min == NULL) {
      h->min = other->min;
    } else {
      /* splice the two circles */
      ds_rp_node_t *t = h->min->right;
      h->min->right = other->min->right;
      other->min->right = t;
      if (rp_less(h, other->min, h->min)) {
        h->min = other->min;
      }
    }
  }
  h->size += other->size;
  h->roots += other->roots;
  if (other->chunks != NULL) {
    if (h->chunks_tail == NULL) {
      h->chunks = other->chunks;
    } else {
      h->chunks_tail->next = other->chunks;
    }
    h->chunks_tail = other->chunks_tail;
  }
  if (other->spare != NULL) {
    other->spare_tail->right = h->spare;
    if (h->spare == NULL) {
      h->spare_tail = other->spare_tail;
    }
    h->spare = other->spare;
  }
  other->min = NULL;
  other->size = 0;
  other->roots = 0;
  other->chunks = NULL;
  other->chunks_tail = NULL;
  other->spare = NULL;
  other->spare_tail = NULL;
}

/* half order, ranks and links of one half tree, iteratively; adds its
 * node count to *count */
static bool rp_check_tree(const ds_rp_heap_t *h, const ds_rp_node_t *root,
                          size_t *count) {
  const ds_rp_node_t *n;
  if (root->parent != NULL || root->rank != rp_rank(root->left) + 1) {
    return false;
  }
  if (root->left != NULL && root->left->parent != root) {
    return false;
  }
  (*count)++;
  /* walk the binary tree under root->left in preorder via parent links;
   * every node's item is no smaller than its heap parent's: the heap
   * parent of a node is the node whose left spine it lies on */
  n = root->left;
  while (n != NULL) {
    const ds_rp_node_t *hp; /* heap parent: up through right links */
    const ds_rp_node_t *c = n;
    (*count)++;
    if ((n->left != NULL && n->left->parent != n) ||
        (n->right != NULL && n->right->parent != n)) {
      return false;
    }
    if (n->rank != rp_rule(h->type, rp_rank(n->left), rp_rank(n->right))) {
      return false; /* links and cuts keep the rule exact */
    }
    while (c->parent->right == c) {
      c = c->parent;
    }
    hp = c->parent;
    if (h->compare(rp_citem(h, n), rp_citem(h, hp), h->user) < 0) {
      return false;
    }
    /* next in preorder */
    if (n->left != NULL) {
      n = n->left;
    } else if (n->right != NULL) {
      n = n->right;
    } else {
      while (n != root->left && n->parent != NULL &&
             (n->parent->right == n || n->parent->right == NULL)) {
        n = n->parent;
      }
      if (n == root->left) {
        break;
      }
      n = n->parent->right;
    }
  }
  return true;
}

bool ds_rp_heap_check(const ds_rp_heap_t *h) {
  const ds_rp_node_t *r;
  size_t count = 0;
  size_t roots = 0;
  if (h == NULL) {
    return false;
  }
  if (h->min == NULL) {
    return h->size == 0 && h->roots == 0;
  }
  r = h->min;
  do {
    if (rp_less(h, r, h->min) || !rp_check_tree(h, r, &count)) {
      return false;
    }
    roots++;
    r = r->right;
  } while (r != h->min && roots <= h->size);
  return roots == h->roots && count == h->size;
}
