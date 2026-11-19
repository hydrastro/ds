#include <dsa/core/stage.h>
#include <dsa/structures/shadow_heap.h>
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

static void sh_moved(ds_shadow_heap_t *h, size_t index, size_t report) {
  if (h->moved != NULL)
    h->moved(SH_AT(h, index), report, h->moved_user);
}

static void sh_swap(ds_shadow_heap_t *h, size_t a, size_t b) {
  sh_swap_bytes(SH_AT(h, a), SH_AT(h, b), h->elem_size);
  sh_moved(h, a, a);
  sh_moved(h, b, b);
}

static int sh_less(const ds_shadow_heap_t *h, size_t i, size_t j) {
  return h->compare(SH_AT(h, i), SH_AT(h, j), h->user) < 0;
}

/* Sift node i down within [0, n). */
static void sh_sift_down(ds_shadow_heap_t *h, size_t i, size_t n) {
  size_t c;
  for (;;) {
    if (n < 2 || i > (n - 2) / 2)
      return;
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
    sh_swap(h, i, c);
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
  heap->context = context != NULL ? context : ds_default_context();
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
  if (capacity > (size_t)-1 / elem_size)
    return DS_ERR_OVERFLOW;
  heap->data = (unsigned char *)buffer;
  heap->capacity = capacity;
  heap->fixed = true;
  return DS_OK;
}

void ds_shadow_heap_destroy(ds_shadow_heap_t *heap) {
  if (heap == NULL) {
    return;
  }
  ds_shadow_heap_clear(heap);
  if (!heap->fixed) {
    ds_context_free(heap->context, heap->data);
  }
  heap->data = NULL;
  heap->count = 0;
  heap->heap_count = 0;
  heap->capacity = 0;
}

void ds_shadow_heap_clear(ds_shadow_heap_t *heap) {
  size_t i;
  if (heap != NULL) {
    if (heap->moved != NULL)
      for (i = 0; i < heap->count; i++)
        sh_moved(heap, i, (size_t)-1);
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
  ds_stage_t stage;
  bool staged = false;
  if (heap == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (heap->count == (size_t)-1)
    return DS_ERR_OVERFLOW;
  if (heap->count == heap->capacity && !heap->fixed) {
    st = ds_stage_copy(&stage, heap->context, true, elem, heap->elem_size, NULL,
                       0);
    if (st != DS_OK)
      return st;
    staged = true;
    elem = stage.key;
  }
  st = sh_reserve(heap, heap->count + 1);
  if (st != DS_OK) {
    if (staged)
      ds_stage_release(&stage);
    return st;
  }
  memmove(SH_AT(heap, heap->count), elem, heap->elem_size);
  if (staged)
    ds_stage_release(&stage);
  sh_moved(heap, heap->count, heap->count);
  heap->count++;
  if (heap->count - heap->heap_count >= sh_threshold(heap)) {
    ds_shadow_heap_flush(heap);
  }
  return DS_OK;
}

const void *ds_shadow_heap_top(const ds_shadow_heap_t *heap) {
  size_t best;
  size_t i;
  if (heap == NULL || heap->count == 0)
    return NULL;
  best = heap->heap_count > 0 ? 0 : heap->heap_count;
  for (i = heap->heap_count; i < heap->count; i++) {
    if (sh_less(heap, i, best)) {
      best = i;
    }
  }
  return SH_AT(heap, best);
}

ds_status_t ds_shadow_heap_peek(const ds_shadow_heap_t *heap, void *out) {
  const void *top;
  if (heap == NULL || out == NULL)
    return DS_ERR_NULL;
  top = ds_shadow_heap_top(heap);
  if (top == NULL)
    return DS_EMPTY;
  memcpy(out, top, heap->elem_size);
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
  return ds_shadow_heap_remove_at(heap, 0, out);
}

ds_status_t ds_shadow_heap_merge(ds_shadow_heap_t *dst, ds_shadow_heap_t *src) {
  ds_status_t st;
  ds_shadow_heap_t tmp;
  size_t total, i;
  if (dst == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (dst == src || src->count == 0) {
    return DS_OK;
  }
  if (dst->elem_size != src->elem_size || dst->compare != src->compare ||
      dst->user != src->user) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_add(dst->count, src->count, &total))
    return DS_ERR_OVERFLOW;
  /* attach the smaller heap behind the larger: swap storage when the
   * source is larger and both own growable storage */
  if (src->count > dst->count && !dst->fixed && !src->fixed &&
      dst->context == src->context && dst->moved == NULL &&
      src->moved == NULL) {
    st = sh_reserve(src, total);
    if (st != DS_OK)
      return st;
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
  st = sh_reserve(dst, total);
  if (st != DS_OK) {
    return st;
  }
  ds_shadow_heap_flush(dst);
  if (src->count > 0) {
    for (i = 0; i < src->count; i++)
      sh_moved(src, i, (size_t)-1);
    memcpy(SH_AT(dst, dst->count), src->data, src->count * src->elem_size);
    for (i = 0; i < src->count; i++)
      sh_moved(dst, dst->count + i, dst->count + i);
    dst->count += src->count;
    ds_shadow_heap_flush(dst);
  }
  src->count = 0;
  src->heap_count = 0;
  return DS_OK;
}

void ds_shadow_heap_set_move_callback(ds_shadow_heap_t *heap,
                                      void (*moved)(const void *, size_t,
                                                    void *),
                                      void *user) {
  size_t i;
  if (heap == NULL)
    return;
  heap->moved = moved;
  heap->moved_user = user;
  if (moved != NULL)
    for (i = 0; i < heap->count; i++)
      sh_moved(heap, i, i);
}

ds_status_t ds_shadow_heap_reserve(ds_shadow_heap_t *heap, size_t capacity) {
  return heap == NULL ? DS_ERR_NULL : sh_reserve(heap, capacity);
}

const void *ds_shadow_heap_at(const ds_shadow_heap_t *heap, size_t index) {
  return heap == NULL || index >= heap->count ? NULL : SH_AT(heap, index);
}

ds_status_t ds_shadow_heap_repair_at(ds_shadow_heap_t *heap, size_t index) {
  size_t parent, original = index;
  if (heap == NULL)
    return DS_ERR_NULL;
  if (index >= heap->count)
    return DS_ERR_RANGE;
  if (index >= heap->heap_count)
    return DS_OK;
  while (index != 0) {
    parent = (index - 1) / 2;
    if (!sh_less(heap, index, parent))
      break;
    sh_swap(heap, index, parent);
    index = parent;
  }
  if (index == original)
    sh_sift_down(heap, index, heap->heap_count);
  return DS_OK;
}

ds_status_t ds_shadow_heap_update_at(ds_shadow_heap_t *heap, size_t index,
                                     const void *elem) {
  if (heap == NULL || elem == NULL)
    return DS_ERR_NULL;
  if (index >= heap->count)
    return DS_ERR_RANGE;
  sh_moved(heap, index, (size_t)-1);
  memmove(SH_AT(heap, index), elem, heap->elem_size);
  sh_moved(heap, index, index);
  return ds_shadow_heap_repair_at(heap, index);
}

ds_status_t ds_shadow_heap_remove_at(ds_shadow_heap_t *heap, size_t index,
                                     void *out) {
  if (heap == NULL)
    return DS_ERR_NULL;
  if (index >= heap->count)
    return DS_ERR_RANGE;
  if (out != NULL)
    memcpy(out, SH_AT(heap, index), heap->elem_size);
  sh_moved(heap, index, (size_t)-1);
  heap->count--;
  if (heap->heap_count > heap->count)
    heap->heap_count = heap->count;
  if (index < heap->count) {
    memcpy(SH_AT(heap, index), SH_AT(heap, heap->count), heap->elem_size);
    sh_moved(heap, index, index);
    (void)ds_shadow_heap_repair_at(heap, index);
  }
  if (heap->count - heap->heap_count >= sh_threshold(heap))
    ds_shadow_heap_flush(heap);
  return DS_OK;
}
