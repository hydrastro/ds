#include <dsa/core/stage.h>
#include <string.h>

ds_status_t ds_stage_copy(ds_stage_t *stage, ds_context_t *context,
                          bool may_allocate, const void *key, size_t key_size,
                          const void *value, size_t value_size) {
  size_t key_room;
  size_t total;
  unsigned char *to;

  if (stage == NULL) {
    return DS_ERR_NULL;
  }
  stage->block = NULL;
  stage->context = context;
  stage->key = key;
  stage->value = value;
  if (key == NULL) {
    key_size = 0;
  }
  if (value == NULL) {
    value_size = 0;
  }
  if (key_size == 0 && value_size == 0) {
    return DS_OK;
  }
  /* the value starts at a multiple of DS_MAX_ALIGN */
  if (!ds_size_add(key_size, DS_MAX_ALIGN - 1, &key_room)) {
    return DS_ERR_OVERFLOW;
  }
  key_room -= key_room % DS_MAX_ALIGN;
  if (!ds_size_add(key_room, value_size, &total)) {
    return DS_ERR_OVERFLOW;
  }
  if (total <= DS_STAGE_BYTES) {
    to = stage->room.bytes;
  } else if (may_allocate) {
    stage->block = (unsigned char *)ds_context_alloc(context, total);
    if (stage->block == NULL) {
      return DS_ERR_ALLOC;
    }
    to = stage->block;
  } else if (key_size <= DS_STAGE_BYTES) {
    if (key_size > 0) { /* the key alone */
      memcpy(stage->room.bytes, key, key_size);
      stage->key = stage->room.bytes;
    }
    return DS_OK;
  } else {
    return DS_OK;
  }
  if (key_size > 0) {
    memcpy(to, key, key_size);
    stage->key = to;
  }
  if (value_size > 0) {
    memcpy(to + key_room, value, value_size);
    stage->value = to + key_room;
  }
  return DS_OK;
}

void ds_stage_release(ds_stage_t *stage) {
  if (stage != NULL && stage->block != NULL) {
    ds_context_free(stage->context, stage->block);
    stage->block = NULL;
  }
}
