#include "shadow_heap.h"
#include <string.h>

#define SH_AT(h, i) ((h)->data + (size_t)(i) * (h)->elem_size)

static void sh_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  unsigned char t;
  while (n-- > 0) {
    t = *a;
    *a++ = *b;
    *b++ = t;
  }
}

static int sh_less(const ds_shadow_heap_t *h, size_t i, size_t j) {
  return h->compare(SH_AT(h, i), SH_AT(h, j), h->user) < 0;
}

/* Sift node i down within [0, n). */
static void sh_sift_down(ds_shadow_heap_t *h, size_t i, size_t n) {
  size_t c;
  for (;;) {
    c = 2 * i + 1;
    if (c >= n) {
      return;
    }
    if (c + 1 < n && sh_less(h, c + 1, c)) {
      c++;
    }
    if (!sh_less(h, c, i)) {
      return;
    }
    sh_swap_bytes(SH_AT(h, i), SH_AT(h, c), h->elem_size);
    i = c;
  }
}

static size_t sh_threshold(const ds_shadow_heap_t *h) {
  size_t n = h->count;
  size_t t = 1;
  if (h->threshold != 0) {
    return h->threshold;
  }
  while (n > 1) {
    n >>= 1;
    t++;
  }
  return t;
}

void ds_shadow_heap_flush(ds_shadow_heap_t *h) {
  size_t lo;
  size_t hi; /* inclusive range of nodes to fix at this level */
  size_t i;
  if (h == NULL || h->heap_count == h->count) {
    return;
  }
  /* the shadow [heap_count, count) and, level by level, the contiguous
   * ranges of their ancestors, each fixed bottom-up (Floyd) */
  lo = h->heap_count;
  hi = h->count - 1;
  for (;;) {
    for (i = hi + 1; i-- > lo;) {
      sh_sift_down(h, i, h->count);
    }
    if (lo == 0) {
      break;
    }
    lo = (lo - 1) / 2;
    hi = hi == 0 ? 0 : (hi - 1) / 2;
  }
  h->heap_count = h->count;
}

static ds_status_t sh_reserve(ds_shadow_heap_t *h, size_t need) {
  size_t cap;
  unsigned char *grown;
  if (need <= h->capacity) {
    return DS_OK;
  }
  if (h->fixed) {
    return DS_ERR_OVERFLOW;
  }
  cap = h->capacity < 8 ? 8 : h->capacity;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2) {
      cap = need;
      break;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / h->elem_size) {
    return DS_ERR_OVERFLOW;
  }
  grown = (unsigned char *)ds_context_realloc(h->context, h->data,
                                              cap * h->elem_size);
  if (grown == NULL) {
    return DS_ERR_ALLOC;
  }
  h->data = grown;
  h->capacity = cap;
  return DS_OK;
}

