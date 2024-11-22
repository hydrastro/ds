#include "dlist.h"
#include <stdio.h>
#include <stdlib.h>

dlist_t *dlist_create() {
  dlist_t *list = (dlist_t *)malloc(sizeof(dlist_t));
  list->nil = (dlist_node_t *)malloc(sizeof(dlist_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->prev = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
#ifdef dlist_THREAD_SAFE
  LOCK_INIT_RECURSIVE(list)
#endif
  return list;
}

void dlist_append(dlist_t *list, dlist_node_t *node) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  dlist_insert_after(list, node, list->tail);
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_prepend(dlist_t *list, dlist_node_t *node) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  dlist_insert_before(list, node, list->head);
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

dlist_node_t *dlist_search(dlist_t *list, dlist_node_t *node,
                           int (*compare)(dlist_node_t *, dlist_node_t *)) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  dlist_node_t *head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
#ifdef dlist_THREAD_SAFE
    UNLOCK(list)
#endif

    return list->nil;
  }
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
  return head;
}

void dlist_insert_before(dlist_t *list, dlist_node_t *node,
                         dlist_node_t *next) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
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
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_insert_after(dlist_t *list, dlist_node_t *node, dlist_node_t *prev) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
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
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_delete_node(dlist_t *list, dlist_node_t *node) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  node->prev->next = node->next;
  node->next->prev = node->prev;

  if (node == list->head) {
    list->head = node->next;
  }

  if (node == list->tail) {
    list->tail = node->prev;
  }
  list->size -= 1;
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_delete(dlist_t *list) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  list->head = list->tail = list->nil;
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_destroy_node(dlist_t *list, dlist_node_t *node,
                        void (*destroy)(dlist_node_t *)) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  dlist_delete_node(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_destroy(dlist_t *list, void (*destroy)(dlist_node_t *)) {
  dlist_node_t *node = list->head;
  while (node != list->nil) {
    dlist_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
#ifdef dlist_THREAD_SAFE
  LOCK_DESTROY(list)
#endif
  free(list->nil);
  free(list);
}

void dlist_walk_forward(dlist_t *list, dlist_node_t *node,
                        void (*callback)(dlist_node_t *)) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->next;
  }
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

void dlist_walk_backwards(dlist_t *list, dlist_node_t *node,
                          void (*callback)(dlist_node_t *)) {
#ifdef dlist_THREAD_SAFE
  LOCK(list)
#endif
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->prev;
  }
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
}

bool dlist_is_empty(dlist_t *list) {
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
  bool result = list->size == 0;
#ifdef dlist_THREAD_SAFE
  UNLOCK(list)
#endif
  return result;
}
