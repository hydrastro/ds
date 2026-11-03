#include "indexed_heap.h"
#include <string.h>

#define IHEAP_SIZE_MAX (~(size_t)0)
#define IHEAP_DEFAULT_ARITY 4

static const unsigned char *iheap_key_of(const ds_indexed_heap_t *heap,
                                         size_t id) {
  return heap->keys + id * heap->key_size;
}

static int iheap_less(const ds_indexed_heap_t *heap, size_t a_id, size_t b_id) {
  return heap->compare(iheap_key_of(heap, a_id), iheap_key_of(heap, b_id),
                       heap->user) < 0;
}

static void iheap_place(ds_indexed_heap_t *heap, size_t position, size_t id) {
  heap->heap[position] = id;
  heap->pos[id] = position;
}

/* Returns true when the item moved. Uses a hole instead of swaps. */
static bool iheap_sift_up(ds_indexed_heap_t *heap, size_t position) {
  size_t id = heap->heap[position];
  size_t parent;
  size_t start = position;
  while (position > 0) {
    parent = (position - 1) / heap->arity;
    if (!iheap_less(heap, id, heap->heap[parent])) {
      break;
    }
    iheap_place(heap, position, heap->heap[parent]);
    position = parent;
  }
  iheap_place(heap, position, id);
  return position != start;
}

static void iheap_sift_down(ds_indexed_heap_t *heap, size_t position) {
  size_t id = heap->heap[position];
  size_t first;
  size_t last;
  size_t best;
  size_t child;
  for (;;) {
    if (heap->size < 2 || position > (heap->size - 2) / heap->arity) {
      break;
    }
    first = position * heap->arity + 1;
    last = heap->size - first > heap->arity ? first + heap->arity : heap->size;
    best = first;
    for (child = first + 1; child < last; child++) {
      if (iheap_less(heap, heap->heap[child], heap->heap[best])) {
        best = child;
      }
    }
    if (!iheap_less(heap, heap->heap[best], id)) {
      break;
    }
    iheap_place(heap, position, heap->heap[best]);
    position = best;
  }
  iheap_place(heap, position, id);
}

static void iheap_fix(ds_indexed_heap_t *heap, size_t position) {
  if (!iheap_sift_up(heap, position)) {
    iheap_sift_down(heap, position);
  }
}

