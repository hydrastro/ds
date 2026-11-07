#include "vec.h"
#include <string.h>

#define VEC_SIZE_MAX (~(size_t)0)
#define VEC_MIN_CAPACITY 4

static unsigned char *vec_ptr(const ds_vec_t *vec, size_t index) {
  return vec->data + index * vec->elem_size;
}

static void vec_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  unsigned char tmp;
  size_t i;
  for (i = 0; i < n; i++) {
    tmp = a[i];
    a[i] = b[i];
    b[i] = tmp;
  }
}

static ds_status_t vec_set_capacity(ds_vec_t *vec, size_t capacity) {
  unsigned char *data;
  if (vec->fixed) {
    return capacity <= vec->capacity ? DS_OK : DS_ERR_OVERFLOW;
  }
  if (capacity > VEC_SIZE_MAX / vec->elem_size) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity == 0) {
    ds_context_free(vec->context, vec->data);
    vec->data = NULL;
    vec->capacity = 0;
    return DS_OK;
  }
  if (vec->data == NULL) {
    data = (unsigned char *)ds_context_alloc(vec->context,
                                             capacity * vec->elem_size);
  } else {
    data = (unsigned char *)ds_context_realloc(vec->context, vec->data,
                                               capacity * vec->elem_size);
  }
  if (data == NULL) {
    return DS_ERR_ALLOC;
  }
  vec->data = data;
  vec->capacity = capacity;
  return DS_OK;
}

/* Make room for `extra` more elements, growing geometrically. */
static ds_status_t vec_grow_for(ds_vec_t *vec, size_t extra) {
  size_t needed;
  size_t capacity;
  if (extra > VEC_SIZE_MAX - vec->size) {
    return DS_ERR_OVERFLOW;
  }
  needed = vec->size + extra;
  if (needed <= vec->capacity) {
    return DS_OK;
  }
  if (vec->fixed) {
    return DS_ERR_OVERFLOW;
  }
  capacity =
      vec->capacity < VEC_MIN_CAPACITY ? VEC_MIN_CAPACITY : vec->capacity;
  while (capacity < needed) {
    if (capacity > VEC_SIZE_MAX / 2) {
      capacity = needed;
      break;
    }
    capacity *= 2;
  }
  return vec_set_capacity(vec, capacity);
}

ds_status_t ds_vec_init(ds_vec_t *vec, size_t elem_size,
                        ds_context_t *context) {
  if (vec == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  vec->data = NULL;
  vec->size = 0;
  vec->capacity = 0;
  vec->elem_size = elem_size;
  vec->context = context != NULL ? context : ds_default_context();
  vec->fixed = false;
  return DS_OK;
}

ds_status_t ds_vec_init_buffer(ds_vec_t *vec, size_t elem_size, void *buffer,
                               size_t capacity) {
  if (vec == NULL || (buffer == NULL && capacity > 0)) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  vec->data = (unsigned char *)buffer;
  vec->size = 0;
  vec->capacity = capacity;
  vec->elem_size = elem_size;
  vec->context = NULL;
  vec->fixed = true;
  return DS_OK;
}

void ds_vec_destroy(ds_vec_t *vec) {
  if (vec == NULL) {
    return;
  }
  if (!vec->fixed) {
    ds_context_free(vec->context, vec->data);
    vec->data = NULL;
    vec->capacity = 0;
  }
  vec->size = 0;
}

size_t ds_vec_size(const ds_vec_t *vec) { return vec->size; }

size_t ds_vec_capacity(const ds_vec_t *vec) { return vec->capacity; }

bool ds_vec_is_empty(const ds_vec_t *vec) { return vec->size == 0; }

bool ds_vec_is_full(const ds_vec_t *vec) { return vec->size == vec->capacity; }

void *ds_vec_data(ds_vec_t *vec) { return vec->data; }

void *ds_vec_at(ds_vec_t *vec, size_t index) {
  if (index >= vec->size) {
    return NULL;
  }
  return vec_ptr(vec, index);
}

void *ds_vec_front(ds_vec_t *vec) { return vec->size == 0 ? NULL : vec->data; }

void *ds_vec_back(ds_vec_t *vec) {
  return vec->size == 0 ? NULL : vec_ptr(vec, vec->size - 1);
}

ds_status_t ds_vec_get(const ds_vec_t *vec, size_t index, void *out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= vec->size) {
    return DS_ERR_RANGE;
  }
  memcpy(out, vec_ptr(vec, index), vec->elem_size);
  return DS_OK;
}

