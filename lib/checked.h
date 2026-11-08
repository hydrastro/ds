#ifndef DS_CHECKED_H
#define DS_CHECKED_H

/*
 * Checked size arithmetic and alignment.
 *
 * Every function returns false (leaving *out unspecified) instead of
 * wrapping, so layouts can be validated as a whole before anything is
 * allocated: an allocator that never fails cannot repair a wrapped size.
 *
 * ds_max_align_t is a union of the C89 types with the strictest alignment
 * requirements; DS_MAX_ALIGN is its size, a safe default alignment for
 * storing arbitrary values.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union ds_max_align {
  long l;
  double d;
  long double ld;
  void *p;
  void (*f)(void);
} ds_max_align_t;

#define DS_MAX_ALIGN (sizeof(ds_max_align_t))

bool ds_size_add(size_t a, size_t b, size_t *out);
bool ds_size_mul(size_t a, size_t b, size_t *out);
/* a * b + c */
bool ds_size_mul_add(size_t a, size_t b, size_t c, size_t *out);
/* n rounded up to a multiple of align (align > 0) */
bool ds_size_align(size_t n, size_t align, size_t *out);
/* is align a power of two */
bool ds_size_is_pow2(size_t align);
/* is the address of p a multiple of align */
bool ds_ptr_aligned(const void *p, size_t align);
/* bytes to add to address p to reach a multiple of align */
size_t ds_ptr_padding(const void *p, size_t align);

#ifdef __cplusplus
}
#endif

#endif /* DS_CHECKED_H */
