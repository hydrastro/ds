#ifndef DS_BITSET_H
#define DS_BITSET_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitset {
  unsigned long *words;
  size_t num_bits;
  size_t num_words;
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bitset_t;

/* NULL when memory runs out (create, create_alloc and clone) */
ds_bitset_t *ds_bitset_create(size_t num_bits);
ds_bitset_t *ds_bitset_create_alloc(size_t num_bits, void *(*allocator)(size_t),
                                    void (*deallocator)(void *));
void ds_bitset_set(ds_bitset_t *bitset, size_t index);
void ds_bitset_clear(ds_bitset_t *bitset, size_t index);
void ds_bitset_flip(ds_bitset_t *bitset, size_t index);
void ds_bitset_set_value(ds_bitset_t *bitset, size_t index, bool value);
/* Checked point access distinguishes an invalid index from a clear bit. */
ds_status_t ds_bitset_set_ex(ds_bitset_t *bitset, size_t index, bool value);
ds_status_t ds_bitset_get_ex(const ds_bitset_t *bitset, size_t index,
                             bool *out);
bool ds_bitset_test(ds_bitset_t *bitset, size_t index);
void ds_bitset_set_all(ds_bitset_t *bitset);
void ds_bitset_clear_all(ds_bitset_t *bitset);
size_t ds_bitset_count(ds_bitset_t *bitset);
size_t ds_bitset_size(ds_bitset_t *bitset);
bool ds_bitset_any(ds_bitset_t *bitset);
bool ds_bitset_none(ds_bitset_t *bitset);
bool ds_bitset_all(ds_bitset_t *bitset);
size_t ds_bitset_find_first(ds_bitset_t *bitset);
size_t ds_bitset_find_next(ds_bitset_t *bitset, size_t index);
bool ds_bitset_and(ds_bitset_t *bitset, ds_bitset_t *other);
bool ds_bitset_or(ds_bitset_t *bitset, ds_bitset_t *other);
bool ds_bitset_xor(ds_bitset_t *bitset, ds_bitset_t *other);
void ds_bitset_not(ds_bitset_t *bitset);
bool ds_bitset_equals(ds_bitset_t *bitset, ds_bitset_t *other);
ds_bitset_t *ds_bitset_clone(ds_bitset_t *bitset);
void ds_bitset_destroy(ds_bitset_t *bitset);

/* sentinel returned by find_first / find_next when no set bit exists */
#define DS_BITSET_NPOS ((size_t) - 1)

/* Context-owned internal storage; NULL context chooses the default.
 * The context must outlive the container. On failure *out is NULL. */
ds_status_t ds_bitset_create_context(size_t num_bits, ds_context_t *context,
                                     ds_bitset_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_BITSET_H */
