#include "sort.h"
#include <limits.h>
#include <string.h>

#define SORT_SIZE_MAX (~(size_t)0)
#define SORT_INSERTION_CUTOFF 16
#define SORT_MERGE_RUN 32
#define SORT_STABLE_BLOCK 20
#define SORT_TMP_BYTES 64

#define AT(base, i, es) ((unsigned char *)(base) + (i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (i) * (es))

static size_t sort_floor_log2(size_t x) {
  size_t r = 0;
  while (x > 1) {
    x >>= 1;
    r++;
  }
  return r;
}

/* Insertion sort of [lo, hi). Stable. Elements up to SORT_TMP_BYTES are
 * shifted with memmove; larger ones are moved with adjacent swaps. */
static void sort_insertion_range(unsigned char *a, size_t lo, size_t hi,
                                 size_t es, ds_compare_func_t compare,
                                 void *user) {
  unsigned char tmp[SORT_TMP_BYTES];
  size_t i;
  size_t j;
  for (i = lo + 1; i < hi; i++) {
    if (compare(AT(a, i - 1, es), AT(a, i, es), user) <= 0) {
      continue;
    }
    if (es <= SORT_TMP_BYTES) {
      memcpy(tmp, AT(a, i, es), es);
      j = i;
      while (j > lo && compare(AT(a, j - 1, es), tmp, user) > 0) {
        j--;
      }
      memmove(AT(a, j + 1, es), AT(a, j, es), (i - j) * es);
      memcpy(AT(a, j, es), tmp, es);
    } else {
      for (j = i; j > lo && compare(AT(a, j - 1, es), AT(a, j, es), user) > 0;
           j--) {
        ds_elem_swap(AT(a, j - 1, es), AT(a, j, es), es);
      }
    }
  }
}

static void sort_sift_down(unsigned char *a, size_t root, size_t end, size_t es,
                           ds_compare_func_t compare, void *user) {
  size_t child;
  for (;;) {
    if (end < 2 || root > (end - 2) / 2) {
      return;
    }
    child = 2 * root + 1;
    if (child + 1 < end &&
        compare(AT(a, child, es), AT(a, child + 1, es), user) < 0) {
      child++;
    }
    if (compare(AT(a, root, es), AT(a, child, es), user) >= 0) {
      return;
    }
    ds_elem_swap(AT(a, root, es), AT(a, child, es), es);
    root = child;
  }
}

static void sort_heap(unsigned char *a, size_t n, size_t es,
                      ds_compare_func_t compare, void *user) {
  size_t i;
  if (n < 2) {
    return;
  }
  for (i = n / 2; i > 0; i--) {
    sort_sift_down(a, i - 1, n, es, compare, user);
  }
  for (i = n - 1; i > 0; i--) {
    ds_elem_swap(a, AT(a, i, es), es);
    sort_sift_down(a, 0, i, es, compare, user);
  }
}

static void sort_median_to_front(unsigned char *a, size_t lo, size_t hi,
                                 size_t es, ds_compare_func_t compare,
                                 void *user) {
  size_t mid = lo + (hi - lo) / 2;
  size_t last = hi - 1;
  if (compare(AT(a, mid, es), AT(a, lo, es), user) < 0) {
    ds_elem_swap(AT(a, mid, es), AT(a, lo, es), es);
  }
  if (compare(AT(a, last, es), AT(a, mid, es), user) < 0) {
    ds_elem_swap(AT(a, last, es), AT(a, mid, es), es);
    if (compare(AT(a, mid, es), AT(a, lo, es), user) < 0) {
      ds_elem_swap(AT(a, mid, es), AT(a, lo, es), es);
    }
  }
  ds_elem_swap(AT(a, lo, es), AT(a, mid, es), es);
}

/* Partition [lo, hi) around the pivot at `lo`; returns its final index.
 * Elements equal to the pivot go to both sides, which keeps runs of
 * duplicates balanced. */
static size_t sort_partition(unsigned char *a, size_t lo, size_t hi, size_t es,
                             ds_compare_func_t compare, void *user) {
  const unsigned char *pivot = AT(a, lo, es);
  size_t i = lo + 1;
  size_t j = hi - 1;
  for (;;) {
    while (i <= j && compare(AT(a, i, es), pivot, user) < 0) {
      i++;
    }
    while (i <= j && compare(AT(a, j, es), pivot, user) > 0) {
      j--;
    }
    if (i >= j) {
      break;
    }
    ds_elem_swap(AT(a, i, es), AT(a, j, es), es);
    i++;
    j--;
  }
  ds_elem_swap(AT(a, lo, es), AT(a, j, es), es);
  return j;
}

