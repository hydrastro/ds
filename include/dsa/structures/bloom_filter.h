#ifndef DS_BLOOM_FILTER_H
#define DS_BLOOM_FILTER_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
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
  ds_context_t *context; /* NULL for allocator functions */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bloom_filter_t;

/* NULL when memory runs out, for sizes no size_t holds, or without an
 * allocator and a deallocator (create_alloc, create_with_params and
 * clone). Adding and querying never allocate. */
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

/* Checked constructors reject invalid sizing/rates; NULL context chooses the
 * default. Context must outlive the filter. Clone inherits the source context
 * when context is NULL. Constructor/clone failures leave out NULL. */
ds_status_t ds_bloom_filter_create_context(size_t expected, double rate,
                                           ds_context_t *context,
                                           ds_bloom_filter_t **out);
ds_status_t ds_bloom_filter_create_params_context(size_t bits, size_t hashes,
                                                  ds_context_t *context,
                                                  ds_bloom_filter_t **out);
ds_status_t ds_bloom_filter_clone_with(const ds_bloom_filter_t *filter,
                                       ds_context_t *context,
                                       ds_bloom_filter_t **out);
/* Keys may be NULL only for length zero. Count overflow/shape mismatch leave
 * the filter unchanged. Contains preserves out on error. A true result remains
 * probabilistic; count counts additions, not distinct keys. */
ds_status_t ds_bloom_filter_add_ex(ds_bloom_filter_t *filter, const void *key,
                                   size_t length);
ds_status_t ds_bloom_filter_contains_ex(ds_bloom_filter_t *filter,
                                        const void *key, size_t length,
                                        bool *out);
ds_status_t ds_bloom_filter_union_ex(ds_bloom_filter_t *filter,
                                     ds_bloom_filter_t *other);

#ifdef __cplusplus
}
#endif

#endif /* DS_BLOOM_FILTER_H */