ds_status_t ds_shadow_heap_init(ds_shadow_heap_t *heap, size_t elem_size,
                                ds_shadow_heap_compare_func_t compare,
                                void *user, size_t threshold,
                                ds_context_t *context) {
  if (heap == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(heap, 0, sizeof(*heap));
  heap->elem_size = elem_size;
  heap->compare = compare;
  heap->user = user;
  heap->threshold = threshold;
  heap->context = context;
  return DS_OK;
}

ds_status_t ds_shadow_heap_init_buffer(ds_shadow_heap_t *heap, size_t elem_size,
                                       ds_shadow_heap_compare_func_t compare,
                                       void *user, size_t threshold,
                                       void *buffer, size_t capacity) {
  ds_status_t st;
  if (buffer == NULL && capacity > 0) {
    return DS_ERR_NULL;
  }
  st = ds_shadow_heap_init(heap, elem_size, compare, user, threshold, NULL);
  if (st != DS_OK) {
    return st;
  }
  heap->data = (unsigned char *)buffer;
  heap->capacity = capacity;
  heap->fixed = true;
  return DS_OK;
}

void ds_shadow_heap_destroy(ds_shadow_heap_t *heap) {
  if (heap == NULL) {
    return;
  }
  if (!heap->fixed) {
    ds_context_free(heap->context, heap->data);
  }
  heap->data = NULL;
  heap->count = 0;
  heap->heap_count = 0;
  heap->capacity = 0;
}

void ds_shadow_heap_clear(ds_shadow_heap_t *heap) {
  if (heap != NULL) {
    heap->count = 0;
    heap->heap_count = 0;
  }
}

size_t ds_shadow_heap_size(const ds_shadow_heap_t *heap) {
  return heap == NULL ? 0 : heap->count;
}

size_t ds_shadow_heap_shadow_size(const ds_shadow_heap_t *heap) {
  return heap == NULL ? 0 : heap->count - heap->heap_count;
}

bool ds_shadow_heap_is_empty(const ds_shadow_heap_t *heap) {
  return heap == NULL || heap->count == 0;
}

ds_status_t ds_shadow_heap_push(ds_shadow_heap_t *heap, const void *elem) {
  ds_status_t st;
  if (heap == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  st = sh_reserve(heap, heap->count + 1);
  if (st != DS_OK) {
    return st;
  }
  memcpy(SH_AT(heap, heap->count), elem, heap->elem_size);
  heap->count++;
  if (heap->count - heap->heap_count >= sh_threshold(heap)) {
    ds_shadow_heap_flush(heap);
  }
  return DS_OK;
}

ds_status_t ds_shadow_heap_peek(const ds_shadow_heap_t *heap, void *out) {
  size_t best;
  size_t i;
  if (heap == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (heap->count == 0) {
    return DS_EMPTY;
  }
  best = heap->heap_count > 0 ? 0 : heap->heap_count;
  for (i = heap->heap_count; i < heap->count; i++) {
    if (sh_less(heap, i, best)) {
      best = i;
    }
  }
  memcpy(out, SH_AT(heap, best), heap->elem_size);
  return DS_OK;
}

ds_status_t ds_shadow_heap_pop(ds_shadow_heap_t *heap, void *out) {
  if (heap == NULL) {
    return DS_ERR_NULL;
  }
  if (heap->count == 0) {
    return DS_EMPTY;
  }
  ds_shadow_heap_flush(heap);
  if (out != NULL) {
    memcpy(out, SH_AT(heap, 0), heap->elem_size);
  }
  heap->count--;
  heap->heap_count--;
  if (heap->count > 0) {
    memcpy(SH_AT(heap, 0), SH_AT(heap, heap->count), heap->elem_size);
    sh_sift_down(heap, 0, heap->count);
  }
  return DS_OK;
}

ds_status_t ds_shadow_heap_merge(ds_shadow_heap_t *dst, ds_shadow_heap_t *src) {
  ds_status_t st;
  ds_shadow_heap_t tmp;
  if (dst == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (dst == src || src->count == 0) {
    return DS_OK;
  }
  if (dst->elem_size != src->elem_size) {
    return DS_ERR_INVALID;
  }
  /* attach the smaller heap behind the larger: swap storage when the
   * source is larger and both own growable storage */
  if (src->count > dst->count && !dst->fixed && !src->fixed &&
      dst->context == src->context) {
    tmp = *dst;
    dst->data = src->data;
    dst->count = src->count;
    dst->heap_count = src->heap_count;
    dst->capacity = src->capacity;
    src->data = tmp.data;
    src->count = tmp.count;
    src->heap_count = tmp.heap_count;
    src->capacity = tmp.capacity;
  }
  st = sh_reserve(dst, dst->count + src->count);
  if (st != DS_OK) {
    return st;
  }
  ds_shadow_heap_flush(dst);
  if (src->count > 0) {
    memcpy(SH_AT(dst, dst->count), src->data, src->count * src->elem_size);
    dst->count += src->count;
    ds_shadow_heap_flush(dst);
  }
  src->count = 0;
  src->heap_count = 0;
  return DS_OK;
}