ds_status_t ds_vec_set(ds_vec_t *vec, size_t index, const void *elem) {
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= vec->size) {
    return DS_ERR_RANGE;
  }
  memmove(vec_ptr(vec, index), elem, vec->elem_size);
  return DS_OK;
}

ds_status_t ds_vec_reserve(ds_vec_t *vec, size_t capacity) {
  if (capacity <= vec->capacity) {
    return DS_OK;
  }
  return vec_set_capacity(vec, capacity);
}

ds_status_t ds_vec_shrink_to_fit(ds_vec_t *vec) {
  if (vec->fixed || vec->size == vec->capacity) {
    return DS_OK;
  }
  return vec_set_capacity(vec, vec->size);
}

ds_status_t ds_vec_resize(ds_vec_t *vec, size_t size, const void *fill) {
  ds_status_t status;
  size_t i;
  if (size <= vec->size) {
    vec->size = size;
    return DS_OK;
  }
  status = vec_grow_for(vec, size - vec->size);
  if (status != DS_OK) {
    return status;
  }
  for (i = vec->size; i < size; i++) {
    if (fill != NULL) {
      memcpy(vec_ptr(vec, i), fill, vec->elem_size);
    } else {
      memset(vec_ptr(vec, i), 0, vec->elem_size);
    }
  }
  vec->size = size;
  return DS_OK;
}

void ds_vec_clear(ds_vec_t *vec) { vec->size = 0; }

ds_status_t ds_vec_push(ds_vec_t *vec, const void *elem) {
  ds_status_t status;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  status = vec_grow_for(vec, 1);
  if (status != DS_OK) {
    return status;
  }
  memcpy(vec_ptr(vec, vec->size), elem, vec->elem_size);
  vec->size++;
  return DS_OK;
}

void *ds_vec_emplace(ds_vec_t *vec) {
  if (vec_grow_for(vec, 1) != DS_OK) {
    return NULL;
  }
  vec->size++;
  return vec_ptr(vec, vec->size - 1);
}

ds_status_t ds_vec_append(ds_vec_t *vec, const void *elems, size_t count) {
  return ds_vec_insert_n(vec, vec->size, elems, count);
}

ds_status_t ds_vec_pop(ds_vec_t *vec, void *out) {
  if (vec->size == 0) {
    return DS_EMPTY;
  }
  vec->size--;
  if (out != NULL) {
    memcpy(out, vec_ptr(vec, vec->size), vec->elem_size);
  }
  return DS_OK;
}

ds_status_t ds_vec_insert(ds_vec_t *vec, size_t index, const void *elem) {
  return ds_vec_insert_n(vec, index, elem, 1);
}

ds_status_t ds_vec_insert_n(ds_vec_t *vec, size_t index, const void *elems,
                            size_t count) {
  ds_status_t status;
  if (count == 0) {
    return DS_OK;
  }
  if (elems == NULL) {
    return DS_ERR_NULL;
  }
  if (index > vec->size) {
    return DS_ERR_RANGE;
  }
  /* `elems` must not point into this vector: growing may move the block. */
  status = vec_grow_for(vec, count);
  if (status != DS_OK) {
    return status;
  }
  if (index < vec->size) {
    memmove(vec_ptr(vec, index + count), vec_ptr(vec, index),
            (vec->size - index) * vec->elem_size);
  }
  memcpy(vec_ptr(vec, index), elems, count * vec->elem_size);
  vec->size += count;
  return DS_OK;
}

ds_status_t ds_vec_erase(ds_vec_t *vec, size_t index, void *out) {
  if (index >= vec->size) {
    return DS_ERR_RANGE;
  }
  if (out != NULL) {
    memcpy(out, vec_ptr(vec, index), vec->elem_size);
  }
  return ds_vec_erase_range(vec, index, 1);
}

ds_status_t ds_vec_erase_range(ds_vec_t *vec, size_t index, size_t count) {
  size_t tail;
  if (index > vec->size || count > vec->size - index) {
    return DS_ERR_RANGE;
  }
  if (count == 0) {
    return DS_OK;
  }
  tail = vec->size - index - count;
  if (tail > 0) {
    memmove(vec_ptr(vec, index), vec_ptr(vec, index + count),
            tail * vec->elem_size);
  }
  vec->size -= count;
  return DS_OK;
}

