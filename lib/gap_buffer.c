#include "gap_buffer.h"
#include <string.h>

#define GB_SIZE_MAX (~(size_t)0)
#define GB_MIN_CAPACITY 64

static size_t gb_gap(const ds_gap_buffer_t *gb) {
  return gb->gap_end - gb->gap_start;
}

static ds_status_t gb_grow(ds_gap_buffer_t *gb, size_t extra) {
  size_t length = ds_gap_buffer_length(gb);
  size_t capacity;
  size_t tail = gb->capacity - gb->gap_end;
  unsigned char *data;
  if (gb_gap(gb) >= extra) {
    return DS_OK;
  }
  if (gb->fixed || extra > GB_SIZE_MAX - length) {
    return DS_ERR_OVERFLOW;
  }
  capacity = gb->capacity < GB_MIN_CAPACITY ? GB_MIN_CAPACITY : gb->capacity;
  while (capacity - length < extra) {
    if (capacity > GB_SIZE_MAX / 2) {
      capacity = length + extra;
      break;
    }
    capacity *= 2;
  }
  data = (unsigned char *)ds_context_alloc(gb->context, capacity);
  if (data == NULL) {
    return DS_ERR_ALLOC;
  }
  if (gb->gap_start > 0) {
    memcpy(data, gb->data, gb->gap_start);
  }
  if (tail > 0) {
    memcpy(data + capacity - tail, gb->data + gb->gap_end, tail);
  }
  ds_context_free(gb->context, gb->data);
  gb->data = data;
  gb->gap_end = capacity - tail;
  gb->capacity = capacity;
  return DS_OK;
}

ds_status_t ds_gap_buffer_init(ds_gap_buffer_t *gb, size_t capacity,
                               ds_context_t *context) {
  if (gb == NULL) {
    return DS_ERR_NULL;
  }
  gb->data = NULL;
  gb->capacity = 0;
  gb->gap_start = 0;
  gb->gap_end = 0;
  gb->context = context != NULL ? context : ds_default_context();
  gb->fixed = false;
  return capacity > 0 ? gb_grow(gb, capacity) : DS_OK;
}

ds_status_t ds_gap_buffer_init_buffer(ds_gap_buffer_t *gb, void *buffer,
                                      size_t capacity) {
  if (gb == NULL || (buffer == NULL && capacity > 0)) {
    return DS_ERR_NULL;
  }
  gb->data = (unsigned char *)buffer;
  gb->capacity = capacity;
  gb->gap_start = 0;
  gb->gap_end = capacity;
  gb->context = NULL;
  gb->fixed = true;
  return DS_OK;
}

void ds_gap_buffer_destroy(ds_gap_buffer_t *gb) {
  if (gb == NULL) {
    return;
  }
  if (!gb->fixed) {
    ds_context_free(gb->context, gb->data);
    gb->data = NULL;
    gb->capacity = 0;
  }
  gb->gap_start = 0;
  gb->gap_end = gb->capacity;
}

size_t ds_gap_buffer_length(const ds_gap_buffer_t *gb) {
  return gb->capacity - gb_gap(gb);
}

size_t ds_gap_buffer_cursor(const ds_gap_buffer_t *gb) {
  return gb->gap_start;
}

ds_status_t ds_gap_buffer_move_to(ds_gap_buffer_t *gb, size_t position) {
  size_t n;
  if (position > ds_gap_buffer_length(gb)) {
    return DS_ERR_RANGE;
  }
  if (position < gb->gap_start) {
    n = gb->gap_start - position;
    memmove(gb->data + gb->gap_end - n, gb->data + position, n);
    gb->gap_start -= n;
    gb->gap_end -= n;
  } else if (position > gb->gap_start) {
    n = position - gb->gap_start;
    memmove(gb->data + gb->gap_start, gb->data + gb->gap_end, n);
    gb->gap_start += n;
    gb->gap_end += n;
  }
  return DS_OK;
}

ds_status_t ds_gap_buffer_insert(ds_gap_buffer_t *gb, const void *text,
                                 size_t size) {
  ds_status_t status;
  if (size == 0) {
    return DS_OK;
  }
  if (text == NULL) {
    return DS_ERR_NULL;
  }
  status = gb_grow(gb, size);
  if (status != DS_OK) {
    return status;
  }
  memcpy(gb->data + gb->gap_start, text, size);
  gb->gap_start += size;
  return DS_OK;
}

size_t ds_gap_buffer_delete_backward(ds_gap_buffer_t *gb, size_t count) {
  if (count > gb->gap_start) {
    count = gb->gap_start;
  }
  gb->gap_start -= count;
  return count;
}

size_t ds_gap_buffer_delete_forward(ds_gap_buffer_t *gb, size_t count) {
  size_t tail = gb->capacity - gb->gap_end;
  if (count > tail) {
    count = tail;
  }
  gb->gap_end += count;
  return count;
}

int ds_gap_buffer_at(const ds_gap_buffer_t *gb, size_t position) {
  if (position >= ds_gap_buffer_length(gb)) {
    return -1;
  }
  if (position >= gb->gap_start) {
    position += gb_gap(gb);
  }
  return gb->data[position];
}

size_t ds_gap_buffer_copy(const ds_gap_buffer_t *gb, size_t position,
                          size_t count, void *out) {
  unsigned char *dst = (unsigned char *)out;
  size_t length = ds_gap_buffer_length(gb);
  size_t first;
  if (out == NULL || position >= length) {
    return 0;
  }
  if (count > length - position) {
    count = length - position;
  }
  first = 0;
  if (position < gb->gap_start) {
    first = gb->gap_start - position;
    if (first > count) {
      first = count;
    }
    memcpy(dst, gb->data + position, first);
  }
  if (count > first) {
    memcpy(dst + first, gb->data + position + first + gb_gap(gb),
           count - first);
  }
  return count;
}

void ds_gap_buffer_clear(ds_gap_buffer_t *gb) {
  gb->gap_start = 0;
  gb->gap_end = gb->capacity;
}
