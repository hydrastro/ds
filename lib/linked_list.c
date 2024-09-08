#include "linked_list.h"
#include <stdlib.h>

linked_list_t *linked_list_create() {
  linked_list_t *list = (linked_list_t *)malloc(sizeof(linked_list_t));
  list->nil = (linked_list_node_t *)malloc(sizeof(linked_list_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->next = list->nil;
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK_INIT_RECURSIVE(list)
#endif
  return list;
}

void linked_list_append(linked_list_t *list, linked_list_node_t *node) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  node->next = list->nil;
  list->tail->next = node;
  list->tail = node;
  if (list->head == list->nil) {
    list->head = node;
  }
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void linked_list_prepend(linked_list_t *list, linked_list_node_t *node) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  node->next = list->head;
  list->head = node;
  if (list->tail == list->nil) {
    list->tail = node;
  }
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

linked_list_node_t *linked_list_search(linked_list_t *list, void *data,
                                       int (*compare)(linked_list_node_t *,
                                                      void *)) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  linked_list_node_t *node;
  node = list->head;
  while (node != list->nil && compare(node, data) != 0) {
    node = node->next;
  }
  if (node == list->nil) {
#ifdef LINKED_LIST_THREAD_SAFE
    UNLOCK(list)
#endif
    return list->nil;
  }
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
  return node;
}

void linked_list_insert_before(linked_list_t *list, linked_list_node_t *node,
                               linked_list_node_t *next) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  linked_list_node_t *prev;
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
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void linked_list_insert_after(linked_list_t *list, linked_list_node_t *node,
                              linked_list_node_t *prev) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  node->next = prev->next;
  prev->next = node;

  if (prev == list->tail) {
    list->tail = node;
  }

  if (prev == list->nil) {
    list->head = node;
  }
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void linked_list_delete_node(linked_list_t *list, linked_list_node_t *node) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  linked_list_node_t *prev;
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
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void linked_list_destroy_node(linked_list_t *list, linked_list_node_t *node,
                              void (*destroy_node)(void *)) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  linked_list_delete_node(list, node);
  destroy_node(CAST(node, void));
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void linked_list_destroy(linked_list_t *list, void (*destroy_node)(void *)) {
  linked_list_node_t *node, *next;
  node = list->head;
  while (node != list->nil) {
    next = node->next;
    destroy_node(CAST(node, void));
    node = next;
  }
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK_DESTROY(list)
#endif
  free(list->nil);
  free(list);
}

void linked_list_walk_forward(linked_list_t *list, linked_list_node_t *node,
                              void (*callback)(void *)) {
#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  linked_list_node_t *cur;
  cur = node;
  while (cur != list->nil) {
    callback(CAST(cur, void));
    cur = cur->next;
  }
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void linked_list_walk_backwards(linked_list_t *list, linked_list_node_t *node,
                                void (*callback)(void *)) {

#ifdef LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  if (node != list->nil) {
    linked_list_walk_backwards(list, node->next, callback);
    callback(CAST(node, void));
  }
#ifdef LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}
