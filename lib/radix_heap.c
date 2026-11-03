#include "radix_heap.h"
#include <string.h>

/* A record is the key followed by the payload, copied with memcpy, so no
 * alignment is assumed. */

static size_t rx_rec_size(const ds_radix_heap_t *heap) {
  return sizeof(unsigned long) + heap->elem_size;
}

static unsigned long rx_key_at(const ds_vec_t *bucket, size_t i) {
  unsigned long key;
  memcpy(&key, bucket->data + i * bucket->elem_size, sizeof key);
  return key;
}

static size_t rx_bucket_of(unsigned long last, unsigned long key) {
  unsigned long x = key ^ last;
  size_t b = 0;
  while (x != 0) {
    x >>= 1;
    b++;
  }
  return b;
}

ds_status_t ds_radix_heap_init(ds_radix_heap_t *heap, size_t elem_size,
                               ds_context_t *context) {
  size_t i;
  ds_status_t status;
  if (heap == NULL) {
    return DS_ERR_NULL;
  }
  heap->last = 0;
  heap->size = 0;
  heap->elem_size = elem_size;
  heap->context = context;
  for (i = 0; i < DS_RADIX_HEAP_BUCKETS; i++) {
    status = ds_vec_init(&heap->buckets[i], rx_rec_size(heap), context);
    if (status != DS_OK) {
      while (i-- > 0) {
        ds_vec_destroy(&heap->buckets[i]);
      }
      return status;
    }
  }
  return DS_OK;
}

void ds_radix_heap_destroy(ds_radix_heap_t *heap) {
  size_t i;
  if (heap == NULL) {
    return;
  }
  for (i = 0; i < DS_RADIX_HEAP_BUCKETS; i++) {
    ds_vec_destroy(&heap->buckets[i]);
  }
  heap->size = 0;
}

void ds_radix_heap_clear(ds_radix_heap_t *heap) {
  size_t i;
  for (i = 0; i < DS_RADIX_HEAP_BUCKETS; i++) {
    ds_vec_clear(&heap->buckets[i]);
  }
  heap->size = 0;
  heap->last = 0;
}

ds_status_t ds_radix_heap_push(ds_radix_heap_t *heap, unsigned long key,
                               const void *elem) {
  unsigned char *rec;
  ds_vec_t *bucket;
  if (key < heap->last) {
    return DS_ERR_RANGE;
  }
  if (elem == NULL && heap->elem_size > 0) {
    return DS_ERR_NULL;
  }
  bucket = &heap->buckets[rx_bucket_of(heap->last, key)];
  rec = (unsigned char *)ds_vec_emplace(bucket);
  if (rec == NULL) {
    return bucket->fixed ? DS_ERR_OVERFLOW : DS_ERR_ALLOC;
  }
  memcpy(rec, &key, sizeof key);
  if (heap->elem_size > 0) {
    memcpy(rec + sizeof key, elem, heap->elem_size);
  }
  heap->size++;
  return DS_OK;
}

/* Make bucket 0 non-empty (the heap must not be empty). */
static ds_status_t rx_refill(ds_radix_heap_t *heap) {
  size_t b = 1;
  size_t i;
  size_t count;
  size_t target;
  size_t need[DS_RADIX_HEAP_BUCKETS];
  unsigned long min;
  unsigned long key;
  ds_vec_t *src;
  ds_status_t status;

  if (heap->buckets[0].size > 0) {
    return DS_OK;
  }
  while (heap->buckets[b].size == 0) {
    b++;
  }
  src = &heap->buckets[b];
  count = src->size;
  min = rx_key_at(src, 0);
  for (i = 1; i < count; i++) {
    key = rx_key_at(src, i);
    if (key < min) {
      min = key;
    }
  }
  /* Reserve every target bucket first so a failure changes nothing. */
  memset(need, 0, sizeof need);
  for (i = 0; i < count; i++) {
    need[rx_bucket_of(min, rx_key_at(src, i))]++;
  }
  for (target = 0; target < b; target++) {
    if (need[target] > 0) {
      status = ds_vec_reserve(&heap->buckets[target],
                              heap->buckets[target].size + need[target]);
      if (status != DS_OK) {
        return status;
      }
    }
  }
  heap->last = min;
  for (i = 0; i < count; i++) {
    target = rx_bucket_of(min, rx_key_at(src, i));
    (void)ds_vec_push(&heap->buckets[target], src->data + i * src->elem_size);
  }
  ds_vec_clear(src);
  return DS_OK;
}

ds_status_t ds_radix_heap_pop(ds_radix_heap_t *heap, unsigned long *out_key,
                              void *out_elem) {
  ds_status_t status;
  ds_vec_t *bucket = &heap->buckets[0];
  unsigned char *rec;
  if (heap->size == 0) {
    return DS_EMPTY;
  }
  status = rx_refill(heap);
  if (status != DS_OK) {
    return status;
  }
  rec = bucket->data + (bucket->size - 1) * bucket->elem_size;
  if (out_key != NULL) {
    memcpy(out_key, rec, sizeof *out_key);
  }
  if (out_elem != NULL && heap->elem_size > 0) {
    memcpy(out_elem, rec + sizeof(unsigned long), heap->elem_size);
  }
  bucket->size--;
  heap->size--;
  return DS_OK;
}

ds_status_t ds_radix_heap_peek(ds_radix_heap_t *heap, unsigned long *out_key,
                               void **out_elem) {
  ds_status_t status;
  ds_vec_t *bucket = &heap->buckets[0];
  unsigned char *rec;
  if (heap->size == 0) {
    return DS_EMPTY;
  }
  status = rx_refill(heap);
  if (status != DS_OK) {
    return status;
  }
  rec = bucket->data + (bucket->size - 1) * bucket->elem_size;
  if (out_key != NULL) {
    memcpy(out_key, rec, sizeof *out_key);
  }
  if (out_elem != NULL) {
    *out_elem = heap->elem_size > 0 ? rec + sizeof(unsigned long) : NULL;
  }
  return DS_OK;
}

size_t ds_radix_heap_size(const ds_radix_heap_t *heap) {
  return heap->size;
}

bool ds_radix_heap_is_empty(const ds_radix_heap_t *heap) {
  return heap->size == 0;
}

unsigned long ds_radix_heap_last_key(const ds_radix_heap_t *heap) {
  return heap->last;
}
