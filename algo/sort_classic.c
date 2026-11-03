#include "sort_classic.h"
#include <string.h>

#define AT(b, i, es) ((unsigned char *)(b) + (i) * (es))
#define CAT(b, i, es) ((const unsigned char *)(b) + (i) * (es))
#define LESS(b, i, j, es, cmp, u) ((cmp)(AT(b, i, es), AT(b, j, es), (u)) < 0)

static void swap_at(void *b, size_t i, size_t j, size_t es) {
  ds_elem_swap(AT(b, i, es), AT(b, j, es), es);
}

void ds_bubble_sort(void *base, size_t count, size_t elem_size,
                    ds_compare_func_t compare, void *user) {
  size_t end = count;
  size_t last;
  size_t i;
  while (end > 1) {
    last = 0;
    for (i = 1; i < end; i++) {
      if (LESS(base, i, i - 1, elem_size, compare, user)) {
        swap_at(base, i, i - 1, elem_size);
        last = i;
      }
    }
    end = last; /* everything after the last swap is in place */
  }
}

void ds_cocktail_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user) {
  size_t lo = 0;
  size_t hi = count;
  size_t i;
  size_t last;
  while (hi > lo + 1) {
    last = lo;
    for (i = lo + 1; i < hi; i++) {
      if (LESS(base, i, i - 1, elem_size, compare, user)) {
        swap_at(base, i, i - 1, elem_size);
        last = i;
      }
    }
    hi = last;
    last = hi;
    for (i = hi; i > lo + 1; i--) {
      if (LESS(base, i - 1, i - 2, elem_size, compare, user)) {
        swap_at(base, i - 1, i - 2, elem_size);
        last = i - 1;
      }
    }
    lo = last;
  }
}

void ds_selection_sort(void *base, size_t count, size_t elem_size,
                       ds_compare_func_t compare, void *user) {
  size_t i;
  size_t j;
  size_t best;
  for (i = 0; i + 1 < count; i++) {
    best = i;
    for (j = i + 1; j < count; j++) {
      if (LESS(base, j, best, elem_size, compare, user)) {
        best = j;
      }
    }
    if (best != i) {
      swap_at(base, i, best, elem_size);
    }
  }
}

void ds_gnome_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user) {
  size_t i = 1;
  while (i < count) {
    if (i == 0 || !LESS(base, i, i - 1, elem_size, compare, user)) {
      i++;
    } else {
      swap_at(base, i, i - 1, elem_size);
      i--;
    }
  }
}

void ds_binary_insertion_sort(void *base, size_t count, size_t elem_size,
                              ds_compare_func_t compare, void *user) {
  size_t i;
  size_t lo;
  size_t hi;
  size_t mid;
  for (i = 1; i < count; i++) {
    lo = 0;
    hi = i;
    /* upper bound keeps equal elements in order (stable) */
    while (lo < hi) {
      mid = lo + (hi - lo) / 2;
      if (compare(AT(base, i, elem_size), AT(base, mid, elem_size), user) < 0) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    ds_elem_rotate(base, lo, i, i + 1, elem_size);
  }
}

void ds_comb_sort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user) {
  size_t gap = count;
  size_t i;
  int swapped = 1;
  while (gap > 1 || swapped) {
    gap = gap * 10 / 13;
    if (gap == 9 || gap == 10) {
      gap = 11; /* "comb sort 11" avoids a bad gap sequence */
    }
    if (gap < 1) {
      gap = 1;
    }
    swapped = 0;
    for (i = 0; i + gap < count; i++) {
      if (LESS(base, i + gap, i, elem_size, compare, user)) {
        swap_at(base, i, i + gap, elem_size);
        swapped = 1;
      }
    }
  }
}

void ds_odd_even_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user) {
  int sorted = 0;
  size_t i;
  size_t phase;
  while (!sorted) {
    sorted = 1;
    for (phase = 1; phase <= 2; phase++) {
      for (i = phase; i < count; i += 2) {
        if (LESS(base, i, i - 1, elem_size, compare, user)) {
          swap_at(base, i, i - 1, elem_size);
          sorted = 0;
        }
      }
    }
  }
}

