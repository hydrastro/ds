#include "list.h"
#include <stdlib.h>

list_t *list_create() {
  list_t *list = (list_t *)malloc(sizeof(list_t));
  list->nil = (list_node_t *)malloc(sizeof(list_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->next = list->nil;
  list->size = 0;
#ifdef list_THREAD_SAFE
  LOCK_INIT_RECURSIVE(list)
#endif
  return list;
}

void list_append(list_t *list, list_node_t *node) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  node->next = list->nil;
  list->tail->next = node;
  list->tail = node;
  if (list->head == list->nil) {
    list->head = node;
  }
  list->size += 1;
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_prepend(list_t *list, list_node_t *node) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  node->next = list->head;
  list->head = node;
  if (list->tail == list->nil) {
    list->tail = node;
  }
  list->size += 1;
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

list_node_t *list_search(list_t *list, list_node_t *node,
                         int (*compare)(list_node_t *, list_node_t *)) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list_node_t *head;
  head = list->head;
  while (head != list->nil && compare(head, node) != 0) {
    head = head->next;
  }
  if (head == list->nil) {
#ifdef list_THREAD_SAFE
    UNLOCK(list)
#endif
    return list->nil;
  }
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
  return head;
}

void list_insert_before(list_t *list, list_node_t *node, list_node_t *next) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list_node_t *prev;
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
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_insert_after(list_t *list, list_node_t *node, list_node_t *prev) {
#ifdef list_THREAD_SAFE
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
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_delete_node(list_t *list, list_node_t *node) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list_node_t *prev;
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
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_delete(list_t *list) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list->head = list->tail = list->nil;
  ;
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_destroy_node(list_t *list, list_node_t *node,
                       void (*destroy)(list_node_t *)) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list_delete_node(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_destroy(list_t *list, void (*destroy)(list_node_t *)) {
  list_node_t *node, *next;
  node = list->head;
  while (node != list->nil) {
    next = node->next;
    if (destroy != NULL) {
      destroy(node);
    }
    node = next;
  }
#ifdef list_THREAD_SAFE
  LOCK_DESTROY(list)
#endif
  free(list->nil);
  free(list);
}

void list_walk_forward(list_t *list, list_node_t *node,
                       void (*callback)(list_node_t *)) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list_node_t *cur;
  cur = node;
  while (cur != list->nil) {
    callback(CAST(cur, void));
    cur = cur->next;
  }
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

void list_walk_backwards(list_t *list, list_node_t *node,
                         void (*callback)(list_node_t *)) {

#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  if (node != list->nil) {
    list_walk_backwards(list, node->next, callback);
    callback(CAST(node, void));
  }
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
}

bool list_is_empty(list_t *list) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  bool result = list->size == 0;
#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
  return result;
}

list_t *list_clone(list_t *list, list_node_t *(*clone_node)(list_node_t *)) {
#ifdef list_THREAD_SAFE
  LOCK(list)
#endif
  list_t *new_list = list_create();
  list_node_t *current = list->head;
  while (current != list->nil) {
    list_node_t *cloned_node = clone_node(current);
    list_append(new_list, cloned_node);
    current = current->next;
  }

#ifdef list_THREAD_SAFE
  UNLOCK(list)
#endif
  return new_list;
}
