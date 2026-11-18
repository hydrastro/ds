#include "min_max_heap.h"
#include <string.h>

static unsigned char *mmh_ptr(const ds_min_max_heap_t *heap, size_t index) {
  return heap->items.data + index * heap->items.elem_size;
}

static void mmh_moved(ds_min_max_heap_t *heap, size_t index, size_t report) {
  if (heap->moved != NULL)
    heap->moved(mmh_ptr(heap, index), report, heap->moved_user);
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
  mmh_moved(heap, a, a);
  mmh_moved(heap, b, b);
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
    if (n < 2 || index > (n - 2) / 2)
      return;
    first_child = 2 * index + 1;
    if (first_child >= n) {
      return;
    }
    best = first_child;
    if (first_child + 1 < n &&
        mmh_better(heap, first_child + 1, best, min_side)) {
      best = first_child + 1;
    }
    first_grandchild = n >= 4 && index <= (n - 4) / 4 ? 4 * index + 3 : n;
    limit = n - first_grandchild > 4 ? first_grandchild + 4 : n;
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
  heap->moved = NULL;
  heap->moved_user = NULL;
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
  heap->moved = NULL;
  heap->moved_user = NULL;
  return ds_vec_init_buffer(&heap->items, elem_size, buffer, capacity);
}

void ds_min_max_heap_destroy(ds_min_max_heap_t *heap) {
  if (heap != NULL) {
    ds_min_max_heap_clear(heap);
    ds_vec_destroy(&heap->items);
  }
}

ds_status_t ds_min_max_heap_push(ds_min_max_heap_t *heap, const void *elem) {
  ds_status_t status;
  size_t index;
  size_t parent;
  bool min_level;
  if (heap == NULL || elem == NULL)
    return DS_ERR_NULL;
  status = ds_vec_push(&heap->items, elem);
  if (status != DS_OK) {
    return status;
  }
  index = heap->items.size - 1;
  mmh_moved(heap, index, index);
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
  return heap == NULL || heap->items.size == 0 ? NULL : heap->items.data;
}

const void *ds_min_max_heap_peek_max(const ds_min_max_heap_t *heap) {
  if (heap == NULL || heap->items.size == 0) {
    return NULL;
  }
  return mmh_ptr(heap, mmh_max_index(heap));
}

ds_status_t ds_min_max_heap_repair_at(ds_min_max_heap_t *heap, size_t index) {
  bool min_side;
  size_t parent;
  if (heap == NULL)
    return DS_ERR_NULL;
  if (index >= heap->items.size)
    return DS_ERR_RANGE;
  min_side = mmh_is_min_level(index);
  if (index != 0) {
    parent = (index - 1) / 2;
    if (mmh_better(heap, index, parent, !min_side)) {
      mmh_swap(heap, index, parent);
      mmh_bubble_up(heap, parent, !min_side);
    } else
      mmh_bubble_up(heap, index, min_side);
  }
  /* A crossing swap may leave the old opposite-level parent too large/small
   * for this subtree; a same-level bubble already preserves it. */
  mmh_trickle_down(heap, index, min_side);
  return DS_OK;
}

ds_status_t ds_min_max_heap_update_at(ds_min_max_heap_t *heap, size_t index,
                                      const void *elem) {
  if (heap == NULL || elem == NULL)
    return DS_ERR_NULL;
  if (index >= heap->items.size)
    return DS_ERR_RANGE;
  mmh_moved(heap, index, (size_t)-1);
  memmove(mmh_ptr(heap, index), elem, heap->items.elem_size);
  mmh_moved(heap, index, index);
  return ds_min_max_heap_repair_at(heap, index);
}

ds_status_t ds_min_max_heap_remove_at(ds_min_max_heap_t *heap, size_t index,
                                      void *out) {
  size_t last, es;
  if (heap == NULL)
    return DS_ERR_NULL;
  if (index >= heap->items.size)
    return DS_ERR_RANGE;
  last = heap->items.size - 1;
  es = heap->items.elem_size;
  if (out != NULL) {
    memcpy(out, mmh_ptr(heap, index), es);
  }
  mmh_moved(heap, index, (size_t)-1);
  if (index != last) {
    memcpy(mmh_ptr(heap, index), mmh_ptr(heap, last), es);
    mmh_moved(heap, index, index);
  }
  heap->items.size--;
  if (index < heap->items.size) {
    (void)ds_min_max_heap_repair_at(heap, index);
  }
  return DS_OK;
}

ds_status_t ds_min_max_heap_pop_min(ds_min_max_heap_t *heap, void *out) {
  if (heap == NULL)
    return DS_ERR_NULL;
  if (heap->items.size == 0) {
    return DS_EMPTY;
  }
  return ds_min_max_heap_remove_at(heap, 0, out);
}

ds_status_t ds_min_max_heap_pop_max(ds_min_max_heap_t *heap, void *out) {
  size_t index;
  if (heap == NULL)
    return DS_ERR_NULL;
  if (heap->items.size == 0) {
    return DS_EMPTY;
  }
  index = mmh_max_index(heap);
  return ds_min_max_heap_remove_at(heap, index, out);
}

size_t ds_min_max_heap_size(const ds_min_max_heap_t *heap) {
  return heap == NULL ? 0 : heap->items.size;
}

bool ds_min_max_heap_is_empty(const ds_min_max_heap_t *heap) {
  return heap == NULL || heap->items.size == 0;
}

void ds_min_max_heap_clear(ds_min_max_heap_t *heap) {
  size_t i;
  if (heap == NULL)
    return;
  if (heap->moved != NULL)
    for (i = 0; i < heap->items.size; i++)
      mmh_moved(heap, i, (size_t)-1);
  ds_vec_clear(&heap->items);
}

void ds_min_max_heap_set_move_callback(ds_min_max_heap_t *heap,
                                       void (*moved)(const void *, size_t,
                                                     void *),
                                       void *user) {
  size_t i;
  if (heap == NULL)
    return;
  heap->moved = moved;
  heap->moved_user = user;
  if (moved != NULL)
    for (i = 0; i < heap->items.size; i++)
      mmh_moved(heap, i, i);
}

const void *ds_min_max_heap_at(const ds_min_max_heap_t *heap, size_t index) {
  return heap == NULL || index >= heap->items.size ? NULL
                                                   : mmh_ptr(heap, index);
}
