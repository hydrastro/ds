#include <dsa/structures/disjoint_set.h>
#include <stdlib.h>

static ds_disjoint_set_t *disjoint_set_create_internal(ds_context_t *context) {
  ds_disjoint_set_t *set;

  set = (ds_disjoint_set_t *)ds_context_alloc(context,
                                              sizeof(ds_disjoint_set_t));
  if (set == NULL) {
    return NULL;
  }
  set->context = context != NULL ? context : ds_default_context();

  set->count = 0;
  return set;
}

ds_disjoint_set_t *ds_disjoint_set_create(ds_context_t *context) {
  return disjoint_set_create_internal(context);
}
ds_status_t ds_disjoint_set_create_context(ds_context_t *context,
                                           ds_disjoint_set_t **out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = disjoint_set_create_internal(context != NULL ? context
                                                      : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_disjoint_set_make(ds_disjoint_set_t *set,
                          ds_disjoint_set_node_t *node) {
  node->parent = node;
  node->rank = 0;
  set->count += 1;
}

ds_disjoint_set_node_t *ds_disjoint_set_find(ds_disjoint_set_t *set,
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

bool ds_disjoint_set_union(ds_disjoint_set_t *set, ds_disjoint_set_node_t *a,
                           ds_disjoint_set_node_t *b) {
  ds_disjoint_set_node_t *root_a;
  ds_disjoint_set_node_t *root_b;
  root_a = ds_disjoint_set_find(set, a);
  root_b = ds_disjoint_set_find(set, b);
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

bool ds_disjoint_set_connected(ds_disjoint_set_t *set,
                               ds_disjoint_set_node_t *a,
                               ds_disjoint_set_node_t *b) {
  bool result;
  result = ds_disjoint_set_find(set, a) == ds_disjoint_set_find(set, b);
  return result;
}

size_t ds_disjoint_set_count(ds_disjoint_set_t *set) {
  size_t result;
  result = set->count;
  return result;
}

void ds_disjoint_set_destroy(ds_disjoint_set_t *set) {
  ds_context_free(set->context, set);
}

ds_status_t ds_disjoint_set_make_ex(ds_disjoint_set_t *set,
                                    ds_disjoint_set_node_t *node) {
  if (set == NULL || node == NULL) {
    return DS_ERR_NULL;
  }
  if (set->count == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  ds_disjoint_set_make(set, node);
  return DS_OK;
}
ds_status_t ds_disjoint_set_find_ex(ds_disjoint_set_t *set,
                                    ds_disjoint_set_node_t *node,
                                    ds_disjoint_set_node_t **out) {
  if (set == NULL || node == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (set->count == 0) {
    return DS_ERR_STATE;
  }
  *out = ds_disjoint_set_find(set, node);
  return DS_OK;
}
ds_status_t ds_disjoint_set_union_ex(ds_disjoint_set_t *set,
                                     ds_disjoint_set_node_t *a,
                                     ds_disjoint_set_node_t *b) {
  ds_disjoint_set_node_t *ra, *rb;
  if (set == NULL || a == NULL || b == NULL) {
    return DS_ERR_NULL;
  }
  if (set->count == 0) {
    return DS_ERR_STATE;
  }
  ra = ds_disjoint_set_find(set, a);
  rb = ds_disjoint_set_find(set, b);
  if (ra == rb) {
    return DS_EXISTS;
  }
  if (set->count < 2) {
    return DS_ERR_STATE;
  }
  (void)ds_disjoint_set_union(set, ra, rb);
  return DS_OK;
}
