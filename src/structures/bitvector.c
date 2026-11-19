#include <dsa/structures/bitvector.h>
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

static ds_bitvector_t *bitvector_create_internal(size_t num_bits,
                                                 ds_context_t *context) {
  ds_bitvector_t *bv;
  size_t num_words;
  size_t i;
  num_words = num_bits / DS_BV_BITS_PER_WORD +
              (num_bits % DS_BV_BITS_PER_WORD != 0 ? 1 : 0);
  if (num_words > (size_t)-1 / sizeof(unsigned long)) {
    return NULL;
  }
  bv = (ds_bitvector_t *)ds_context_alloc(context, sizeof(ds_bitvector_t));
  if (bv == NULL) {
    return NULL;
  }
  bv->context = context != NULL ? context : ds_default_context();

  bv->num_bits = num_bits;
  bv->num_words = num_words;
  bv->words = num_words == 0 ? NULL
                             : (unsigned long *)ds_context_alloc(
                                   context, num_words * sizeof(unsigned long));
  if (num_words != 0 && bv->words == NULL) {
    ds_context_free(context, bv);
    return NULL;
  }
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
ds_bitvector_t *ds_bitvector_create(size_t num_bits, ds_context_t *context) {
  return bitvector_create_internal(num_bits, context);
}
ds_status_t ds_bitvector_create_context(size_t num_bits, ds_context_t *context,
                                        ds_bitvector_t **out) {
  size_t words = num_bits / (sizeof(unsigned long) * CHAR_BIT) +
                 (num_bits % (sizeof(unsigned long) * CHAR_BIT) != 0);
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (words > (size_t)-1 / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  *out = bitvector_create_internal(
      num_bits, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
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

ds_status_t ds_bitvector_set_ex(ds_bitvector_t *bv, size_t index, bool value) {
  if (bv == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= bv->num_bits) {
    return DS_ERR_RANGE;
  }
  if (bv->built) {
    return DS_ERR_STATE;
  }
  ds_bitvector_set(bv, index, value);
  return DS_OK;
}

ds_status_t ds_bitvector_build_ex(ds_bitvector_t *bv) {
  size_t i;
  size_t cumulative = 0;
  size_t sb;
  size_t blocks;
  size_t superblocks;
  size_t *superblock_rank;
  unsigned short *block_rank;
  if (bv == NULL) {
    return DS_ERR_NULL;
  }
  blocks = bv->num_words;
  superblocks = blocks / DS_BV_BLOCKS_PER_SUPERBLOCK +
                (blocks % DS_BV_BLOCKS_PER_SUPERBLOCK != 0 ? 1 : 0) + 1;
  if (superblocks > (size_t)-1 / sizeof(size_t) ||
      blocks >= (size_t)-1 / sizeof(unsigned short)) {
    return DS_ERR_OVERFLOW;
  }
  /* the new directory first: a failure leaves the old one in place */
  superblock_rank =
      (size_t *)ds_context_alloc(bv->context, superblocks * sizeof(size_t));
  block_rank = (unsigned short *)ds_context_alloc(
      bv->context, (blocks + 1) * sizeof(unsigned short));
  if (superblock_rank == NULL || block_rank == NULL) {
    if (superblock_rank != NULL) {
      ds_context_free(bv->context, superblock_rank);
    }
    if (block_rank != NULL) {
      ds_context_free(bv->context, block_rank);
    }
    return DS_ERR_ALLOC;
  }
  if (bv->superblock_rank != NULL) {
    ds_context_free(bv->context, bv->superblock_rank);
  }
  if (bv->block_rank != NULL) {
    ds_context_free(bv->context, bv->block_rank);
  }
  bv->num_blocks = blocks;
  bv->num_superblocks = superblocks;
  bv->superblock_rank = superblock_rank;
  bv->block_rank = block_rank;
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
  return DS_OK;
}

void ds_bitvector_build(ds_bitvector_t *bv) {
  (void)ds_bitvector_build_ex(bv);
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
    ds_context_free(bv->context, bv->words);
  }
  if (bv->superblock_rank != NULL) {
    ds_context_free(bv->context, bv->superblock_rank);
  }
  if (bv->block_rank != NULL) {
    ds_context_free(bv->context, bv->block_rank);
  }
  ds_context_free(bv->context, bv);
}

static ds_status_t bv_query_checked(ds_bitvector_t *bv, size_t index,
                                    bool ones, bool select, size_t *out) {
  size_t value;
  if (bv == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!bv->built) {
    return DS_ERR_STATE;
  }
  if (!select && index > bv->num_bits) {
    return DS_ERR_RANGE;
  }
  if (select) {
    value = ones ? ds_bitvector_select1(bv, index)
                 : ds_bitvector_select0(bv, index);
    if (value == DS_BITVECTOR_NPOS) {
      return DS_NOT_FOUND;
    }
  } else {
    value =
        ones ? ds_bitvector_rank1(bv, index) : ds_bitvector_rank0(bv, index);
  }
  *out = value;
  return DS_OK;
}
ds_status_t ds_bitvector_rank1_ex(ds_bitvector_t *bv, size_t index,
                                  size_t *out) {
  return bv_query_checked(bv, index, true, false, out);
}
ds_status_t ds_bitvector_rank0_ex(ds_bitvector_t *bv, size_t index,
                                  size_t *out) {
  return bv_query_checked(bv, index, false, false, out);
}
ds_status_t ds_bitvector_select1_ex(ds_bitvector_t *bv, size_t k,
                                    size_t *out) {
  return bv_query_checked(bv, k, true, true, out);
}
ds_status_t ds_bitvector_select0_ex(ds_bitvector_t *bv, size_t k,
                                    size_t *out) {
  return bv_query_checked(bv, k, false, true, out);
}
