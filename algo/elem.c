#include "elem.h"
#include <string.h>

void ds_elem_swap(void *a, void *b, size_t elem_size) {
  unsigned char *pa = (unsigned char *)a;
  unsigned char *pb = (unsigned char *)b;
  unsigned char tmp;
  size_t i;
  if (pa == pb) {
    return;
  }
  for (i = 0; i < elem_size; i++) {
    tmp = pa[i];
    pa[i] = pb[i];
    pb[i] = tmp;
  }
}

void ds_elem_reverse(void *base, size_t count, size_t elem_size) {
  unsigned char *p = (unsigned char *)base;
  size_t i = 0;
  size_t j = count;
  while (i + 1 < j) {
    j--;
    ds_elem_swap(p + i * elem_size, p + j * elem_size, elem_size);
    i++;
  }
}

void ds_elem_rotate(void *base, size_t first, size_t middle, size_t last,
                    size_t elem_size) {
  unsigned char *p = (unsigned char *)base;
  if (first >= middle || middle >= last) {
    return;
  }
  ds_elem_reverse(p + first * elem_size, middle - first, elem_size);
  ds_elem_reverse(p + middle * elem_size, last - middle, elem_size);
  ds_elem_reverse(p + first * elem_size, last - first, elem_size);
}

int ds_compare_int(const void *a, const void *b, void *user) {
  int x = *(const int *)a;
  int y = *(const int *)b;
  (void)user;
  return (x > y) - (x < y);
}

int ds_compare_long(const void *a, const void *b, void *user) {
  long x = *(const long *)a;
  long y = *(const long *)b;
  (void)user;
  return (x > y) - (x < y);
}

int ds_compare_ulong(const void *a, const void *b, void *user) {
  unsigned long x = *(const unsigned long *)a;
  unsigned long y = *(const unsigned long *)b;
  (void)user;
  return (x > y) - (x < y);
}

int ds_compare_size(const void *a, const void *b, void *user) {
  size_t x = *(const size_t *)a;
  size_t y = *(const size_t *)b;
  (void)user;
  return (x > y) - (x < y);
}

int ds_compare_double(const void *a, const void *b, void *user) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  (void)user;
  return (x > y) - (x < y);
}

int ds_compare_cstr(const void *a, const void *b, void *user) {
  (void)user;
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}
