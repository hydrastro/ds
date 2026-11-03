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
  return set;
}

void FUNC(ds_disjoint_set_make)(ds_disjoint_set_t *set,
                                ds_disjoint_set_node_t *node) {
  node->parent = node;
  node->rank = 0;
  set->count += 1;
}

ds_disjoint_set_node_t *
FUNC(ds_disjoint_set_find)(ds_disjoint_set_t *set,
                           ds_disjoint_set_node_t *node) {
  ds_disjoint_set_node_t *root;
  ds_disjoint_set_node_t *current;
  (void)set;
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
  return root;
}

bool FUNC(ds_disjoint_set_union)(ds_disjoint_set_t *set,
                                 ds_disjoint_set_node_t *a,
                                 ds_disjoint_set_node_t *b) {
  ds_disjoint_set_node_t *root_a;
  ds_disjoint_set_node_t *root_b;
  root_a = FUNC(ds_disjoint_set_find)(set, a);
  root_b = FUNC(ds_disjoint_set_find)(set, b);
  if (root_a == root_b) {
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
  return true;
}

bool FUNC(ds_disjoint_set_connected)(ds_disjoint_set_t *set,
                                     ds_disjoint_set_node_t *a,
                                     ds_disjoint_set_node_t *b) {
  bool result;
  result =
      FUNC(ds_disjoint_set_find)(set, a) == FUNC(ds_disjoint_set_find)(set, b);
  return result;
}

size_t FUNC(ds_disjoint_set_count)(ds_disjoint_set_t *set) {
  size_t result;
  result = set->count;
  return result;
}

void FUNC(ds_disjoint_set_destroy)(ds_disjoint_set_t *set) {
  set->deallocator(set);
}
