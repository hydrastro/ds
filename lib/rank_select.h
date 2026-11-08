#ifndef DS_RANK_SELECT_H
#define DS_RANK_SELECT_H

/*
 * The rank/select capability over static bit vectors: what succinct
 * structures (wavelet trees and matrices, FM-indexes, succinct trees,
 * compressed posting lists) need from a bit vector, with the space/time
 * trade-off left to the caller.
 *
 *   rank1(i)    ones in [0, i)            rank0 follows
 *   select1(k)  position of the k-th one (0-based); n when k >= ones
 *   select0(k)  likewise for zeros
 *   access(i)   bit i
 *
 * Backends:
 *   ds_rs_plain()       the raw bits plus a two-level rank directory and
 *                       sampled select hints: n + o(n) bits, O(1) rank
 *                       and near-constant select
 *   ds_rs_rrr()         RRR (Raman, Raman & Rao): 15-bit blocks stored as
 *                       (class, offset) pairs in nH0 + o(n) bits; rank
 *                       and access decode one block
 *   ds_rs_elias_fano()  the positions of the ones in Elias-Fano form
 *                       (lib/elias_fano): about m(2 + log(n/m)) bits for
 *                       m ones, best for sparse vectors
 *
 * Bits come as unsigned long words, bit i at (words[i / W] >> (i % W)) & 1.
 * Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_rs_ops {
  const char *name;
  ds_status_t (*build)(void **state, const unsigned long *words, size_t n,
                       ds_context_t *context);
  size_t (*rank1)(const void *state, size_t i);
  size_t (*select1)(const void *state, size_t k);
  size_t (*select0)(const void *state, size_t k);
  bool (*access)(const void *state, size_t i);
  size_t (*bytes)(const void *state);
  void (*destroy)(void *state, ds_context_t *context);
} ds_rs_ops_t;

const ds_rs_ops_t *ds_rs_plain(void);
const ds_rs_ops_t *ds_rs_rrr(void);
const ds_rs_ops_t *ds_rs_elias_fano(void);

typedef struct ds_rs {
  const ds_rs_ops_t *ops;
  void *state;
  size_t n;
  size_t ones;
  ds_context_t *context;
} ds_rs_t;

/* ops NULL selects ds_rs_plain() */
ds_status_t ds_rs_build(ds_rs_t *rs, const ds_rs_ops_t *ops,
                        const unsigned long *words, size_t n,
                        ds_context_t *context);
void ds_rs_destroy(ds_rs_t *rs);
size_t ds_rs_size(const ds_rs_t *rs);
size_t ds_rs_ones(const ds_rs_t *rs);
size_t ds_rs_rank1(const ds_rs_t *rs, size_t i);
size_t ds_rs_rank0(const ds_rs_t *rs, size_t i);
size_t ds_rs_select1(const ds_rs_t *rs, size_t k);
size_t ds_rs_select0(const ds_rs_t *rs, size_t k);
bool ds_rs_access(const ds_rs_t *rs, size_t i);
size_t ds_rs_bytes(const ds_rs_t *rs);

#ifdef __cplusplus
}
#endif

#endif /* DS_RANK_SELECT_H */
