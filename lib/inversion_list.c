#include "inversion_list.h"
#include <limits.h>
#include <string.h>

#define IL_MODE_REMOVE 0
#define IL_MODE_ADD 1

static size_t il_lower_bound(const ds_inversion_list_t *list,
                             unsigned long value) {
  size_t lo = 0;
  size_t hi = list->count;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (list->bounds[mid] < value) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static size_t il_upper_bound(const ds_inversion_list_t *list,
                             unsigned long value) {
  size_t lo = 0;
  size_t hi = list->count;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (list->bounds[mid] <= value) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

ds_status_t ds_inversion_list_init(ds_inversion_list_t *list,
                                   ds_context_t *context) {
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  list->bounds = NULL;
  list->count = 0;
  list->capacity = 0;
  list->fixed = false;
  list->context = context;
  return DS_OK;
}

ds_status_t ds_inversion_list_init_buffer(ds_inversion_list_t *list,
                                          unsigned long *buffer,
                                          size_t capacity) {
  if (list == NULL || (buffer == NULL && capacity > 0)) {
    return DS_ERR_NULL;
  }
  list->bounds = buffer;
  list->count = 0;
  list->capacity = capacity;
  list->fixed = true;
  list->context = NULL;
  return DS_OK;
}

void ds_inversion_list_destroy(ds_inversion_list_t *list) {
  if (list == NULL) {
    return;
  }
  if (!list->fixed) {
    ds_context_free(list->context, list->bounds);
  }
  list->bounds = NULL;
  list->count = 0;
  list->capacity = 0;
}

void ds_inversion_list_clear(ds_inversion_list_t *list) {
  if (list != NULL) {
    list->count = 0;
  }
}

ds_status_t ds_inversion_list_reserve(ds_inversion_list_t *list, size_t count) {
  size_t cap;
  unsigned long *grown;
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  if (count <= list->capacity) {
    return DS_OK;
  }
  if (list->fixed) {
    return DS_ERR_OVERFLOW;
  }
  cap = list->capacity < 8 ? 8 : list->capacity;
  while (cap < count) {
    if (cap > ((size_t)-1) / 2 / sizeof(unsigned long)) {
      cap = count;
      break;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  grown = (unsigned long *)ds_context_realloc(list->context, list->bounds,
                                              cap * sizeof(unsigned long));
  if (grown == NULL) {
    return DS_ERR_ALLOC;
  }
  list->bounds = grown;
  list->capacity = cap;
  return DS_OK;
}

ds_status_t ds_inversion_list_assign(ds_inversion_list_t *list,
                                     const ds_inversion_list_t *src) {
  ds_status_t st;
  if (list == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (list == src) {
    return DS_OK;
  }
  st = ds_inversion_list_reserve(list, src->count);
  if (st != DS_OK) {
    return st;
  }
  if (src->count > 0) {
    memcpy(list->bounds, src->bounds, src->count * sizeof(unsigned long));
  }
  list->count = src->count;
  return DS_OK;
}

/* Make every value of [low, high] a member (mode ADD) or a non-member
 * (mode REMOVE). Boundaries < low and > high + 1 are kept; a boundary is
 * inserted at low (high + 1) when the state just before (from) it
 * differs from the target. */
static ds_status_t il_assign_range(ds_inversion_list_t *list, unsigned long low,
                                   unsigned long high, int mode) {
  size_t i;
  size_t j;
  size_t ins_lo;
  size_t ins_hi;
  size_t tail;
  size_t new_count;
  unsigned long end;
  ds_status_t st;
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  if (low > high) {
    return DS_ERR_INVALID;
  }
  i = il_lower_bound(list, low);
  ins_lo = ((i % 2) == 0) == (mode == IL_MODE_ADD) ? 1u : 0u;
  if (high == ULONG_MAX) {
    j = list->count;
    ins_hi = 0;
    end = 0;
  } else {
    end = high + 1;
    j = il_upper_bound(list, end);
    ins_hi = ((j % 2) == 0) == (mode == IL_MODE_ADD) ? 1u : 0u;
  }
  tail = list->count - j;
  new_count = i + ins_lo + ins_hi + tail;
  st = ds_inversion_list_reserve(list, new_count);
  if (st != DS_OK) {
    return st;
  }
  if (tail > 0 && i + ins_lo + ins_hi != j) {
    memmove(list->bounds + i + ins_lo + ins_hi, list->bounds + j,
            tail * sizeof(unsigned long));
  }
  if (ins_lo) {
    list->bounds[i] = low;
  }
  if (ins_hi) {
    list->bounds[i + ins_lo] = end;
  }
  list->count = new_count;
  return DS_OK;
}

ds_status_t ds_inversion_list_add_range(ds_inversion_list_t *list,
                                        unsigned long low, unsigned long high) {
  return il_assign_range(list, low, high, IL_MODE_ADD);
}

ds_status_t ds_inversion_list_remove_range(ds_inversion_list_t *list,
                                           unsigned long low,
                                           unsigned long high) {
  return il_assign_range(list, low, high, IL_MODE_REMOVE);
}

ds_status_t ds_inversion_list_add(ds_inversion_list_t *list,
                                  unsigned long value) {
  return il_assign_range(list, value, value, IL_MODE_ADD);
}

ds_status_t ds_inversion_list_remove(ds_inversion_list_t *list,
                                     unsigned long value) {
  return il_assign_range(list, value, value, IL_MODE_REMOVE);
}

/* Insert value as a boundary, or cancel an equal one. */
static void il_toggle_boundary(ds_inversion_list_t *list, unsigned long value) {
  size_t p = il_lower_bound(list, value);
  if (p < list->count && list->bounds[p] == value) {
    memmove(list->bounds + p, list->bounds + p + 1,
            (list->count - p - 1) * sizeof(unsigned long));
    list->count--;
  } else {
    memmove(list->bounds + p + 1, list->bounds + p,
            (list->count - p) * sizeof(unsigned long));
    list->bounds[p] = value;
    list->count++;
  }
}

static bool il_has_boundary(const ds_inversion_list_t *list,
                            unsigned long value) {
  size_t p = il_lower_bound(list, value);
  return p < list->count && list->bounds[p] == value;
}

ds_status_t ds_inversion_list_flip_range(ds_inversion_list_t *list,
                                         unsigned long low,
                                         unsigned long high) {
  size_t need;
  ds_status_t st;
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  if (low > high) {
    return DS_ERR_INVALID;
  }
  need = list->count;
  need = il_has_boundary(list, low) ? need - 1 : need + 1;
  if (high != ULONG_MAX) {
    need = il_has_boundary(list, high + 1) ? need - 1 : need + 1;
  }
  st = ds_inversion_list_reserve(list, need);
  if (st != DS_OK) {
    return st;
  }
  /* Removals first, so the array never exceeds the final count. */
  if (high != ULONG_MAX && il_has_boundary(list, high + 1)) {
    il_toggle_boundary(list, high + 1);
    il_toggle_boundary(list, low);
  } else {
    il_toggle_boundary(list, low);
    if (high != ULONG_MAX) {
      il_toggle_boundary(list, high + 1);
    }
  }
  return DS_OK;
}

bool ds_inversion_list_contains(const ds_inversion_list_t *list,
                                unsigned long value) {
  if (list == NULL) {
    return false;
  }
  return (il_upper_bound(list, value) % 2) == 1;
}

bool ds_inversion_list_is_empty(const ds_inversion_list_t *list) {
  return list == NULL || list->count == 0;
}

size_t ds_inversion_list_range_count(const ds_inversion_list_t *list) {
  return list == NULL ? 0 : (list->count + 1) / 2;
}

ds_status_t ds_inversion_list_get_range(const ds_inversion_list_t *list,
                                        size_t index, unsigned long *out_low,
                                        unsigned long *out_high) {
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= (list->count + 1) / 2) {
    return DS_ERR_RANGE;
  }
  if (out_low != NULL) {
    *out_low = list->bounds[2 * index];
  }
  if (out_high != NULL) {
    *out_high = 2 * index + 1 < list->count ? list->bounds[2 * index + 1] - 1
                                            : ULONG_MAX;
  }
  return DS_OK;
}

ds_status_t ds_inversion_list_find(const ds_inversion_list_t *list,
                                   unsigned long value, size_t *out_index) {
  size_t k;
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  k = il_upper_bound(list, value);
  if ((k % 2) == 0) {
    return DS_NOT_FOUND;
  }
  if (out_index != NULL) {
    *out_index = (k - 1) / 2;
  }
  return DS_OK;
}

ds_status_t ds_inversion_list_next_member(const ds_inversion_list_t *list,
                                          unsigned long value,
                                          unsigned long *out_value) {
  size_t k;
  if (list == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  k = il_upper_bound(list, value);
  if ((k % 2) == 1) {
    *out_value = value;
    return DS_OK;
  }
  if (k >= list->count) {
    return DS_NOT_FOUND;
  }
  *out_value = list->bounds[k];
  return DS_OK;
}

ds_status_t ds_inversion_list_next_gap(const ds_inversion_list_t *list,
                                       unsigned long value,
                                       unsigned long *out_value) {
  size_t k;
  if (list == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  k = il_upper_bound(list, value);
  if ((k % 2) == 0) {
    *out_value = value;
    return DS_OK;
  }
  if (k >= list->count) {
    return DS_NOT_FOUND;
  }
  *out_value = list->bounds[k];
  return DS_OK;
}

ds_status_t ds_inversion_list_cardinality(const ds_inversion_list_t *list,
                                          unsigned long *out_count) {
  size_t r;
  size_t ranges;
  unsigned long total = 0;
  unsigned long lo;
  unsigned long hi;
  unsigned long span; /* size - 1 */
  if (list == NULL || out_count == NULL) {
    return DS_ERR_NULL;
  }
  ranges = (list->count + 1) / 2;
  for (r = 0; r < ranges; r++) {
    ds_inversion_list_get_range(list, r, &lo, &hi);
    span = hi - lo;
    if (span == ULONG_MAX || total > ULONG_MAX - span - 1) {
      return DS_ERR_OVERFLOW;
    }
    total += span + 1;
  }
  *out_count = total;
  return DS_OK;
}

unsigned long ds_inversion_list_rank(const ds_inversion_list_t *list,
                                     unsigned long value) {
  size_t r;
  size_t ranges;
  unsigned long total = 0;
  unsigned long lo;
  unsigned long hi;
  if (list == NULL) {
    return 0;
  }
  ranges = (list->count + 1) / 2;
  for (r = 0; r < ranges; r++) {
    ds_inversion_list_get_range(list, r, &lo, &hi);
    if (lo >= value) {
      break;
    }
    total += (hi < value ? hi : value - 1) - lo + 1;
  }
  return total;
}

ds_status_t ds_inversion_list_select(const ds_inversion_list_t *list,
                                     unsigned long rank,
                                     unsigned long *out_value) {
  size_t r;
  size_t ranges;
  unsigned long lo;
  unsigned long hi;
  if (list == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  ranges = (list->count + 1) / 2;
  for (r = 0; r < ranges; r++) {
    ds_inversion_list_get_range(list, r, &lo, &hi);
    if (rank <= hi - lo) {
      *out_value = lo + rank;
      return DS_OK;
    }
    rank -= hi - lo + 1;
  }
  return DS_NOT_FOUND;
}

/* Walk the merged boundaries of a and b, emitting the boundaries of
 * table(a, b) into out (when non-NULL). Returns the number emitted. */
static size_t il_merge(const ds_inversion_list_t *a,
                       const ds_inversion_list_t *b, unsigned table,
                       unsigned long *out) {
  size_t ia = 0;
  size_t ib = 0;
  size_t n = 0;
  unsigned sa = 0;
  unsigned sb = 0;
  unsigned prev = 0;
  unsigned cur;
  unsigned long x = 0;
  bool first = true;
  for (;;) {
    if (!first) {
      if (ia < a->count && ib < b->count) {
        x = a->bounds[ia] < b->bounds[ib] ? a->bounds[ia] : b->bounds[ib];
      } else if (ia < a->count) {
        x = a->bounds[ia];
      } else if (ib < b->count) {
        x = b->bounds[ib];
      } else {
        break;
      }
    }
    first = false;
    while (ia < a->count && a->bounds[ia] == x) {
      sa ^= 1u;
      ia++;
    }
    while (ib < b->count && b->bounds[ib] == x) {
      sb ^= 1u;
      ib++;
    }
    cur = (table >> (sa * 2 + sb)) & 1u;
    if (cur != prev) {
      if (out != NULL) {
        out[n] = x;
      }
      n++;
      prev = cur;
    }
  }
  return n;
}

ds_status_t ds_inversion_list_combine(ds_inversion_list_t *out,
                                      const ds_inversion_list_t *a,
                                      const ds_inversion_list_t *b,
                                      unsigned table) {
  size_t n;
  unsigned long *tmp;
  ds_context_t *ctx;
  ds_status_t st;
  if (out == NULL || a == NULL || b == NULL) {
    return DS_ERR_NULL;
  }
  table &= 0xFu;
  n = il_merge(a, b, table, NULL);
  if (out != a && out != b) {
    st = ds_inversion_list_reserve(out, n);
    if (st != DS_OK) {
      return st;
    }
    il_merge(a, b, table, out->bounds);
    out->count = n;
    return DS_OK;
  }
  if (n > out->capacity && out->fixed) {
    return DS_ERR_OVERFLOW;
  }
  ctx = out->context;
  tmp = (unsigned long *)ds_context_alloc(ctx, (n == 0 ? 1 : n) *
                                                   sizeof(unsigned long));
  if (tmp == NULL) {
    return DS_ERR_ALLOC;
  }
  il_merge(a, b, table, tmp);
  st = ds_inversion_list_reserve(out, n);
  if (st == DS_OK) {
    if (n > 0) {
      memcpy(out->bounds, tmp, n * sizeof(unsigned long));
    }
    out->count = n;
  }
  ds_context_free(ctx, tmp);
  return st;
}

ds_status_t ds_inversion_list_union(ds_inversion_list_t *out,
                                    const ds_inversion_list_t *a,
                                    const ds_inversion_list_t *b) {
  return ds_inversion_list_combine(out, a, b, DS_INVLIST_UNION);
}

ds_status_t ds_inversion_list_intersection(ds_inversion_list_t *out,
                                           const ds_inversion_list_t *a,
                                           const ds_inversion_list_t *b) {
  return ds_inversion_list_combine(out, a, b, DS_INVLIST_INTERSECTION);
}

ds_status_t ds_inversion_list_difference(ds_inversion_list_t *out,
                                         const ds_inversion_list_t *a,
                                         const ds_inversion_list_t *b) {
  return ds_inversion_list_combine(out, a, b, DS_INVLIST_DIFFERENCE);
}

ds_status_t
ds_inversion_list_symmetric_difference(ds_inversion_list_t *out,
                                       const ds_inversion_list_t *a,
                                       const ds_inversion_list_t *b) {
  return ds_inversion_list_combine(out, a, b, DS_INVLIST_SYMMETRIC_DIFFERENCE);
}

ds_status_t ds_inversion_list_complement(ds_inversion_list_t *list) {
  ds_status_t st;
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  if (list->count > 0 && list->bounds[0] == 0) {
    memmove(list->bounds, list->bounds + 1,
            (list->count - 1) * sizeof(unsigned long));
    list->count--;
    return DS_OK;
  }
  st = ds_inversion_list_reserve(list, list->count + 1);
  if (st != DS_OK) {
    return st;
  }
  memmove(list->bounds + 1, list->bounds, list->count * sizeof(unsigned long));
  list->bounds[0] = 0;
  list->count++;
  return DS_OK;
}

bool ds_inversion_list_equal(const ds_inversion_list_t *a,
                             const ds_inversion_list_t *b) {
  size_t i;
  if (a == NULL || b == NULL) {
    return a == b;
  }
  if (a->count != b->count) {
    return false;
  }
  for (i = 0; i < a->count; i++) {
    if (a->bounds[i] != b->bounds[i]) {
      return false;
    }
  }
  return true;
}

bool ds_inversion_list_is_subset(const ds_inversion_list_t *a,
                                 const ds_inversion_list_t *b) {
  if (a == NULL || b == NULL) {
    return false;
  }
  return il_merge(a, b, DS_INVLIST_DIFFERENCE, NULL) == 0;
}

bool ds_inversion_list_intersects(const ds_inversion_list_t *a,
                                  const ds_inversion_list_t *b) {
  if (a == NULL || b == NULL) {
    return false;
  }
  return il_merge(a, b, DS_INVLIST_INTERSECTION, NULL) != 0;
}

static unsigned il_bit(const unsigned char *bits, unsigned long i) {
  return (unsigned)((bits[i / 8] >> (i % 8)) & 1u);
}

ds_status_t ds_inversion_list_from_bits(ds_inversion_list_t *list,
                                        const unsigned char *bits,
                                        unsigned long bit_count) {
  unsigned long i;
  size_t n = 0;
  unsigned prev = 0;
  unsigned cur;
  ds_status_t st;
  if (list == NULL || (bits == NULL && bit_count > 0)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < bit_count; i++) {
    cur = il_bit(bits, i);
    if (cur != prev) {
      n++;
      prev = cur;
    }
  }
  if (prev) {
    n++;
  }
  st = ds_inversion_list_reserve(list, n);
  if (st != DS_OK) {
    return st;
  }
  n = 0;
  prev = 0;
  for (i = 0; i < bit_count; i++) {
    cur = il_bit(bits, i);
    if (cur != prev) {
      list->bounds[n++] = i;
      prev = cur;
    }
  }
  if (prev) {
    list->bounds[n++] = bit_count;
  }
  list->count = n;
  return DS_OK;
}

void ds_inversion_list_to_bits(const ds_inversion_list_t *list,
                               unsigned char *bits, unsigned long bit_count) {
  size_t r;
  size_t ranges;
  unsigned long lo;
  unsigned long hi;
  unsigned long i;
  if (bits == NULL || bit_count == 0) {
    return;
  }
  memset(bits, 0, (size_t)((bit_count - 1) / 8 + 1));
  if (list == NULL) {
    return;
  }
  ranges = (list->count + 1) / 2;
  for (r = 0; r < ranges; r++) {
    ds_inversion_list_get_range(list, r, &lo, &hi);
    if (lo >= bit_count) {
      break;
    }
    if (hi >= bit_count) {
      hi = bit_count - 1;
    }
    for (i = lo;; i++) {
      bits[i / 8] = (unsigned char)(bits[i / 8] | (1u << (i % 8)));
      if (i == hi) {
        break;
      }
    }
  }
}
