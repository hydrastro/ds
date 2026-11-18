#ifndef DS_BITVECTOR_H
#define DS_BITVECTOR_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
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
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bitvector_t;

#define DS_BITVECTOR_NPOS ((size_t) - 1)

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_bitvector_t *ds_bitvector_create(size_t num_bits);
ds_bitvector_t *ds_bitvector_create_alloc(size_t num_bits,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *));
void ds_bitvector_set(ds_bitvector_t *bv, size_t index, bool value);
/* Checked set rejects writes after build (DS_ERR_STATE). Checked rank/select
 * require a completed build; errors preserve outputs. Rank accepts 0..num_bits;
 * select returns DS_NOT_FOUND when k has no corresponding bit. */
ds_status_t ds_bitvector_set_ex(ds_bitvector_t *bv, size_t index, bool value);
ds_status_t ds_bitvector_rank1_ex(ds_bitvector_t *bv, size_t index,
                                  size_t *out);
ds_status_t ds_bitvector_rank0_ex(ds_bitvector_t *bv, size_t index,
                                  size_t *out);
ds_status_t ds_bitvector_select1_ex(ds_bitvector_t *bv, size_t k, size_t *out);
ds_status_t ds_bitvector_select0_ex(ds_bitvector_t *bv, size_t k, size_t *out);
bool ds_bitvector_get(ds_bitvector_t *bv, size_t index);
/* Direct build: when the rank directory cannot be allocated, the vector
 * keeps the directory it had (none before a first build, so rank and
 * select must not be called); build_ex reports it */
void ds_bitvector_build(ds_bitvector_t *bv);
/* DS_ERR_ALLOC when the rank directory cannot be allocated, with the
 * vector unchanged */
ds_status_t ds_bitvector_build_ex(ds_bitvector_t *bv);
size_t ds_bitvector_size(ds_bitvector_t *bv);
size_t ds_bitvector_count_ones(ds_bitvector_t *bv);
/* rank1(i): number of set bits in [0, i). rank0(i) = i - rank1(i). */
size_t ds_bitvector_rank1(ds_bitvector_t *bv, size_t index);
size_t ds_bitvector_rank0(ds_bitvector_t *bv, size_t index);
/* select1(k): position of the k-th set bit (0-based). NPOS if out of range. */
size_t ds_bitvector_select1(ds_bitvector_t *bv, size_t k);
size_t ds_bitvector_select0(ds_bitvector_t *bv, size_t k);
void ds_bitvector_destroy(ds_bitvector_t *bv);

/* Context-owned internal storage; NULL context chooses the default.
 * The context must outlive the container. On failure *out is NULL. */
ds_status_t ds_bitvector_create_context(size_t num_bits, ds_context_t *context,
                                        ds_bitvector_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_BITVECTOR_H */
