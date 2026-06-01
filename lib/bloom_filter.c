#include "bloom_filter.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define DS_BLOOM_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)

/* FNV-1a hash using 32-bit parameters so the constants fit in any
 * conforming unsigned long (which is at least 32 bits wide). */
static unsigned long FUNC(bloom_hash_fnv)(const void *key, size_t length,
                                          unsigned long basis) {
  const unsigned char *bytes = (const unsigned char *)key;
  unsigned long hash = basis;
  size_t i;
  for (i = 0; i < length; i++) {
    hash ^= (unsigned long)bytes[i];
    hash *= 16777619UL;
    hash &= 0xFFFFFFFFUL;
  }
  return hash;
}

static size_t FUNC(bloom_compute_words)(size_t num_bits) {
  if (num_bits == 0) {
    return 1;
  }
  return (num_bits + DS_BLOOM_BITS_PER_WORD - 1) / DS_BLOOM_BITS_PER_WORD;
}

ds_bloom_filter_t *FUNC(ds_bloom_filter_create)(size_t expected_items,
                                                double false_positive_rate) {
  return FUNC(ds_bloom_filter_create_alloc)(expected_items, false_positive_rate,
                                            malloc, free);
}

ds_bloom_filter_t *FUNC(ds_bloom_filter_create_alloc)(
    size_t expected_items, double false_positive_rate,
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
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
  return FUNC(ds_bloom_filter_create_with_params)(num_bits, num_hashes,
                                                  allocator, deallocator);
}

ds_bloom_filter_t *
FUNC(ds_bloom_filter_create_with_params)(size_t num_bits, size_t num_hashes,
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
  num_words = FUNC(bloom_compute_words)(num_bits);
  filter = (ds_bloom_filter_t *)allocator(sizeof(ds_bloom_filter_t));
  filter->allocator = allocator;
  filter->deallocator = deallocator;
  filter->num_bits = num_words * DS_BLOOM_BITS_PER_WORD;
  filter->num_words = num_words;
  filter->num_hashes = num_hashes;
  filter->num_items = 0;
  filter->words = (unsigned long *)allocator(num_words * sizeof(unsigned long));
  for (i = 0; i < num_words; i++) {
    filter->words[i] = 0UL;
  }
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(filter);
#endif
  return filter;
}

static void FUNC(bloom_each_index)(ds_bloom_filter_t *filter, const void *key,
                                   size_t length, size_t *out) {
  unsigned long h1 = FUNC(bloom_hash_fnv)(key, length, 2166136261UL);
  unsigned long h2 = FUNC(bloom_hash_fnv)(key, length, 2166136333UL);
  size_t i;
  if ((h2 % filter->num_bits) == 0) {
    h2 += 1;
  }
  for (i = 0; i < filter->num_hashes; i++) {
    unsigned long combined = h1 + (unsigned long)i * h2;
    out[i] = (size_t)(combined % filter->num_bits);
  }
}

void FUNC(ds_bloom_filter_add)(ds_bloom_filter_t *filter, const void *key,
                               size_t length) {
  size_t *indices;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  indices = (size_t *)filter->allocator(filter->num_hashes * sizeof(size_t));
  FUNC(bloom_each_index)(filter, key, length, indices);
  for (i = 0; i < filter->num_hashes; i++) {
    size_t bit = indices[i];
    filter->words[bit / DS_BLOOM_BITS_PER_WORD] |=
        1UL << (bit % DS_BLOOM_BITS_PER_WORD);
  }
  filter->num_items += 1;
  filter->deallocator(indices);
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
}

bool FUNC(ds_bloom_filter_maybe_contains)(ds_bloom_filter_t *filter,
                                          const void *key, size_t length) {
  size_t *indices;
  size_t i;
  bool result = true;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  indices = (size_t *)filter->allocator(filter->num_hashes * sizeof(size_t));
  FUNC(bloom_each_index)(filter, key, length, indices);
  for (i = 0; i < filter->num_hashes; i++) {
    size_t bit = indices[i];
    if ((filter->words[bit / DS_BLOOM_BITS_PER_WORD] &
         (1UL << (bit % DS_BLOOM_BITS_PER_WORD))) == 0UL) {
      result = false;
      break;
    }
  }
  filter->deallocator(indices);
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
  return result;
}

void FUNC(ds_bloom_filter_clear)(ds_bloom_filter_t *filter) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  for (i = 0; i < filter->num_words; i++) {
    filter->words[i] = 0UL;
  }
  filter->num_items = 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
}

size_t FUNC(ds_bloom_filter_count)(ds_bloom_filter_t *filter) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  result = filter->num_items;
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
  return result;
}

double FUNC(ds_bloom_filter_estimated_fpr)(ds_bloom_filter_t *filter) {
  double k;
  double n;
  double m;
  double result;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  k = (double)filter->num_hashes;
  n = (double)filter->num_items;
  m = (double)filter->num_bits;
  /* (1 - e^(-kn/m))^k */
  result = pow(1.0 - exp(-(k * n) / m), k);
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
  return result;
}

bool FUNC(ds_bloom_filter_union)(ds_bloom_filter_t *filter,
                                 ds_bloom_filter_t *other) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  if (filter->num_bits != other->num_bits ||
      filter->num_hashes != other->num_hashes) {
#ifdef DS_THREAD_SAFE
    UNLOCK(filter);
#endif
    return false;
  }
  for (i = 0; i < filter->num_words; i++) {
    filter->words[i] |= other->words[i];
  }
  filter->num_items += other->num_items;
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
  return true;
}

ds_bloom_filter_t *FUNC(ds_bloom_filter_clone)(ds_bloom_filter_t *filter) {
  ds_bloom_filter_t *new_filter;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(filter);
#endif
  new_filter = FUNC(ds_bloom_filter_create_with_params)(
      filter->num_bits, filter->num_hashes, filter->allocator,
      filter->deallocator);
  for (i = 0; i < filter->num_words; i++) {
    new_filter->words[i] = filter->words[i];
  }
  new_filter->num_items = filter->num_items;
#ifdef DS_THREAD_SAFE
  UNLOCK(filter);
#endif
  return new_filter;
}

void FUNC(ds_bloom_filter_destroy)(ds_bloom_filter_t *filter) {
  filter->deallocator(filter->words);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(filter);
#endif
  filter->deallocator(filter);
}
