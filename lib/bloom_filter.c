#include "bloom_filter.h"
#include "../algo/hash.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define DS_BLOOM_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)

static size_t bloom_compute_words(size_t num_bits) {
  if (num_bits == 0) {
    return 1;
  }
  return (num_bits + DS_BLOOM_BITS_PER_WORD - 1) / DS_BLOOM_BITS_PER_WORD;
}

ds_bloom_filter_t *ds_bloom_filter_create(size_t expected_items,
                                          double false_positive_rate) {
  return ds_bloom_filter_create_alloc(expected_items, false_positive_rate,
                                      malloc, free);
}

ds_bloom_filter_t *ds_bloom_filter_create_alloc(size_t expected_items,
                                                double false_positive_rate,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  size_t num_bits;
  size_t num_hashes;
  double n;
  double p;
  double ln2;
  double m;
  double k;
  n = expected_items > 0 ? (double)expected_items : 1.0;
  p = (false_positive_rate > 0.0 && false_positive_rate < 1.0)
          ? false_positive_rate
          : 0.01;
  ln2 = log(2.0);
  /* m = -n ln(p) / (ln2)^2 */
  m = -(n * log(p)) / (ln2 * ln2);
  if (!(m < (double)((size_t)-1 / 2))) { /* no size_t holds the bits */
    return NULL;
  }
  num_bits = (size_t)(m + 0.5);
  if (num_bits < DS_BLOOM_BITS_PER_WORD) {
    num_bits = DS_BLOOM_BITS_PER_WORD;
  }
  /* k = (m/n) ln2 */
  k = ((double)num_bits / n) * ln2;
  num_hashes = (size_t)(k + 0.5);
  if (num_hashes < 1) {
    num_hashes = 1;
  }
  return ds_bloom_filter_create_with_params(num_bits, num_hashes, allocator,
                                            deallocator);
}

ds_bloom_filter_t *
ds_bloom_filter_create_with_params(size_t num_bits, size_t num_hashes,
                                   void *(*allocator)(size_t),
                                   void (*deallocator)(void *)) {
  ds_bloom_filter_t *filter;
  size_t num_words;
  size_t i;
  if (num_bits < DS_BLOOM_BITS_PER_WORD) {
    num_bits = DS_BLOOM_BITS_PER_WORD;
  }
  if (num_hashes < 1) {
    num_hashes = 1;
  }
  if (allocator == NULL || deallocator == NULL ||
      num_bits > (size_t)-1 - DS_BLOOM_BITS_PER_WORD) {
    return NULL;
  }
  num_words = bloom_compute_words(num_bits);
  filter = (ds_bloom_filter_t *)allocator(sizeof(ds_bloom_filter_t));
  if (filter == NULL) {
    return NULL;
  }
  filter->allocator = allocator;
  filter->deallocator = deallocator;
  filter->num_bits = num_words * DS_BLOOM_BITS_PER_WORD;
  filter->num_words = num_words;
  filter->num_hashes = num_hashes;
  filter->num_items = 0;
  filter->words = (unsigned long *)allocator(num_words * sizeof(unsigned long));
  if (filter->words == NULL) {
    deallocator(filter);
    return NULL;
  }
  for (i = 0; i < num_words; i++) {
    filter->words[i] = 0UL;
  }
  return filter;
}

/* Hash the bytes twice, then avalanche each probe separately. Two nearby
 * FNV bases do not make independent hashes; arithmetic progressions also
 * produce unusually correlated codewords in small filters. O(length + k). */
static void bloom_hashes(const ds_bloom_filter_t *filter, const void *key,
                         size_t length, unsigned long *h1, unsigned long *h2) {
  (void)filter;
  *h1 = ds_hash_murmur3_32(key, length, 0UL);
  *h2 = ds_hash_murmur3_32(key, length, 0x9747b28cUL);
}

static size_t bloom_index(const ds_bloom_filter_t *filter, unsigned long h1,
                          unsigned long h2, size_t i) {
  unsigned long lo = ds_hash_mix32(h1 + (unsigned long)i * 0x9e3779b9UL);
  unsigned long hi = ds_hash_mix32(h2 + (unsigned long)i * 0x85ebca6bUL);
  size_t combined = (size_t)lo | (((size_t)hi << 16) << 16);
  return combined % filter->num_bits;
}

void ds_bloom_filter_add(ds_bloom_filter_t *filter, const void *key,
                         size_t length) {
  unsigned long h1;
  unsigned long h2;
  size_t i;
  bloom_hashes(filter, key, length, &h1, &h2);
  for (i = 0; i < filter->num_hashes; i++) {
    size_t bit = bloom_index(filter, h1, h2, i);
    filter->words[bit / DS_BLOOM_BITS_PER_WORD] |=
        1UL << (bit % DS_BLOOM_BITS_PER_WORD);
  }
  if (filter->num_items != (size_t)-1) {
    filter->num_items += 1;
  }
}

bool ds_bloom_filter_maybe_contains(ds_bloom_filter_t *filter, const void *key,
                                    size_t length) {
  unsigned long h1;
  unsigned long h2;
  size_t i;
  bool result = true;
  bloom_hashes(filter, key, length, &h1, &h2);
  for (i = 0; i < filter->num_hashes; i++) {
    size_t bit = bloom_index(filter, h1, h2, i);
    if ((filter->words[bit / DS_BLOOM_BITS_PER_WORD] &
         (1UL << (bit % DS_BLOOM_BITS_PER_WORD))) == 0UL) {
      result = false;
      break;
    }
  }
  return result;
}

void ds_bloom_filter_clear(ds_bloom_filter_t *filter) {
  size_t i;
  for (i = 0; i < filter->num_words; i++) {
    filter->words[i] = 0UL;
  }
  filter->num_items = 0;
}

size_t ds_bloom_filter_count(ds_bloom_filter_t *filter) {
  size_t result;
  result = filter->num_items;
  return result;
}

double ds_bloom_filter_estimated_fpr(ds_bloom_filter_t *filter) {
  double k;
  double n;
  double m;
  double result;
  k = (double)filter->num_hashes;
  n = (double)filter->num_items;
  m = (double)filter->num_bits;
  /* (1 - e^(-kn/m))^k */
  result = pow(1.0 - exp(-(k * n) / m), k);
  return result;
}

bool ds_bloom_filter_union(ds_bloom_filter_t *filter,
                           ds_bloom_filter_t *other) {
  size_t i;
  if (filter->num_bits != other->num_bits ||
      filter->num_hashes != other->num_hashes) {
    return false;
  }
  for (i = 0; i < filter->num_words; i++) {
    filter->words[i] |= other->words[i];
  }
  filter->num_items = other->num_items > (size_t)-1 - filter->num_items
                          ? (size_t)-1
                          : filter->num_items + other->num_items;
  return true;
}

ds_bloom_filter_t *ds_bloom_filter_clone(ds_bloom_filter_t *filter) {
  ds_bloom_filter_t *new_filter;
  size_t i;
  if (filter == NULL) {
    return NULL;
  }
  new_filter = ds_bloom_filter_create_with_params(
      filter->num_bits, filter->num_hashes, filter->allocator,
      filter->deallocator);
  if (new_filter == NULL) {
    return NULL;
  }
  for (i = 0; i < filter->num_words; i++) {
    new_filter->words[i] = filter->words[i];
  }
  new_filter->num_items = filter->num_items;
  return new_filter;
}

void ds_bloom_filter_destroy(ds_bloom_filter_t *filter) {
  filter->deallocator(filter->words);
  filter->deallocator(filter);
}
