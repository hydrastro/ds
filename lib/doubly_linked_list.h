#ifndef DS_DOUBLY_LINKED_LIST_H
#define DS_DOUBLY_LINKED_LIST_H

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

typedef struct doubly_linked_list_node {
  struct doubly_linked_list_node *next;
  struct doubly_linked_list_node *prev;
} doubly_linked_list_node_t;

typedef struct doubly_linked_list {
  doubly_linked_list_node_t *head;
  doubly_linked_list_node_t *tail;
  doubly_linked_list_node_t *nil;
} doubly_linked_list_t;

doubly_linked_list_t *doubly_linked_list_create();

void doubly_linked_list_append(doubly_linked_list_t *list,
                                             doubly_linked_list_node_t *node);

void doubly_linked_list_prepend(doubly_linked_list_t *list,
                                              doubly_linked_list_node_t *node);

doubly_linked_list_node_t *
doubly_linked_list_search(doubly_linked_list_t *list, void *data,
                          int (*compare)(doubly_linked_list_node_t *, void *));

void
doubly_linked_list_insert_before(doubly_linked_list_t *list,
                                 doubly_linked_list_node_t *node,
                                 doubly_linked_list_node_t *next);

void
doubly_linked_list_insert_after(doubly_linked_list_t *list,
                                doubly_linked_list_node_t *node,
                                doubly_linked_list_node_t *prev);

// removes the node from the list
void
doubly_linked_list_delete_node(doubly_linked_list_t *list,
                               doubly_linked_list_node_t *node);

// destroys the node
void doubly_linked_list_destroy_node(
    doubly_linked_list_t *list, doubly_linked_list_node_t *node,
    void (*destroy_node)(doubly_linked_list_node_t *));

// destroys the list and all its nodes
void
doubly_linked_list_destroy(doubly_linked_list_t *list,
                           void (*destroy_node)(doubly_linked_list_node_t *));

void
doubly_linked_list_walk_forward(doubly_linked_list_t *list,
                                doubly_linked_list_node_t *node,
                                void (*callback)(void *));

void
doubly_linked_list_walk_backwards(doubly_linked_list_t *list,
                                doubly_linked_list_node_t *node,
                                void (*callback)(void *));
#endif // DS_DOUBLY_LINKED_LIST_H