ds_status_t ds_vec_swap_remove(ds_vec_t *vec, size_t index, void *out) {
  if (index >= vec->size) {
    return DS_ERR_RANGE;
  }
  if (out != NULL) {
    memcpy(out, vec_ptr(vec, index), vec->elem_size);
  }
  vec->size--;
  if (index != vec->size) {
    memcpy(vec_ptr(vec, index), vec_ptr(vec, vec->size), vec->elem_size);
  }
  return DS_OK;
}

ds_status_t ds_vec_swap(ds_vec_t *vec, size_t a, size_t b) {
  if (a >= vec->size || b >= vec->size) {
    return DS_ERR_RANGE;
  }
  if (a != b) {
    vec_swap_bytes(vec_ptr(vec, a), vec_ptr(vec, b), vec->elem_size);
  }
  return DS_OK;
}

void ds_vec_reverse(ds_vec_t *vec) {
  size_t i;
  size_t j;
  if (vec->size < 2) {
    return;
  }
  i = 0;
  j = vec->size - 1;
  while (i < j) {
    vec_swap_bytes(vec_ptr(vec, i), vec_ptr(vec, j), vec->elem_size);
    i++;
    j--;
  }
}

static void vec_sift_down(ds_vec_t *vec, size_t root, size_t end,
                          ds_vec_compare_func_t compare, void *user) {
  size_t child;
  for (;;) {
    child = 2 * root + 1;
    if (child >= end) {
      return;
    }
    if (child + 1 < end &&
        compare(vec_ptr(vec, child), vec_ptr(vec, child + 1), user) < 0) {
      child++;
    }
    if (compare(vec_ptr(vec, root), vec_ptr(vec, child), user) >= 0) {
      return;
    }
    vec_swap_bytes(vec_ptr(vec, root), vec_ptr(vec, child), vec->elem_size);
    root = child;
  }
}

void ds_vec_sort(ds_vec_t *vec, ds_vec_compare_func_t compare, void *user) {
  size_t i;
  size_t end;
  if (vec->size < 2 || compare == NULL) {
    return;
  }
  for (i = vec->size / 2; i > 0; i--) {
    vec_sift_down(vec, i - 1, vec->size, compare, user);
  }
  for (end = vec->size - 1; end > 0; end--) {
    vec_swap_bytes(vec->data, vec_ptr(vec, end), vec->elem_size);
    vec_sift_down(vec, 0, end, compare, user);
  }
}

size_t ds_vec_lower_bound(const ds_vec_t *vec, const void *key,
                          ds_vec_compare_func_t compare, void *user) {
  size_t lo = 0;
  size_t hi = vec->size;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (compare(vec_ptr(vec, mid), key, user) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

ds_status_t ds_vec_binary_search(const ds_vec_t *vec, const void *key,
                                 ds_vec_compare_func_t compare, void *user,
                                 size_t *out_index) {
  size_t index;
  if (key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  index = ds_vec_lower_bound(vec, key, compare, user);
  if (out_index != NULL) {
    *out_index = index;
  }
  if (index < vec->size && compare(vec_ptr(vec, index), key, user) == 0) {
    return DS_OK;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_vec_find(const ds_vec_t *vec, const void *key,
                        ds_vec_compare_func_t compare, void *user,
                        size_t *out_index) {
  size_t i;
  if (key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < vec->size; i++) {
    if (compare(vec_ptr(vec, i), key, user) == 0) {
      if (out_index != NULL) {
        *out_index = i;
      }
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_vec_copy(ds_vec_t *dst, const ds_vec_t *src,
                        ds_context_t *context) {
  ds_status_t status;
  if (dst == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  status = ds_vec_init(dst, src->elem_size, context);
  if (status != DS_OK) {
    return status;
  }
  if (src->size == 0) {
    return DS_OK;
  }
  status = vec_set_capacity(dst, src->size);
  if (status != DS_OK) {
    return status;
  }
  memcpy(dst->data, src->data, src->size * src->elem_size);
  dst->size = src->size;
  return DS_OK;
}
