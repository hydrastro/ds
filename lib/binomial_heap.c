#include "binomial_heap.h"

static int bh_cmp(const ds_binomial_heap_t *heap,
                  const ds_binomial_heap_node_t *a,
                  const ds_binomial_heap_node_t *b) {
  return heap->compare(a, b, heap->user);
}

ds_status_t ds_binomial_heap_init(ds_binomial_heap_t *heap,
                                  ds_binomial_heap_compare_func_t compare,
                                  void *user) {
  if (heap == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  heap->head = NULL;
  heap->min = NULL;
  heap->size = 0;
  heap->compare = compare;
  heap->user = user;
  return DS_OK;
}

void ds_binomial_heap_node_init(ds_binomial_heap_node_t *node) {
  node->parent = NULL;
  node->child = NULL;
  node->sibling = NULL;
  node->prev = NULL;
  node->order = 0;
}

/* Make y a child of z (same order, z not greater than y). */
static void bh_link(ds_binomial_heap_node_t *y, ds_binomial_heap_node_t *z) {
  y->parent = z;
  y->prev = NULL;
  y->sibling = z->child;
  if (z->child != NULL) {
    z->child->prev = y;
  }
  z->child = y;
  z->order++;
}

/* Union of two root lists sorted by increasing order. Returns the new head;
 * prev pointers of the result are rebuilt. */
static ds_binomial_heap_node_t *bh_union(const ds_binomial_heap_t *heap,
                                         ds_binomial_heap_node_t *a,
                                         ds_binomial_heap_node_t *b) {
  ds_binomial_heap_node_t *head = NULL;
  ds_binomial_heap_node_t **tail = &head;
  ds_binomial_heap_node_t *prev;
  ds_binomial_heap_node_t *x;
  ds_binomial_heap_node_t *next;

  /* Merge by order. */
  while (a != NULL && b != NULL) {
    if (a->order <= b->order) {
      *tail = a;
      a = a->sibling;
    } else {
      *tail = b;
      b = b->sibling;
    }
    tail = &(*tail)->sibling;
  }
  *tail = a != NULL ? a : b;
  if (head == NULL) {
    return NULL;
  }

  /* Link trees of equal order. */
  prev = NULL;
  x = head;
  next = x->sibling;
  while (next != NULL) {
    if (x->order != next->order ||
        (next->sibling != NULL && next->sibling->order == x->order)) {
      prev = x;
      x = next;
    } else if (bh_cmp(heap, x, next) <= 0) {
      x->sibling = next->sibling;
      bh_link(next, x);
    } else {
      if (prev == NULL) {
        head = next;
      } else {
        prev->sibling = next;
      }
      bh_link(x, next);
      x = next;
    }
    next = x->sibling;
  }

  prev = NULL;
  for (x = head; x != NULL; x = x->sibling) {
    x->prev = prev;
    x->parent = NULL;
    prev = x;
  }
  return head;
}

/* Linking may hang the minimum below an equal key; its root is then an
 * equally small minimum. */
static void bh_fix_min(ds_binomial_heap_t *heap) {
  if (heap->min != NULL) {
    while (heap->min->parent != NULL) {
      heap->min = heap->min->parent;
    }
  }
}

static void bh_find_min(ds_binomial_heap_t *heap) {
  ds_binomial_heap_node_t *x;
  heap->min = heap->head;
  for (x = heap->head; x != NULL; x = x->sibling) {
    if (bh_cmp(heap, x, heap->min) < 0) {
      heap->min = x;
    }
  }
}

void ds_binomial_heap_push(ds_binomial_heap_t *heap,
                           ds_binomial_heap_node_t *node) {
  ds_binomial_heap_node_init(node);
  if (heap->min == NULL || bh_cmp(heap, node, heap->min) < 0) {
    heap->min = node;
  }
  heap->head = bh_union(heap, heap->head, node);
  heap->size++;
  bh_fix_min(heap);
}

ds_binomial_heap_node_t *ds_binomial_heap_peek(const ds_binomial_heap_t *heap) {
  return heap->min;
}

/* Detach a root and put its children back into the root list. */
static void bh_remove_root(ds_binomial_heap_t *heap,
                           ds_binomial_heap_node_t *root) {
  ds_binomial_heap_node_t *children = NULL;
  ds_binomial_heap_node_t *c;
  ds_binomial_heap_node_t *next;

  if (root->prev != NULL) {
    root->prev->sibling = root->sibling;
  } else {
    heap->head = root->sibling;
  }
  if (root->sibling != NULL) {
    root->sibling->prev = root->prev;
  }

  /* Children are stored by decreasing order; reverse them. */
  c = root->child;
  while (c != NULL) {
    next = c->sibling;
    c->sibling = children;
    c->parent = NULL;
    children = c;
    c = next;
  }
  heap->head = bh_union(heap, heap->head, children);
  heap->size--;
  ds_binomial_heap_node_init(root);
  bh_find_min(heap);
}

ds_binomial_heap_node_t *ds_binomial_heap_pop(ds_binomial_heap_t *heap) {
  ds_binomial_heap_node_t *min = heap->min;
  if (min != NULL) {
    bh_remove_root(heap, min);
  }
  return min;
}

void ds_binomial_heap_meld(ds_binomial_heap_t *heap,
                           ds_binomial_heap_t *other) {
  if (heap == other || other->head == NULL) {
    return;
  }
  if (heap->min == NULL || bh_cmp(heap, other->min, heap->min) < 0) {
    heap->min = other->min;
  }
  heap->head = bh_union(heap, heap->head, other->head);
  heap->size += other->size;
  bh_fix_min(heap);
  other->head = NULL;
  other->min = NULL;
  other->size = 0;
}

/* Exchange the tree positions of c and its parent p. */
static void bh_swap_with_parent(ds_binomial_heap_t *heap,
                                ds_binomial_heap_node_t *c) {
  ds_binomial_heap_node_t *p = c->parent;
  ds_binomial_heap_node_t *g = p->parent;
  ds_binomial_heap_node_t *pp = p->prev;
  ds_binomial_heap_node_t *pn = p->sibling;
  ds_binomial_heap_node_t *cp = c->prev;
  ds_binomial_heap_node_t *cn = c->sibling;
  ds_binomial_heap_node_t *pchild = p->child;
  ds_binomial_heap_node_t *cchild = c->child;
  unsigned int porder = p->order;
  unsigned int corder = c->order;
  ds_binomial_heap_node_t *x;

  c->parent = g;
  c->prev = pp;
  c->sibling = pn;
  c->order = porder;
  c->child = pchild == c ? p : pchild;

  p->parent = c;
  p->prev = cp;
  p->sibling = cn;
  p->child = cchild;
  p->order = corder;

  if (pp != NULL) {
    pp->sibling = c;
  } else if (g != NULL) {
    g->child = c;
  } else {
    heap->head = c;
  }
  if (pn != NULL) {
    pn->prev = c;
  }
  if (cp != NULL) {
    cp->sibling = p;
  }
  if (cn != NULL) {
    cn->prev = p;
  }
  for (x = c->child; x != NULL; x = x->sibling) {
    x->parent = c;
  }
  for (x = p->child; x != NULL; x = x->sibling) {
    x->parent = p;
  }
  if (heap->min == p) {
    heap->min = c;
  }
}

static void bh_sift_up(ds_binomial_heap_t *heap, ds_binomial_heap_node_t *node,
                       bool force) {
  while (node->parent != NULL &&
         (force || bh_cmp(heap, node, node->parent) < 0)) {
    bh_swap_with_parent(heap, node);
  }
  if (node->parent == NULL && (force || bh_cmp(heap, node, heap->min) < 0)) {
    heap->min = node;
  }
}

void ds_binomial_heap_decrease(ds_binomial_heap_t *heap,
                               ds_binomial_heap_node_t *node) {
  bh_sift_up(heap, node, false);
}

void ds_binomial_heap_remove(ds_binomial_heap_t *heap,
                             ds_binomial_heap_node_t *node) {
  bh_sift_up(heap, node, true);
  bh_remove_root(heap, node);
}

void ds_binomial_heap_update(ds_binomial_heap_t *heap,
                             ds_binomial_heap_node_t *node) {
  ds_binomial_heap_remove(heap, node);
  ds_binomial_heap_push(heap, node);
}

size_t ds_binomial_heap_size(const ds_binomial_heap_t *heap) {
  return heap->size;
}

bool ds_binomial_heap_is_empty(const ds_binomial_heap_t *heap) {
  return heap->size == 0;
}

size_t ds_binomial_heap_tree_count(const ds_binomial_heap_t *heap) {
  size_t count = 0;
  const ds_binomial_heap_node_t *x;
  for (x = heap->head; x != NULL; x = x->sibling) {
    count++;
  }
  return count;
}

ds_status_t ds_binomial_heap_visit(const ds_binomial_heap_t *heap,
                                   ds_binomial_heap_visit_func_t visit,
                                   void *user) {
  ds_binomial_heap_node_t *node = heap->head;
  ds_status_t status;
  while (node != NULL) {
    status = visit(node, user);
    if (status != DS_OK) {
      return status;
    }
    if (node->child != NULL) {
      node = node->child;
      continue;
    }
    while (node != NULL && node->sibling == NULL) {
      node = node->parent;
    }
    if (node != NULL) {
      node = node->sibling;
    }
  }
  return DS_OK;
}

static ds_binomial_heap_node_t *bh_deepest(ds_binomial_heap_node_t *node) {
  while (node->child != NULL) {
    node = node->child;
  }
  return node;
}

void ds_binomial_heap_clear(ds_binomial_heap_t *heap,
                            void (*release)(ds_binomial_heap_node_t *node,
                                            void *user),
                            void *user) {
  ds_binomial_heap_node_t *node;
  ds_binomial_heap_node_t *next;
  if (heap->head != NULL) {
    node = bh_deepest(heap->head);
    while (node != NULL) {
      next = node->sibling != NULL ? bh_deepest(node->sibling) : node->parent;
      ds_binomial_heap_node_init(node);
      if (release != NULL) {
        release(node, user);
      }
      node = next;
    }
  }
  heap->head = NULL;
  heap->min = NULL;
  heap->size = 0;
}
