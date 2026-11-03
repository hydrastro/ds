#include "hyperloglog.h"
#include <math.h>
#include <string.h>

#define HLL_M32 0xFFFFFFFFUL

static ds_status_t hll_check(unsigned int precision) {
  return precision < 4 || precision > 16 ? DS_ERR_INVALID : DS_OK;
}

ds_status_t ds_hll_init(ds_hll_t *hll, unsigned int precision,
                        ds_context_t *context) {
  if (hll == NULL) {
    return DS_ERR_NULL;
  }
  if (hll_check(precision) != DS_OK) {
    return DS_ERR_INVALID;
  }
  hll->precision = precision;
  hll->count = (size_t)1 << precision;
  hll->context = context != NULL ? context : ds_default_context();
  hll->fixed = false;
  hll->registers =
      (unsigned char *)ds_context_calloc(hll->context, hll->count, 1);
  return hll->registers == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_status_t ds_hll_init_buffer(ds_hll_t *hll, unsigned int precision,
                               unsigned char *registers) {
  if (hll == NULL || registers == NULL) {
    return DS_ERR_NULL;
  }
  if (hll_check(precision) != DS_OK) {
    return DS_ERR_INVALID;
  }
  hll->precision = precision;
  hll->count = (size_t)1 << precision;
  hll->registers = registers;
  hll->context = NULL;
  hll->fixed = true;
  memset(registers, 0, hll->count);
  return DS_OK;
}

void ds_hll_destroy(ds_hll_t *hll) {
  if (hll == NULL) {
    return;
  }
  if (!hll->fixed) {
    ds_context_free(hll->context, hll->registers);
  }
  hll->registers = NULL;
}

void ds_hll_add_hash(ds_hll_t *hll, unsigned long hash32) {
  unsigned long h = hash32 & HLL_M32;
  unsigned int p = hll->precision;
  size_t index = (size_t)(h >> (32U - p));
  unsigned long rest = (h << p) & HLL_M32;
  unsigned char rank = 1;
  /* rank = leading zeros of the remaining 32 - p bits, plus one */
  while (rank <= 32U - p && (rest & 0x80000000UL) == 0) {
    rank++;
    rest <<= 1;
  }
  if (rank > hll->registers[index]) {
    hll->registers[index] = rank;
  }
}

double ds_hll_estimate(const ds_hll_t *hll) {
  double m = (double)hll->count;
  double alpha;
  double sum = 0.0;
  double estimate;
  size_t zeros = 0;
  size_t i;
  const double two32 = 4294967296.0;
  if (hll->count == 16) {
    alpha = 0.673;
  } else if (hll->count == 32) {
    alpha = 0.697;
  } else if (hll->count == 64) {
    alpha = 0.709;
  } else {
    alpha = 0.7213 / (1.0 + 1.079 / m);
  }
  for (i = 0; i < hll->count; i++) {
    sum += ldexp(1.0, -(int)hll->registers[i]);
    if (hll->registers[i] == 0) {
      zeros++;
    }
  }
  estimate = alpha * m * m / sum;
  if (estimate <= 2.5 * m && zeros > 0) {
    return m * log(m / (double)zeros); /* linear counting */
  }
  if (estimate > two32 / 30.0) {
    return -two32 * log(1.0 - estimate / two32);
  }
  return estimate;
}

ds_status_t ds_hll_merge(ds_hll_t *dst, const ds_hll_t *src) {
  size_t i;
  if (dst == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (dst->precision != src->precision) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < dst->count; i++) {
    if (src->registers[i] > dst->registers[i]) {
      dst->registers[i] = src->registers[i];
    }
  }
  return DS_OK;
}

void ds_hll_clear(ds_hll_t *hll) {
  memset(hll->registers, 0, hll->count);
}
