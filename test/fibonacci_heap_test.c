#include "../lib/fibonacci_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct item {
  ds_fibonacci_heap_node_t node;
  int key;
} item_t;

static int compare_items(ds_fibonacci_heap_node_t *a,
                         ds_fibonacci_heap_node_t *b) {
  return CAST(a, item_t)->key - CAST(b, item_t)->key;
}

static int cmp_int(const void *a, const void *b) {
  return *(const int *)a - *(const int *)b;
}

int main(void) {
  int n = 3000;
  item_t *items = (item_t *)malloc(sizeof(item_t) * (size_t)n);
  int *sorted = (int *)malloc(sizeof(int) * (size_t)n);
  ds_fibonacci_heap_t *heap = ds_fibonacci_heap_create(compare_items);
  int i;
  unsigned state = 4321U;
  int prev;

  for (i = 0; i < n; ++i) {
    state = state * 1103515245U + 12345U;
    items[i].key = (int)((state >> 8) % 100000U);
    sorted[i] = items[i].key;
    ds_fibonacci_heap_insert(heap, &items[i].node);
  }
  qsort(sorted, (size_t)n, sizeof(int), cmp_int);
  assert(ds_fibonacci_heap_size(heap) == (size_t)n);
  assert(CAST(ds_fibonacci_heap_peek(heap), item_t)->key == sorted[0]);

  /* extract-min yields sorted order (heapsort property) */
  prev = -1;
  for (i = 0; i < n; ++i) {
    ds_fibonacci_heap_node_t *m = ds_fibonacci_heap_extract(heap);
    int k;
    assert(m != heap->nil);
    k = CAST(m, item_t)->key;
    assert(k == sorted[i]);
    assert(k >= prev);
    prev = k;
  }
  assert(ds_fibonacci_heap_is_empty(heap));
  assert(ds_fibonacci_heap_extract(heap) == heap->nil);
  printf("heapsort property verified over %d elements\n", n);
  ds_fibonacci_heap_destroy(heap, NULL);

  /* decrease-key: lower several keys, confirm one becomes the new min */
  heap = ds_fibonacci_heap_create(compare_items);
  for (i = 0; i < 500; ++i) {
    items[i].key = 1000 + i;
    ds_fibonacci_heap_insert(heap, &items[i].node);
  }
  /* force some structure by extracting once (triggers consolidation) */
  ds_fibonacci_heap_extract(heap);
  /* now decrease item 400 to the global minimum */
  items[400].key = -5;
  ds_fibonacci_heap_decrease_key(heap, &items[400].node);
  assert(CAST(ds_fibonacci_heap_peek(heap), item_t)->key == -5);
  /* decrease a few more and check monotone extraction */
  items[450].key = -3;
  ds_fibonacci_heap_decrease_key(heap, &items[450].node);
  items[470].key = -4;
  ds_fibonacci_heap_decrease_key(heap, &items[470].node);
  {
    int last = -1000;
    int extracted = 0;
    while (!ds_fibonacci_heap_is_empty(heap)) {
      ds_fibonacci_heap_node_t *m = ds_fibonacci_heap_extract(heap);
      int k = CAST(m, item_t)->key;
      assert(k >= last);
      last = k;
      extracted += 1;
    }
    printf("decrease-key ok, extracted %d in order\n", extracted);
  }
  ds_fibonacci_heap_destroy(heap, NULL);

  /* merge two heaps */
  {
    ds_fibonacci_heap_t *a = ds_fibonacci_heap_create(compare_items);
    ds_fibonacci_heap_t *b = ds_fibonacci_heap_create(compare_items);
    int last = -1000;
    int total;
    for (i = 0; i < 100; ++i) {
      items[i].key = i * 2;
      ds_fibonacci_heap_insert(a, &items[i].node);
    }
    for (i = 100; i < 200; ++i) {
      items[i].key = i * 2 + 1;
      ds_fibonacci_heap_insert(b, &items[i].node);
    }
    ds_fibonacci_heap_merge(a, b);
    assert(ds_fibonacci_heap_size(a) == 200);
    assert(ds_fibonacci_heap_is_empty(b));
    total = 0;
    while (!ds_fibonacci_heap_is_empty(a)) {
      ds_fibonacci_heap_node_t *m = ds_fibonacci_heap_extract(a);
      int k = CAST(m, item_t)->key;
      assert(k >= last);
      last = k;
      total += 1;
    }
    assert(total == 200);
    printf("merge ok, %d elements in order\n", total);
    ds_fibonacci_heap_destroy(a, NULL);
    ds_fibonacci_heap_destroy(b, NULL);
  }

  free(items);
  free(sorted);
  printf("ok\n");
  return 0;
}
