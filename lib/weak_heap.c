#include "weak_heap.h"
#include <string.h>

typedef struct wh_view {
  unsigned char *a;
  size_t es;
  unsigned char *r;
  ds_weak_heap_compare_func_t compare;
  void *user;
  int sign; /* +1 min-heap, -1 max-heap (used by the sort) */
} wh_view_t;

static unsigned char *wh_at(const wh_view_t *v, size_t i) {
  return v->a + i * v->es;
}

static unsigned int wh_bit(const wh_view_t *v, size_t i) {
  return (unsigned int)(v->r[i >> 3] >> (i & 7)) & 1U;
}

static void wh_flip(const wh_view_t *v, size_t i) {
  v->r[i >> 3] = (unsigned char)(v->r[i >> 3] ^ (1U << (i & 7)));
}

static void wh_clear_bit(const wh_view_t *v, size_t i) {
  v->r[i >> 3] = (unsigned char)(v->r[i >> 3] & ~(1U << (i & 7)));
}

static void wh_swap(const wh_view_t *v, size_t i, size_t j) {
  unsigned char *p = wh_at(v, i);
  unsigned char *q = wh_at(v, j);
  unsigned char t;
  size_t k;
  for (k = 0; k < v->es; k++) {
    t = p[k];
    p[k] = q[k];
    q[k] = t;
  }
}

static bool wh_less(const wh_view_t *v, size_t i, size_t j) {
  int c = v->compare(wh_at(v, i), wh_at(v, j), v->user);
  return v->sign > 0 ? c < 0 : c > 0;
}

/* Distinguished ancestor: climb while j is a left child. */
static size_t wh_dancestor(const wh_view_t *v, size_t j) {
  while ((j & 1U) == wh_bit(v, j >> 1)) {
    j >>= 1;
  }
  return j >> 1;
}

/* Restore order between i and j (i the distinguished ancestor of j).
 * Returns true when they were exchanged. */
static bool wh_join(const wh_view_t *v, size_t i, size_t j) {
  if (wh_less(v, j, i)) {
    wh_swap(v, i, j);
    wh_flip(v, j);
    return true;
  }
  return false;
}

static void wh_build(const wh_view_t *v, size_t n) {
  size_t j;
  memset(v->r, 0, DS_WEAK_HEAP_BITS_SIZE(n));
  for (j = n; j-- > 1;) {
    (void)wh_join(v, wh_dancestor(v, j), j);
  }
}

/* Re-establish the order after the root of a heap of n elements changed. */
static void wh_sift_down(const wh_view_t *v, size_t n) {
  size_t x;
  if (n < 2) {
    return;
  }
  x = 1;
  while (2 * x + wh_bit(v, x) < n) {
    x = 2 * x + wh_bit(v, x);
  }
  while (x > 0) {
    (void)wh_join(v, 0, x);
    x >>= 1;
  }
}

/* Element n has just been appended. */
static void wh_sift_up(const wh_view_t *v, size_t n) {
  size_t j = n;
  size_t i;
  wh_clear_bit(v, n);
  if ((n & 1U) == 0 && n > 0) {
    wh_clear_bit(v, n >> 1);
  }
  while (j != 0) {
    i = wh_dancestor(v, j);
    if (!wh_join(v, i, j)) {
      break;
    }
    j = i;
  }
}

static void wh_view_of(const ds_weak_heap_t *heap, wh_view_t *v) {
  v->a = heap->items.data;
  v->es = heap->items.elem_size;
  v->r = heap->bits;
  v->compare = heap->compare;
  v->user = heap->user;
  v->sign = 1;
}

