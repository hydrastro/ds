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
  list->size = 0;
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  LOCK_INIT_RECURSIVE(list)
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

doubly_linked_list_node_t *doubly_linked_list_search(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    int (*compare)(doubly_linked_list_node_t *, doubly_linked_list_node_t *)) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  doubly_linked_list_node_t *head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
    UNLOCK(list)
#endif

    return list->nil;
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
  return head;
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
  list->size += 1;
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
  list->size += 1;
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
  list->size -= 1;
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void doubly_linked_list_delete(doubly_linked_list_t *list) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  LOCK(list)
#endif
  list->head = list->tail = list->nil;
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
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
}

void doubly_linked_list_destroy(doubly_linked_list_t *list,
                                void (*destroy)(doubly_linked_list_node_t *)) {
  doubly_linked_list_node_t *node = list->head;
  while (node != list->nil) {
    doubly_linked_list_node_t *next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  LOCK_DESTROY(list)
#endif
  free(list->nil);
  free(list);
}

void doubly_linked_list_walk_forward(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*callback)(doubly_linked_list_node_t *)) {
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

void doubly_linked_list_walk_backwards(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*callback)(doubly_linked_list_node_t *)) {
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

bool doubly_linked_list_is_empty(doubly_linked_list_t *list) {
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
  bool result = list->size == 0;
#ifdef DOUBLY_LINKED_LIST_THREAD_SAFE
  UNLOCK(list)
#endif
  return result;
}
