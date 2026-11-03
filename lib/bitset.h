#ifndef DS_BITSET_H
#define DS_BITSET_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitset {
  unsigned long *words;
  size_t num_bits;
  size_t num_words;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bitset_t;

ds_bitset_t *FUNC(ds_bitset_create)(size_t num_bits);
ds_bitset_t *FUNC(ds_bitset_create_alloc)(size_t num_bits,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *));
void FUNC(ds_bitset_set)(ds_bitset_t *bitset, size_t index);
void FUNC(ds_bitset_clear)(ds_bitset_t *bitset, size_t index);
void FUNC(ds_bitset_flip)(ds_bitset_t *bitset, size_t index);
void FUNC(ds_bitset_set_value)(ds_bitset_t *bitset, size_t index, bool value);
bool FUNC(ds_bitset_test)(ds_bitset_t *bitset, size_t index);
void FUNC(ds_bitset_set_all)(ds_bitset_t *bitset);
void FUNC(ds_bitset_clear_all)(ds_bitset_t *bitset);
size_t FUNC(ds_bitset_count)(ds_bitset_t *bitset);
size_t FUNC(ds_bitset_size)(ds_bitset_t *bitset);
bool FUNC(ds_bitset_any)(ds_bitset_t *bitset);
bool FUNC(ds_bitset_none)(ds_bitset_t *bitset);
bool FUNC(ds_bitset_all)(ds_bitset_t *bitset);
size_t FUNC(ds_bitset_find_first)(ds_bitset_t *bitset);
size_t FUNC(ds_bitset_find_next)(ds_bitset_t *bitset, size_t index);
bool FUNC(ds_bitset_and)(ds_bitset_t *bitset, ds_bitset_t *other);
bool FUNC(ds_bitset_or)(ds_bitset_t *bitset, ds_bitset_t *other);
bool FUNC(ds_bitset_xor)(ds_bitset_t *bitset, ds_bitset_t *other);
void FUNC(ds_bitset_not)(ds_bitset_t *bitset);
bool FUNC(ds_bitset_equals)(ds_bitset_t *bitset, ds_bitset_t *other);
ds_bitset_t *FUNC(ds_bitset_clone)(ds_bitset_t *bitset);
void FUNC(ds_bitset_destroy)(ds_bitset_t *bitset);

/* sentinel returned by find_first / find_next when no set bit exists */
#define DS_BITSET_NPOS ((size_t)-1)

#ifdef __cplusplus
}
#endif

#endif /* DS_BITSET_H */
