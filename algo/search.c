#include "search.h"

#define CAT(base, i, es) ((const unsigned char *)(base) + (i) * (es))

size_t ds_lower_bound(const void *base, size_t count, size_t elem_size,
                      const void *key, ds_compare_func_t compare, void *user) {
  size_t lo = 0;
  size_t hi = count;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (compare(CAT(base, mid, elem_size), key, user) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

size_t ds_upper_bound(const void *base, size_t count, size_t elem_size,
                      const void *key, ds_compare_func_t compare, void *user) {
  size_t lo = 0;
  size_t hi = count;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (compare(key, CAT(base, mid, elem_size), user) < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

void ds_equal_range(const void *base, size_t count, size_t elem_size,
                    const void *key, ds_compare_func_t compare, void *user,
                    size_t *out_first, size_t *out_last) {
  size_t first = ds_lower_bound(base, count, elem_size, key, compare, user);
  size_t last =
      first + ds_upper_bound(CAT(base, first, elem_size), count - first,
                             elem_size, key, compare, user);
  if (out_first != NULL) {
    *out_first = first;
  }
  if (out_last != NULL) {
    *out_last = last;
  }
}

ds_status_t ds_binary_search(const void *base, size_t count, size_t elem_size,
                             const void *key, ds_compare_func_t compare,
                             void *user, size_t *out_index) {
  size_t index;
  if (base == NULL && count > 0) {
    return DS_ERR_NULL;
  }
  if (key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  index = ds_lower_bound(base, count, elem_size, key, compare, user);
  if (out_index != NULL) {
    *out_index = index;
  }
  if (index < count && compare(CAT(base, index, elem_size), key, user) == 0) {
    return DS_OK;
  }
  return DS_NOT_FOUND;
}

size_t ds_exponential_search(const void *base, size_t count, size_t elem_size,
                             const void *key, ds_compare_func_t compare,
                             void *user) {
  size_t bound = 1;
  size_t lo;
  size_t hi;
  if (count == 0 || compare(base, key, user) >= 0) {
    return 0;
  }
  /* Invariant: element[bound / 2] < key. */
  while (bound < count && compare(CAT(base, bound, elem_size), key, user) < 0) {
    if (bound > count / 2) {
      bound = count;
      break;
    }
    bound *= 2;
  }
  lo = bound / 2 + 1;
  hi = bound < count ? bound + 1 : count;
  return lo + ds_lower_bound(CAT(base, lo, elem_size), hi - lo, elem_size, key,
                             compare, user);
}

size_t ds_interpolation_search_long(const long *values, size_t count,
                                    long key) {
  size_t lo = 0;
  size_t hi;
  size_t pos;
  double span;
  if (values == NULL || count == 0) {
    return count;
  }
  hi = count - 1;
  while (lo <= hi && key >= values[lo] && key <= values[hi]) {
    if (values[hi] == values[lo]) {
      return values[lo] == key ? lo : count;
    }
    span = ((double)key - (double)values[lo]) /
           ((double)values[hi] - (double)values[lo]);
    pos = lo + (size_t)(span * (double)(hi - lo));
    if (pos > hi) {
      pos = hi;
    }
    if (values[pos] == key) {
      /* Step back to the first occurrence. */
      while (pos > lo && values[pos - 1] == key) {
        pos--;
      }
      return pos;
    }
    if (values[pos] < key) {
      lo = pos + 1;
    } else {
      if (pos == 0) {
        break;
      }
      hi = pos - 1;
    }
  }
  return count;
}

size_t ds_find(const void *base, size_t count, size_t elem_size,
               const void *key, ds_compare_func_t compare, void *user) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (compare(CAT(base, i, elem_size), key, user) == 0) {
      return i;
    }
  }
  return count;
}

size_t ds_find_if(const void *base, size_t count, size_t elem_size,
                  ds_predicate_func_t predicate, void *user) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (predicate(CAT(base, i, elem_size), user)) {
      return i;
    }
  }
  return count;
}

size_t ds_count_if(const void *base, size_t count, size_t elem_size,
                   ds_predicate_func_t predicate, void *user) {
  size_t i;
  size_t n = 0;
  for (i = 0; i < count; i++) {
    if (predicate(CAT(base, i, elem_size), user)) {
      n++;
    }
  }
  return n;
}

size_t ds_min_element(const void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user) {
  size_t best = 0;
  size_t i;
  if (count == 0) {
    return 0;
  }
  for (i = 1; i < count; i++) {
    if (compare(CAT(base, i, elem_size), CAT(base, best, elem_size), user) <
        0) {
      best = i;
    }
  }
  return best;
}

size_t ds_max_element(const void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user) {
  size_t best = 0;
  size_t i;
  if (count == 0) {
    return 0;
  }
  for (i = 1; i < count; i++) {
    if (compare(CAT(base, best, elem_size), CAT(base, i, elem_size), user) <
        0) {
      best = i;
    }
  }
  return best;
}

void ds_minmax_element(const void *base, size_t count, size_t elem_size,
                       ds_compare_func_t compare, void *user, size_t *out_min,
                       size_t *out_max) {
  size_t lo = 0;
  size_t hi = 0;
  size_t i;
  for (i = 1; i < count; i++) {
    if (compare(CAT(base, i, elem_size), CAT(base, lo, elem_size), user) < 0) {
      lo = i;
    }
    if (compare(CAT(base, i, elem_size), CAT(base, hi, elem_size), user) >= 0) {
      hi = i; /* last maximum, as std::minmax_element */
    }
  }
  if (out_min != NULL) {
    *out_min = lo;
  }
  if (out_max != NULL) {
    *out_max = hi;
  }
}

static size_t search_isqrt(size_t n) {
  size_t r = 0;
  size_t bit = (size_t)1 << (sizeof(size_t) * 8 - 2);
  while (bit > n) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (n >= r + bit) {
      n -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return r;
}

size_t ds_jump_search(const void *base, size_t count, size_t elem_size,
                      const void *key, size_t step, ds_compare_func_t compare,
                      void *user) {
  size_t end;
  size_t i;
  if (base == NULL || compare == NULL || count == 0) {
    return count;
  }
  if (step == 0) {
    step = search_isqrt(count);
    if (step == 0) {
      step = 1;
    }
  }
  /* find the first block whose last element is not less than key */
  end = step < count ? step : count;
  while (end < count && compare(CAT(base, end - 1, elem_size), key, user) < 0) {
    end = count - end > step ? end + step : count;
  }
  for (i = end - (end % step == 0 ? step : end % step); i < end; i++) {
    if (compare(CAT(base, i, elem_size), key, user) >= 0) {
      return i;
    }
  }
  return end;
}

size_t ds_fibonacci_search(const void *base, size_t count, size_t elem_size,
                           const void *key, ds_compare_func_t compare,
                           void *user) {
  /* Region [lo, lo + F(k) - 1) holds the answer range [lo, lo + F(k) - 1];
   * positions past count count as not less than key. */
  size_t fk = 1;  /* F(k) */
  size_t fk1 = 1; /* F(k - 1) */
  size_t t;
  size_t lo = 0;
  size_t i;
  if (base == NULL || compare == NULL || count == 0) {
    return count;
  }
  while (fk - 1 < count) {
    if (fk > ((size_t)-1) - fk1) {
      break;
    }
    t = fk + fk1;
    fk1 = fk;
    fk = t;
  }
  while (fk > 1) {
    i = lo + fk1 - 1;
    if (i < count && compare(CAT(base, i, elem_size), key, user) < 0) {
      /* answer in (i, lo + F(k) - 1]: size F(k - 2) - 1 */
      lo = i + 1;
      t = fk - fk1;  /* F(k - 2) */
      fk1 = fk1 - t; /* F(k - 3) */
      fk = t;
    } else {
      /* answer in [lo, i]: size F(k - 1) - 1 */
      t = fk - fk1;
      fk = fk1;
      fk1 = t;
    }
  }
  return lo < count ? lo : count;
}

size_t ds_ternary_search(const void *base, size_t count, size_t elem_size,
                         const void *key, ds_compare_func_t compare,
                         void *user) {
  size_t lo = 0;
  size_t hi = count; /* answer in [lo, hi] */
  size_t m1;
  size_t m2;
  if (base == NULL || compare == NULL) {
    return count;
  }
  while (hi - lo >= 3) {
    m1 = lo + (hi - lo) / 3;
    m2 = lo + 2 * ((hi - lo) / 3);
    if (compare(CAT(base, m1, elem_size), key, user) >= 0) {
      hi = m1;
    } else if (compare(CAT(base, m2, elem_size), key, user) >= 0) {
      lo = m1 + 1;
      hi = m2;
    } else {
      lo = m2 + 1;
    }
  }
  while (lo < hi && compare(CAT(base, lo, elem_size), key, user) < 0) {
    lo++;
  }
  return lo;
}

static size_t search_interp_seq(size_t count, double pos_frac, const void *base,
                                size_t elem_size, const void *key,
                                ds_compare_func_t compare, void *user) {
  size_t p;
  double f = pos_frac * (double)(count - 1);
  if (!(f > 0.0)) {
    p = 0;
  } else if (f >= (double)(count - 1)) {
    p = count - 1;
  } else {
    p = (size_t)f;
  }
  if (compare(CAT(base, p, elem_size), key, user) < 0) {
    while (p < count && compare(CAT(base, p, elem_size), key, user) < 0) {
      p++;
    }
  } else {
    while (p > 0 && compare(CAT(base, p - 1, elem_size), key, user) >= 0) {
      p--;
    }
  }
  if (p < count && compare(CAT(base, p, elem_size), key, user) == 0) {
    return p;
  }
  return count;
}

static int search_cmp_long(const void *a, const void *b, void *user) {
  long x = *(const long *)a;
  long y = *(const long *)b;
  (void)user;
  return (x > y) - (x < y);
}

size_t ds_interpolation_sequential_search_long(const long *values, size_t count,
                                               long key) {
  double lo;
  double hi;
  double frac;
  if (values == NULL || count == 0) {
    return count;
  }
  if (key < values[0] || key > values[count - 1]) {
    return count;
  }
  lo = (double)values[0];
  hi = (double)values[count - 1];
  frac = hi > lo ? ((double)key - lo) / (hi - lo) : 0.0;
  return search_interp_seq(count, frac, values, sizeof(long), &key,
                           search_cmp_long, NULL);
}

size_t ds_interpolation_sequential_search(const void *base, size_t count,
                                          size_t elem_size, const void *key,
                                          ds_compare_func_t compare,
                                          ds_numeric_func_t numeric,
                                          void *user) {
  double lo;
  double hi;
  double k;
  double frac = 0.0;
  if (base == NULL || compare == NULL || numeric == NULL || count == 0) {
    return count;
  }
  lo = numeric(base, user);
  hi = numeric(CAT(base, count - 1, elem_size), user);
  k = numeric(key, user);
  if (hi > lo) {
    frac = (k - lo) / (hi - lo);
  }
  return search_interp_seq(count, frac, base, elem_size, key, compare, user);
}

size_t ds_ternary_search_peak(const void *base, size_t count, size_t elem_size,
                              ds_compare_func_t compare, void *user) {
  size_t lo = 0;
  size_t hi;
  size_t m1;
  size_t m2;
  size_t best;
  size_t i;
  int c;
  if (base == NULL || compare == NULL || count == 0) {
    return count;
  }
  hi = count - 1;
  while (hi - lo > 2) {
    m1 = lo + (hi - lo) / 3;
    m2 = hi - (hi - lo) / 3;
    c = compare(CAT(base, m1, elem_size), CAT(base, m2, elem_size), user);
    if (c < 0) {
      lo = m1 + 1;
    } else if (c > 0) {
      hi = m2 - 1;
    } else {
      lo = m1;
      hi = m2;
    }
  }
  best = lo;
  for (i = lo + 1; i <= hi; i++) {
    if (compare(CAT(base, i, elem_size), CAT(base, best, elem_size), user) >
        0) {
      best = i;
    }
  }
  return best;
}

double ds_ternary_search_min(ds_real_func_t f, void *user, double lo, double hi,
                             double tolerance) {
  double m1;
  double m2;
  int round;
  if (f == NULL) {
    return lo;
  }
  if (hi < lo) {
    m1 = lo;
    lo = hi;
    hi = m1;
  }
  for (round = 0; round < 300 && hi - lo > tolerance; round++) {
    m1 = lo + (hi - lo) / 3.0;
    m2 = hi - (hi - lo) / 3.0;
    if (f(m1, user) < f(m2, user)) {
      hi = m2;
    } else {
      lo = m1;
    }
  }
  return lo + (hi - lo) / 2.0;
}

void ds_bond_search(const void *base, size_t count, size_t elem_size,
                    const void *key1, const void *key2,
                    ds_compare_func_t compare, void *user, size_t *out_index1,
                    size_t *out_index2) {
  size_t i;
  size_t r1 = count;
  size_t r2 = count;
  const unsigned char *e;
  if (base != NULL && compare != NULL) {
    for (i = 0; i < count && (r1 == count || r2 == count); i++) {
      e = CAT(base, i, elem_size);
      if (r1 == count && compare(e, key1, user) == 0) {
        r1 = i;
      }
      if (r2 == count && compare(e, key2, user) == 0) {
        r2 = i;
      }
    }
  }
  if (out_index1 != NULL) {
    *out_index1 = r1;
  }
  if (out_index2 != NULL) {
    *out_index2 = r2;
  }
}

void ds_bond_search_ulong(const unsigned long *values, size_t count,
                          unsigned long key1, unsigned long key2,
                          size_t *out_index1, size_t *out_index2) {
  size_t i;
  size_t r1 = count;
  size_t r2 = count;
  unsigned long mask = key1 | key2;
  unsigned long v;
  if (values != NULL) {
    for (i = 0; i < count && (r1 == count || r2 == count); i++) {
      v = values[i];
      /* v can only equal a key if it has no bit outside key1 | key2 */
      if ((v | mask) != mask) {
        continue;
      }
      if (r1 == count && v == key1) {
        r1 = i;
      }
      if (r2 == count && v == key2) {
        r2 = i;
      }
    }
  }
  if (out_index1 != NULL) {
    *out_index1 = r1;
  }
  if (out_index2 != NULL) {
    *out_index2 = r2;
  }
}
