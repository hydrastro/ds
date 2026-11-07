#include "../lib/circular_buffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_circular_buffer_node_t node;
  int value;
} int_node_t;

static int_node_t pool[16];

static ds_circular_buffer_node_t *clone_node(ds_circular_buffer_node_t *node) {
  int_node_t *src = CAST(node, int_node_t);
  int_node_t *copy = (int_node_t *)malloc(sizeof(int_node_t));
  copy->value = src->value;
  return &copy->node;
}

static void free_clone(ds_circular_buffer_node_t *node) { free(node); }

int main(void) {
  ds_circular_buffer_t *buffer = ds_circular_buffer_create(4);
  ds_circular_buffer_t *clone;
  ds_circular_buffer_node_t *popped;
  int i;

  assert(ds_circular_buffer_is_empty(buffer));

  for (i = 0; i < 4; ++i) {
    pool[i].value = i;
    assert(ds_circular_buffer_push(buffer, &pool[i].node));
  }
  assert(ds_circular_buffer_is_full(buffer));
  printf("size: %lu\n", (unsigned long)buffer->size);

  /* non-overwrite: 5th push fails */
  pool[4].value = 99;
  assert(!ds_circular_buffer_push(buffer, &pool[4].node));

  printf("front: %d\n",
         CAST(ds_circular_buffer_peek(buffer), int_node_t)->value);
  printf("back: %d\n",
         CAST(ds_circular_buffer_peek_tail(buffer), int_node_t)->value);
  printf("at(2): %d\n",
         CAST(ds_circular_buffer_at(buffer, 2), int_node_t)->value);

  clone = ds_circular_buffer_clone(buffer, clone_node);
  printf("clone size: %lu\n", (unsigned long)clone->size);

  popped = ds_circular_buffer_pop(buffer);
  printf("popped: %d\n", CAST(popped, int_node_t)->value);
  assert(buffer->size == 3);

  /* wrap-around: push two more */
  pool[5].value = 5;
  pool[6].value = 6;
  assert(ds_circular_buffer_push(buffer, &pool[5].node));
  assert(ds_circular_buffer_is_full(buffer));
  assert(!ds_circular_buffer_push(buffer, &pool[6].node));

  while (!ds_circular_buffer_is_empty(buffer)) {
    ds_circular_buffer_node_t *n = ds_circular_buffer_pop(buffer);
    printf("drain: %d\n", CAST(n, int_node_t)->value);
  }
  assert(ds_circular_buffer_pop(buffer) == buffer->nil);

  ds_circular_buffer_destroy(buffer, NULL);
  ds_circular_buffer_destroy(clone, free_clone);

  /* overwrite mode: capacity 3, push 5 -> keeps last 3 */
  buffer = ds_circular_buffer_create_alloc(3, true, malloc, free);
  for (i = 0; i < 5; ++i) {
    pool[i].value = i;
    ds_circular_buffer_push(buffer, &pool[i].node);
  }
  printf("overwrite size: %lu\n", (unsigned long)buffer->size);
  assert(buffer->size == 3);
  printf("overwrite front: %d\n",
         CAST(ds_circular_buffer_peek(buffer), int_node_t)->value);
  assert(CAST(ds_circular_buffer_peek(buffer), int_node_t)->value == 2);
  assert(CAST(ds_circular_buffer_peek_tail(buffer), int_node_t)->value == 4);
  ds_circular_buffer_destroy(buffer, NULL);

  printf("ok\n");
  return 0;
}
