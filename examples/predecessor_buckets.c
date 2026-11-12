/*
 * A Y-fast trie with two bucket representations.
 *
 * The X-fast trie indexes buckets of Theta(w) keys; each bucket is an
 * instance of the ordered-map capability chosen at init. Sorted arrays
 * keep a bucket in one block (updates shift up to 2w entries, queries are
 * a binary search over contiguous memory); treaps give O(log w) updates,
 * the bound of Willard's design, at a node per key. The answers are the
 * same; the per-update work and the memory layout differ.
 */
#include "ds.h"
#include <stdio.h>

static int fill(ds_yfast_t *t, const ds_ordered_ops_t *buckets,
                const char *label) {
  unsigned long k;
  unsigned long out;
  if (ds_yfast_init_buckets(t, 32, 0, NULL, buckets, NULL) != DS_OK) {
    return 1;
  }
  for (k = 0; k < 2000; k++) {
    if (ds_yfast_insert(t, k * k % 99991UL, NULL) != DS_OK) {
      return 1;
    }
  }
  printf("%-14s %lu keys in %lu buckets", label,
         (unsigned long)ds_yfast_size(t), (unsigned long)ds_yfast_buckets(t));
  if (ds_yfast_predecessor(t, 50000UL, &out) == DS_OK) {
    printf(", predecessor(50000) = %lu", out);
  }
  if (ds_yfast_successor(t, 50000UL, &out) == DS_OK) {
    printf(", successor(50000) = %lu", out);
  }
  printf("\n");
  return 0;
}

int main(void) {
  ds_yfast_t a;
  ds_yfast_t b;
  if (fill(&a, ds_ordered_array_ops(), "sorted arrays") != 0 ||
      fill(&b, ds_ordered_treap_ops(), "treaps") != 0) {
    return 1;
  }
  ds_yfast_destroy(&a);
  ds_yfast_destroy(&b);
  return 0;
}
