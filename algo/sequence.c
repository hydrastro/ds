#include "sequence.h"
#include <limits.h>
#include <string.h>

#define AT(base, i, es) ((unsigned char *)(base) + (i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (i) * (es))
#define SEQ_SIZE_MAX (~(size_t)0)
#define SEQ_U32 0xFFFFFFFFUL

/* Uniform value in [0, bound) from a 32-bit generator, by rejection. */
static size_t seq_bounded(ds_random_func_t random, void *state, size_t bound) {
  unsigned long b;
  unsigned long threshold;
  unsigned long x;
  size_t r;
  size_t limit;
  size_t bits;
  if (bound <= SEQ_U32) {
    b = (unsigned long)bound;
    threshold = (SEQ_U32 - b + 1UL) % b;
    for (;;) {
      x = random(state) & SEQ_U32;
      if (x >= threshold) {
        return (size_t)(x % b);
      }
    }
  }
  limit = SEQ_SIZE_MAX - SEQ_SIZE_MAX % bound;
  for (;;) {
    r = 0;
    for (bits = 0; bits < sizeof(size_t) * CHAR_BIT; bits += 32) {
      r = (r << 16 << 16) | (size_t)(random(state) & SEQ_U32);
    }
    if (r < limit) {
      return r % bound;
    }
  }
}

void ds_rotate(void *base, size_t count, size_t elem_size, size_t middle) {
  if (base == NULL || middle >= count) {
    return;
  }
  ds_elem_rotate(base, 0, middle, count, elem_size);
}

static bool seq_permute(void *base, size_t count, size_t es,
                        ds_compare_func_t compare, void *user, int sign) {
  size_t i;
  size_t j;
  if (base == NULL || compare == NULL || count < 2) {
    return false;
  }
  i = count - 1;
  while (i > 0 &&
         sign * compare(CAT(base, i - 1, es), CAT(base, i, es), user) >= 0) {
    i--;
  }
  if (i == 0) {
    ds_elem_reverse(base, count, es);
    return false;
  }
  j = count - 1;
  while (sign * compare(CAT(base, i - 1, es), CAT(base, j, es), user) >= 0) {
    j--;
  }
  ds_elem_swap(AT(base, i - 1, es), AT(base, j, es), es);
  ds_elem_reverse(AT(base, i, es), count - i, es);
  return true;
}

bool ds_next_permutation(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user) {
  return seq_permute(base, count, elem_size, compare, user, 1);
}

bool ds_prev_permutation(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user) {
  return seq_permute(base, count, elem_size, compare, user, -1);
}

void ds_shuffle(void *base, size_t count, size_t elem_size,
                ds_random_func_t random, void *random_state) {
  size_t i;
  size_t j;
  if (base == NULL || random == NULL) {
    return;
  }
  for (i = count; i > 1; i--) {
    j = seq_bounded(random, random_state, i);
    ds_elem_swap(AT(base, i - 1, elem_size), AT(base, j, elem_size), elem_size);
  }
}

size_t ds_unique(void *base, size_t count, size_t elem_size,
                 ds_compare_func_t compare, void *user) {
  size_t w = 1;
  size_t r;
  if (base == NULL || compare == NULL || count == 0) {
    return 0;
  }
  for (r = 1; r < count; r++) {
    if (compare(CAT(base, w - 1, elem_size), CAT(base, r, elem_size), user) !=
        0) {
      if (w != r) {
        memcpy(AT(base, w, elem_size), CAT(base, r, elem_size), elem_size);
      }
      w++;
    }
  }
  return w;
}

size_t ds_partition(void *base, size_t count, size_t elem_size,
                    ds_predicate_func_t predicate, void *user) {
  size_t i = 0;
  size_t j = count;
  if (base == NULL || predicate == NULL) {
    return 0;
  }
  for (;;) {
    while (i < j && predicate(CAT(base, i, elem_size), user)) {
      i++;
    }
    while (i < j && !predicate(CAT(base, j - 1, elem_size), user)) {
      j--;
    }
    if (i + 1 >= j) {
      return i;
    }
    ds_elem_swap(AT(base, i, elem_size), AT(base, j - 1, elem_size), elem_size);
    i++;
    j--;
  }
}

