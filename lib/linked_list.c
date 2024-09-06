#include <stdlib.h>
#include "linked_list.h"

linked_list_t *linked_list_create() {
  linked_list_t *list = (linked_list_t *)malloc(sizeof(linked_list_t));
  list->nil = (linked_list_node_t *)malloc(sizeof(linked_list_node_t));
  list->head = list->nil;
  list->tail = list->nil;
  list->nil->next = list->nil;
  return list;
}

void linked_list_append(linked_list_t *list,
                                      linked_list_node_t *node) {
  node->next = list->nil;
  list->tail->next = node;
  list->tail = node;
  if (list->head == list->nil) {
    list->head = node;
  }
}

void linked_list_prepend(linked_list_t *list,
                                       linked_list_node_t *node) {
  node->next = list->head;
  list->head = node;
  if (list->tail == list->nil) {
    list->tail = node;
  }
}

linked_list_node_t *
linked_list_search(linked_list_t *list, void *data,
int (*compare)(linked_list_node_t *, void *)) {
  linked_list_node_t *node;
  node = list->head;
  while (node != list->nil && compare(node, data) != 0) {
    node = node->next;
  }
  if (node == list->nil) {
    return NULL;
  }
  return node;
}

void linked_list_insert_before(linked_list_t *list,
                                             linked_list_node_t *node,
                                             linked_list_node_t *next) {
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
}

void linked_list_insert_after(linked_list_t *list,
                                            linked_list_node_t *node,
                                            linked_list_node_t *prev) {
  node->next = prev->next;
  prev->next = node;

  if (prev == list->tail) {
    list->tail = node;
  }

  if (prev == list->nil) {
    list->head = node;
  }
}

void linked_list_delete_node(linked_list_t *list,
                                            linked_list_node_t *node) {
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
}

void
linked_list_destroy_node(linked_list_t *list, linked_list_node_t *node,
                         void (*destroy_node)(void *)) {
  linked_list_delete_node(list, node);
  destroy_node(CAST(node, void));
}

void
linked_list_destroy(linked_list_t *list,
                    void (*destroy_node)(void *)) {
  linked_list_node_t *node, *next;
  node  = list->head;
  while (node != list->nil) {
    next = node->next;
    destroy_node(CAST(node, void));
    node = next;
  }
  free(list);
}

void linked_list_walk_forward(linked_list_t *list,
                      linked_list_node_t *node,
                      void (*callback)(void *)) {
  linked_list_node_t *cur;
  cur = node;
  while(cur != list->nil) {
    callback(CAST(cur, void));
    cur = cur->next;
  }
}

void linked_list_walk_backwards(linked_list_t *list,
                      linked_list_node_t *node,
                      void (*callback)(void *)) {
   if(node != list->nil) {
       linked_list_walk_backwards(list, node->next, callback);
       callback(CAST(node, void));
   }
}
