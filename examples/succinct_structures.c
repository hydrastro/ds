/*
 * Succinct / bit structures: bitset, rank/select bitvector, and wavelet tree.
 *
 * These are value-based (no embedded node): you build them from data and query
 * by index or symbol. The bitvector adds rank/select over a frozen bitset; the
 * wavelet tree generalizes rank/select to an alphabet of symbols.
 */
#include "ds.h"
#include <stdio.h>

static void bitset_demo(void) {
  ds_bitset_t *bs = ds_bitset_create(64);
  size_t i;

  for (i = 0; i < 64; ++i) {
    if (i % 5 == 0) {
      ds_bitset_set(bs, i);
    }
  }
  printf("bitset: %lu bits set, bit 10 = %d, bit 11 = %d\n",
         (unsigned long)ds_bitset_count(bs), ds_bitset_test(bs, 10) ? 1 : 0,
         ds_bitset_test(bs, 11) ? 1 : 0);
  ds_bitset_destroy(bs);
}

static void bitvector_demo(void) {
  ds_bitvector_t *bv = ds_bitvector_create(32);
  size_t i;

  /* set even positions, then freeze with build() to enable rank/select */
  for (i = 0; i < 32; ++i) {
    if (i % 2 == 0) {
      ds_bitvector_set(bv, i, true);
    }
  }
  ds_bitvector_build(bv);

  /* rank1(i) = set bits in [0, i); select1(k) = position of the k-th set bit */
  printf("bitvector: ones=%lu, rank1(10)=%lu, select1(3)=%lu\n",
         (unsigned long)ds_bitvector_count_ones(bv),
         (unsigned long)ds_bitvector_rank1(bv, 10),
         (unsigned long)ds_bitvector_select1(bv, 3));
  ds_bitvector_destroy(bv);
}

static void wavelet_demo(void) {
  /* symbols over an alphabet [0, 6) */
  size_t seq[9];
  ds_wavelet_tree_t *wt;
  seq[0] = 3;
  seq[1] = 1;
  seq[2] = 4;
  seq[3] = 1;
  seq[4] = 5;
  seq[5] = 2;
  seq[6] = 1;
  seq[7] = 4;
  seq[8] = 3;

  wt = ds_wavelet_tree_create(seq, 9, 6);

  /* access(i) returns the symbol; rank(c, i) counts c in the prefix [0, i);
   * quantile(l, r, k) is the k-th smallest symbol in [l, r) */
  printf("wavelet: access(2)=%lu, rank(1, 9)=%lu, quantile([0,9), 4)=%lu\n",
         (unsigned long)ds_wavelet_tree_access(wt, 2),
         (unsigned long)ds_wavelet_tree_rank(wt, 1, 9),
         (unsigned long)ds_wavelet_tree_quantile(wt, 0, 9, 4));
  ds_wavelet_tree_destroy(wt);
}

int main(void) {
  bitset_demo();
  bitvector_demo();
  wavelet_demo();
  return 0;
}
