#ifndef DS_BITVECTOR_H
#define DS_BITVECTOR_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Static rank/select bitvector. Bits are written while the vector is in the
 * "mutable" phase (set/clear); calling ds_bitvector_build freezes the contents
 * and computes a two-level rank index (superblocks + blocks) so that rank1
 * answers in O(1) and select1 in O(log n) via binary search over the index.
 * After build the bit contents must not be modified.
 */

typedef struct bitvector {
  unsigned long *words;
  size_t num_bits;
  size_t num_words;
  /* rank index, populated by ds_bitvector_build */
  size_t *superblock_rank;    /* cumulative popcount at each superblock start */
  unsigned short *block_rank; /* popcount within superblock at each block */
  size_t num_superblocks;
  size_t num_blocks;
  size_t total_ones;
  bool built;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_bitvector_t;

#define DS_BITVECTOR_NPOS ((size_t)-1)

ds_bitvector_t *FUNC(ds_bitvector_create)(size_t num_bits);
ds_bitvector_t *FUNC(ds_bitvector_create_alloc)(size_t num_bits,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
void FUNC(ds_bitvector_set)(ds_bitvector_t *bv, size_t index, bool value);
bool FUNC(ds_bitvector_get)(ds_bitvector_t *bv, size_t index);
void FUNC(ds_bitvector_build)(ds_bitvector_t *bv);
size_t FUNC(ds_bitvector_size)(ds_bitvector_t *bv);
size_t FUNC(ds_bitvector_count_ones)(ds_bitvector_t *bv);
/* rank1(i): number of set bits in [0, i). rank0(i) = i - rank1(i). */
size_t FUNC(ds_bitvector_rank1)(ds_bitvector_t *bv, size_t index);
size_t FUNC(ds_bitvector_rank0)(ds_bitvector_t *bv, size_t index);
/* select1(k): position of the k-th set bit (0-based). NPOS if out of range. */
size_t FUNC(ds_bitvector_select1)(ds_bitvector_t *bv, size_t k);
size_t FUNC(ds_bitvector_select0)(ds_bitvector_t *bv, size_t k);
void FUNC(ds_bitvector_destroy)(ds_bitvector_t *bv);

#ifdef __cplusplus
}
#endif

#endif /* DS_BITVECTOR_H */
