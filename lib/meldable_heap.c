#include "meldable_heap.h"

/* ---- policies ------------------------------------------------------------ */

static size_t mh_aux(const ds_meldable_heap_node_t *node) {
  return node == NULL ? 0 : node->aux;
}

static bool mh_leftist_fix(ds_meldable_heap_node_t *node, void *state) {
  size_t l = mh_aux(node->left);
  size_t r = mh_aux(node->right);
  (void)state;
  node->aux = (l < r ? l : r) + 1;
  return l < r;
}

static bool mh_weight_fix(ds_meldable_heap_node_t *node, void *state) {
  size_t l = mh_aux(node->left);
  size_t r = mh_aux(node->right);
  (void)state;
  node->aux = l + r + 1;
  return l < r;
}

static bool mh_skew_fix(ds_meldable_heap_node_t *node, void *state) {
  (void)node;
  (void)state;
  return true;
}

static bool mh_random_fix(ds_meldable_heap_node_t *node, void *state) {
  unsigned long *x = (unsigned long *)state;
  unsigned long v = *x & 0xffffffffUL;
  (void)node;
  if (v == 0) {
    v = 0x9e3779b9UL;
  }
  v ^= (v << 13) & 0xffffffffUL;
  v ^= v >> 17;
  v ^= (v << 5) & 0xffffffffUL;
  *x = v;
  return (v & 0x100UL) != 0;
}

static const ds_meldable_heap_policy_t mh_leftist = {"leftist", mh_leftist_fix,
                                                     1};
static const ds_meldable_heap_policy_t mh_weight = {"weight-biased",
                                                    mh_weight_fix, 1};
static const ds_meldable_heap_policy_t mh_skew = {"skew", mh_skew_fix, 0};
static const ds_meldable_heap_policy_t mh_random = {"random", mh_random_fix, 0};

const ds_meldable_heap_policy_t *ds_meldable_heap_leftist_policy(void) {
  return &mh_leftist;
}
const ds_meldable_heap_policy_t *ds_meldable_heap_weight_policy(void) {
  return &mh_weight;
}
const ds_meldable_heap_policy_t *ds_meldable_heap_skew_policy(void) {
  return &mh_skew;
}
const ds_meldable_heap_policy_t *ds_meldable_heap_random_policy(void) {
  return &mh_random;
}

/* ---- core ---------------------------------------------------------------- */

static int mh_cmp(const ds_meldable_heap_t *heap,
                  const ds_meldable_heap_node_t *a,
                  const ds_meldable_heap_node_t *b) {
  return heap->compare(a, b, heap->user);
}

static void mh_fix(ds_meldable_heap_t *heap, ds_meldable_heap_node_t *node) {
  ds_meldable_heap_node_t *t;
  void *state =
      heap->policy_state != NULL ? heap->policy_state : (void *)&heap->rng;
  if (heap->policy->fix(node, state)) {
    t = node->left;
    node->left = node->right;
    node->right = t;
  }
}

static void mh_node_reset(const ds_meldable_heap_t *heap,
                          ds_meldable_heap_node_t *node) {
  node->parent = NULL;
  node->left = NULL;
  node->right = NULL;
  node->aux = heap->policy->leaf_aux;
}

/* Meld two detached trees along their right spines; returns the root, whose
 * parent is NULL. */
static ds_meldable_heap_node_t *mh_merge(ds_meldable_heap_t *heap,
                                         ds_meldable_heap_node_t *a,
                                         ds_meldable_heap_node_t *b) {
  ds_meldable_heap_node_t *root;
  ds_meldable_heap_node_t *cur;
  ds_meldable_heap_node_t *r;
  ds_meldable_heap_node_t *t;
  if (a == NULL) {
    if (b != NULL) {
      b->parent = NULL;
    }
    return b;
  }
  if (b == NULL) {
    a->parent = NULL;
    return a;
  }
  if (mh_cmp(heap, b, a) < 0) {
    t = a;
    a = b;
    b = t;
  }
  root = a;
  root->parent = NULL;
  cur = a;
  for (;;) {
    r = cur->right;
    if (r == NULL) {
      cur->right = b;
      b->parent = cur;
      break;
    }
    if (mh_cmp(heap, b, r) < 0) {
      cur->right = b;
      b->parent = cur;
      t = r;
      r = b;
      b = t;
    }
    cur = r;
  }
  for (t = cur; t != NULL; t = t->parent) {
    mh_fix(heap, t);
  }
  return root;
}

