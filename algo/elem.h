#ifndef DS_ALGO_ELEM_H
#define DS_ALGO_ELEM_H

/*
 * Shared vocabulary for the generic algorithms in algo/.
 *
 * Algorithms work on arrays of fixed-size elements stored by value:
 * `base` points at the first element, `count` is the number of elements and
 * `elem_size` their size in bytes. Comparators follow the qsort convention
 * (negative, zero, positive) and receive a `user` pointer, so no globals are
 * needed.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_compare_func_t)(const void *a, const void *b, void *user);
typedef bool (*ds_predicate_func_t)(const void *elem, void *user);
/* Returns a uniformly distributed value in [0, 2^32). */
typedef unsigned long (*ds_random_func_t)(void *state);

void ds_elem_swap(void *a, void *b, size_t elem_size);
void ds_elem_reverse(void *base, size_t count, size_t elem_size);
/* Rotate [first, last) left so that `middle` becomes the first element. */
void ds_elem_rotate(void *base, size_t first, size_t middle, size_t last,
                    size_t elem_size);

/* Comparators for common element types. */
int ds_compare_int(const void *a, const void *b, void *user);
int ds_compare_long(const void *a, const void *b, void *user);
int ds_compare_ulong(const void *a, const void *b, void *user);
int ds_compare_size(const void *a, const void *b, void *user);
int ds_compare_double(const void *a, const void *b, void *user);
int ds_compare_cstr(const void *a, const void *b, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_ELEM_H */
