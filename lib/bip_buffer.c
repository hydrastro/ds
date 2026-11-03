#include "bip_buffer.h"
#include <string.h>

static void bip_reset(ds_bip_buffer_t *bip) {
  bip->a_start = 0;
  bip->a_end = 0;
  bip->b_end = 0;
  bip->b_in_use = false;
  bip->reserve_start = 0;
  bip->reserve_end = 0;
}

ds_status_t ds_bip_buffer_init(ds_bip_buffer_t *bip, size_t capacity,
                               ds_context_t *context) {
  if (bip == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity == 0) {
    return DS_ERR_INVALID;
  }
  bip->context = context != NULL ? context : ds_default_context();
  bip->fixed = false;
  bip->capacity = capacity;
  bip_reset(bip);
  bip->data = (unsigned char *)ds_context_alloc(bip->context, capacity);
  return bip->data == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_status_t ds_bip_buffer_init_buffer(ds_bip_buffer_t *bip, void *buffer,
                                      size_t capacity) {
  if (bip == NULL || buffer == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity == 0) {
    return DS_ERR_INVALID;
  }
  bip->data = (unsigned char *)buffer;
  bip->capacity = capacity;
  bip->context = NULL;
  bip->fixed = true;
  bip_reset(bip);
  return DS_OK;
}

void ds_bip_buffer_destroy(ds_bip_buffer_t *bip) {
  if (bip == NULL) {
    return;
  }
  if (!bip->fixed) {
    ds_context_free(bip->context, bip->data);
  }
  bip->data = NULL;
  bip->capacity = 0;
  bip_reset(bip);
}

void *ds_bip_buffer_reserve(ds_bip_buffer_t *bip, size_t wanted,
                            size_t *out_granted) {
  size_t start;
  size_t space;
  size_t after_a;
  if (out_granted != NULL) {
    *out_granted = 0;
  }
  bip->reserve_start = 0;
  bip->reserve_end = 0;
  if (wanted == 0) {
    return NULL;
  }
  if (bip->b_in_use) {
    start = bip->b_end;
    space = bip->a_start - bip->b_end;
  } else {
    after_a = bip->capacity - bip->a_end;
    /* Prefer the space after A unless the gap before A is larger and A
     * cannot hold the whole request. */
    if (after_a >= wanted || after_a >= bip->a_start) {
      start = bip->a_end;
      space = after_a;
    } else {
      start = 0;
      space = bip->a_start;
    }
  }
  if (space == 0) {
    return NULL;
  }
  if (space > wanted) {
    space = wanted;
  }
  bip->reserve_start = start;
  bip->reserve_end = start + space;
  if (out_granted != NULL) {
    *out_granted = space;
  }
  return bip->data + start;
}

void ds_bip_buffer_commit(ds_bip_buffer_t *bip, size_t written) {
  size_t reserved = bip->reserve_end - bip->reserve_start;
  if (written > reserved) {
    written = reserved;
  }
  if (written > 0) {
    if (bip->a_start == bip->a_end && !bip->b_in_use) {
      /* A is empty: the reservation starts a fresh A. */
      bip->a_start = bip->reserve_start;
      bip->a_end = bip->reserve_start + written;
    } else if (bip->reserve_start == bip->a_end && !bip->b_in_use) {
      bip->a_end += written;
    } else {
      bip->b_end += written;
      bip->b_in_use = true;
    }
  }
  bip->reserve_start = 0;
  bip->reserve_end = 0;
}

const void *ds_bip_buffer_read_block(const ds_bip_buffer_t *bip,
                                     size_t *out_size) {
  size_t size = bip->a_end - bip->a_start;
  if (out_size != NULL) {
    *out_size = size;
  }
  return size == 0 ? NULL : bip->data + bip->a_start;
}

void ds_bip_buffer_consume(ds_bip_buffer_t *bip, size_t count) {
  size_t size = bip->a_end - bip->a_start;
  if (count >= size) {
    /* A drained: B (if any) becomes A. */
    bip->a_start = 0;
    bip->a_end = bip->b_in_use ? bip->b_end : 0;
    bip->b_end = 0;
    bip->b_in_use = false;
  } else {
    bip->a_start += count;
  }
}

ds_status_t ds_bip_buffer_write(ds_bip_buffer_t *bip, const void *data,
                                size_t size) {
  size_t granted;
  void *dst;
  if (size == 0) {
    return DS_OK;
  }
  if (data == NULL) {
    return DS_ERR_NULL;
  }
  dst = ds_bip_buffer_reserve(bip, size, &granted);
  if (dst == NULL || granted < size) {
    ds_bip_buffer_commit(bip, 0);
    return DS_ERR_OVERFLOW; /* no contiguous room for the whole record */
  }
  memcpy(dst, data, size);
  ds_bip_buffer_commit(bip, size);
  return DS_OK;
}

size_t ds_bip_buffer_committed(const ds_bip_buffer_t *bip) {
  return (bip->a_end - bip->a_start) + bip->b_end;
}

size_t ds_bip_buffer_capacity(const ds_bip_buffer_t *bip) {
  return bip->capacity;
}

void ds_bip_buffer_clear(ds_bip_buffer_t *bip) {
  bip_reset(bip);
}
