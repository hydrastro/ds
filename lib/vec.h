#ifndef DS_VEC_H
#define DS_VEC_H

/*
 * Vector: a contiguous array of fixed-size elements stored by value.
 *
 * Two storage modes:
 *   - dynamic: ds_vec_init() grows the block through a ds_context_t
 *     (amortized O(1) push);
 *   - fixed: ds_vec_init_buffer() uses caller-owned memory (a static array,
 *     a stack array, arena memory) and never allocates. Operations that would
 *     need more room return DS_ERR_OVERFLOW.
 *
 * The struct is public so a vector can live on the stack or in static
 * storage. Element pointers returned by the accessors stay valid until the
 * next operation that changes the size or the capacity.
 *
 * Complexity:
 *   at / get / set / back / swap_remove     O(1)
 *   push / pop                              O(1) amortized (O(1) when fixed)
 *   insert / erase                          O(n - index)
 *   sort (heapsort, in place, no recursion) O(n log n) worst case, O(1) space
 *   lower_bound / binary_search             O(log n)
 *
 * Concurrency: a vector may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_vec_compare_func_t)(const void *a, const void *b, void *user);

typedef struct ds_vec {
  unsigned char *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  ds_context_t *context;
  bool fixed;
} ds_vec_t;

ds_status_t FUNC(ds_vec_init)(ds_vec_t *vec, size_t elem_size,
                              ds_context_t *context);
ds_status_t FUNC(ds_vec_init_buffer)(ds_vec_t *vec, size_t elem_size,
                                     void *buffer, size_t capacity);
void FUNC(ds_vec_destroy)(ds_vec_t *vec);

size_t FUNC(ds_vec_size)(const ds_vec_t *vec);
size_t FUNC(ds_vec_capacity)(const ds_vec_t *vec);
bool FUNC(ds_vec_is_empty)(const ds_vec_t *vec);
bool FUNC(ds_vec_is_full)(const ds_vec_t *vec);
void *FUNC(ds_vec_data)(ds_vec_t *vec);

void *FUNC(ds_vec_at)(ds_vec_t *vec, size_t index);
void *FUNC(ds_vec_front)(ds_vec_t *vec);
void *FUNC(ds_vec_back)(ds_vec_t *vec);
ds_status_t FUNC(ds_vec_get)(const ds_vec_t *vec, size_t index, void *out);
ds_status_t FUNC(ds_vec_set)(ds_vec_t *vec, size_t index, const void *elem);

ds_status_t FUNC(ds_vec_reserve)(ds_vec_t *vec, size_t capacity);
ds_status_t FUNC(ds_vec_shrink_to_fit)(ds_vec_t *vec);
ds_status_t FUNC(ds_vec_resize)(ds_vec_t *vec, size_t size, const void *fill);
void FUNC(ds_vec_clear)(ds_vec_t *vec);

ds_status_t FUNC(ds_vec_push)(ds_vec_t *vec, const void *elem);
void *FUNC(ds_vec_emplace)(ds_vec_t *vec);
ds_status_t FUNC(ds_vec_append)(ds_vec_t *vec, const void *elems, size_t count);
ds_status_t FUNC(ds_vec_pop)(ds_vec_t *vec, void *out);
ds_status_t FUNC(ds_vec_insert)(ds_vec_t *vec, size_t index, const void *elem);
ds_status_t FUNC(ds_vec_insert_n)(ds_vec_t *vec, size_t index,
                                  const void *elems, size_t count);
ds_status_t FUNC(ds_vec_erase)(ds_vec_t *vec, size_t index, void *out);
ds_status_t FUNC(ds_vec_erase_range)(ds_vec_t *vec, size_t index, size_t count);
ds_status_t FUNC(ds_vec_swap_remove)(ds_vec_t *vec, size_t index, void *out);

ds_status_t FUNC(ds_vec_swap)(ds_vec_t *vec, size_t a, size_t b);
void FUNC(ds_vec_reverse)(ds_vec_t *vec);
void FUNC(ds_vec_sort)(ds_vec_t *vec, ds_vec_compare_func_t compare,
                       void *user);
size_t FUNC(ds_vec_lower_bound)(const ds_vec_t *vec, const void *key,
                                ds_vec_compare_func_t compare, void *user);
ds_status_t FUNC(ds_vec_binary_search)(const ds_vec_t *vec, const void *key,
                                       ds_vec_compare_func_t compare,
                                       void *user, size_t *out_index);
ds_status_t FUNC(ds_vec_find)(const ds_vec_t *vec, const void *key,
                              ds_vec_compare_func_t compare, void *user,
                              size_t *out_index);
ds_status_t FUNC(ds_vec_copy)(ds_vec_t *dst, const ds_vec_t *src,
                              ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_VEC_H */
