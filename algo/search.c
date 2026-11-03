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
