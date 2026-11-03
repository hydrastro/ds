#include "list.h"
#include <stdlib.h>

ds_list_t *FUNC(ds_list_create)(void) {
  return FUNC(ds_list_create_alloc)(malloc, free);
}

ds_list_t *FUNC(ds_list_create_alloc)(void *(*allocator)(size_t),
                                      void (*deallocator)(void *)) {
  ds_list_t *list = (ds_list_t *)allocator(sizeof(ds_list_t));
  list->allocator = allocator;
  list->deallocator = deallocator;
  list->nil = (ds_list_node_t *)allocator(sizeof(ds_list_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
  return list;
}

void FUNC(ds_list_append)(ds_list_t *list, ds_list_node_t *node) {
  node->next = list->nil;
  list->tail->next = node;
  list->tail = node;
  if (list->head == list->nil) {
    list->head = node;
  }
  list->size += 1;
}

void FUNC(ds_list_prepend)(ds_list_t *list, ds_list_node_t *node) {
  node->next = list->head;
  list->head = node;
  if (list->tail == list->nil) {
    list->tail = node;
  }
  list->size += 1;
}

ds_list_node_t *FUNC(ds_list_search)(ds_list_t *list, ds_list_node_t *node,
                                     int (*compare)(ds_list_node_t *,
                                                    ds_list_node_t *)) {
  ds_list_node_t *head;
  head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
    return list->nil;
  }
  return head;
}

void FUNC(ds_list_insert_before)(ds_list_t *list, ds_list_node_t *node,
                                 ds_list_node_t *next) {
  ds_list_node_t *prev;
  prev = list->head;
  while (prev->next != next) {
    prev = prev->next;
  }
  node->next = next;
  prev->next = node;
  if (next == list->head) {
    list->head = node;
  }
  if (list->tail == list->nil) {
    list->tail = node;
  }
  list->size += 1;
}

void FUNC(ds_list_insert_after)(ds_list_t *list, ds_list_node_t *node,
                                ds_list_node_t *prev) {
  node->next = prev->next;
  prev->next = node;

  if (prev == list->tail) {
    list->tail = node;
  }

  if (prev == list->nil) {
    list->head = node;
  }
  list->size += 1;
}

void FUNC(ds_list_delete_node)(ds_list_t *list, ds_list_node_t *node) {
  ds_list_node_t *prev;
  prev = list->nil;
  while (prev->next != node) {
    prev = prev->next;
  }
  prev->next = node->next;
  if (node == list->head) {
    list->head = node->next;
  }
  if (node == list->tail) {
    list->tail = prev;
  }
  list->size -= 1;
}

void FUNC(ds_list_delete)(ds_list_t *list) {
  list->head = list->tail = list->nil;
  list->size = 0;
}

void FUNC(ds_list_destroy_node)(ds_list_t *list, ds_list_node_t *node,
                                void (*destroy)(ds_list_node_t *)) {
  FUNC(ds_list_delete_node)(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void FUNC(ds_list_destroy)(ds_list_t *list, void (*destroy)(ds_list_node_t *)) {
  ds_list_node_t *node, *next;
  node = list->head;
  while (node != list->nil) {
    next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
  list->deallocator(list->nil);
  list->deallocator(list);
}

void FUNC(ds_list_walk_forward)(ds_list_t *list, ds_list_node_t *node,
                                void (*callback)(ds_list_node_t *)) {
  ds_list_node_t *cur;
  cur = node;
  while (cur != list->nil) {
    callback(CAST(cur, void));
    cur = cur->next;
  }
}

void FUNC(ds_list_walk_backwards)(ds_list_t *list, ds_list_node_t *node,
                                  void (*callback)(ds_list_node_t *)) {
  if (node != list->nil) {
    FUNC(ds_list_walk_backwards)(list, node->next, callback);
    callback(CAST(node, void));
  }
}

bool FUNC(ds_list_is_empty)(ds_list_t *list) {
  bool result;
  result = list->size == 0;
  return result;
}

ds_list_t *
FUNC(ds_list_clone)(ds_list_t *list,
                    ds_list_node_t *(*clone_node)(ds_list_node_t *)) {
  ds_list_t *new_list;
  ds_list_node_t *current;
  new_list = FUNC(ds_list_create_alloc)(list->allocator, list->deallocator);
  current = list->head;
  while (current != list->nil) {
    ds_list_node_t *cloned_node = clone_node(current);
    FUNC(ds_list_append)(new_list, cloned_node);
    current = current->next;
  }

  return new_list;
}