static void sort_intro(unsigned char *a, size_t lo, size_t hi, size_t depth,
                       size_t es, ds_compare_func_t compare, void *user) {
  size_t p;
  while (hi - lo > SORT_INSERTION_CUTOFF) {
    if (depth == 0) {
      sort_heap(AT(a, lo, es), hi - lo, es, compare, user);
      return;
    }
    depth--;
    sort_median_to_front(a, lo, hi, es, compare, user);
    p = sort_partition(a, lo, hi, es, compare, user);
    /* Recurse into the smaller side, loop on the larger: O(log n) stack. */
    if (p - lo < hi - p - 1) {
      sort_intro(a, lo, p, depth, es, compare, user);
      lo = p + 1;
    } else {
      sort_intro(a, p + 1, hi, depth, es, compare, user);
      hi = p;
    }
  }
  sort_insertion_range(a, lo, hi, es, compare, user);
}

void ds_insertion_sort(void *base, size_t count, size_t elem_size,
                       ds_compare_func_t compare, void *user) {
  if (base == NULL || compare == NULL || count < 2) {
    return;
  }
  sort_insertion_range((unsigned char *)base, 0, count, elem_size, compare,
                       user);
}

void ds_shell_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user) {
  /* Ciura's gaps, extended by a factor of 2.25. */
  static const size_t ciura[] = {1, 4, 10, 23, 57, 132, 301, 701, 1750};
  size_t gaps[80];
  size_t ngaps = 0;
  size_t gap;
  size_t g;
  size_t i;
  size_t j;
  unsigned char *a = (unsigned char *)base;
  if (base == NULL || compare == NULL || count < 2) {
    return;
  }
  for (i = 0; i < sizeof(ciura) / sizeof(ciura[0]) && ciura[i] < count; i++) {
    gaps[ngaps++] = ciura[i];
  }
  gap = gaps[ngaps - 1];
  while (ngaps < 80 && gap <= SORT_SIZE_MAX / 9 && gap * 9 / 4 < count &&
         gap >= 1750) {
    gap = gap * 9 / 4;
    gaps[ngaps++] = gap;
  }
  for (g = ngaps; g > 0; g--) {
    gap = gaps[g - 1];
    for (i = gap; i < count; i++) {
      for (j = i; j >= gap && compare(AT(a, j - gap, elem_size),
                                      AT(a, j, elem_size), user) > 0;
           j -= gap) {
        ds_elem_swap(AT(a, j - gap, elem_size), AT(a, j, elem_size), elem_size);
      }
    }
  }
}

void ds_heap_sort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user) {
  if (base == NULL || compare == NULL) {
    return;
  }
  sort_heap((unsigned char *)base, count, elem_size, compare, user);
}

void ds_intro_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user) {
  if (base == NULL || compare == NULL || count < 2) {
    return;
  }
  sort_intro((unsigned char *)base, 0, count, 2 * sort_floor_log2(count),
             elem_size, compare, user);
}

static void sort_merge_runs(const unsigned char *src, unsigned char *dst,
                            size_t lo, size_t mid, size_t hi, size_t es,
                            ds_compare_func_t compare, void *user) {
  size_t i = lo;
  size_t j = mid;
  size_t k = lo;
  if (mid >= hi ||
      compare(CAT(src, mid - 1, es), CAT(src, mid, es), user) <= 0) {
    memcpy(AT(dst, lo, es), CAT(src, lo, es), (hi - lo) * es);
    return;
  }
  while (i < mid && j < hi) {
    if (compare(CAT(src, j, es), CAT(src, i, es), user) < 0) {
      memcpy(AT(dst, k, es), CAT(src, j, es), es);
      j++;
    } else {
      memcpy(AT(dst, k, es), CAT(src, i, es), es);
      i++;
    }
    k++;
  }
  if (i < mid) {
    memcpy(AT(dst, k, es), CAT(src, i, es), (mid - i) * es);
  }
  if (j < hi) {
    memcpy(AT(dst, k, es), CAT(src, j, es), (hi - j) * es);
  }
}

