#include "ds.h"
#include <stdio.h>
#include <string.h>

/* A storage stack assembled from capabilities: a page store (memory here;
 * ds_page_store_file for a file) under a buffer pool whose frame table is
 * a Swiss map and whose replacement policy is SIEVE, under a B+ tree that
 * keeps its nodes in pages. Each layer only sees the capability below it,
 * so any piece can be swapped without touching the others.
 *
 * The pager is closed with ds_pager_close, which keeps the pager and its
 * dirty pages alive when a write fails, so the close can be retried (or
 * the pages dropped with ds_pager_discard). */

int main(void) {
  ds_page_store_t store;
  ds_pager_t pager;
  ds_pbtree_t tree;
  ds_pbtree_iter_t it;
  unsigned long key;
  double value;
  ds_status_t st;

  if (ds_page_store_memory(&store, 512, NULL) != DS_OK) {
    return 1;
  }
  if (ds_pager_init_policy(&pager, &store, 8, ds_map_swiss_ops(),
                           ds_replacement_sieve(), NULL) != DS_OK) {
    ds_page_store_close(&store);
    return 1;
  }
  if (ds_pbtree_open(&tree, &pager, sizeof(double)) != DS_OK) {
    ds_pager_discard(&pager);
    ds_page_store_close(&store);
    return 1;
  }
  /* 500 keys on 8 frames: the pool evicts and writes back as it goes */
  for (key = 0; key < 500; key++) {
    value = (double)key * 0.5;
    st = ds_pbtree_put(&tree, key * 7 % 500, &value);
    if (st != DS_OK) {
      printf("put failed: %d\n", (int)st);
      return 1;
    }
  }
  value = -1.0;
  (void)ds_pbtree_put(&tree, 42, &value); /* DS_EXISTS: replaced */
  if (ds_pbtree_get(&tree, 42, &value) == DS_OK) {
    printf("key 42 -> %.1f\n", value);
  }
  printf("entries=%lu pages=%lu\n", ds_pbtree_size(&tree),
         ds_pager_page_count(&pager));

  /* ordered scan from 495 through the linked leaves */
  if (ds_pbtree_iter_init(&it, &tree, 495) == DS_OK) {
    printf("from 495:");
    while (ds_pbtree_iter_valid(&it)) {
      double v;
      memcpy(&v, it.value, sizeof(v));
      printf(" %lu=%.1f", it.key, v);
      if (ds_pbtree_iter_next(&it) != DS_OK) {
        break;
      }
    }
    printf("\n");
    ds_pbtree_iter_destroy(&it);
  }

  if (ds_pbtree_sync(&tree) != DS_OK || ds_pager_close(&pager) != DS_OK) {
    ds_pager_discard(&pager);
    ds_page_store_close(&store);
    return 1;
  }
  ds_page_store_close(&store);
  return 0;
}
