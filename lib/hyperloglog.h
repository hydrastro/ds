#ifndef DS_HYPERLOGLOG_H
#define DS_HYPERLOGLOG_H

/*
 * HyperLogLog (Flajolet et al.): estimates the number of distinct items in
 * a stream using 2^precision one-byte registers. The standard error is about
 * 1.04 / sqrt(2^precision): precision 14 uses 16 KiB for ~0.8% error.
 *
 * Items are fed as 32-bit hashes computed by the caller (any good hash,
 * e.g. ds_hash_xxh32 from algo/hash.h). Small cardinalities use linear
 * counting and the large-range correction for 32-bit hashes is applied, so
 * estimates are good from 0 up to around 10^9 distinct items.
 *
 * Sketches with the same precision can be merged (union of the streams).
 * Storage: dynamic (ds_hll_init) or a caller array of 2^precision bytes
 * (ds_hll_init_buffer). Precision must be in [4, 16].
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_hll {
  unsigned char *registers;
  unsigned int precision;
  size_t count;
  ds_context_t *context;
  bool fixed;
} ds_hll_t;

ds_status_t ds_hll_init(ds_hll_t *hll, unsigned int precision,
                        ds_context_t *context);
ds_status_t ds_hll_init_buffer(ds_hll_t *hll, unsigned int precision,
                               unsigned char *registers);
void ds_hll_destroy(ds_hll_t *hll);
void ds_hll_add_hash(ds_hll_t *hll, unsigned long hash32);
double ds_hll_estimate(const ds_hll_t *hll);
ds_status_t ds_hll_merge(ds_hll_t *dst, const ds_hll_t *src);
void ds_hll_clear(ds_hll_t *hll);

#ifdef __cplusplus
}
#endif

#endif /* DS_HYPERLOGLOG_H */
