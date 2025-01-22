#include "list.h"
#include <stdlib.h>

list_t *FUNC(list_create)(void) {
  list_t *list = (list_t *)malloc(sizeof(list_t));
  list->nil = (list_node_t *)malloc(sizeof(list_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(list)
#endif
  return list;
}

void FUNC(list_append)(list_t *list, list_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  node->next = list->nil;
  list->tail->next = node;
  list->tail = node;
  if (list->head == list->nil) {
    list->head = node;
  }
  list->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_prepend)(list_t *list, list_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  node->next = list->head;
  list->head = node;
  if (list->tail == list->nil) {
    list->tail = node;
  }
  list->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

list_node_t *FUNC(list_search)(list_t *list, list_node_t *node,
                               int (*compare)(list_node_t *, list_node_t *)) {
  list_node_t *head;
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list)
#endif
    return list->nil;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
  return head;
}

void FUNC(list_insert_before)(list_t *list, list_node_t *node,
                              list_node_t *next) {
  list_node_t *prev;
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
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
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_insert_after)(list_t *list, list_node_t *node,
                             list_node_t *prev) {
#ifdef DS_THREAD_SAFE
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
  list->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_delete_node)(list_t *list, list_node_t *node) {
  list_node_t *prev;
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
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
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_delete)(list_t *list) {
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  list->head = list->tail = list->nil;
  ;
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_destroy_node)(list_t *list, list_node_t *node,
                             void (*destroy)(list_node_t *)) {
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  FUNC(list_delete_node)(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_destroy)(list_t *list, void (*destroy)(list_node_t *)) {
  list_node_t *node, *next;
  node = list->head;
  while (node != list->nil) {
    next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(list)
#endif
  free(list->nil);
  free(list);
}

void FUNC(list_walk_forward)(list_t *list, list_node_t *node,
                             void (*callback)(list_node_t *)) {
  list_node_t *cur;
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  cur = node;
  while (cur != list->nil) {
    callback(CAST(cur, void));
    cur = cur->next;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

void FUNC(list_walk_backwards)(list_t *list, list_node_t *node,
                               void (*callback)(list_node_t *)) {

#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  if (node != list->nil) {
    FUNC(list_walk_backwards)(list, node->next, callback);
    callback(CAST(node, void));
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
}

bool FUNC(list_is_empty)(list_t *list) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  result = list->size == 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
  return result;
}

list_t *FUNC(list_clone)(list_t *list,
                         list_node_t *(*clone_node)(list_node_t *)) {
  list_t *new_list;
  list_node_t *current;
#ifdef DS_THREAD_SAFE
  LOCK(list)
#endif
  new_list = FUNC(list_create)();
  current = list->head;
  while (current != list->nil) {
    list_node_t *cloned_node = clone_node(current);
    FUNC(list_append)(new_list, cloned_node);
    current = current->next;
  }

#ifdef DS_THREAD_SAFE
  UNLOCK(list)
#endif
  return new_list;
}
