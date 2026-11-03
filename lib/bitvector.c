#include "bitvector.h"
#include <limits.h>
#include <stdlib.h>

#define DS_BV_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)
/* one block == one word; a superblock groups DS_BV_BLOCKS_PER_SUPERBLOCK
 * blocks. block_rank holds the cumulative count within the superblock, which
 * fits in unsigned short as long as a superblock spans < 65536 bits. */
#define DS_BV_BLOCKS_PER_SUPERBLOCK 8

static size_t bv_popcount(unsigned long word) {
  size_t count = 0;
  while (word != 0UL) {
    word &= word - 1UL;
    count += 1;
  }
  return count;
}

ds_bitvector_t *ds_bitvector_create(size_t num_bits) {
  return ds_bitvector_create_alloc(num_bits, malloc, free);
}

ds_bitvector_t *ds_bitvector_create_alloc(size_t num_bits,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *)) {
  ds_bitvector_t *bv;
  size_t num_words;
  size_t i;
  num_words = num_bits == 0
                  ? 0
                  : (num_bits + DS_BV_BITS_PER_WORD - 1) / DS_BV_BITS_PER_WORD;
  bv = (ds_bitvector_t *)allocator(sizeof(ds_bitvector_t));
  bv->allocator = allocator;
  bv->deallocator = deallocator;
  bv->num_bits = num_bits;
  bv->num_words = num_words;
  bv->words =
      num_words == 0
          ? NULL
          : (unsigned long *)allocator(num_words * sizeof(unsigned long));
  for (i = 0; i < num_words; i++) {
    bv->words[i] = 0UL;
  }
  bv->superblock_rank = NULL;
  bv->block_rank = NULL;
  bv->num_superblocks = 0;
  bv->num_blocks = 0;
  bv->total_ones = 0;
  bv->built = false;
  return bv;
}

void ds_bitvector_set(ds_bitvector_t *bv, size_t index, bool value) {
  if (index < bv->num_bits) {
    size_t w = index / DS_BV_BITS_PER_WORD;
    unsigned long mask = 1UL << (index % DS_BV_BITS_PER_WORD);
    if (value) {
      bv->words[w] |= mask;
    } else {
      bv->words[w] &= ~mask;
    }
  }
}

bool ds_bitvector_get(ds_bitvector_t *bv, size_t index) {
  bool result = false;
  if (index < bv->num_bits) {
    size_t w = index / DS_BV_BITS_PER_WORD;
    result = (bv->words[w] & (1UL << (index % DS_BV_BITS_PER_WORD))) != 0UL;
  }
  return result;
}

void ds_bitvector_build(ds_bitvector_t *bv) {
  size_t i;
  size_t cumulative = 0;
  size_t sb;
  bv->num_blocks = bv->num_words;
  bv->num_superblocks = (bv->num_blocks + DS_BV_BLOCKS_PER_SUPERBLOCK - 1) /
                            DS_BV_BLOCKS_PER_SUPERBLOCK +
                        1;
  if (bv->superblock_rank != NULL) {
    bv->deallocator(bv->superblock_rank);
  }
  if (bv->block_rank != NULL) {
    bv->deallocator(bv->block_rank);
  }
  bv->superblock_rank =
      (size_t *)bv->allocator(bv->num_superblocks * sizeof(size_t));
  bv->block_rank = (unsigned short *)bv->allocator((bv->num_blocks + 1) *
                                                   sizeof(unsigned short));
  sb = 0;
  bv->superblock_rank[0] = 0;
  for (i = 0; i < bv->num_blocks; i++) {
    if (i % DS_BV_BLOCKS_PER_SUPERBLOCK == 0) {
      sb = i / DS_BV_BLOCKS_PER_SUPERBLOCK;
      bv->superblock_rank[sb] = cumulative;
    }
    bv->block_rank[i] = (unsigned short)(cumulative - bv->superblock_rank[sb]);
    cumulative += bv_popcount(bv->words[i]);
  }
  bv->block_rank[bv->num_blocks] = 0;
  bv->superblock_rank[(bv->num_blocks + DS_BV_BLOCKS_PER_SUPERBLOCK - 1) /
                      DS_BV_BLOCKS_PER_SUPERBLOCK] = cumulative;
  bv->total_ones = cumulative;
  bv->built = true;
}

size_t ds_bitvector_size(ds_bitvector_t *bv) {
  size_t result;
  result = bv->num_bits;
  return result;
}

size_t ds_bitvector_count_ones(ds_bitvector_t *bv) {
  size_t result;
  result = bv->total_ones;
  return result;
}

/* internal rank1 with no locking (callers hold the lock) */
static size_t bv_rank1_nolock(ds_bitvector_t *bv, size_t index) {
  size_t block;
  size_t sb;
  size_t count;
  unsigned long word;
  unsigned long mask;
  size_t bit_in_word;
  if (index >= bv->num_bits) {
    index = bv->num_bits;
  }
  if (index == 0) {
    return 0;
  }
  /* if index lands exactly on the end, return the precomputed total */
  if (index == bv->num_bits && bv->num_bits % DS_BV_BITS_PER_WORD == 0) {
    return bv->total_ones;
  }
  block = index / DS_BV_BITS_PER_WORD;
  sb = block / DS_BV_BLOCKS_PER_SUPERBLOCK;
  count = bv->superblock_rank[sb] + (size_t)bv->block_rank[block];
  bit_in_word = index % DS_BV_BITS_PER_WORD;
  if (bit_in_word != 0) {
    word = bv->words[block];
    mask = (1UL << bit_in_word) - 1UL;
    count += bv_popcount(word & mask);
  }
  return count;
}

size_t ds_bitvector_rank1(ds_bitvector_t *bv, size_t index) {
  size_t result;
  result = bv_rank1_nolock(bv, index);
  return result;
}

size_t ds_bitvector_rank0(ds_bitvector_t *bv, size_t index) {
  size_t result;
  if (index > bv->num_bits) {
    index = bv->num_bits;
  }
  result = index - bv_rank1_nolock(bv, index);
  return result;
}

size_t ds_bitvector_select1(ds_bitvector_t *bv, size_t k) {
  size_t lo;
  size_t hi;
  size_t result = DS_BITVECTOR_NPOS;
  if (k < bv->total_ones) {
    /* find smallest index i such that rank1(i+1) == k+1, i.e. the (k+1)-th one
     * lives at position i; binary search on prefix rank */
    lo = 0;
    hi = bv->num_bits;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (bv_rank1_nolock(bv, mid + 1) >= k + 1) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    result = lo;
  }
  return result;
}

size_t ds_bitvector_select0(ds_bitvector_t *bv, size_t k) {
  size_t lo;
  size_t hi;
  size_t total_zeros;
  size_t result = DS_BITVECTOR_NPOS;
  total_zeros = bv->num_bits - bv->total_ones;
  if (k < total_zeros) {
    lo = 0;
    hi = bv->num_bits;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      size_t zeros = (mid + 1) - bv_rank1_nolock(bv, mid + 1);
      if (zeros >= k + 1) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    result = lo;
  }
  return result;
}

void ds_bitvector_destroy(ds_bitvector_t *bv) {
  if (bv->words != NULL) {
    bv->deallocator(bv->words);
  }
  if (bv->superblock_rank != NULL) {
    bv->deallocator(bv->superblock_rank);
  }
  if (bv->block_rank != NULL) {
    bv->deallocator(bv->block_rank);
  }
  bv->deallocator(bv);
}
