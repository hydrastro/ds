#include "ring_list.h"
#include <stdlib.h>

ds_ring_list_t *FUNC(ring_list_create)(void) {
  return FUNC(ring_list_create_alloc)(malloc, free);
}

ds_ring_list_t *FUNC(ring_list_create_alloc)(void *(*allocator)(size_t),
                                             void (*deallocator)(void *)) {
  ds_ring_list_t *list = (ds_ring_list_t *)allocator(sizeof(ds_ring_list_t));
  list->allocator = allocator;
  list->deallocator = deallocator;
  list->nil = (ds_ring_list_node_t *)allocator(sizeof(ds_ring_list_node_t));
  list->nil->next = list->nil;
  list->tail = list->nil;
  list->size = 0;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(list);
#endif
  return list;
}

void FUNC(ring_list_push_front)(ds_ring_list_t *list,
                                ds_ring_list_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (node == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list);
#endif
    return;
  }
  if (list->tail == list->nil) {
    node->next = node;
    list->tail = node;
  } else {
    node->next = list->tail->next;
    list->tail->next = node;
  }
  list->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
}

void FUNC(ring_list_push_back)(ds_ring_list_t *list,
                               ds_ring_list_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (node == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list);
#endif
    return;
  }
  if (list->tail == list->nil) {
    node->next = node;
    list->tail = node;
  } else {
    node->next = list->tail->next;
    list->tail->next = node;
    list->tail = node;
  }
  list->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
}

ds_ring_list_node_t *FUNC(ring_list_pop_front)(ds_ring_list_t *list) {
  ds_ring_list_node_t *head;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (list->tail == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list);
#endif
    return list->nil;
  }
  head = list->tail->next;
  if (head == list->tail) {
    list->tail = list->nil;
  } else {
    list->tail->next = head->next;
  }
  head->next = list->nil;
  list->size -= 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
  return head;
}

ds_ring_list_node_t *FUNC(ring_list_front)(ds_ring_list_t *list) {
  ds_ring_list_node_t *result;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  result = list->tail == list->nil ? list->nil : list->tail->next;
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
  return result;
}

ds_ring_list_node_t *FUNC(ring_list_back)(ds_ring_list_t *list) {
  ds_ring_list_node_t *result;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  result = list->tail;
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
  return result;
}

void FUNC(ring_list_rotate)(ds_ring_list_t *list) {
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (list->tail != list->nil) {
    list->tail = list->tail->next;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
}

bool FUNC(ring_list_is_empty)(ds_ring_list_t *list) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  result = list->tail == list->nil;
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
  return result;
}

ds_ring_list_node_t *FUNC(ring_list_search)(
    ds_ring_list_t *list, ds_ring_list_node_t *node,
    int (*compare)(ds_ring_list_node_t *, ds_ring_list_node_t *)) {
  ds_ring_list_node_t *current;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (list->tail == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list);
#endif
    return list->nil;
  }
  current = list->tail->next;
  for (i = 0; i < list->size; i++) {
    if (compare(current, node) == 0) {
#ifdef DS_THREAD_SAFE
      UNLOCK(list);
#endif
      return current;
    }
    current = current->next;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
  return list->nil;
}

void FUNC(ring_list_delete_node)(ds_ring_list_t *list,
                                 ds_ring_list_node_t *node) {
  ds_ring_list_node_t *current;
  ds_ring_list_node_t *previous;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (list->tail == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list);
#endif
    return;
  }
  previous = list->tail;
  current = list->tail->next;
  for (i = 0; i < list->size; i++) {
    if (current == node) {
      if (current == current->next) {
        list->tail = list->nil;
      } else {
        previous->next = current->next;
        if (current == list->tail) {
          list->tail = previous;
        }
      }
      current->next = list->nil;
      list->size -= 1;
#ifdef DS_THREAD_SAFE
      UNLOCK(list);
#endif
      return;
    }
    previous = current;
    current = current->next;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
}

void FUNC(ring_list_destroy_node)(ds_ring_list_t *list,
                                  ds_ring_list_node_t *node,
                                  void (*destroy)(ds_ring_list_node_t *)) {
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  FUNC(ring_list_delete_node)(list, node);
  if (destroy != NULL) {
    destroy(node);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
}

void FUNC(ring_list_walk_forward)(ds_ring_list_t *list,
                                  ds_ring_list_node_t *start_node,
                                  void (*callback)(ds_ring_list_node_t *)) {
  ds_ring_list_node_t *current;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  if (list->tail == list->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(list);
#endif
    return;
  }
  current = start_node != list->nil ? start_node : list->tail->next;
  for (i = 0; i < list->size; i++) {
    callback(current);
    current = current->next;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
}

void FUNC(ring_list_destroy)(ds_ring_list_t *list,
                             void (*destroy)(ds_ring_list_node_t *)) {
  ds_ring_list_node_t *current;
  size_t remaining = list->size;
  if (list->tail != list->nil) {
    current = list->tail->next;
    while (remaining > 0) {
      ds_ring_list_node_t *next = current->next;
      if (destroy != NULL) {
        destroy(current);
      }
      current = next;
      remaining -= 1;
    }
  }
  list->deallocator(list->nil);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(list);
#endif
  list->deallocator(list);
}

ds_ring_list_t *FUNC(ring_list_clone)(
    ds_ring_list_t *list,
    ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *)) {
  ds_ring_list_t *new_list;
  ds_ring_list_node_t *current;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(list);
#endif
  new_list = FUNC(ring_list_create_alloc)(list->allocator, list->deallocator);
  if (list->tail != list->nil) {
    current = list->tail->next;
    for (i = 0; i < list->size; i++) {
      FUNC(ring_list_push_back)(new_list, clone_node(current));
      current = current->next;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(list);
#endif
  return new_list;
}
