#include "doubly_linked_list.h"
#include <stdio.h>
#include <stdlib.h>

doubly_linked_list_t *doubly_linked_list_create() {
  doubly_linked_list_t *list =
      (doubly_linked_list_t *)malloc(sizeof(doubly_linked_list_t));
  list->nil =
      (doubly_linked_list_node_t *)malloc(sizeof(doubly_linked_list_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->prev = list->nil;
  list->nil->next = list->nil;
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  list->is_thread_safe = true;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&list->lock, &attr);
#endif
  return list;
}

void doubly_linked_list_append(doubly_linked_list_t *list,
                               doubly_linked_list_node_t *node) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
LOCK(list)
#endif
  doubly_linked_list_insert_after(list, node, list->tail);
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}

void doubly_linked_list_prepend(doubly_linked_list_t *list,
                                doubly_linked_list_node_t *node) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 LOCK(list)
#endif
  doubly_linked_list_insert_before(list, node, list->head);
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}

doubly_linked_list_node_t *
doubly_linked_list_search(doubly_linked_list_t *list, void *data,
                          int (*compare)(doubly_linked_list_node_t *, void *)) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 LOCK(list)
#endif
  doubly_linked_list_node_t *node = list->head;
  while (node != list->nil && compare(node, data) != 0) {
    node = node->next;
  }
  if (node == list->nil) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
    return NULL;
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
  return node;
}

void doubly_linked_list_insert_before(doubly_linked_list_t *list,
                                      doubly_linked_list_node_t *node,
                                      doubly_linked_list_node_t *next) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
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
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
UNLOCK(list)
#endif
}

void doubly_linked_list_insert_after(doubly_linked_list_t *list,
                                     doubly_linked_list_node_t *node,
                                     doubly_linked_list_node_t *prev) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
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
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}

void doubly_linked_list_delete_node(doubly_linked_list_t *list,
                                    doubly_linked_list_node_t *node) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
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
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}

void doubly_linked_list_destroy_node(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*destroy)(doubly_linked_list_node_t *)) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 LOCK(list)
#endif
  doubly_linked_list_delete_node(list, node);
  destroy(node);
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}

void doubly_linked_list_destroy(doubly_linked_list_t *list,
                                void (*destroy)(doubly_linked_list_node_t *)) {
  doubly_linked_list_node_t *node = list->head;
  while (node != list->nil) {
    doubly_linked_list_node_t *next = node->next;
    destroy(node);
    node = next;
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  pthread_mutex_destroy(&list->lock);
#endif
  free(list->nil);
  free(list);
}

void doubly_linked_list_walk_forward(doubly_linked_list_t *list,
                                     doubly_linked_list_node_t *node,
                                     void (*callback)(void *)) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 LOCK(list)
#endif
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->next;
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}

void doubly_linked_list_walk_backwards(doubly_linked_list_t *list,
                                       doubly_linked_list_node_t *node,
                                       void (*callback)(void *)) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 LOCK(list)
#endif
  while (node != list->nil) {
    callback(CAST(node, void));
    node = node->prev;
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
 UNLOCK(list)
#endif
}