static ds_status_t iheap_check_config(const ds_indexed_heap_config_t *config) {
  if (config == NULL || config->compare == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || config->arity < 2) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

static void iheap_apply_config(ds_indexed_heap_t *heap,
                               const ds_indexed_heap_config_t *config) {
  heap->heap = NULL;
  heap->pos = NULL;
  heap->keys = NULL;
  heap->size = 0;
  heap->max_ids = 0;
  heap->key_size = config->key_size;
  heap->arity = config->arity;
  heap->compare = config->compare;
  heap->user = config->user;
}

void FUNC(ds_indexed_heap_config_init)(ds_indexed_heap_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->key_size = 0;
  config->arity = IHEAP_DEFAULT_ARITY;
  config->compare = NULL;
  config->user = NULL;
  config->context = NULL;
}

ds_status_t FUNC(ds_indexed_heap_init)(ds_indexed_heap_t *heap,
                                       const ds_indexed_heap_config_t *config,
                                       size_t max_ids) {
  ds_status_t status;
  if (heap == NULL) {
    return DS_ERR_NULL;
  }
  status = iheap_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  iheap_apply_config(heap, config);
  heap->context =
      config->context != NULL ? config->context : ds_default_context();
  heap->fixed = false;
  status = FUNC(ds_indexed_heap_set_max_ids)(heap, max_ids);
  if (status != DS_OK) {
    FUNC(ds_indexed_heap_destroy)(heap);
  }
  return status;
}

ds_status_t FUNC(ds_indexed_heap_init_buffer)(
    ds_indexed_heap_t *heap, const ds_indexed_heap_config_t *config,
    size_t *heap_buffer, size_t *pos_buffer, void *key_buffer, size_t max_ids) {
  ds_status_t status;
  size_t i;
  if (heap == NULL ||
      (max_ids > 0 &&
       (heap_buffer == NULL || pos_buffer == NULL || key_buffer == NULL))) {
    return DS_ERR_NULL;
  }
  status = iheap_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  iheap_apply_config(heap, config);
  heap->heap = heap_buffer;
  heap->pos = pos_buffer;
  heap->keys = (unsigned char *)key_buffer;
  heap->max_ids = max_ids;
  heap->context = NULL;
  heap->fixed = true;
  for (i = 0; i < max_ids; i++) {
    heap->pos[i] = DS_INDEXED_HEAP_NONE;
  }
  return DS_OK;
}

void FUNC(ds_indexed_heap_destroy)(ds_indexed_heap_t *heap) {
  if (heap == NULL) {
    return;
  }
  if (!heap->fixed) {
    ds_context_free(heap->context, heap->heap);
    ds_context_free(heap->context, heap->pos);
    ds_context_free(heap->context, heap->keys);
    heap->heap = NULL;
    heap->pos = NULL;
    heap->keys = NULL;
    heap->max_ids = 0;
  }
  heap->size = 0;
}

bool FUNC(ds_indexed_heap_contains)(const ds_indexed_heap_t *heap, size_t id) {
  return id < heap->max_ids && heap->pos[id] != DS_INDEXED_HEAP_NONE;
}

const void *FUNC(ds_indexed_heap_key)(const ds_indexed_heap_t *heap,
                                      size_t id) {
  if (!FUNC(ds_indexed_heap_contains)(heap, id)) {
    return NULL;
  }
  return iheap_key_of(heap, id);
}

ds_status_t FUNC(ds_indexed_heap_push)(ds_indexed_heap_t *heap, size_t id,
                                       const void *key) {
  if (key == NULL) {
    return DS_ERR_NULL;
  }
  if (id >= heap->max_ids) {
    return DS_ERR_RANGE;
  }
  if (heap->pos[id] != DS_INDEXED_HEAP_NONE) {
    return DS_EXISTS;
  }
  memcpy(heap->keys + id * heap->key_size, key, heap->key_size);
  iheap_place(heap, heap->size, id);
  heap->size++;
  iheap_sift_up(heap, heap->size - 1);
  return DS_OK;
}

ds_status_t FUNC(ds_indexed_heap_update)(ds_indexed_heap_t *heap, size_t id,
                                         const void *key) {
  if (key == NULL) {
    return DS_ERR_NULL;
  }
  if (!FUNC(ds_indexed_heap_contains)(heap, id)) {
    return id < heap->max_ids ? DS_NOT_FOUND : DS_ERR_RANGE;
  }
  memmove(heap->keys + id * heap->key_size, key, heap->key_size);
  iheap_fix(heap, heap->pos[id]);
  return DS_OK;
}

ds_status_t FUNC(ds_indexed_heap_push_or_update)(ds_indexed_heap_t *heap,
                                                 size_t id, const void *key) {
  if (FUNC(ds_indexed_heap_contains)(heap, id)) {
    return FUNC(ds_indexed_heap_update)(heap, id, key);
  }
  return FUNC(ds_indexed_heap_push)(heap, id, key);
}

ds_status_t FUNC(ds_indexed_heap_top)(const ds_indexed_heap_t *heap,
                                      size_t *out_id, void *out_key) {
  if (heap->size == 0) {
    return DS_EMPTY;
  }
  if (out_id != NULL) {
    *out_id = heap->heap[0];
  }
  if (out_key != NULL) {
    memcpy(out_key, iheap_key_of(heap, heap->heap[0]), heap->key_size);
  }
  return DS_OK;
}

ds_status_t FUNC(ds_indexed_heap_remove)(ds_indexed_heap_t *heap, size_t id,
                                         void *out_key) {
  size_t position;
  size_t last_id;
  if (!FUNC(ds_indexed_heap_contains)(heap, id)) {
    return id < heap->max_ids ? DS_NOT_FOUND : DS_ERR_RANGE;
  }
  if (out_key != NULL) {
    memcpy(out_key, iheap_key_of(heap, id), heap->key_size);
  }
  position = heap->pos[id];
  heap->pos[id] = DS_INDEXED_HEAP_NONE;
  heap->size--;
  if (position != heap->size) {
    last_id = heap->heap[heap->size];
    iheap_place(heap, position, last_id);
    iheap_fix(heap, position);
  }
  return DS_OK;
}

ds_status_t FUNC(ds_indexed_heap_pop)(ds_indexed_heap_t *heap, size_t *out_id,
                                      void *out_key) {
  size_t id;
  if (heap->size == 0) {
    return DS_EMPTY;
  }
  id = heap->heap[0];
  if (out_id != NULL) {
    *out_id = id;
  }
  return FUNC(ds_indexed_heap_remove)(heap, id, out_key);
}

void FUNC(ds_indexed_heap_clear)(ds_indexed_heap_t *heap) {
  size_t i;
  for (i = 0; i < heap->size; i++) {
    heap->pos[heap->heap[i]] = DS_INDEXED_HEAP_NONE;
  }
  heap->size = 0;
}

size_t FUNC(ds_indexed_heap_size)(const ds_indexed_heap_t *heap) {
  return heap->size;
}

bool FUNC(ds_indexed_heap_is_empty)(const ds_indexed_heap_t *heap) {
  return heap->size == 0;
}

size_t FUNC(ds_indexed_heap_max_ids)(const ds_indexed_heap_t *heap) {
  return heap->max_ids;
}

ds_status_t FUNC(ds_indexed_heap_set_max_ids)(ds_indexed_heap_t *heap,
                                              size_t max_ids) {
  void *block;
  size_t i;
  if (max_ids <= heap->max_ids) {
    /* Shrinking keeps the memory; no live id may fall out of range. */
    for (i = 0; i < heap->size; i++) {
      if (heap->heap[i] >= max_ids) {
        return DS_ERR_STATE;
      }
    }
    heap->max_ids = max_ids;
    return DS_OK;
  }
  if (heap->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (max_ids > IHEAP_SIZE_MAX / sizeof(size_t) ||
      max_ids > IHEAP_SIZE_MAX / heap->key_size) {
    return DS_ERR_OVERFLOW;
  }
  block = heap->heap == NULL
              ? ds_context_alloc(heap->context, max_ids * sizeof(size_t))
              : ds_context_realloc(heap->context, heap->heap,
                                   max_ids * sizeof(size_t));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  heap->heap = (size_t *)block;
  block = heap->keys == NULL
              ? ds_context_alloc(heap->context, max_ids * heap->key_size)
              : ds_context_realloc(heap->context, heap->keys,
                                   max_ids * heap->key_size);
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  heap->keys = (unsigned char *)block;
  block = heap->pos == NULL
              ? ds_context_alloc(heap->context, max_ids * sizeof(size_t))
              : ds_context_realloc(heap->context, heap->pos,
                                   max_ids * sizeof(size_t));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  heap->pos = (size_t *)block;
  for (i = heap->max_ids; i < max_ids; i++) {
    heap->pos[i] = DS_INDEXED_HEAP_NONE;
  }
  heap->max_ids = max_ids;
  return DS_OK;
}
