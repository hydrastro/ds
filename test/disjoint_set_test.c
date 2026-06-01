#include "../lib/disjoint_set.h"
#include <assert.h>
#include <stdio.h>

typedef struct cell {
  ds_disjoint_set_node_t node;
  int id;
} cell_t;

int main(void) {
  ds_disjoint_set_t *set = ds_disjoint_set_create();
  cell_t cells[10];
  int i;

  for (i = 0; i < 10; ++i) {
    cells[i].id = i;
    ds_disjoint_set_make(set, &cells[i].node);
  }
  printf("count after make: %lu\n", (unsigned long)ds_disjoint_set_count(set));
  assert(ds_disjoint_set_count(set) == 10);

  ds_disjoint_set_union(set, &cells[0].node, &cells[1].node);
  ds_disjoint_set_union(set, &cells[1].node, &cells[2].node);
  ds_disjoint_set_union(set, &cells[3].node, &cells[4].node);

  assert(ds_disjoint_set_connected(set, &cells[0].node, &cells[2].node));
  assert(!ds_disjoint_set_connected(set, &cells[0].node, &cells[3].node));
  printf("count after 3 unions: %lu\n",
         (unsigned long)ds_disjoint_set_count(set));
  assert(ds_disjoint_set_count(set) == 7);

  /* unioning already-connected does nothing */
  assert(!ds_disjoint_set_union(set, &cells[0].node, &cells[2].node));
  assert(ds_disjoint_set_count(set) == 7);

  /* join the two groups */
  ds_disjoint_set_union(set, &cells[2].node, &cells[4].node);
  assert(ds_disjoint_set_connected(set, &cells[0].node, &cells[3].node));
  assert(ds_disjoint_set_count(set) == 6);

  printf("find(0) == find(4): %s\n",
         ds_disjoint_set_find(set, &cells[0].node) ==
                 ds_disjoint_set_find(set, &cells[4].node)
             ? "yes"
             : "no");

  ds_disjoint_set_destroy(set);
  printf("ok\n");
  return 0;
}