void ds_merge_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user, void *scratch) {
  unsigned char *src = (unsigned char *)base;
  unsigned char *dst = (unsigned char *)scratch;
  unsigned char *tmp;
  size_t width;
  size_t lo;
  size_t mid;
  size_t hi;
  if (base == NULL || compare == NULL || count < 2) {
    return;
  }
  if (scratch == NULL) {
    ds_inplace_stable_sort(base, count, elem_size, compare, user);
    return;
  }
  for (lo = 0; lo < count; lo += SORT_MERGE_RUN) {
    hi = count - lo > SORT_MERGE_RUN ? lo + SORT_MERGE_RUN : count;
    sort_insertion_range(src, lo, hi, elem_size, compare, user);
    if (hi == count) {
      break;
    }
  }
  for (width = SORT_MERGE_RUN; width < count; width *= 2) {
    for (lo = 0; lo < count; lo += 2 * width) {
      mid = count - lo > width ? lo + width : count;
      hi = count - mid > width ? mid + width : count;
      sort_merge_runs(src, dst, lo, mid, hi, elem_size, compare, user);
      if (hi == count) {
        break;
      }
    }
    tmp = src;
    src = dst;
    dst = tmp;
    if (width > SORT_SIZE_MAX / 2) {
      break;
    }
  }
  if (src != (unsigned char *)base) {
    memcpy(base, src, count * elem_size);
  }
}

/* SymMerge (Kim & Kutzner): merge sorted [a, m) and [m, b) in place. */
static void sort_symmerge(unsigned char *d, size_t a, size_t m, size_t b,
                          size_t es, ds_compare_func_t compare, void *user) {
  size_t mid;
  size_t n;
  size_t start;
  size_t r;
  size_t c;
  size_t end;
  size_t lo;
  size_t hi;
  if (m - a == 1) {
    /* Find the first element in [m, b) not less than d[a]. */
    lo = m;
    hi = b;
    while (lo < hi) {
      c = lo + (hi - lo) / 2;
      if (compare(AT(d, c, es), AT(d, a, es), user) < 0) {
        lo = c + 1;
      } else {
        hi = c;
      }
    }
    ds_elem_rotate(d, a, a + 1, lo, es);
    return;
  }
  if (b - m == 1) {
    /* Find the first element in [a, m) greater than d[m]. */
    lo = a;
    hi = m;
    while (lo < hi) {
      c = lo + (hi - lo) / 2;
      if (compare(AT(d, m, es), AT(d, c, es), user) >= 0) {
        lo = c + 1;
      } else {
        hi = c;
      }
    }
    ds_elem_rotate(d, lo, m, b, es);
    return;
  }
  mid = a + (b - a) / 2;
  n = mid + m;
  if (m > mid) {
    start = n - b;
    r = mid;
  } else {
    start = a;
    r = m;
  }
  while (start < r) {
    c = start + (r - start) / 2;
    if (compare(AT(d, n - 1 - c, es), AT(d, c, es), user) >= 0) {
      start = c + 1;
    } else {
      r = c;
    }
  }
  end = n - start;
  if (start < m && m < end) {
    ds_elem_rotate(d, start, m, end, es);
  }
  if (a < start && start < mid) {
    sort_symmerge(d, a, start, mid, es, compare, user);
  }
  if (mid < end && end < b) {
    sort_symmerge(d, mid, end, b, es, compare, user);
  }
}

void ds_inplace_stable_sort(void *base, size_t count, size_t elem_size,
                            ds_compare_func_t compare, void *user) {
  unsigned char *d = (unsigned char *)base;
  size_t block = SORT_STABLE_BLOCK;
  size_t a;
  size_t b;
  if (base == NULL || compare == NULL || count < 2) {
    return;
  }
  for (a = 0; a < count; a += block) {
    b = count - a > block ? a + block : count;
    sort_insertion_range(d, a, b, elem_size, compare, user);
  }
  while (block < count) {
    for (a = 0; count - a > 2 * block; a += 2 * block) {
      sort_symmerge(d, a, a + block, a + 2 * block, elem_size, compare, user);
    }
    if (count - a > block) {
      sort_symmerge(d, a, a + block, count, elem_size, compare, user);
    }
    if (block > SORT_SIZE_MAX / 2) {
      break;
    }
    block *= 2;
  }
}

