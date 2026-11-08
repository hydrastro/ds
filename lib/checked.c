#include "checked.h"

bool ds_size_add(size_t a, size_t b, size_t *out) {
  if (a > (size_t)-1 - b) {
    return false;
  }
  *out = a + b;
  return true;
}

bool ds_size_mul(size_t a, size_t b, size_t *out) {
  if (a != 0 && b > (size_t)-1 / a) {
    return false;
  }
  *out = a * b;
  return true;
}

bool ds_size_mul_add(size_t a, size_t b, size_t c, size_t *out) {
  size_t m;
  return ds_size_mul(a, b, &m) && ds_size_add(m, c, out);
}

bool ds_size_align(size_t n, size_t align, size_t *out) {
  size_t r;
  if (align == 0) {
    return false;
  }
  r = n % align;
  if (r == 0) {
    *out = n;
    return true;
  }
  return ds_size_add(n, align - r, out);
}

bool ds_size_is_pow2(size_t align) {
  return align != 0 && (align & (align - 1)) == 0;
}

/* The integer value of an address (C89 has no uintptr_t; size_t holds
 * addresses on every supported platform). */
static size_t ck_addr(const void *p) { return (size_t)p; }

bool ds_ptr_aligned(const void *p, size_t align) {
  return align != 0 && ck_addr(p) % align == 0;
}

size_t ds_ptr_padding(const void *p, size_t align) {
  size_t r;
  if (align == 0) {
    return 0;
  }
  r = ck_addr(p) % align;
  return r == 0 ? 0 : align - r;
}