static size_t seq_stable_partition(unsigned char *a, size_t lo, size_t hi,
                                   size_t es, ds_predicate_func_t predicate,
                                   void *user) {
  size_t mid;
  size_t left;
  size_t right;
  if (hi - lo == 0) {
    return lo;
  }
  if (hi - lo == 1) {
    return predicate(AT(a, lo, es), user) ? hi : lo;
  }
  mid = lo + (hi - lo) / 2;
  left = seq_stable_partition(a, lo, mid, es, predicate, user);
  right = seq_stable_partition(a, mid, hi, es, predicate, user);
  ds_elem_rotate(a, left, mid, right, es);
  return left + (right - mid);
}

size_t ds_stable_partition(void *base, size_t count, size_t elem_size,
                           ds_predicate_func_t predicate, void *user) {
  if (base == NULL || predicate == NULL) {
    return 0;
  }
  return seq_stable_partition((unsigned char *)base, 0, count, elem_size,
                              predicate, user);
}

/* Shared walker for the sorted-sequence operations. */
enum seq_set_op { SEQ_MERGE, SEQ_UNION, SEQ_INTERSECT, SEQ_DIFF, SEQ_SYMDIFF };

static size_t seq_set(const void *a, size_t na, const void *b, size_t nb,
                      size_t es, ds_compare_func_t compare, void *user,
                      void *out, enum seq_set_op op) {
  size_t i = 0;
  size_t j = 0;
  size_t k = 0;
  int c;
  bool keep_a_tail = op != SEQ_INTERSECT;
  bool keep_b_tail = op == SEQ_MERGE || op == SEQ_UNION || op == SEQ_SYMDIFF;
  while (i < na && j < nb) {
    c = compare(CAT(a, i, es), CAT(b, j, es), user);
    if (op == SEQ_MERGE) {
      if (c <= 0) {
        memcpy(AT(out, k++, es), CAT(a, i++, es), es);
      } else {
        memcpy(AT(out, k++, es), CAT(b, j++, es), es);
      }
    } else if (c < 0) {
      if (op != SEQ_INTERSECT) {
        memcpy(AT(out, k++, es), CAT(a, i, es), es);
      }
      i++;
    } else if (c > 0) {
      if (op == SEQ_UNION || op == SEQ_SYMDIFF) {
        memcpy(AT(out, k++, es), CAT(b, j, es), es);
      }
      j++;
    } else {
      if (op == SEQ_UNION || op == SEQ_INTERSECT) {
        memcpy(AT(out, k++, es), CAT(a, i, es), es);
      }
      i++;
      j++;
    }
  }
  if (keep_a_tail && i < na) {
    memcpy(AT(out, k, es), CAT(a, i, es), (na - i) * es);
    k += na - i;
  }
  if (keep_b_tail && j < nb) {
    memcpy(AT(out, k, es), CAT(b, j, es), (nb - j) * es);
    k += nb - j;
  }
  return k;
}

size_t ds_merge(const void *a, size_t a_count, const void *b, size_t b_count,
                size_t elem_size, ds_compare_func_t compare, void *user,
                void *out) {
  return seq_set(a, a_count, b, b_count, elem_size, compare, user, out,
                 SEQ_MERGE);
}

size_t ds_set_union(const void *a, size_t a_count, const void *b,
                    size_t b_count, size_t elem_size, ds_compare_func_t compare,
                    void *user, void *out) {
  return seq_set(a, a_count, b, b_count, elem_size, compare, user, out,
                 SEQ_UNION);
}

size_t ds_set_intersection(const void *a, size_t a_count, const void *b,
                           size_t b_count, size_t elem_size,
                           ds_compare_func_t compare, void *user, void *out) {
  return seq_set(a, a_count, b, b_count, elem_size, compare, user, out,
                 SEQ_INTERSECT);
}

size_t ds_set_difference(const void *a, size_t a_count, const void *b,
                         size_t b_count, size_t elem_size,
                         ds_compare_func_t compare, void *user, void *out) {
  return seq_set(a, a_count, b, b_count, elem_size, compare, user, out,
                 SEQ_DIFF);
}

size_t ds_set_symmetric_difference(const void *a, size_t a_count, const void *b,
                                   size_t b_count, size_t elem_size,
                                   ds_compare_func_t compare, void *user,
                                   void *out) {
  return seq_set(a, a_count, b, b_count, elem_size, compare, user, out,
                 SEQ_SYMDIFF);
}

