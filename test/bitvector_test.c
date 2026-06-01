#include "../lib/bitvector.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  size_t n = 5000;
  ds_bitvector_t *bv = ds_bitvector_create(n);
  size_t i;
  size_t ones = 0;
  size_t naive_rank;
  size_t r;
  size_t *positions;
  size_t pos_count = 0;

  positions = (size_t *)malloc(sizeof(size_t) * n);

  /* deterministic pseudo-random pattern */
  for (i = 0; i < n; ++i) {
    int bit = ((i * 2654435761U) >> 13) & 1U;
    if (bit) {
      ds_bitvector_set(bv, i, true);
      positions[pos_count] = i;
      pos_count += 1;
      ones += 1;
    }
  }
  ds_bitvector_build(bv);
  printf("size=%lu ones=%lu\n", (unsigned long)ds_bitvector_size(bv),
         (unsigned long)ds_bitvector_count_ones(bv));
  assert(ds_bitvector_count_ones(bv) == ones);

  /* rank1 cross-checked against a naive scan at several points */
  for (i = 0; i <= n; i += 137) {
    size_t j;
    naive_rank = 0;
    for (j = 0; j < i; ++j) {
      if (ds_bitvector_get(bv, j)) {
        naive_rank += 1;
      }
    }
    r = ds_bitvector_rank1(bv, i);
    assert(r == naive_rank);
    assert(ds_bitvector_rank0(bv, i) == i - naive_rank);
  }
  printf("rank1/rank0 verified\n");

  /* exact word-boundary index must equal the total count of ones */
  assert(ds_bitvector_rank1(bv, n) == ones);
  {
    size_t wb = 64; /* a likely word multiple */
    while (wb < n) {
      assert(ds_bitvector_rank1(bv, wb) + ds_bitvector_rank0(bv, wb) == wb);
      wb += 64;
    }
  }
  printf("word-boundary ranks verified\n");

  /* select1: the k-th set bit must equal positions[k], and rank1 inverts it */
  for (i = 0; i < pos_count; i += 7) {
    size_t sel = ds_bitvector_select1(bv, i);
    assert(sel == positions[i]);
    assert(ds_bitvector_rank1(bv, sel) == i);
    assert(ds_bitvector_get(bv, sel));
  }
  printf("select1 verified across %lu ones\n", (unsigned long)pos_count);

  /* out of range select returns NPOS */
  assert(ds_bitvector_select1(bv, ones) == DS_BITVECTOR_NPOS);
  assert(ds_bitvector_select0(bv, n - ones) == DS_BITVECTOR_NPOS);

  /* select0 inverts rank0 */
  for (i = 0; i < n - ones; i += 53) {
    size_t sel = ds_bitvector_select0(bv, i);
    assert(sel != DS_BITVECTOR_NPOS);
    assert(!ds_bitvector_get(bv, sel));
    assert((sel + 1) - ds_bitvector_rank1(bv, sel + 1) == i + 1);
  }
  printf("select0 verified\n");

  free(positions);
  ds_bitvector_destroy(bv);
  printf("ok\n");
  return 0;
}