ds_status_t ds_weak_heap_init(ds_weak_heap_t *heap, size_t elem_size,
                              ds_weak_heap_compare_func_t compare, void *user,
                              ds_context_t *context) {
  if (heap == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  heap->bits = NULL;
  heap->bits_capacity = 0;
  heap->compare = compare;
  heap->user = user;
  heap->context = context;
  heap->fixed = false;
  return ds_vec_init(&heap->items, elem_size, context);
}

ds_status_t ds_weak_heap_init_buffer(ds_weak_heap_t *heap, size_t elem_size,
                                     ds_weak_heap_compare_func_t compare,
                                     void *user, void *buffer, size_t capacity,
                                     unsigned char *bits_buffer) {
  if (heap == NULL || compare == NULL || bits_buffer == NULL) {
    return DS_ERR_NULL;
  }
  heap->bits = bits_buffer;
  heap->bits_capacity = DS_WEAK_HEAP_BITS_SIZE(capacity) * 8;
  heap->compare = compare;
  heap->user = user;
  heap->context = NULL;
  heap->fixed = true;
  return ds_vec_init_buffer(&heap->items, elem_size, buffer, capacity);
}

void ds_weak_heap_destroy(ds_weak_heap_t *heap) {
  if (heap == NULL) {
    return;
  }
  if (!heap->fixed && heap->bits != NULL) {
    ds_context_free(heap->context, heap->bits);
  }
  heap->bits = NULL;
  heap->bits_capacity = 0;
  ds_vec_destroy(&heap->items);
}

static ds_status_t wh_reserve_bits(ds_weak_heap_t *heap, size_t count) {
  size_t want;
  unsigned char *bits;
  if (count <= heap->bits_capacity) {
    return DS_OK;
  }
  if (heap->fixed) {
    return DS_ERR_OVERFLOW;
  }
  want = heap->bits_capacity < 64 ? 64 : heap->bits_capacity;
  while (want < count) {
    if (want > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    want *= 2;
  }
  bits = (unsigned char *)ds_context_realloc(heap->context, heap->bits,
                                             DS_WEAK_HEAP_BITS_SIZE(want));
  if (bits == NULL) {
    return DS_ERR_ALLOC;
  }
  heap->bits = bits;
  heap->bits_capacity = want;
  return DS_OK;
}

ds_status_t ds_weak_heap_push(ds_weak_heap_t *heap, const void *elem) {
  ds_status_t status;
  wh_view_t v;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  status = wh_reserve_bits(heap, heap->items.size + 1);
  if (status != DS_OK) {
    return status;
  }
  status = ds_vec_push(&heap->items, elem);
  if (status != DS_OK) {
    return status;
  }
  wh_view_of(heap, &v);
  wh_sift_up(&v, heap->items.size - 1);
  return DS_OK;
}

const void *ds_weak_heap_peek(const ds_weak_heap_t *heap) {
  return heap->items.size == 0 ? NULL : heap->items.data;
}

ds_status_t ds_weak_heap_pop(ds_weak_heap_t *heap, void *out) {
  wh_view_t v;
  size_t n = heap->items.size;
  size_t es = heap->items.elem_size;
  if (n == 0) {
    return DS_EMPTY;
  }
  if (out != NULL) {
    memcpy(out, heap->items.data, es);
  }
  n--;
  if (n > 0) {
    memcpy(heap->items.data, heap->items.data + n * es, es);
  }
  heap->items.size = n;
  wh_view_of(heap, &v);
  wh_sift_down(&v, n);
  return DS_OK;
}

ds_status_t ds_weak_heap_assign(ds_weak_heap_t *heap, const void *elems,
                                size_t count) {
  ds_status_t status;
  wh_view_t v;
  if (elems == NULL && count > 0) {
    return DS_ERR_NULL;
  }
  status = wh_reserve_bits(heap, count);
  if (status != DS_OK) {
    return status;
  }
  ds_vec_clear(&heap->items);
  status = ds_vec_append(&heap->items, elems, count);
  if (status != DS_OK) {
    return status;
  }
  wh_view_of(heap, &v);
  if (count > 0) {
    wh_build(&v, count);
  }
  return DS_OK;
}

size_t ds_weak_heap_size(const ds_weak_heap_t *heap) {
  return heap->items.size;
}

bool ds_weak_heap_is_empty(const ds_weak_heap_t *heap) {
  return heap->items.size == 0;
}

void ds_weak_heap_clear(ds_weak_heap_t *heap) {
  ds_vec_clear(&heap->items);
}

ds_status_t ds_weak_heap_sort(void *base, size_t count, size_t elem_size,
                              ds_weak_heap_compare_func_t compare, void *user,
                              unsigned char *bits, ds_context_t *context) {
  wh_view_t v;
  unsigned char *scratch = bits;
  size_t n;
  if (compare == NULL || (base == NULL && count > 0)) {
    return DS_ERR_NULL;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL) {
    scratch = (unsigned char *)ds_context_alloc(context,
                                                DS_WEAK_HEAP_BITS_SIZE(count));
    if (scratch == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  v.a = (unsigned char *)base;
  v.es = elem_size;
  v.r = scratch;
  v.compare = compare;
  v.user = user;
  v.sign = -1; /* max-heap: the largest goes to the back */
  wh_build(&v, count);
  for (n = count - 1; n > 0; n--) {
    wh_swap(&v, 0, n);
    wh_sift_down(&v, n);
  }
  if (bits == NULL) {
    ds_context_free(context, scratch);
  }
  return DS_OK;
}
