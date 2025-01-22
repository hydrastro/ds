#ifndef DS_DLIST_H
#define DS_DLIST_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct dlist_node {
  struct dlist_node *next;
  struct dlist_node *prev;
} dlist_node_t;

typedef struct dlist {
  size_t size;
  dlist_node_t *head;
  dlist_node_t *tail;
  dlist_node_t *nil;
#ifdef DLIST_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} dlist_t;

dlist_t *dlist_create(void);

void dlist_append(dlist_t *list, dlist_node_t *node);

void dlist_prepend(dlist_t *list, dlist_node_t *node);

dlist_node_t *dlist_search(dlist_t *list, dlist_node_t *node,
                           int (*compare)(dlist_node_t *, dlist_node_t *));

void dlist_insert_before(dlist_t *list, dlist_node_t *node, dlist_node_t *next);

void dlist_insert_after(dlist_t *list, dlist_node_t *node, dlist_node_t *prev);

void dlist_delete_node(dlist_t *list, dlist_node_t *node);

void dlist_delete(dlist_t *list);

void dlist_destroy_node(dlist_t *list, dlist_node_t *node,
                        void (*destroy)(dlist_node_t *));

void dlist_destroy(dlist_t *list, void (*destroy)(dlist_node_t *));

void dlist_walk_forward(dlist_t *list, dlist_node_t *node,
                        void (*callback)(dlist_node_t *));

void dlist_walk_backwards(dlist_t *list, dlist_node_t *node,
                          void (*callback)(dlist_node_t *));

bool dlist_is_empty(dlist_t *);

dlist_t *dlist_clone(dlist_t *list,
                     dlist_node_t *(*clone_node)(dlist_node_t *));

#endif /* DS_DLIST_H */
