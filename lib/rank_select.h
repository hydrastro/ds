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
 * Backends, with their practical (not asymptotic-ideal) costs; W is the
 * word size, m the number of ones:
 *   ds_rs_plain()       the raw bits plus a rank directory (a size_t per
 *                       8 words) and a select hint per 512 ones and per
 *                       512 zeros: n + n / 8 (64-bit words) + O(n / 512)
 *                       words. rank and access O(1). select binary
 *                       searches the superblocks between two hints, then
 *                       scans at most 8 words: O(log g) for g superblocks
 *                       between hints, which is O(log n) when the bit
 *                       value is sparse in a region.
 *   ds_rs_rrr()         RRR (Raman, Raman & Rao) with fixed 15-bit blocks
 *                       stored as a 4-bit class plus a ceil(log2 C(15, c))
 *                       -bit offset, and per 32 blocks a rank and an
 *                       offset pointer: about nH0 + n (4 / 15 + 2 sizeof
 *                       (size_t) * 8 / 480) bits, i.e. the class overhead
 *                       is n / 3.75 bits, not o(n). rank and access walk
 *                       up to 32 classes and decode one block (O(1) with
 *                       a constant of 32); select binary searches all
 *                       superblocks: O(log n + 32).
 *   ds_rs_elias_fano()  the positions of the ones in Elias-Fano form
 *                       (lib/elias_fano): about m(2 + log(n/m)) bits for
 *                       m ones plus its select directories, best for
 *                       sparse vectors. select1 O(1); rank1 and access
 *                       one next_geq, O(1 + log b) for the b ones sharing
 *                       a high part; select0 binary searches the ones,
 *                       O(log m).
 *
 * The tables the backends use are constants: independent instances share
 * no mutable state.
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