ds_status_t ds_meldable_heap_init(ds_meldable_heap_t *heap,
                                  ds_meldable_heap_compare_func_t compare,
                                  void *user,
                                  const ds_meldable_heap_policy_t *policy,
                                  void *policy_state) {
  if (heap == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (policy != NULL && policy->fix == NULL) {
    return DS_ERR_INVALID;
  }
  heap->root = NULL;
  heap->size = 0;
  heap->compare = compare;
  heap->user = user;
  heap->policy = policy != NULL ? policy : &mh_leftist;
  heap->policy_state = policy_state;
  heap->rng = 0x2545f491UL;
  return DS_OK;
}

void ds_meldable_heap_push(ds_meldable_heap_t *heap,
                           ds_meldable_heap_node_t *node) {
  mh_node_reset(heap, node);
  heap->root = mh_merge(heap, heap->root, node);
  heap->size++;
}

ds_meldable_heap_node_t *ds_meldable_heap_peek(const ds_meldable_heap_t *heap) {
  return heap->root;
}

ds_meldable_heap_node_t *ds_meldable_heap_pop(ds_meldable_heap_t *heap) {
  ds_meldable_heap_node_t *top = heap->root;
  if (top == NULL) {
    return NULL;
  }
  heap->root = mh_merge(heap, top->left, top->right);
  heap->size--;
  mh_node_reset(heap, top);
  return top;
}

void ds_meldable_heap_meld(ds_meldable_heap_t *heap,
                           ds_meldable_heap_t *other) {
  if (heap == other) {
    return;
  }
  heap->root = mh_merge(heap, heap->root, other->root);
  heap->size += other->size;
  other->root = NULL;
  other->size = 0;
}

/* Replace `node` (not the root) by `sub` under its parent, then refresh the
 * shape data upwards while it changes. */
static void mh_replace(ds_meldable_heap_t *heap, ds_meldable_heap_node_t *node,
                       ds_meldable_heap_node_t *sub) {
  ds_meldable_heap_node_t *p = node->parent;
  ds_meldable_heap_node_t *x;
  size_t old;
  if (p->left == node) {
    p->left = sub;
  } else {
    p->right = sub;
  }
  if (sub != NULL) {
    sub->parent = p;
  }
  for (x = p; x != NULL; x = x->parent) {
    old = x->aux;
    mh_fix(heap, x);
    if (x->aux == old) {
      break;
    }
  }
}

void ds_meldable_heap_remove(ds_meldable_heap_t *heap,
                             ds_meldable_heap_node_t *node) {
  ds_meldable_heap_node_t *sub;
  if (node == heap->root) {
    (void)ds_meldable_heap_pop(heap);
    return;
  }
  sub = mh_merge(heap, node->left, node->right);
  mh_replace(heap, node, sub);
  heap->size--;
  mh_node_reset(heap, node);
}

void ds_meldable_heap_decrease(ds_meldable_heap_t *heap,
                               ds_meldable_heap_node_t *node) {
  if (node == heap->root || mh_cmp(heap, node, node->parent) >= 0) {
    return;
  }
  /* The subtree under node stays heap-ordered: cut it and meld it back. */
  mh_replace(heap, node, NULL);
  node->parent = NULL;
  heap->root = mh_merge(heap, heap->root, node);
}

void ds_meldable_heap_update(ds_meldable_heap_t *heap,
                             ds_meldable_heap_node_t *node) {
  ds_meldable_heap_remove(heap, node);
  ds_meldable_heap_push(heap, node);
}

size_t ds_meldable_heap_size(const ds_meldable_heap_t *heap) {
  return heap->size;
}

bool ds_meldable_heap_is_empty(const ds_meldable_heap_t *heap) {
  return heap->size == 0;
}

ds_status_t ds_meldable_heap_visit(const ds_meldable_heap_t *heap,
                                   ds_meldable_heap_visit_func_t visit,
                                   void *user) {
  ds_meldable_heap_node_t *n = heap->root;
  ds_status_t status;
  while (n != NULL) {
    status = visit(n, user);
    if (status != DS_OK) {
      return status;
    }
    if (n->left != NULL) {
      n = n->left;
    } else if (n->right != NULL) {
      n = n->right;
    } else {
      while (n->parent != NULL &&
             (n == n->parent->right || n->parent->right == NULL)) {
        n = n->parent;
      }
      n = n->parent != NULL ? n->parent->right : NULL;
    }
  }
  return DS_OK;
}

void ds_meldable_heap_clear(ds_meldable_heap_t *heap,
                            void (*release)(ds_meldable_heap_node_t *node,
                                            void *user),
                            void *user) {
  ds_meldable_heap_node_t *n = heap->root;
  ds_meldable_heap_node_t *p;
  while (n != NULL) {
    if (n->left != NULL) {
      n = n->left;
    } else if (n->right != NULL) {
      n = n->right;
    } else {
      p = n->parent;
      if (p != NULL) {
        if (p->left == n) {
          p->left = NULL;
        } else {
          p->right = NULL;
        }
      }
      mh_node_reset(heap, n);
      if (release != NULL) {
        release(n, user);
      }
      n = p;
    }
  }
  heap->root = NULL;
  heap->size = 0;
}
