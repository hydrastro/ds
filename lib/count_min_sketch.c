#include "count_min_sketch.h"
#include "checked.h"
#include <math.h>
#include <string.h>

#define CMS_SIZE_MAX (~(size_t)0)

static unsigned long cms_mix(unsigned long h) {
  h &= 0xFFFFFFFFUL;
  h ^= h >> 16;
  h = (h * 0x7FEB352DUL) & 0xFFFFFFFFUL;
  h ^= h >> 15;
  h = (h * 0x846CA68BUL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

/* Kirsch-Mitzenmacher: row i uses h1 + i * h2. */
static size_t cms_index(const ds_cms_t *cms, unsigned long hash, size_t row) {
  unsigned long h1 = cms_mix(hash ^ (hash >> 16 >> 16));
  unsigned long h2 = cms_mix(h1 ^ 0x9E3779B9UL) | 1UL;
  unsigned long h = (h1 + (unsigned long)row * h2) & 0xFFFFFFFFUL;
  return row * cms->width + (size_t)(h % (unsigned long)cms->width);
}

void ds_cms_dimensions(double epsilon, double delta, size_t *out_width,
                       size_t *out_depth) {
  double w = ceil(2.718281828459045 / epsilon);
  double d = ceil(log(1.0 / delta));
  if (out_width != NULL) {
    *out_width = w < 1.0 ? 1 : (size_t)w;
  }
  if (out_depth != NULL) {
    *out_depth = d < 1.0 ? 1 : (size_t)d;
  }
}

static ds_status_t cms_setup(ds_cms_t *cms, size_t width, size_t depth,
                             bool conservative) {
  if (cms == NULL) {
    return DS_ERR_NULL;
  }
  if (width == 0 || depth == 0 || DS_ABOVE_U32(width)) {
    return DS_ERR_INVALID;
  }
  if (depth > CMS_SIZE_MAX / width ||
      width * depth > CMS_SIZE_MAX / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  cms->width = width;
  cms->depth = depth;
  cms->total = 0;
  cms->conservative = conservative;
  return DS_OK;
}

ds_status_t ds_cms_init(ds_cms_t *cms, size_t width, size_t depth,
                        bool conservative, ds_context_t *context) {
  ds_status_t status = cms_setup(cms, width, depth, conservative);
  if (status != DS_OK) {
    return status;
  }
  cms->context = context != NULL ? context : ds_default_context();
  cms->fixed = false;
  cms->counters = (unsigned long *)ds_context_calloc(
      cms->context, width * depth, sizeof(unsigned long));
  return cms->counters == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_status_t ds_cms_init_buffer(ds_cms_t *cms, size_t width, size_t depth,
                               bool conservative, unsigned long *counters) {
  ds_status_t status;
  if (counters == NULL) {
    return DS_ERR_NULL;
  }
  status = cms_setup(cms, width, depth, conservative);
  if (status != DS_OK) {
    return status;
  }
  cms->counters = counters;
  cms->context = NULL;
  cms->fixed = true;
  ds_cms_clear(cms);
  return DS_OK;
}

void ds_cms_destroy(ds_cms_t *cms) {
  if (cms == NULL) {
    return;
  }
  if (!cms->fixed) {
    ds_context_free(cms->context, cms->counters);
  }
  cms->counters = NULL;
}

void ds_cms_add(ds_cms_t *cms, unsigned long hash, unsigned long count) {
  size_t row;
  size_t i;
  unsigned long target;
  cms->total += count;
  if (!cms->conservative) {
    for (row = 0; row < cms->depth; row++) {
      cms->counters[cms_index(cms, hash, row)] += count;
    }
    return;
  }
  target = ds_cms_estimate(cms, hash) + count;
  for (row = 0; row < cms->depth; row++) {
    i = cms_index(cms, hash, row);
    if (cms->counters[i] < target) {
      cms->counters[i] = target;
    }
  }
}

unsigned long ds_cms_estimate(const ds_cms_t *cms, unsigned long hash) {
  unsigned long best = ~0UL;
  unsigned long c;
  size_t row;
  for (row = 0; row < cms->depth; row++) {
    c = cms->counters[cms_index(cms, hash, row)];
    if (c < best) {
      best = c;
    }
  }
  return best;
}

unsigned long ds_cms_total(const ds_cms_t *cms) { return cms->total; }

ds_status_t ds_cms_merge(ds_cms_t *dst, const ds_cms_t *src) {
  size_t i;
  if (dst == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (dst->width != src->width || dst->depth != src->depth) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < dst->width * dst->depth; i++) {
    dst->counters[i] += src->counters[i];
  }
  dst->total += src->total;
  return DS_OK;
}

void ds_cms_clear(ds_cms_t *cms) {
  memset(cms->counters, 0, cms->width * cms->depth * sizeof(unsigned long));
  cms->total = 0;
}
