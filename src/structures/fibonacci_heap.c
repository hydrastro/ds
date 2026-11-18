#include "core/owner_alloc.h"
#include <dsa/core/checked.h>
#include <dsa/structures/fibonacci_heap.h>
#include <stdlib.h>
#include <string.h>

static int fib_compare(ds_fibonacci_heap_t *heap, ds_fibonacci_heap_node_t *a,
                       ds_fibonacci_heap_node_t *b) {
  return heap->compare_with != NULL
             ? heap->compare_with(a, b, heap->compare_user)
             : heap->compare(a, b);
}

ds_fibonacci_heap_t *ds_fibonacci_heap_create(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *)) {
  return ds_fibonacci_heap_create_alloc(compare, malloc, free);
}

static ds_fibonacci_heap_t *fib_create_internal(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *),
    ds_context_t *context, ds_fibonacci_heap_compare_fn compare_with,
    void *compare_user) {
  ds_fibonacci_heap_t *heap;
  if (allocator == NULL || deallocator == NULL ||
      (compare == NULL && compare_with == NULL)) {
    return NULL;
  }
  heap = (ds_fibonacci_heap_t *)DS_OWNER_ALLOCATE(context, allocator,
                                                  sizeof(ds_fibonacci_heap_t));
  if (heap == NULL) {
    return NULL;
  }
  heap->context = context;
  heap->compare_with = compare_with;
  heap->compare_user = compare_user;
  heap->compare = compare;
  heap->allocator = allocator;
  heap->deallocator = deallocator;
  heap->size = 0;
  heap->nil = (ds_fibonacci_heap_node_t *)DS_OWNER_ALLOCATE(
      context, allocator, sizeof(ds_fibonacci_heap_node_t));
  if (heap->nil == NULL) {
    DS_OWNER_RELEASE(context, deallocator, heap);
    return NULL;
  }
  heap->nil->parent = heap->nil;
  heap->nil->child = heap->nil;
  heap->nil->left = heap->nil;
  heap->nil->right = heap->nil;
  heap->nil->degree = 0;
  heap->nil->mark = false;
  heap->min = heap->nil;
  return heap;
}
ds_fibonacci_heap_t *ds_fibonacci_heap_create_alloc(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  return fib_create_internal(compare, allocator, deallocator, NULL, NULL, NULL);
}
ds_status_t
ds_fibonacci_heap_create_context(ds_fibonacci_heap_compare_fn compare,
                                 void *user, ds_context_t *context,
                                 ds_fibonacci_heap_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (compare == NULL)
    return DS_ERR_NULL;
  *out = fib_create_internal(NULL, malloc, free,
                             context != NULL ? context : ds_default_context(),
                             compare, user);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_fibonacci_heap_node_init(ds_fibonacci_heap_node_t *node) {
  node->parent = NULL;
  node->child = NULL;
  node->left = node;
  node->right = node;
  node->degree = 0;
  node->mark = false;
}

/* splice node into the circular list to the right of `list` */
static void fib_list_insert(ds_fibonacci_heap_node_t *list,
                            ds_fibonacci_heap_node_t *node) {
  node->left = list;
  node->right = list->right;
  list->right->left = node;
  list->right = node;
}

/* remove node from its circular sibling list */
static void fib_list_remove(ds_fibonacci_heap_node_t *node) {
  node->left->right = node->right;
  node->right->left = node->left;
  node->left = node;
  node->right = node;
}

void ds_fibonacci_heap_insert(ds_fibonacci_heap_t *heap,
                              ds_fibonacci_heap_node_t *node) {
  node->parent = NULL;
  node->child = NULL;
  node->left = node;
  node->right = node;
  node->degree = 0;
  node->mark = false;
  if (heap->min == heap->nil) {
    heap->min = node;
  } else {
    fib_list_insert(heap->min, node);
    if (fib_compare(heap, node, heap->min) < 0) {
      heap->min = node;
    }
  }
  heap->size += 1;
}

ds_fibonacci_heap_node_t *ds_fibonacci_heap_peek(ds_fibonacci_heap_t *heap) {
  ds_fibonacci_heap_node_t *result;
  result = heap->min;
  return result;
}

/* make `child` a child of `parent`; both are roots beforehand */
static void fib_link(ds_fibonacci_heap_node_t *child,
                     ds_fibonacci_heap_node_t *parent) {
  child->parent = parent;
  if (parent->child == NULL) {
    parent->child = child;
    child->left = child;
    child->right = child;
  } else {
    fib_list_insert(parent->child, child);
  }
  parent->degree += 1;
  child->mark = false;
}

/* log_phi(n)+1 is less than twice the bit width of n plus two. */
#define FIB_MAX_DEGREE DS_FIB_MAX_DEGREE

static void fib_consolidate(ds_fibonacci_heap_t *heap) {
  ds_fibonacci_heap_node_t *table[FIB_MAX_DEGREE];
  ds_fibonacci_heap_node_t *chain;
  size_t i;
  for (i = 0; i < FIB_MAX_DEGREE; i++) {
    table[i] = NULL;
  }
  /* the root ring becomes a chain through right, so linking can rewire
   * the nodes already visited */
  chain = heap->min;
  chain->left->right = NULL;
  while (chain != NULL) {
    ds_fibonacci_heap_node_t *x = chain;
    int d;
    chain = chain->right;
    x->left = x;
    x->right = x;
    d = x->degree;
    while (table[d] != NULL) {
      ds_fibonacci_heap_node_t *y = table[d];
      if (fib_compare(heap, x, y) > 0) {
        ds_fibonacci_heap_node_t *tmp = x;
        x = y;
        y = tmp;
      }
      fib_link(y, x);
      table[d] = NULL;
      d += 1;
    }
    table[d] = x;
  }
  /* rebuild the root list and find the new min */
  heap->min = heap->nil;
  for (i = 0; i < FIB_MAX_DEGREE; i++) {
    if (table[i] != NULL) {
      ds_fibonacci_heap_node_t *node = table[i];
      node->parent = NULL;
      node->left = node;
      node->right = node;
      if (heap->min == heap->nil) {
        heap->min = node;
      } else {
        fib_list_insert(heap->min, node);
        if (fib_compare(heap, node, heap->min) < 0) {
          heap->min = node;
        }
      }
    }
  }
}

ds_fibonacci_heap_node_t *ds_fibonacci_heap_extract(ds_fibonacci_heap_t *heap) {
  ds_fibonacci_heap_node_t *z;
  z = heap->min;
  if (z == heap->nil) {
    return heap->nil;
  }
  /* move z's children to the root list */
  if (z->child != NULL) {
    ds_fibonacci_heap_node_t *c = z->child;
    ds_fibonacci_heap_node_t *first = c;
    do {
      ds_fibonacci_heap_node_t *nxt = c->right;
      c->parent = NULL;
      c->left = c;
      c->right = c;
      fib_list_insert(heap->min, c);
      c = nxt;
    } while (c != first);
    z->child = NULL;
  }
  /* remove z from the root list */
  if (z->right == z) {
    heap->min = heap->nil;
  } else {
    heap->min = z->right;
    fib_list_remove(z);
    fib_consolidate(heap);
  }
  heap->size -= 1;
  z->left = z;
  z->right = z;
  z->parent = NULL;
  z->child = NULL;
  return z;
}

static void fib_cut(ds_fibonacci_heap_t *heap, ds_fibonacci_heap_node_t *x,
                    ds_fibonacci_heap_node_t *y) {
  /* remove x from y's child list, add x to the root list */
  if (x->right == x) {
    y->child = NULL;
  } else {
    if (y->child == x) {
      y->child = x->right;
    }
    fib_list_remove(x);
  }
  y->degree -= 1;
  x->parent = NULL;
  x->left = x;
  x->right = x;
  x->mark = false;
  fib_list_insert(heap->min, x);
}

/* up through the marked ancestors, iteratively (a tree can be deep) */
static void fib_cascading_cut(ds_fibonacci_heap_t *heap,
                              ds_fibonacci_heap_node_t *y) {
  ds_fibonacci_heap_node_t *z = y->parent;
  while (z != NULL) {
    if (!y->mark) {
      y->mark = true;
      return;
    }
    fib_cut(heap, y, z);
    y = z;
    z = y->parent;
  }
}

void ds_fibonacci_heap_decrease_key(ds_fibonacci_heap_t *heap,
                                    ds_fibonacci_heap_node_t *node) {
  ds_fibonacci_heap_node_t *parent;
  parent = node->parent;
  if (parent != NULL && fib_compare(heap, node, parent) < 0) {
    fib_cut(heap, node, parent);
    fib_cascading_cut(heap, parent);
  }
  if (fib_compare(heap, node, heap->min) < 0) {
    heap->min = node;
  }
}

ds_status_t ds_fibonacci_heap_merge_ex(ds_fibonacci_heap_t *heap,
                                       ds_fibonacci_heap_t *other) {
  if (heap == NULL || other == NULL)
    return DS_ERR_NULL;
  if (heap == other)
    return DS_ERR_INVALID;
  if (heap->compare != other->compare ||
      heap->compare_with != other->compare_with ||
      heap->compare_user != other->compare_user)
    return DS_ERR_INVALID;
  if (other->size > (size_t)-1 - heap->size)
    return DS_ERR_OVERFLOW;

  if (other->min == other->nil) {
    return DS_OK;
  }
  if (heap->min == heap->nil) {
    heap->min = other->min;
  } else {
    /* concatenate the two circular root lists */
    ds_fibonacci_heap_node_t *a_right = heap->min->right;
    ds_fibonacci_heap_node_t *b_left = other->min->left;
    heap->min->right = other->min;
    other->min->left = heap->min;
    a_right->left = b_left;
    b_left->right = a_right;
    if (fib_compare(heap, other->min, heap->min) < 0) {
      heap->min = other->min;
    }
  }
  heap->size += other->size;
  other->min = other->nil;
  other->size = 0;
  return DS_OK;
}

void ds_fibonacci_heap_merge(ds_fibonacci_heap_t *heap,
                             ds_fibonacci_heap_t *other) {
  (void)ds_fibonacci_heap_merge_ex(heap, other);
}

size_t ds_fibonacci_heap_size(ds_fibonacci_heap_t *heap) {
  size_t result;
  result = heap->size;
  return result;
}

bool ds_fibonacci_heap_is_empty(ds_fibonacci_heap_t *heap) {
  bool result;
  result = heap->size == 0;
  return result;
}

/* every node once, without recursion: a node's children join the chain
 * of nodes still to visit before it is handed to destroy */
void ds_fibonacci_heap_destroy_with(ds_fibonacci_heap_t *heap,
                                    ds_fibonacci_heap_destroy_fn destroy,
                                    void *user) {
  ds_fibonacci_heap_node_t *chain = NULL;
  if (heap == NULL)
    return;
  if (heap->min != heap->nil) {
    chain = heap->min;
    chain->left->right = NULL;
  }
  while (chain != NULL) {
    ds_fibonacci_heap_node_t *x = chain;
    chain = x->right;
    if (x->child != NULL) {
      ds_fibonacci_heap_node_t *first = x->child;
      first->left->right = chain;
      chain = first;
      x->child = NULL;
    }
    if (destroy != NULL) {
      destroy(x, user);
    }
  }
  DS_OWNER_FREE(heap, heap->nil);
  DS_OWNER_FREE(heap, heap);
}

static void fib_destroy_direct(ds_fibonacci_heap_node_t *node, void *user) {
  void (**destroy)(ds_fibonacci_heap_node_t *) =
      (void (**)(ds_fibonacci_heap_node_t *))user;
  (*destroy)(node);
}
void ds_fibonacci_heap_destroy(ds_fibonacci_heap_t *heap,
                               void (*destroy)(ds_fibonacci_heap_node_t *)) {
  ds_fibonacci_heap_destroy_with(
      heap, destroy != NULL ? fib_destroy_direct : NULL, &destroy);
}
ds_status_t ds_fibonacci_heap_insert_ex(ds_fibonacci_heap_t *heap,
                                        ds_fibonacci_heap_node_t *node) {
  if (heap == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == heap->nil)
    return DS_ERR_INVALID;
  if (heap->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_fibonacci_heap_insert(heap, node);
  return DS_OK;
}
ds_status_t ds_fibonacci_heap_extract_ex(ds_fibonacci_heap_t *heap,
                                         ds_fibonacci_heap_node_t **out) {
  if (heap == NULL || out == NULL)
    return DS_ERR_NULL;
  if (heap->size == 0)
    return DS_EMPTY;
  *out = ds_fibonacci_heap_extract(heap);
  return DS_OK;
}
ds_status_t ds_fibonacci_heap_decrease_key_ex(ds_fibonacci_heap_t *heap,
                                              ds_fibonacci_heap_node_t *node) {
  if (heap == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == heap->nil)
    return DS_ERR_INVALID;
  if (heap->size == 0)
    return DS_ERR_STATE;
  ds_fibonacci_heap_decrease_key(heap, node);
  return DS_OK;
}
ds_status_t ds_fibonacci_heap_walk_with(const ds_fibonacci_heap_t *heap,
                                        ds_fibonacci_heap_visit_fn visit,
                                        void *user) {
  ds_fibonacci_heap_node_t *node, *parent, *first;
  ds_status_t status;
  if (heap == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (heap->min == heap->nil)
    return DS_OK;
  node = heap->min;
  while (node != NULL) {
    status = visit(node, user);
    if (status != DS_OK)
      return status;
    if (node->child != NULL) {
      node = node->child;
      continue;
    }
    while (node != NULL) {
      parent = node->parent;
      first = parent != NULL ? parent->child : heap->min;
      if (node->right != first) {
        node = node->right;
        break;
      }
      node = parent;
    }
  }
  return DS_OK;
}
typedef struct fib_clone_policy {
  ds_fibonacci_heap_t *copy;
  ds_fibonacci_heap_clone_fn clone;
  void *user;
} fib_clone_policy_t;
static ds_status_t fib_clone_visit(ds_fibonacci_heap_node_t *source,
                                   void *user) {
  fib_clone_policy_t *policy = (fib_clone_policy_t *)user;
  ds_fibonacci_heap_node_t *node = NULL;
  ds_status_t status = policy->clone(source, &node, policy->user);
  if (status != DS_OK || node == NULL)
    return status < 0 ? status : DS_ERR_CALLBACK;
  /* Source size already bounds the copy; no insertion allocation can fail. */
  ds_fibonacci_heap_insert(policy->copy, node);
  return DS_OK;
}
ds_status_t ds_fibonacci_heap_clone_with(const ds_fibonacci_heap_t *heap,
                                         ds_fibonacci_heap_clone_fn clone,
                                         ds_fibonacci_heap_destroy_fn destroy,
                                         void *user, ds_context_t *context,
                                         ds_fibonacci_heap_t **out) {
  fib_clone_policy_t policy;
  ds_status_t status;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (heap == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  policy.copy =
      fib_create_internal(heap->compare, heap->allocator, heap->deallocator,
                          context != NULL ? context : heap->context,
                          heap->compare_with, heap->compare_user);
  if (policy.copy == NULL)
    return DS_ERR_ALLOC;
  policy.clone = clone;
  policy.user = user;
  status = ds_fibonacci_heap_walk_with(heap, fib_clone_visit, &policy);
  if (status != DS_OK) {
    ds_fibonacci_heap_destroy_with(policy.copy, destroy, user);
    return status;
  }
  *out = policy.copy;
  return DS_OK;
}

/* ---- the item heap ------------------------------------------------------ */

struct ds_fib_item {
  ds_fib_item_t *parent;
  ds_fib_item_t *child;
  ds_fib_item_t *left;
  ds_fib_item_t *right;
  size_t degree;
  unsigned char mark;
  unsigned char forced; /* below everything while it is being taken out */
};

static size_t fh_at(void) {
  return (sizeof(ds_fib_item_t) + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN *
         DS_MAX_ALIGN;
}

static unsigned char *fh_item(ds_fib_item_t *x) {
  return (unsigned char *)(void *)x + fh_at();
}

static const unsigned char *fh_citem(const ds_fib_item_t *x) {
  return (const unsigned char *)(const void *)x + fh_at();
}

static int fh_cmp(const ds_fib_heap_t *h, const ds_fib_item_t *a,
                  const ds_fib_item_t *b) {
  if (a->forced) {
    return -1;
  }
  if (b->forced) {
    return 1;
  }
  return h->compare(fh_citem(a), fh_citem(b), h->user);
}

/* x (a ring of its own) joins the ring of list, to its right */
static void fh_ring_insert(ds_fib_item_t *list, ds_fib_item_t *x) {
  x->left = list;
  x->right = list->right;
  list->right->left = x;
  list->right = x;
}

static void fh_ring_remove(ds_fib_item_t *x) {
  x->left->right = x->right;
  x->right->left = x->left;
  x->left = x;
  x->right = x;
}

static void fh_add_root(ds_fib_heap_t *h, ds_fib_item_t *x) {
  x->parent = NULL;
  x->mark = 0;
  if (h->min == NULL) {
    x->left = x;
    x->right = x;
    h->min = x;
    return;
  }
  fh_ring_insert(h->min, x);
  if (fh_cmp(h, x, h->min) < 0) {
    h->min = x;
  }
}

ds_status_t ds_fib_heap_init(ds_fib_heap_t *h, size_t item_size,
                             ds_fib_compare_fn compare, void *user,
                             ds_context_t *context) {
  size_t i;
  if (h == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (item_size == 0 || item_size > (size_t)-1 - fh_at()) {
    return DS_ERR_INVALID;
  }
  h->min = NULL;
  h->size = 0;
  h->item_size = item_size;
  h->compare = compare;
  h->user = user;
  h->context = context;
  for (i = 0; i < DS_FIB_MAX_DEGREE; i++) {
    h->table[i] = NULL;
  }
  return DS_OK;
}

void ds_fib_heap_destroy(ds_fib_heap_t *h) {
  ds_fib_item_t *chain = NULL;
  if (h == NULL) {
    return;
  }
  if (h->min != NULL) {
    chain = h->min;
    chain->left->right = NULL;
  }
  while (chain != NULL) {
    ds_fib_item_t *x = chain;
    chain = x->right;
    if (x->child != NULL) {
      x->child->left->right = chain;
      chain = x->child;
    }
    ds_context_free(h->context, x);
  }
  h->min = NULL;
  h->size = 0;
}

ds_status_t ds_fib_heap_push(ds_fib_heap_t *h, const void *item,
                             ds_fib_item_t **out_handle) {
  ds_fib_item_t *x;
  if (h == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  x = (ds_fib_item_t *)ds_context_alloc(h->context, fh_at() + h->item_size);
  if (x == NULL) {
    return DS_ERR_ALLOC;
  }
  x->child = NULL;
  x->degree = 0;
  x->forced = 0;
  memcpy(fh_item(x), item, h->item_size);
  fh_add_root(h, x);
  h->size++;
  if (out_handle != NULL) {
    *out_handle = x;
  }
  return DS_OK;
}

const void *ds_fib_heap_top(const ds_fib_heap_t *h) {
  return h == NULL || h->min == NULL ? NULL : fh_citem(h->min);
}

size_t ds_fib_heap_size(const ds_fib_heap_t *h) {
  return h == NULL ? 0 : h->size;
}

const void *ds_fib_heap_item(const ds_fib_heap_t *h,
                             const ds_fib_item_t *handle) {
  (void)h;
  return handle == NULL ? NULL : fh_citem(handle);
}

/* the root ring's trees, linked by degree until all degrees differ */
static void fh_consolidate(ds_fib_heap_t *h) {
  ds_fib_item_t *chain = h->min;
  size_t i;
  if (chain == NULL)
    return;
  chain->left->right = NULL;
  h->min = NULL;
  while (chain != NULL) {
    ds_fib_item_t *x = chain;
    size_t d;
    chain = chain->right;
    x->left = x;
    x->right = x;
    d = x->degree;
    while (h->table[d] != NULL) {
      ds_fib_item_t *y = h->table[d];
      if (fh_cmp(h, y, x) < 0) {
        ds_fib_item_t *t = x;
        x = y;
        y = t;
      }
      y->parent = x; /* y under x */
      y->mark = 0;
      if (x->child == NULL) {
        x->child = y;
      } else {
        fh_ring_insert(x->child, y);
      }
      x->degree++;
      h->table[d] = NULL;
      d++;
    }
    h->table[d] = x;
  }
  for (i = 0; i < DS_FIB_MAX_DEGREE; i++) {
    if (h->table[i] != NULL) {
      fh_add_root(h, h->table[i]);
      h->table[i] = NULL;
    }
  }
}

/* the minimum leaves (its node is not freed) */
static ds_fib_item_t *fh_extract(ds_fib_heap_t *h) {
  ds_fib_item_t *z = h->min;
  ds_fib_item_t *c = z->child;
  if (c != NULL) { /* the children become roots */
    ds_fib_item_t *last = c->left;
    ds_fib_item_t *x = c;
    for (;;) {
      ds_fib_item_t *next = x->right;
      x->parent = NULL;
      x->mark = 0;
      if (x == last) {
        break;
      }
      x = next;
    }
    /* splice the child ring into the root ring after z */
    last->right = z->right;
    z->right->left = last;
    z->right = c;
    c->left = z;
    z->child = NULL;
    z->degree = 0;
  }
  if (z->right == z) {
    h->min = NULL;
  } else {
    h->min = z->right;
    fh_ring_remove(z);
    fh_consolidate(h);
  }
  h->size--;
  return z;
}

ds_status_t ds_fib_heap_pop(ds_fib_heap_t *h, void *out_item) {
  ds_fib_item_t *z;
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (h->min == NULL) {
    return DS_EMPTY;
  }
  z = fh_extract(h);
  if (out_item != NULL) {
    memcpy(out_item, fh_item(z), h->item_size);
  }
  ds_context_free(h->context, z);
  return DS_OK;
}

/* x (no longer heavier than its parent) moves to the root ring, and the
 * marked ancestors above it follow */
static void fh_decrease(ds_fib_heap_t *h, ds_fib_item_t *x) {
  ds_fib_item_t *y = x->parent;
  if (y != NULL && fh_cmp(h, x, y) < 0) {
    for (;;) {
      if (y->child == x) {
        y->child = x->right == x ? NULL : x->right;
      }
      fh_ring_remove(x);
      y->degree--;
      fh_add_root(h, x);
      if (y->parent == NULL) {
        break;
      }
      if (!y->mark) {
        y->mark = 1;
        break;
      }
      x = y; /* a marked parent is cut in turn */
      y = x->parent;
    }
  }
  if (fh_cmp(h, x, h->min) < 0) {
    h->min = x;
  }
}

ds_status_t ds_fib_heap_update(ds_fib_heap_t *h, ds_fib_item_t *handle,
                               const void *item) {
  int c;
  if (h == NULL || handle == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  c = h->compare(item, fh_citem(handle), h->user);
  memcpy(fh_item(handle), item, h->item_size);
  if (c < 0) {
    fh_decrease(h, handle);
    if (fh_cmp(h, handle, h->min) < 0) {
      h->min = handle;
    }
  } else if (c > 0) { /* out and back in, in its own node */
    ds_fib_item_t *z;
    handle->forced = 1;
    fh_decrease(h, handle);
    h->min = handle;
    z = fh_extract(h);
    z->forced = 0;
    z->child = NULL;
    z->degree = 0;
    fh_add_root(h, z);
    h->size++;
  }
  return DS_OK;
}

ds_status_t ds_fib_heap_remove(ds_fib_heap_t *h, ds_fib_item_t *handle,
                               void *out_item) {
  ds_fib_item_t *z;
  if (h == NULL || handle == NULL) {
    return DS_ERR_NULL;
  }
  handle->forced = 1;
  fh_decrease(h, handle);
  h->min = handle;
  z = fh_extract(h);
  if (out_item != NULL) {
    memcpy(out_item, fh_item(z), h->item_size);
  }
  ds_context_free(h->context, z);
  return DS_OK;
}

void ds_fib_heap_meld(ds_fib_heap_t *h, ds_fib_heap_t *other) {
  if (h == NULL || other == NULL || other == h || other->min == NULL) {
    return;
  }
  if (h->min == NULL) {
    h->min = other->min;
  } else {
    ds_fib_item_t *a = h->min->right;
    ds_fib_item_t *b = other->min->left;
    h->min->right = other->min;
    other->min->left = h->min;
    a->left = b;
    b->right = a;
    if (fh_cmp(h, other->min, h->min) < 0) {
      h->min = other->min;
    }
  }
  h->size += other->size;
  other->min = NULL;
  other->size = 0;
}

bool ds_fib_heap_check(const ds_fib_heap_t *h) {
  const ds_fib_item_t *x;
  size_t count = 0;
  if (h == NULL) {
    return false;
  }
  if (h->min == NULL) {
    return h->size == 0;
  }
  /* depth first over the root ring and every child ring, by the links */
  x = h->min;
  for (;;) {
    const ds_fib_item_t *c = x->child;
    size_t deg = 0;
    if (++count > h->size || x->right->left != x || x->left->right != x ||
        x->forced || x->degree >= DS_FIB_MAX_DEGREE) {
      return false;
    }
    if (x->parent == NULL) {
      if (x->mark || fh_cmp(h, x, h->min) < 0) {
        return false; /* roots are unmarked, none below the minimum */
      }
    } else if (fh_cmp(h, x, x->parent) < 0) {
      return false; /* heap order */
    }
    if (c != NULL) {
      do {
        if (c->parent != x || ++deg > h->size) {
          return false;
        }
        c = c->right;
      } while (c != x->child);
      if (deg != x->degree) {
        return false;
      }
      x = x->child;
      continue;
    }
    if (x->degree != 0) {
      return false;
    }
    /* the next node: a right sibling, else up until there is one */
    for (;;) {
      const ds_fib_item_t *first =
          x->parent != NULL ? x->parent->child : h->min;
      if (x->right != first) {
        x = x->right;
        break;
      }
      if (x->parent == NULL) {
        return count == h->size;
      }
      x = x->parent;
    }
  }
}