bool ds_includes(const void *a, size_t a_count, const void *b, size_t b_count,
                 size_t elem_size, ds_compare_func_t compare, void *user) {
  size_t i = 0;
  size_t j = 0;
  int c;
  while (j < b_count) {
    if (i == a_count) {
      return false;
    }
    c = compare(CAT(a, i, elem_size), CAT(b, j, elem_size), user);
    if (c > 0) {
      return false;
    }
    if (c == 0) {
      j++;
    }
    i++;
  }
  return true;
}

int ds_lexicographic_compare(const void *a, size_t a_count, const void *b,
                             size_t b_count, size_t elem_size,
                             ds_compare_func_t compare, void *user) {
  size_t i;
  size_t n = a_count < b_count ? a_count : b_count;
  int c;
  for (i = 0; i < n; i++) {
    c = compare(CAT(a, i, elem_size), CAT(b, i, elem_size), user);
    if (c != 0) {
      return c < 0 ? -1 : 1;
    }
  }
  return (a_count > b_count) - (a_count < b_count);
}

unsigned long ds_count_inversions(void *base, size_t count, size_t elem_size,
                                  ds_compare_func_t compare, void *user,
                                  void *scratch) {
  unsigned char *src = (unsigned char *)base;
  unsigned char *dst = (unsigned char *)scratch;
  unsigned char *tmp;
  unsigned long inversions = 0;
  size_t width;
  size_t lo;
  size_t mid;
  size_t hi;
  size_t i;
  size_t j;
  size_t k;
  if (base == NULL || scratch == NULL || compare == NULL || count < 2) {
    return 0;
  }
  for (width = 1; width < count; width *= 2) {
    for (lo = 0; lo < count; lo += 2 * width) {
      mid = count - lo > width ? lo + width : count;
      hi = count - mid > width ? mid + width : count;
      i = lo;
      j = mid;
      k = lo;
      while (i < mid && j < hi) {
        if (compare(AT(src, j, elem_size), AT(src, i, elem_size), user) < 0) {
          inversions += (unsigned long)(mid - i);
          memcpy(AT(dst, k++, elem_size), AT(src, j++, elem_size), elem_size);
        } else {
          memcpy(AT(dst, k++, elem_size), AT(src, i++, elem_size), elem_size);
        }
      }
      if (i < mid) {
        memcpy(AT(dst, k, elem_size), AT(src, i, elem_size),
               (mid - i) * elem_size);
        k += mid - i;
      }
      if (j < hi) {
        memcpy(AT(dst, k, elem_size), AT(src, j, elem_size),
               (hi - j) * elem_size);
      }
      if (hi == count) {
        break;
      }
    }
    tmp = src;
    src = dst;
    dst = tmp;
    if (width > SEQ_SIZE_MAX / 2) {
      break;
    }
  }
  if (src != (unsigned char *)base) {
    memcpy(base, src, count * elem_size);
  }
  return inversions;
}