void ds_radix_sort_keyed(void *base, size_t count, size_t elem_size,
                         ds_key_func_t key, void *user, void *scratch) {
  size_t counts[256];
  unsigned char *src = (unsigned char *)base;
  unsigned char *dst = (unsigned char *)scratch;
  unsigned char *tmp;
  unsigned int shift;
  size_t i;
  size_t sum;
  size_t c;
  unsigned int digit;
  bool skip;
  if (base == NULL || key == NULL || scratch == NULL || count < 2) {
    return;
  }
  for (shift = 0; shift < CHAR_BIT * sizeof(unsigned long); shift += 8) {
    for (i = 0; i < 256; i++) {
      counts[i] = 0;
    }
    for (i = 0; i < count; i++) {
      counts[(key(AT(src, i, elem_size), user) >> shift) & 0xFFUL]++;
    }
    skip = false;
    for (i = 0; i < 256; i++) {
      if (counts[i] == count) {
        skip = true;
      }
    }
    if (skip) {
      continue;
    }
    sum = 0;
    for (i = 0; i < 256; i++) {
      c = counts[i];
      counts[i] = sum;
      sum += c;
    }
    for (i = 0; i < count; i++) {
      digit =
          (unsigned int)((key(AT(src, i, elem_size), user) >> shift) & 0xFFUL);
      memcpy(AT(dst, counts[digit]++, elem_size), AT(src, i, elem_size),
             elem_size);
    }
    tmp = src;
    src = dst;
    dst = tmp;
  }
  if (src != (unsigned char *)base) {
    memcpy(base, src, count * elem_size);
  }
}

static unsigned long sort_ulong_key(const void *elem, void *user) {
  (void)user;
  return *(const unsigned long *)elem;
}

void ds_radix_sort_ulong(unsigned long *values, size_t count,
                         unsigned long *scratch) {
  ds_radix_sort_keyed(values, count, sizeof(unsigned long), sort_ulong_key,
                      NULL, scratch);
}

void ds_nth_element(void *base, size_t count, size_t elem_size, size_t nth,
                    ds_compare_func_t compare, void *user) {
  unsigned char *a = (unsigned char *)base;
  size_t lo = 0;
  size_t hi = count;
  size_t depth;
  size_t p;
  if (base == NULL || compare == NULL || nth >= count) {
    return;
  }
  depth = 2 * sort_floor_log2(count);
  while (hi - lo > SORT_INSERTION_CUTOFF) {
    if (depth == 0) {
      sort_heap(AT(a, lo, elem_size), hi - lo, elem_size, compare, user);
      return;
    }
    depth--;
    sort_median_to_front(a, lo, hi, elem_size, compare, user);
    p = sort_partition(a, lo, hi, elem_size, compare, user);
    if (p == nth) {
      return;
    }
    if (nth < p) {
      hi = p;
    } else {
      lo = p + 1;
    }
  }
  sort_insertion_range(a, lo, hi, elem_size, compare, user);
}

void ds_partial_sort(void *base, size_t count, size_t elem_size, size_t k,
                     ds_compare_func_t compare, void *user) {
  unsigned char *a = (unsigned char *)base;
  size_t i;
  if (base == NULL || compare == NULL || k == 0) {
    return;
  }
  if (k > count) {
    k = count;
  }
  /* Max-heap of the k smallest seen so far, then heapsort it. */
  for (i = k / 2; i > 0; i--) {
    sort_sift_down(a, i - 1, k, elem_size, compare, user);
  }
  for (i = k; i < count; i++) {
    if (compare(AT(a, i, elem_size), a, user) < 0) {
      ds_elem_swap(AT(a, i, elem_size), a, elem_size);
      sort_sift_down(a, 0, k, elem_size, compare, user);
    }
  }
  for (i = k - 1; i > 0; i--) {
    ds_elem_swap(a, AT(a, i, elem_size), elem_size);
    sort_sift_down(a, 0, i, elem_size, compare, user);
  }
}

size_t ds_is_sorted_until(const void *base, size_t count, size_t elem_size,
                          ds_compare_func_t compare, void *user) {
  size_t i;
  if (base == NULL || compare == NULL || count < 2) {
    return count;
  }
  for (i = 1; i < count; i++) {
    if (compare(CAT(base, i - 1, elem_size), CAT(base, i, elem_size), user) >
        0) {
      return i;
    }
  }
  return count;
}

bool ds_is_sorted(const void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user) {
  return ds_is_sorted_until(base, count, elem_size, compare, user) == count;
}
