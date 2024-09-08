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
  return list;
}

void doubly_linked_list_append(doubly_linked_list_t *list,
                               doubly_linked_list_node_t *node) {
  doubly_linked_list_insert_after(list, node, list->tail);
}

void doubly_linked_list_prepend(doubly_linked_list_t *list,
                                doubly_linked_list_node_t *node) {
  doubly_linked_list_insert_before(list, node, list->head);
}

doubly_linked_list_node_t *
doubly_linked_list_search(doubly_linked_list_t *list, void *data,
                          int (*compare)(doubly_linked_list_node_t *, void *)) {
  doubly_linked_list_node_t *node = list->head;
  while (node != list->nil && compare(node, data) != 0) {
    node = node->next;
  }
  if (node == list->nil) {
    return NULL;
  }
  return node;
}

void doubly_linked_list_insert_before(doubly_linked_list_t *list,
                                      doubly_linked_list_node_t *node,
                                      doubly_linked_list_node_t *next) {
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
}

void doubly_linked_list_insert_after(doubly_linked_list_t *list,
                                     doubly_linked_list_node_t *node,
                                     doubly_linked_list_node_t *prev) {
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
}

void doubly_linked_list_delete_node(doubly_linked_list_t *list,
                                    doubly_linked_list_node_t *node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;

  if (node == list->head) {
    list->head = node->next;
  }

  if (node == list->tail) {
    list->tail = node->prev;
  }
}

void doubly_linked_list_destroy_node(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*destroy)(doubly_linked_list_node_t *)) {
  doubly_linked_list_delete_node(list, node);
  destroy(node);
}

void doubly_linked_list_destroy(doubly_linked_list_t *list,
                                void (*destroy)(doubly_linked_list_node_t *)) {
  doubly_linked_list_node_t *node = list->head;
  while (node != list->nil) {
    doubly_linked_list_node_t *next = node->next;
    destroy(node);
    node = next;
  }
  free(list->nil);
  free(list);
}

void doubly_linked_list_walk_forward(doubly_linked_list_t *list,
                                     doubly_linked_list_node_t *node,
                                     void (*callback)(void *)) {
  while (node != list->nil) {
    callback(DOUBLY_LINKED_LIST_GET_STRUCT_FROM_NODE(node, void));
    node = node->next;
  }
  printf("\n");
}

void doubly_linked_list_walk_backwards(doubly_linked_list_t *list,
                                       doubly_linked_list_node_t *node,
                                       void (*callback)(void *)) {
  while (node != list->nil) {
    callback(DOUBLY_LINKED_LIST_GET_STRUCT_FROM_NODE(node, void));
    node = node->prev;
  }
}
