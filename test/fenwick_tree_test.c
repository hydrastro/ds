#include "../lib/fenwick_tree.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  ds_fenwick_tree_t *tree = fenwick_tree_create(10);
  ds_fenwick_tree_t *clone;
  long naive[10];
  int i;
  int j;

  for (i = 0; i < 10; ++i) {
    naive[i] = 0;
  }

  /* values: index i gets (i+1) */
  for (i = 0; i < 10; ++i) {
    fenwick_tree_update(tree, (size_t)i, i + 1);
    naive[i] += i + 1;
  }

  /* prefix sums must match a naive scan */
  for (i = 0; i < 10; ++i) {
    long expected = 0;
    long got;
    for (j = 0; j <= i; ++j) {
      expected += naive[j];
    }
    got = fenwick_tree_prefix_sum(tree, (size_t)i);
    assert(got == expected);
  }
  printf("prefix_sum(9): %ld\n", fenwick_tree_prefix_sum(tree, 9));
  assert(fenwick_tree_prefix_sum(tree, 9) == 55);

  printf("range_sum(3,6): %ld\n", fenwick_tree_range_sum(tree, 3, 6));
  /* 4+5+6+7 = 22 */
  assert(fenwick_tree_range_sum(tree, 3, 6) == 22);

  printf("at(5): %ld\n", fenwick_tree_at(tree, 5));
  assert(fenwick_tree_at(tree, 5) == 6);

  fenwick_tree_set(tree, 5, 100);
  printf("at(5) after set: %ld\n", fenwick_tree_at(tree, 5));
  assert(fenwick_tree_at(tree, 5) == 100);
  assert(fenwick_tree_prefix_sum(tree, 9) == 55 - 6 + 100);

  clone = fenwick_tree_clone(tree);
  assert(fenwick_tree_prefix_sum(clone, 9) == fenwick_tree_prefix_sum(tree, 9));

  fenwick_tree_clear(tree);
  assert(fenwick_tree_prefix_sum(tree, 9) == 0);

  fenwick_tree_destroy(tree);
  fenwick_tree_destroy(clone);
  printf("ok\n");
  return 0;
}
