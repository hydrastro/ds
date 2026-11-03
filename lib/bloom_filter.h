#ifndef DS_BLOOM_FILTER_H
#define DS_BLOOM_FILTER_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bloom_filter {
  unsigned long *words;
  size_t num_bits;
  size_t num_words;
  size_t num_hashes;
  size_t num_items;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bloom_filter_t;

ds_bloom_filter_t *ds_bloom_filter_create(size_t expected_items,
                                          double false_positive_rate);
ds_bloom_filter_t *ds_bloom_filter_create_alloc(size_t expected_items,
                                                double false_positive_rate,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
ds_bloom_filter_t *
ds_bloom_filter_create_with_params(size_t num_bits, size_t num_hashes,
                                   void *(*allocator)(size_t),
                                   void (*deallocator)(void *));
void ds_bloom_filter_add(ds_bloom_filter_t *filter, const void *key,
                         size_t length);
bool ds_bloom_filter_maybe_contains(ds_bloom_filter_t *filter, const void *key,
                                    size_t length);
void ds_bloom_filter_clear(ds_bloom_filter_t *filter);
size_t ds_bloom_filter_count(ds_bloom_filter_t *filter);
double ds_bloom_filter_estimated_fpr(ds_bloom_filter_t *filter);
bool ds_bloom_filter_union(ds_bloom_filter_t *filter, ds_bloom_filter_t *other);
ds_bloom_filter_t *ds_bloom_filter_clone(ds_bloom_filter_t *filter);
void ds_bloom_filter_destroy(ds_bloom_filter_t *filter);

#ifdef __cplusplus
}
#endif

#endif /* DS_BLOOM_FILTER_H */
