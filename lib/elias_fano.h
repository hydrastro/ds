#ifndef DS_ELIAS_FANO_H
#define DS_ELIAS_FANO_H

/*
 * Elias-Fano encoding of a nondecreasing sequence of n integers below a
 * universe u, in about n (2 + log2(u / n)) bits: each value's low
 * l = floor(log2(u / n)) bits are packed verbatim, its high bits are
 * written in unary into a bitvector of n + (u >> l) + 1 bits.
 *
 * access(i) is one select1 on the high part plus a packed read;
 * next_geq(x) (the first element >= x, the core of posting-list
 * intersection) is one select0 plus a short scan; prev_leq likewise.
 * select uses samples every DS_EF_SAMPLE ones / zeros and word popcounts,
 * so both are O(1 + DS_EF_SAMPLE / word bits) word operations. Static:
 * built once from a sorted array. Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_EF_SAMPLE 256

typedef struct ds_elias_fano {
  size_t n;
  unsigned long universe;
  unsigned low_bits;
  unsigned long *low;  /* n * low_bits bits */
  unsigned long *high; /* high_bits bits */
  size_t high_bits;
  size_t *sample1; /* position of every DS_EF_SAMPLE-th one */
  size_t *sample0; /* position of every DS_EF_SAMPLE-th zero */
  size_t zeros;
  ds_context_t *context;
} ds_elias_fano_t;

/* DS_ERR_INVALID unless values are nondecreasing and < universe */
ds_status_t ds_elias_fano_build(ds_elias_fano_t *ef,
                                const unsigned long *values, size_t n,
                                unsigned long universe, ds_context_t *context);
void ds_elias_fano_destroy(ds_elias_fano_t *ef);
size_t ds_elias_fano_size(const ds_elias_fano_t *ef);
unsigned long ds_elias_fano_get(const ds_elias_fano_t *ef, size_t i);
/* first element >= x: its index and value (DS_NOT_FOUND if none) */
ds_status_t ds_elias_fano_next_geq(const ds_elias_fano_t *ef, unsigned long x,
                                   size_t *out_index, unsigned long *out);
/* last element <= x */
ds_status_t ds_elias_fano_prev_leq(const ds_elias_fano_t *ef, unsigned long x,
                                   size_t *out_index, unsigned long *out);
/* encoded size in bits (low + high parts, without samples) */
size_t ds_elias_fano_bits(const ds_elias_fano_t *ef);

#ifdef __cplusplus
}
#endif

#endif /* DS_ELIAS_FANO_H */
