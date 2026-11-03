#include "min_max_heap.h"
#include <string.h>

static unsigned char *mmh_ptr(const ds_min_max_heap_t *heap, size_t index) {
  return heap->items.data + index * heap->items.elem_size;
}

/* On min levels "better" means smaller; on max levels, larger. */
static bool mmh_better(const ds_min_max_heap_t *heap, size_t a, size_t b,
                       bool min_side) {
  int c = heap->compare(mmh_ptr(heap, a), mmh_ptr(heap, b), heap->user);
  return min_side ? c < 0 : c > 0;
}

static bool mmh_is_min_level(size_t index) {
  size_t n = index + 1;
  unsigned int depth = 0;
  while (n > 1) {
    n >>= 1;
    depth++;
  }
  return (depth & 1U) == 0;
}

static void mmh_swap(ds_min_max_heap_t *heap, size_t a, size_t b) {
  unsigned char *pa = mmh_ptr(heap, a);
  unsigned char *pb = mmh_ptr(heap, b);
  unsigned char tmp;
  size_t i;
  for (i = 0; i < heap->items.elem_size; i++) {
    tmp = pa[i];
    pa[i] = pb[i];
    pb[i] = tmp;
  }
}

static void mmh_bubble_up(ds_min_max_heap_t *heap, size_t index,
                          bool min_side) {
  size_t grandparent;
  while (index >= 3) {
    grandparent = ((index - 1) / 2 - 1) / 2;
    if (!mmh_better(heap, index, grandparent, min_side)) {
      break;
    }
    mmh_swap(heap, index, grandparent);
    index = grandparent;
  }
}

static void mmh_trickle_down(ds_min_max_heap_t *heap, size_t index,
                             bool min_side) {
  size_t n = heap->items.size;
  size_t first_child;
  size_t first_grandchild;
  size_t best;
  size_t k;
  size_t limit;
  size_t parent;
  for (;;) {
    first_child = 2 * index + 1;
    if (first_child >= n) {
      return;
    }
    best = first_child;
    if (first_child + 1 < n &&
        mmh_better(heap, first_child + 1, best, min_side)) {
      best = first_child + 1;
    }
    first_grandchild = 4 * index + 3;
    limit = first_grandchild + 4 < n ? first_grandchild + 4 : n;
    for (k = first_grandchild; k < limit; k++) {
      if (mmh_better(heap, k, best, min_side)) {
        best = k;
      }
    }
    if (!mmh_better(heap, best, index, min_side)) {
      return;
    }
    mmh_swap(heap, best, index);
    if (best < first_grandchild) {
      return; /* a child: it sits on the opposite level, nothing below */
    }
    parent = (best - 1) / 2;
    if (mmh_better(heap, parent, best, min_side)) {
      mmh_swap(heap, parent, best);
    }
    index = best;
  }
}

ds_status_t ds_min_max_heap_init(ds_min_max_heap_t *heap, size_t elem_size,
                                 ds_min_max_heap_compare_func_t compare,
                                 void *user, ds_context_t *context) {
  if (heap == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  heap->compare = compare;
  heap->user = user;
  return ds_vec_init(&heap->items, elem_size, context);
}

ds_status_t ds_min_max_heap_init_buffer(ds_min_max_heap_t *heap,
                                        size_t elem_size,
                                        ds_min_max_heap_compare_func_t compare,
                                        void *user, void *buffer,
                                        size_t capacity) {
  if (heap == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  heap->compare = compare;
  heap->user = user;
  return ds_vec_init_buffer(&heap->items, elem_size, buffer, capacity);
}

void ds_min_max_heap_destroy(ds_min_max_heap_t *heap) {
  if (heap != NULL) {
    ds_vec_destroy(&heap->items);
  }
}

ds_status_t ds_min_max_heap_push(ds_min_max_heap_t *heap, const void *elem) {
  ds_status_t status;
  size_t index;
  size_t parent;
  bool min_level;
  status = ds_vec_push(&heap->items, elem);
  if (status != DS_OK) {
    return status;
  }
  index = heap->items.size - 1;
  if (index == 0) {
    return DS_OK;
  }
  parent = (index - 1) / 2;
  min_level = mmh_is_min_level(index);
  if (mmh_better(heap, index, parent, !min_level)) {
    mmh_swap(heap, index, parent);
    mmh_bubble_up(heap, parent, !min_level);
  } else {
    mmh_bubble_up(heap, index, min_level);
  }
  return DS_OK;
}

static size_t mmh_max_index(const ds_min_max_heap_t *heap) {
  if (heap->items.size < 3) {
    return heap->items.size - 1;
  }
  return mmh_better(heap, 2, 1, false) ? 2 : 1;
}

const void *ds_min_max_heap_peek_min(const ds_min_max_heap_t *heap) {
  return heap->items.size == 0 ? NULL : heap->items.data;
}

const void *ds_min_max_heap_peek_max(const ds_min_max_heap_t *heap) {
  if (heap->items.size == 0) {
    return NULL;
  }
  return mmh_ptr(heap, mmh_max_index(heap));
}

static void mmh_remove_at(ds_min_max_heap_t *heap, size_t index, void *out,
                          bool min_side) {
  size_t last = heap->items.size - 1;
  size_t es = heap->items.elem_size;
  if (out != NULL) {
    memcpy(out, mmh_ptr(heap, index), es);
  }
  if (index != last) {
    memcpy(mmh_ptr(heap, index), mmh_ptr(heap, last), es);
  }
  heap->items.size--;
  if (index < heap->items.size) {
    mmh_trickle_down(heap, index, min_side);
  }
}

ds_status_t ds_min_max_heap_pop_min(ds_min_max_heap_t *heap, void *out) {
  if (heap->items.size == 0) {
    return DS_EMPTY;
  }
  mmh_remove_at(heap, 0, out, true);
  return DS_OK;
}

ds_status_t ds_min_max_heap_pop_max(ds_min_max_heap_t *heap, void *out) {
  size_t index;
  if (heap->items.size == 0) {
    return DS_EMPTY;
  }
  index = mmh_max_index(heap);
  mmh_remove_at(heap, index, out, index == 0);
  return DS_OK;
}

size_t ds_min_max_heap_size(const ds_min_max_heap_t *heap) {
  return heap->items.size;
}

bool ds_min_max_heap_is_empty(const ds_min_max_heap_t *heap) {
  return heap->items.size == 0;
}

void ds_min_max_heap_clear(ds_min_max_heap_t *heap) {
  ds_vec_clear(&heap->items);
}
