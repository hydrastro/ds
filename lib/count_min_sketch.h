#ifndef DS_COUNT_MIN_SKETCH_H
#define DS_COUNT_MIN_SKETCH_H

/*
 * Count-Min sketch (Cormode & Muthukrishnan): approximate frequency counts
 * for a stream in width x depth counters. Estimates never undercount; with
 * width = ceil(e / epsilon) and depth = ceil(ln(1 / delta)) the overcount
 * is at most epsilon * (total count) with probability 1 - delta
 * (ds_cms_dimensions computes these).
 *
 * Items are fed as hashes computed by the caller; each row derives its own
 * index from that hash. With `conservative` set, an update only raises the
 * counters that are at the current minimum, which reduces overcounting
 * (estimates stay upper bounds). Sketches with equal dimensions merge by
 * adding counters (only meaningful without conservative updates).
 *
 * Storage: dynamic (ds_cms_init) or a caller array of width * depth
 * unsigned longs (ds_cms_init_buffer).
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_cms {
  unsigned long *counters;
  size_t width;
  size_t depth;
  unsigned long total;
  bool conservative;
  ds_context_t *context;
  bool fixed;
} ds_cms_t;

void ds_cms_dimensions(double epsilon, double delta, size_t *out_width,
                       size_t *out_depth);
ds_status_t ds_cms_init(ds_cms_t *cms, size_t width, size_t depth,
                        bool conservative, ds_context_t *context);
ds_status_t ds_cms_init_buffer(ds_cms_t *cms, size_t width, size_t depth,
                               bool conservative, unsigned long *counters);
void ds_cms_destroy(ds_cms_t *cms);
void ds_cms_add(ds_cms_t *cms, unsigned long hash, unsigned long count);
unsigned long ds_cms_estimate(const ds_cms_t *cms, unsigned long hash);
unsigned long ds_cms_total(const ds_cms_t *cms);
ds_status_t ds_cms_merge(ds_cms_t *dst, const ds_cms_t *src);
void ds_cms_clear(ds_cms_t *cms);

#ifdef __cplusplus
}
#endif

#endif /* DS_COUNT_MIN_SKETCH_H */
