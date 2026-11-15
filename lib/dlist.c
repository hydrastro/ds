#include "dlist.h"
#include <stdio.h>
#include <stdlib.h>

ds_dlist_t *ds_dlist_create(void) {
  return ds_dlist_create_alloc(malloc, free);
}

ds_dlist_t *ds_dlist_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  ds_dlist_t *list;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  list = (ds_dlist_t *)allocator(sizeof(ds_dlist_t));
  if (list == NULL) {
    return NULL;
  }
  list->allocator = allocator;
  list->deallocator = deallocator;
  list->nil = (ds_dlist_node_t *)allocator(sizeof(ds_dlist_node_t));
  if (list->nil == NULL) {
    deallocator(list);
    return NULL;
  }
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->prev = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
  return list;
}

void ds_dlist_append(ds_dlist_t *list, ds_dlist_node_t *node) {
  ds_dlist_insert_after(list, node, list->tail);
}

void ds_dlist_prepend(ds_dlist_t *list, ds_dlist_node_t *node) {
  ds_dlist_insert_before(list, node, list->head);
}

ds_dlist_node_t *ds_dlist_search(ds_dlist_t *list, ds_dlist_node_t *node,
                                 int (*compare)(ds_dlist_node_t *,
                                                ds_dlist_node_t *)) {
  ds_dlist_node_t *head;
  head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
    return list->nil;
  }
  return head;
}

void ds_dlist_insert_before(ds_dlist_t *list, ds_dlist_node_t *node,
                            ds_dlist_node_t *next) {
  node->next = next;
  node->prev = next->prev;
  next->prev->next = node;
  next->prev = node;

  if (next == list->head) {
    list->head = node;
  }

  if (list->tail == list->nil) {
    list->tail = node;
  }
  list->size += 1;
}

void ds_dlist_insert_after(ds_dlist_t *list, ds_dlist_node_t *node,
                           ds_dlist_node_t *prev) {
  node->next = prev->next;
  node->prev = prev;
  prev->next->prev = node;
  prev->next = node;

  if (prev == list->tail) {
    list->tail = node;
  }

  if (list->head == list->nil) {
    list->head = node;
  }
  list->size += 1;
}

void ds_dlist_delete_node(ds_dlist_t *list, ds_dlist_node_t *node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;

  if (node == list->head) {
    list->head = node->next;
  }

  if (node == list->tail) {
    list->tail = node->prev;
  }
  list->size -= 1;
}

void ds_dlist_delete(ds_dlist_t *list) {
  list->head = list->tail = list->nil;
  list->size = 0;
}

void ds_dlist_destroy_node(ds_dlist_t *list, ds_dlist_node_t *node,
                           void (*destroy)(ds_dlist_node_t *)) {
  ds_dlist_delete_node(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_dlist_destroy(ds_dlist_t *list, void (*destroy)(ds_dlist_node_t *)) {
  ds_dlist_node_t *node = list->head;
  while (node != list->nil) {
    ds_dlist_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
  list->deallocator(list->nil);
  list->deallocator(list);
}

void ds_dlist_walk_forward(ds_dlist_t *list, ds_dlist_node_t *node,
                           void (*callback)(ds_dlist_node_t *)) {
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->next;
  }
}

void ds_dlist_walk_backwards(ds_dlist_t *list, ds_dlist_node_t *node,
                             void (*callback)(ds_dlist_node_t *)) {
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->prev;
  }
}

bool ds_dlist_is_empty(ds_dlist_t *list) {
  bool result;
  result = list->size == 0;
  return result;
}

/* clones of the source's nodes, in order, added to copy; false when
 * clone_node returns NULL (the clones made so far are in copy) */
static bool
dlist_clone_nodes(const ds_dlist_t *list, ds_dlist_t *copy,
                  ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *)) {
  ds_dlist_node_t *current;
  for (current = list->head; current != list->nil; current = current->next) {
    ds_dlist_node_t *node = clone_node(current);
    if (node == NULL) {
      return false;
    }
    ds_dlist_append(copy, node);
  }
  return true;
}

ds_dlist_t *ds_dlist_clone(ds_dlist_t *list,
                           ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *)) {
  ds_dlist_t *copy;
  if (list == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = ds_dlist_create_alloc(list->allocator, list->deallocator);
  if (copy != NULL && !dlist_clone_nodes(list, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_dlist_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_dlist_clone_ex(const ds_dlist_t *list,
                              ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *),
                              void (*destroy_clone)(ds_dlist_node_t *),
                              ds_dlist_t **out) {
  ds_dlist_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (list == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = ds_dlist_create_alloc(list->allocator, list->deallocator);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!dlist_clone_nodes(list, copy, clone_node)) {
    ds_dlist_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}
