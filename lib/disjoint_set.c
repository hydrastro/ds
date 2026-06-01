#include "disjoint_set.h"
#include <stdlib.h>

ds_disjoint_set_t *FUNC(ds_disjoint_set_create)(void) {
  return FUNC(ds_disjoint_set_create_alloc)(malloc, free);
}

ds_disjoint_set_t *
FUNC(ds_disjoint_set_create_alloc)(void *(*allocator)(size_t),
                                   void (*deallocator)(void *)) {
  ds_disjoint_set_t *set =
      (ds_disjoint_set_t *)allocator(sizeof(ds_disjoint_set_t));
  set->allocator = allocator;
  set->deallocator = deallocator;
  set->count = 0;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(set);
#endif
  return set;
}

void FUNC(ds_disjoint_set_make)(ds_disjoint_set_t *set,
                                ds_disjoint_set_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(set);
#endif
  node->parent = node;
  node->rank = 0;
  set->count += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(set);
#endif
}

ds_disjoint_set_node_t *
FUNC(ds_disjoint_set_find)(ds_disjoint_set_t *set,
                           ds_disjoint_set_node_t *node) {
  ds_disjoint_set_node_t *root;
  ds_disjoint_set_node_t *current;
  (void)set;
#ifdef DS_THREAD_SAFE
  LOCK(set);
#endif
  root = node;
  while (root->parent != root) {
    root = root->parent;
  }
  current = node;
  while (current->parent != root) {
    ds_disjoint_set_node_t *next = current->parent;
    current->parent = root;
    current = next;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(set);
#endif
  return root;
}

bool FUNC(ds_disjoint_set_union)(ds_disjoint_set_t *set,
                                 ds_disjoint_set_node_t *a,
                                 ds_disjoint_set_node_t *b) {
  ds_disjoint_set_node_t *root_a;
  ds_disjoint_set_node_t *root_b;
#ifdef DS_THREAD_SAFE
  LOCK(set);
#endif
  root_a = FUNC(ds_disjoint_set_find)(set, a);
  root_b = FUNC(ds_disjoint_set_find)(set, b);
  if (root_a == root_b) {
#ifdef DS_THREAD_SAFE
    UNLOCK(set);
#endif
    return false;
  }
  if (root_a->rank < root_b->rank) {
    root_a->parent = root_b;
  } else if (root_a->rank > root_b->rank) {
    root_b->parent = root_a;
  } else {
    root_b->parent = root_a;
    root_a->rank += 1;
  }
  if (set->count > 0) {
    set->count -= 1;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(set);
#endif
  return true;
}

bool FUNC(ds_disjoint_set_connected)(ds_disjoint_set_t *set,
                                     ds_disjoint_set_node_t *a,
                                     ds_disjoint_set_node_t *b) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(set);
#endif
  result =
      FUNC(ds_disjoint_set_find)(set, a) == FUNC(ds_disjoint_set_find)(set, b);
#ifdef DS_THREAD_SAFE
  UNLOCK(set);
#endif
  return result;
}

size_t FUNC(ds_disjoint_set_count)(ds_disjoint_set_t *set) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(set);
#endif
  result = set->count;
#ifdef DS_THREAD_SAFE
  UNLOCK(set);
#endif
  return result;
}

void FUNC(ds_disjoint_set_destroy)(ds_disjoint_set_t *set) {
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(set);
#endif
  set->deallocator(set);
}
