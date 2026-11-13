/*
 * A fusion tree holds the start of every allocated address block of a
 * 32-bit address space; floor finds the block containing an address,
 * ceiling the next allocation, rank and select count and enumerate. One
 * node is opened up to show the search itself: the bits where its keys
 * branch, each key's sketch, and the packed word the query is compared
 * against in one subtraction.
 */
#include "ds.h"
#include <stdio.h>

typedef struct {
  unsigned long length;
  char owner[12];
} block_t;

static int show(unsigned long key, void *value, void *user) {
  const block_t *b = (const block_t *)value;
  int *left = (int *)user;
  printf("  %08lx +%lu %s\n", key, b->length, b->owner);
  return --*left == 0; /* stop after a few */
}

int main(void) {
  static const char *owners[4] = {"alpha", "beta", "gamma", "delta"};
  ds_fusion_tree_t t;
  unsigned long start = 0x0A000000ul, key = 0, addr;
  block_t b;
  size_t i, k;
  int left = 4;
  bool equal = false;
  bool valid = false;
  const ds_fusion_node_t *x;
  if (ds_fusion_init(&t, sizeof(block_t), NULL) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 5000; i++) { /* blocks of 256 to 4096 addresses */
    unsigned long len = 256ul << (i * 7 % 5);
    b.length = len;
    sprintf(b.owner, "%s-%lu", owners[i % 4], (unsigned long)(i % 97));
    (void)ds_fusion_insert(&t, start, &b);
    start += len + (i % 3 == 0 ? 512ul : 0ul); /* some gaps */
  }
  (void)ds_fusion_check(&t, &valid);
  printf("%lu blocks in a fusion tree of height %lu (%d keys per node), "
         "structure %s\n",
         (unsigned long)ds_fusion_size(&t), (unsigned long)ds_fusion_height(&t),
         DS_FUSION_K, valid ? "valid" : "INVALID");
  addr = 0x0A123456ul;
  if (ds_fusion_floor(&t, addr, &key, &b) == DS_OK) {
    printf("address %08lx: block %08lx +%lu (%s), %s\n", addr, key, b.length,
           b.owner, addr < key + b.length ? "inside" : "in a gap after it");
  }
  if (ds_fusion_successor(&t, addr, &key, &b) == DS_OK) {
    printf("next allocation after it: %08lx (%s)\n", key, b.owner);
  }
  printf("blocks below %08lx: %lu; the 1000th block starts at ", addr,
         (unsigned long)ds_fusion_rank(&t, addr));
  (void)ds_fusion_select(&t, 999, &key, NULL);
  printf("%08lx\nthe first blocks:\n", key);
  (void)ds_fusion_foreach(&t, show, &left);

  /* the root node's search, opened up */
  x = t.root;
  printf("root node: %u keys, branching at bits", (unsigned)x->count);
  for (k = 0; k < x->bits; k++) {
    printf(" %u", (unsigned)x->bit[k]);
  }
  printf("\n  sketches:");
  for (k = 0; k < x->count; k++) {
    printf(" %u", (unsigned)x->sketch[k]);
  }
  printf("\n  packed word %lx; query %08lx ranks %lu\n", x->packed, addr,
         (unsigned long)ds_fusion_node_rank(x, addr, &equal));
  ds_fusion_destroy(&t);
  return valid ? 0 : 1;
}