size_t ds_cycle_sort(void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user) {
  size_t writes = 0;
  size_t start;
  size_t pos;
  size_t i;
  /* The element in hand sits at `start`; each swap writes it to its final
   * position and picks up the element that was there, so every element is
   * written at most once. */
  for (start = 0; start + 1 < count; start++) {
    for (;;) {
      pos = start;
      for (i = start + 1; i < count; i++) {
        if (LESS(base, i, start, elem_size, compare, user)) {
          pos++;
        }
      }
      if (pos == start) {
        break;
      }
      while (compare(AT(base, start, elem_size), AT(base, pos, elem_size),
                     user) == 0) {
        pos++; /* skip equal elements already in place */
      }
      swap_at(base, start, pos, elem_size);
      writes++;
    }
  }
  return writes;
}

void ds_pancake_sort(void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user) {
  size_t size;
  size_t i;
  size_t best;
  for (size = count; size > 1; size--) {
    best = 0;
    for (i = 1; i < size; i++) {
      if (!LESS(base, i, best, elem_size, compare, user)) {
        best = i;
      }
    }
    if (best == size - 1) {
      continue;
    }
    if (best > 0) {
      ds_elem_reverse(base, best + 1, elem_size); /* flip max to the top */
    }
    ds_elem_reverse(base, size, elem_size); /* flip it to the bottom */
  }
}

static size_t bitonic_pow2_below(size_t n) {
  size_t k = 1;
  while (k < n - k) {
    k <<= 1;
  }
  return k < n ? k : k >> 1;
}

static void bitonic_merge(void *b, size_t lo, size_t n, int up, size_t es,
                          ds_compare_func_t compare, void *user) {
  size_t m;
  size_t i;
  if (n < 2) {
    return;
  }
  m = bitonic_pow2_below(n);
  for (i = lo; i < lo + n - m; i++) {
    if ((compare(AT(b, i, es), AT(b, i + m, es), user) > 0) == (up != 0)) {
      swap_at(b, i, i + m, es);
    }
  }
  bitonic_merge(b, lo, m, up, es, compare, user);
  bitonic_merge(b, lo + m, n - m, up, es, compare, user);
}

static void bitonic_rec(void *b, size_t lo, size_t n, int up, size_t es,
                        ds_compare_func_t compare, void *user) {
  size_t m;
  if (n < 2) {
    return;
  }
  m = n / 2;
  bitonic_rec(b, lo, m, !up, es, compare, user);
  bitonic_rec(b, lo + m, n - m, up, es, compare, user);
  bitonic_merge(b, lo, n, up, es, compare, user);
}

void ds_bitonic_sort(void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user) {
  bitonic_rec(base, 0, count, 1, elem_size, compare, user);
}

static void stooge_rec(void *b, size_t lo, size_t hi, size_t es,
                       ds_compare_func_t compare, void *user) {
  size_t third;
  if (LESS(b, hi, lo, es, compare, user)) {
    swap_at(b, lo, hi, es);
  }
  if (hi - lo + 1 > 2) {
    third = (hi - lo + 1) / 3;
    stooge_rec(b, lo, hi - third, es, compare, user);
    stooge_rec(b, lo + third, hi, es, compare, user);
    stooge_rec(b, lo, hi - third, es, compare, user);
  }
}

void ds_stooge_sort(void *base, size_t count, size_t elem_size,
                    ds_compare_func_t compare, void *user) {
  if (count > 1) {
    stooge_rec(base, 0, count - 1, elem_size, compare, user);
  }
}

void ds_counting_sort_keyed(const void *base, size_t count, size_t elem_size,
                            ds_index_key_func_t key, void *user,
                            size_t key_range, size_t *counts, void *out) {
  size_t i;
  size_t sum = 0;
  size_t c;
  size_t k;
  if (base == NULL || key == NULL || counts == NULL || out == NULL) {
    return;
  }
  for (i = 0; i < key_range; i++) {
    counts[i] = 0;
  }
  for (i = 0; i < count; i++) {
    k = key(CAT(base, i, elem_size), user);
    if (k < key_range) {
      counts[k]++;
    }
  }
  for (i = 0; i < key_range; i++) {
    c = counts[i];
    counts[i] = sum;
    sum += c;
  }
  for (i = 0; i < count; i++) {
    k = key(CAT(base, i, elem_size), user);
    if (k < key_range) {
      memcpy(AT(out, counts[k]++, elem_size), CAT(base, i, elem_size),
             elem_size);
    }
  }
}
