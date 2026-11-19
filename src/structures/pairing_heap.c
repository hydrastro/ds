#include <dsa/structures/pairing_heap.h>
#include <stdlib.h>

static int pairing_heap_compare(ds_pairing_heap_t *owner,
                                ds_pairing_heap_node_t *a,
                                ds_pairing_heap_node_t *b) {
  return owner->compare_with != NULL
             ? owner->compare_with(a, b, owner->compare_user)
             : owner->compare(a, b);
}

static ds_pairing_heap_t *pairing_heap_create_internal(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *),
    ds_context_t *context, ds_pairing_heap_compare_fn compare_with,
    void *compare_user) {
  ds_pairing_heap_t *heap;
  if (compare == NULL && compare_with == NULL) {
    return NULL;
  }
  heap = (ds_pairing_heap_t *)ds_context_alloc(context,
                                               sizeof(ds_pairing_heap_t));
  if (heap == NULL) {
    return NULL;
  }
  heap->context = context != NULL ? context : ds_default_context();
  heap->compare_with = compare_with;
  heap->compare_user = compare_user;

  heap->compare = compare;
  heap->nil = (ds_pairing_heap_node_t *)ds_context_alloc(
      context, sizeof(ds_pairing_heap_node_t));
  if (heap->nil == NULL) {
    ds_context_free(context, heap);
    return NULL;
  }
  heap->nil->child = NULL;
  heap->nil->sibling = NULL;
  heap->nil->prev = NULL;
  heap->root = NULL;
  heap->size = 0;
  return heap;
}
ds_pairing_heap_t *ds_pairing_heap_create(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *),
    ds_context_t *context) {
  return pairing_heap_create_internal(compare, context, NULL, NULL);
}
ds_status_t ds_pairing_heap_create_context(ds_pairing_heap_compare_fn compare,
                                           void *user, ds_context_t *context,
                                           ds_pairing_heap_t **out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (compare == NULL) {
    return DS_ERR_NULL;
  }

  *out = pairing_heap_create_internal(
      NULL, context != NULL ? context : ds_default_context(), compare, user);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_pairing_heap_node_init(ds_pairing_heap_node_t *node) {
  node->child = NULL;
  node->sibling = NULL;
  node->prev = NULL;
}

/*
 * Internal child/sibling/prev links use NULL as the terminator so that nodes
 * are independent of any particular heap's sentinel. heap->nil is only used as
 * the public "empty"/"not found" return value.
 */
static ds_pairing_heap_node_t *pairing_heap_link(ds_pairing_heap_t *heap,
                                                 ds_pairing_heap_node_t *a,
                                                 ds_pairing_heap_node_t *b) {
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  if (pairing_heap_compare(heap, b, a) < 0) {
    ds_pairing_heap_node_t *temp = a;
    a = b;
    b = temp;
  }
  b->prev = a;
  b->sibling = a->child;
  if (a->child != NULL) {
    a->child->prev = b;
  }
  a->child = b;
  a->sibling = NULL;
  a->prev = NULL;
  return a;
}

void ds_pairing_heap_insert(ds_pairing_heap_t *heap,
                            ds_pairing_heap_node_t *node) {
  if (node == heap->nil || node == NULL) {
    return;
  }
  node->child = NULL;
  node->sibling = NULL;
  node->prev = NULL;
  heap->root = pairing_heap_link(heap, heap->root, node);
  heap->size += 1;
}

ds_pairing_heap_node_t *ds_pairing_heap_peek(ds_pairing_heap_t *heap) {
  ds_pairing_heap_node_t *result;
  result = heap->root != NULL ? heap->root : heap->nil;
  return result;
}

static ds_pairing_heap_node_t *
pairing_heap_combine_siblings(ds_pairing_heap_t *heap,
                              ds_pairing_heap_node_t *first) {
  ds_pairing_heap_node_t *pairs;
  ds_pairing_heap_node_t *result;
  ds_pairing_heap_node_t *current;
  if (first == NULL || first->sibling == NULL) {
    return first;
  }
  pairs = NULL;
  current = first;
  /* first pass: link consecutive pairs, stacking results into pairs list */
  while (current != NULL) {
    ds_pairing_heap_node_t *a = current;
    ds_pairing_heap_node_t *b = current->sibling;
    if (b != NULL) {
      ds_pairing_heap_node_t *next = b->sibling;
      ds_pairing_heap_node_t *linked;
      a->sibling = NULL;
      a->prev = NULL;
      b->sibling = NULL;
      b->prev = NULL;
      linked = pairing_heap_link(heap, a, b);
      linked->sibling = pairs;
      pairs = linked;
      current = next;
    } else {
      a->sibling = pairs;
      pairs = a;
      current = NULL;
    }
  }
  /* second pass: merge the pairs list into a single tree */
  result = NULL;
  while (pairs != NULL) {
    ds_pairing_heap_node_t *next = pairs->sibling;
    pairs->sibling = NULL;
    pairs->prev = NULL;
    result = pairing_heap_link(heap, result, pairs);
    pairs = next;
  }
  return result;
}

ds_pairing_heap_node_t *ds_pairing_heap_extract(ds_pairing_heap_t *heap) {
  ds_pairing_heap_node_t *root;
  ds_pairing_heap_node_t *child;
  if (heap->root == NULL) {
    return heap->nil;
  }
  root = heap->root;
  child = root->child;
  if (child != NULL) {
    child->prev = NULL;
  }
  heap->root = pairing_heap_combine_siblings(heap, child);
  if (heap->root != NULL) {
    heap->root->prev = NULL;
    heap->root->sibling = NULL;
  }
  root->child = NULL;
  root->sibling = NULL;
  root->prev = NULL;
  heap->size -= 1;
  return root;
}

void ds_pairing_heap_decrease_key(ds_pairing_heap_t *heap,
                                  ds_pairing_heap_node_t *node) {
  if (node == heap->nil || node == NULL || node == heap->root) {
    return;
  }
  /* detach node (with its subtree) from its current position */
  if (node->prev != NULL) {
    if (node->prev->child == node) {
      node->prev->child = node->sibling;
    } else {
      node->prev->sibling = node->sibling;
    }
    if (node->sibling != NULL) {
      node->sibling->prev = node->prev;
    }
  }
  node->sibling = NULL;
  node->prev = NULL;
  heap->root = pairing_heap_link(heap, heap->root, node);
}

bool ds_pairing_heap_is_empty(ds_pairing_heap_t *heap) {
  bool result;
  result = heap->root == NULL;
  return result;
}

size_t ds_pairing_heap_size(ds_pairing_heap_t *heap) {
  size_t result;
  result = heap->size;
  return result;
}

ds_status_t ds_pairing_heap_merge_ex(ds_pairing_heap_t *heap,
                                     ds_pairing_heap_t *other) {
  if (heap == NULL || other == NULL) {
    return DS_ERR_NULL;
  }
  if (heap == other) {
    return DS_ERR_INVALID;
  }
  if (heap->compare != other->compare ||
      heap->compare_with != other->compare_with ||
      heap->compare_user != other->compare_user) {
    return DS_ERR_INVALID;
  }
  if (other->size > (size_t)-1 - heap->size) {
    return DS_ERR_OVERFLOW;
  }
  heap->root = pairing_heap_link(heap, heap->root, other->root);
  heap->size += other->size;
  other->root = NULL;
  other->size = 0;
  return DS_OK;
}
void ds_pairing_heap_merge(ds_pairing_heap_t *heap, ds_pairing_heap_t *other) {
  (void)ds_pairing_heap_merge_ex(heap, other);
}
ds_status_t ds_pairing_heap_extract_ex(ds_pairing_heap_t *heap,
                                       ds_pairing_heap_node_t **out) {
  if (heap == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (heap->size == 0) {
    return DS_EMPTY;
  }
  *out = ds_pairing_heap_extract(heap);
  return DS_OK;
}

/* parent of node: the prev link of the first node in its sibling list */
static ds_pairing_heap_node_t *
pairing_heap_parent(ds_pairing_heap_node_t *node) {
  while (node->prev != NULL && node->prev->child != node) {
    node = node->prev;
  }
  return node->prev;
}

/* preorder without recursion (child chains can be linear in length):
 * descend through child links, climb back through prev links */
void ds_pairing_heap_walk(ds_pairing_heap_t *heap,
                          void (*callback)(ds_pairing_heap_node_t *)) {
  ds_pairing_heap_node_t *node = heap->root;
  while (node != NULL) {
    callback(node);
    if (node->child != NULL) {
      node = node->child;
      continue;
    }
    while (node != NULL && node->sibling == NULL) {
      node = pairing_heap_parent(node);
    }
    if (node != NULL) {
      node = node->sibling;
    }
  }
}

static void pairing_heap_destroy_internal(ds_pairing_heap_t *heap,
                                          ds_pairing_heap_destroy_fn destroy,
                                          void *user) {
  ds_pairing_heap_node_t *work = heap->root;
  /* splice each node's child list onto the work list, then drop it */
  while (work != NULL) {
    ds_pairing_heap_node_t *node = work;
    work = node->sibling;
    if (node->child != NULL) {
      ds_pairing_heap_node_t *last = node->child;
      while (last->sibling != NULL) {
        last = last->sibling;
      }
      last->sibling = work;
      work = node->child;
    }
    if (destroy != NULL) {
      destroy(node, user);
    }
  }
  ds_context_free(heap->context, heap->nil);
  ds_context_free(heap->context, heap);
}
void ds_pairing_heap_destroy_with(ds_pairing_heap_t *container,
                                  ds_pairing_heap_destroy_fn destroy,
                                  void *user) {
  if (container != NULL) {
    pairing_heap_destroy_internal(container, destroy, user);
  }
}
static void pairing_heap_destroy_direct(ds_pairing_heap_node_t *node,
                                        void *user) {
  void (**destroy)(ds_pairing_heap_node_t *) =
      (void (**)(ds_pairing_heap_node_t *))user;
  (*destroy)(node);
}
void ds_pairing_heap_destroy(ds_pairing_heap_t *container,
                             void (*destroy)(ds_pairing_heap_node_t *)) {
  ds_pairing_heap_destroy_with(
      container, destroy != NULL ? pairing_heap_destroy_direct : NULL,
      &destroy);
}

ds_status_t ds_pairing_heap_walk_with(const ds_pairing_heap_t *container,
                                      ds_pairing_heap_visit_fn visit,
                                      void *user) {
  ds_pairing_heap_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  node = container->root;
  while (node != NULL) {
    st = visit(node, user);
    if (st != DS_OK) {
      return st;
    }
    if (node->child != NULL) {
      node = node->child;
      continue;
    }
    while (node != NULL && node->sibling == NULL) {
      node = pairing_heap_parent(node);
    }
    if (node != NULL) {
      node = node->sibling;
    }
  }
  return DS_OK;
}

ds_status_t ds_pairing_heap_insert_ex(ds_pairing_heap_t *container,
                                      ds_pairing_heap_node_t *node) {
  if (container == NULL || node == NULL) {
    return DS_ERR_NULL;
  }
  if (node == container->nil) {
    return DS_ERR_INVALID;
  }
  if (container->size == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  ds_pairing_heap_insert(container, node);
  return DS_OK;
}

typedef struct pairing_heap_clone_policy {
  ds_pairing_heap_t *copy;
  ds_pairing_heap_clone_fn clone;
  ds_pairing_heap_destroy_fn destroy;
  void *user;
} pairing_heap_clone_policy_t;
static ds_status_t pairing_heap_clone_visit(ds_pairing_heap_node_t *source,
                                            void *user) {
  pairing_heap_clone_policy_t *policy = (pairing_heap_clone_policy_t *)user;
  ds_pairing_heap_node_t *node = NULL;
  ds_status_t st = policy->clone(source, &node, policy->user);
  if (st != DS_OK || node == NULL) {
    return st < 0 ? st : DS_ERR_CALLBACK;
  }
  st = ds_pairing_heap_insert_ex(policy->copy, node);
  if (st != DS_OK) {
    policy->destroy(node, policy->user);
    return st < 0 ? st : DS_ERR_CALLBACK;
  }
  return DS_OK;
}
ds_status_t ds_pairing_heap_clone_with(const ds_pairing_heap_t *container,
                                       ds_pairing_heap_clone_fn clone,
                                       ds_pairing_heap_destroy_fn destroy,
                                       void *user, ds_context_t *context,
                                       ds_pairing_heap_t **out) {
  pairing_heap_clone_policy_t policy;
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL) {
    return DS_ERR_NULL;
  }
  policy.copy = pairing_heap_create_internal(
      container->compare, context != NULL ? context : container->context,
      container->compare_with, container->compare_user);
  if (policy.copy == NULL) {
    return DS_ERR_ALLOC;
  }
  policy.clone = clone;
  policy.destroy = destroy;
  policy.user = user;
  st = ds_pairing_heap_walk_with(container, pairing_heap_clone_visit, &policy);
  if (st != DS_OK) {
    ds_pairing_heap_destroy_with(policy.copy, destroy, user);
    return st;
  }
  *out = policy.copy;
  return DS_OK;
}