size_t ds_lis_length(const void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user, void *scratch) {
  size_t length = 0;
  size_t i;
  size_t lo;
  size_t hi;
  size_t mid;
  if (base == NULL || scratch == NULL || compare == NULL) {
    return 0;
  }
  for (i = 0; i < count; i++) {
    /* tails[pos] = smallest tail of an increasing run of length pos + 1 */
    lo = 0;
    hi = length;
    while (lo < hi) {
      mid = lo + (hi - lo) / 2;
      if (compare(AT(scratch, mid, elem_size), CAT(base, i, elem_size), user) <
          0) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    memcpy(AT(scratch, lo, elem_size), CAT(base, i, elem_size), elem_size);
    if (lo == length) {
      length++;
    }
  }
  return length;
}

ds_status_t ds_majority_element(const void *base, size_t count,
                                size_t elem_size, ds_compare_func_t compare,
                                void *user, size_t *out_index) {
  size_t candidate = 0;
  size_t votes = 0;
  size_t i;
  size_t occurrences = 0;
  size_t first = count;
  if (base == NULL || compare == NULL || count == 0) {
    return DS_NOT_FOUND;
  }
  for (i = 0; i < count; i++) {
    if (votes == 0) {
      candidate = i;
      votes = 1;
    } else if (compare(CAT(base, i, elem_size), CAT(base, candidate, elem_size),
                       user) == 0) {
      votes++;
    } else {
      votes--;
    }
  }
  for (i = 0; i < count; i++) {
    if (compare(CAT(base, i, elem_size), CAT(base, candidate, elem_size),
                user) == 0) {
      if (first == count) {
        first = i;
      }
      occurrences++;
    }
  }
  if (occurrences <= count / 2) {
    return DS_NOT_FOUND;
  }
  if (out_index != NULL) {
    *out_index = first;
  }
  return DS_OK;
}

long ds_max_subarray_long(const long *values, size_t count, size_t *out_first,
                          size_t *out_last) {
  long best;
  long current;
  size_t start = 0;
  size_t best_first = 0;
  size_t best_last = 0;
  size_t i;
  if (values == NULL || count == 0) {
    if (out_first != NULL) {
      *out_first = 0;
    }
    if (out_last != NULL) {
      *out_last = 0;
    }
    return 0;
  }
  best = values[0];
  current = values[0];
  for (i = 1; i < count; i++) {
    if (current < 0) {
      current = values[i];
      start = i;
    } else {
      current += values[i];
    }
    if (current > best) {
      best = current;
      best_first = start;
      best_last = i;
    }
  }
  if (out_first != NULL) {
    *out_first = best_first;
  }
  if (out_last != NULL) {
    *out_last = best_last;
  }
  return best;
}

/* ---- perfect shuffle --------------------------------------------------------
 */

/* the riffle of the 2m elements at a (1-based positions): i -> 2i mod
 * (2m + 1) when forward, i -> (m + 1) i mod (2m + 1) when inverse; for
 * 2m + 1 = 3^k the cycles are led by 1, 3, ..., 3^(k-1) */
static void sq_cycles(unsigned char *a, size_t m, size_t elem_size,
                      bool forward) {
  size_t mod = 2 * m + 1;
  size_t mult = forward ? 2 : m + 1;
  size_t s;
  for (s = 1; s < mod; s *= 3) {
    size_t j = s * mult % mod;
    while (j != s) {
      ds_elem_swap(a + (s - 1) * elem_size, a + (j - 1) * elem_size, elem_size);
      j = j * mult % mod;
    }
  }
}

/* largest m with 2m + 1 = 3^k <= 2n + 1 */
static size_t sq_block(size_t n) {
  size_t p = 1;
  while (p <= (2 * n + 1) / 3) {
    p *= 3;
  }
  return (p - 1) / 2;
}

static void sq_in_shuffle(unsigned char *a, size_t n, size_t es) {
  while (n > 0) {
    size_t m = sq_block(n);
    /* bring the first m of the second half next to the first m */
    ds_rotate(a + m * es, n, es, n - m);
    sq_cycles(a, m, es, true);
    a += 2 * m * es;
    n -= m;
  }
}

static void sq_in_unshuffle(unsigned char *a, size_t n, size_t es) {
  size_t steps[64];
  size_t offs[64];
  size_t ns[64];
  size_t k = 0;
  size_t off = 0;
  while (n > 0 && k < 64) {
    size_t m = sq_block(n);
    steps[k] = m;
    offs[k] = off;
    ns[k] = n;
    k++;
    off += 2 * m;
    n -= m;
  }
  while (k-- > 0) {
    unsigned char *b = a + offs[k] * es;
    size_t m = steps[k];
    sq_cycles(b, m, es, false);
    ds_rotate(b + m * es, ns[k], es, m);
  }
}

ds_status_t ds_perfect_shuffle(void *base, size_t count, size_t elem_size,
                               bool in_shuffle) {
  unsigned char *a = (unsigned char *)base;
  if (base == NULL && count > 0) {
    return DS_ERR_NULL;
  }
  if (count % 2 != 0) {
    return DS_ERR_INVALID;
  }
  if (count < 4 && !in_shuffle) {
    return DS_OK;
  }
  if (in_shuffle) {
    sq_in_shuffle(a, count / 2, elem_size);
  } else {
    /* out-shuffle: the first and last stay, the middle is in-shuffled */
    sq_in_shuffle(a + elem_size, count / 2 - 1, elem_size);
  }
  return DS_OK;
}

ds_status_t ds_perfect_unshuffle(void *base, size_t count, size_t elem_size,
                                 bool in_shuffle) {
  unsigned char *a = (unsigned char *)base;
  if (base == NULL && count > 0) {
    return DS_ERR_NULL;
  }
  if (count % 2 != 0) {
    return DS_ERR_INVALID;
  }
  if (count < 4 && !in_shuffle) {
    return DS_OK;
  }
  if (in_shuffle) {
    sq_in_unshuffle(a, count / 2, elem_size);
  } else {
    sq_in_unshuffle(a + elem_size, count / 2 - 1, elem_size);
  }
  return DS_OK;
}
