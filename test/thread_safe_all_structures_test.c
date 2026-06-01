#include "../ds.h"
#include <assert.h>
#include <stdlib.h>

typedef struct avl_item {
  ds_avl_node_t node;
  int key;
} avl_item_t;

typedef struct btree_item {
  ds_btree_node_t node;
  int key;
} btree_item_t;

typedef struct dlist_item {
  ds_dlist_node_t node;
  int key;
} dlist_item_t;

typedef struct list_item {
  ds_list_node_t node;
  int key;
} list_item_t;

typedef struct queue_item {
  ds_queue_node_t node;
  int key;
} queue_item_t;

typedef struct stack_item {
  ds_stack_node_t node;
  int key;
} stack_item_t;

static int avl_compare(ds_avl_node_t *a, ds_avl_node_t *b) {
  return ((avl_item_t *)a)->key - ((avl_item_t *)b)->key;
}

static int btree_compare(ds_btree_node_t *a, ds_btree_node_t *b) {
  return ((btree_item_t *)a)->key - ((btree_item_t *)b)->key;
}

static void destroy_avl_payload(ds_avl_node_t *node) {
  free(node);
}
static void destroy_btree_payload(ds_btree_node_t *node) {
  free(node);
}
static void noop_avl_walk(ds_avl_node_t *node) {
  (void)node;
}
static void noop_btree_walk(ds_btree_node_t *node) {
  (void)node;
}
static void destroy_queue_payload(ds_queue_node_t *node) {
  (void)node;
}
static void destroy_stack_payload(ds_stack_node_t *node) {
  (void)node;
}

int main(void) {
  ds_dlist_t *dlist;
  ds_list_t *list;
  ds_queue_t *queue;
  ds_stack_t *stack;
  ds_avl_t *avl;
  ds_btree_t *btree;
  dlist_item_t dnode;
  list_item_t lnode;
  queue_item_t qnode;
  stack_item_t snode;
  avl_item_t *avl_node;
  btree_item_t *btree_node;
  btree_item_t probe;
  size_t i;

  dlist = FUNC(ds_dlist_create)();
  assert(FUNC(ds_dlist_is_empty)(dlist));
  FUNC(ds_dlist_append)(dlist, &dnode.node);
  assert(!FUNC(ds_dlist_is_empty)(dlist));
  FUNC(ds_dlist_delete)(dlist);
  assert(dlist->size == 0U);
  FUNC(ds_dlist_destroy)(dlist, NULL);

  list = FUNC(ds_list_create)();
  FUNC(ds_list_append)(list, &lnode.node);
  FUNC(ds_list_delete)(list);
  assert(list->size == 0U);
  FUNC(ds_list_destroy)(list, NULL);

  queue = FUNC(ds_queue_create)();
  FUNC(ds_queue_enqueue)(queue, &qnode.node);
  FUNC(ds_queue_pop_destroy)(queue, destroy_queue_payload);
  assert(FUNC(ds_queue_is_empty)(queue));
  FUNC(ds_queue_destroy)(queue, NULL);

  stack = FUNC(ds_stack_create)();
  FUNC(ds_stack_push)(stack, stack->nil);
  assert(FUNC(ds_stack_is_empty)(stack));
  assert(FUNC(ds_stack_pop)(stack) == stack->nil);
  FUNC(ds_stack_push)(stack, &snode.node);
  FUNC(ds_stack_pop_destroy)(stack, destroy_stack_payload);
  assert(FUNC(ds_stack_is_empty)(stack));
  FUNC(ds_stack_walk_backwards)(stack, stack->nil, destroy_stack_payload);
  FUNC(ds_stack_destroy)(stack, NULL);

  avl = FUNC(ds_avl_create)();
  for (i = 0U; i < 32U; i++) {
    avl_node = (avl_item_t *)malloc(sizeof(*avl_node));
    assert(avl_node != NULL);
    avl_node->key = (int)((i * 7U) % 97U);
    FUNC(ds_avl_insert)(avl, &avl_node->node, avl_compare);
  }
  FUNC(ds_avl_inorder_walk_tree)(avl, noop_avl_walk);
  FUNC(ds_avl_preorder_walk_tree)(avl, noop_avl_walk);
  FUNC(ds_avl_postorder_walk_tree)(avl, noop_avl_walk);
  FUNC(ds_avl_destroy_tree)(avl, destroy_avl_payload);

  btree = FUNC(ds_btree_create)(2);
  for (i = 0U; i < 16U; i++) {
    btree_node = (btree_item_t *)malloc(sizeof(*btree_node));
    assert(btree_node != NULL);
    btree_node->key = (int)i;
    FUNC(ds_btree_insert)(btree, &btree_node->node, btree_compare);
  }
  probe.key = 99;
  assert(FUNC(ds_btree_search)(btree, &probe.node, btree_compare) ==
         btree->nil);
  FUNC(ds_btree_inorder_walk_tree)(btree, noop_btree_walk);
  FUNC(ds_btree_preorder_walk_tree)(btree, noop_btree_walk);
  FUNC(ds_btree_postorder_walk_tree)(btree, noop_btree_walk);
  FUNC(ds_btree_destroy_tree)(btree, destroy_btree_payload);

  return 0;
}
