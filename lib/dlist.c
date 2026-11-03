#include "dlist.h"
#include <stdio.h>
#include <stdlib.h>

ds_dlist_t *FUNC(ds_dlist_create)(void) {
  return FUNC(ds_dlist_create_alloc)(malloc, free);
}

ds_dlist_t *FUNC(ds_dlist_create_alloc)(void *(*allocator)(size_t),
                                        void (*deallocator)(void *)) {
  ds_dlist_t *list = (ds_dlist_t *)allocator(sizeof(ds_dlist_t));
  list->allocator = allocator;
  list->deallocator = deallocator;
  list->nil = (ds_dlist_node_t *)allocator(sizeof(ds_dlist_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->prev = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
  return list;
}

void FUNC(ds_dlist_append)(ds_dlist_t *list, ds_dlist_node_t *node) {
  FUNC(ds_dlist_insert_after)(list, node, list->tail);
}

void FUNC(ds_dlist_prepend)(ds_dlist_t *list, ds_dlist_node_t *node) {
  FUNC(ds_dlist_insert_before)(list, node, list->head);
}

ds_dlist_node_t *FUNC(ds_dlist_search)(ds_dlist_t *list, ds_dlist_node_t *node,
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

void FUNC(ds_dlist_insert_before)(ds_dlist_t *list, ds_dlist_node_t *node,
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

void FUNC(ds_dlist_insert_after)(ds_dlist_t *list, ds_dlist_node_t *node,
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

void FUNC(ds_dlist_delete_node)(ds_dlist_t *list, ds_dlist_node_t *node) {
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

void FUNC(ds_dlist_delete)(ds_dlist_t *list) {
  list->head = list->tail = list->nil;
  list->size = 0;
}

void FUNC(ds_dlist_destroy_node)(ds_dlist_t *list, ds_dlist_node_t *node,
                                 void (*destroy)(ds_dlist_node_t *)) {
  FUNC(ds_dlist_delete_node)(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void FUNC(ds_dlist_destroy)(ds_dlist_t *list,
                            void (*destroy)(ds_dlist_node_t *)) {
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

void FUNC(ds_dlist_walk_forward)(ds_dlist_t *list, ds_dlist_node_t *node,
                                 void (*callback)(ds_dlist_node_t *)) {
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->next;
  }
}

void FUNC(ds_dlist_walk_backwards)(ds_dlist_t *list, ds_dlist_node_t *node,
                                   void (*callback)(ds_dlist_node_t *)) {
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->prev;
  }
}

bool FUNC(ds_dlist_is_empty)(ds_dlist_t *list) {
  bool result;
  result = list->size == 0;
  return result;
}

ds_dlist_t *
FUNC(ds_dlist_clone)(ds_dlist_t *list,
                     ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *)) {
  ds_dlist_t *new_list;
  ds_dlist_node_t *current;
  new_list = FUNC(ds_dlist_create_alloc)(list->allocator, list->deallocator);
  current = list->head;
  while (current != list->nil) {
    ds_dlist_node_t *cloned_node = clone_node(current);
    FUNC(ds_dlist_append)(new_list, cloned_node);
    current = current->next;
  }

  return new_list;
}
