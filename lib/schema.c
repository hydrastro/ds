#include "schema.h"
#include "checked.h"
#include <string.h>

void ds_schema_init(ds_schema_t *schema, size_t size) {
  if (schema == NULL) {
    return;
  }
  memset(schema, 0, sizeof(*schema));
  schema->size = size;
}

ds_schema_policy_t ds_schema_policy(const ds_schema_t *schema) {
  if (schema == NULL) {
    return DS_SCHEMA_INVALID;
  }
  if (schema->destroy != NULL) {
    return schema->copy != NULL ? DS_SCHEMA_OWNED : DS_SCHEMA_INVALID;
  }
  return schema->copy != NULL ? DS_SCHEMA_COPY_ONLY : DS_SCHEMA_PLAIN;
}

size_t ds_schema_align(const ds_schema_t *schema) {
  size_t a;
  if (schema == NULL) {
    return 1;
  }
  if (schema->align != 0) {
    return schema->align;
  }
  if (schema->size == 0) {
    return 1;
  }
  /* the lowest set bit of size, capped */
  a = schema->size & (~schema->size + 1);
  return a > DS_MAX_ALIGN ? DS_MAX_ALIGN : a;
}

ds_status_t ds_schema_validate(const ds_schema_t *schema) {
  size_t a;
  if (schema == NULL) {
    return DS_ERR_NULL;
  }
  if (ds_schema_policy(schema) == DS_SCHEMA_INVALID) {
    return DS_ERR_INVALID;
  }
  a = ds_schema_align(schema);
  if (!ds_size_is_pow2(a) || a > DS_MAX_ALIGN ||
      (schema->size != 0 && schema->size % a != 0)) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

ds_status_t ds_schema_array_bytes(const ds_schema_t *schema, size_t count,
                                  size_t *out_bytes) {
  if (schema == NULL || out_bytes == NULL) {
    return DS_ERR_NULL;
  }
  return ds_size_mul(schema->size, count, out_bytes) ? DS_OK : DS_ERR_OVERFLOW;
}

bool ds_schema_same_layout(const ds_schema_t *a, const ds_schema_t *b) {
  return a != NULL && b != NULL && a->size == b->size &&
         ds_schema_align(a) == ds_schema_align(b) && a->id == b->id;
}

bool ds_schema_equal(const ds_schema_t *a, const ds_schema_t *b) {
  return ds_schema_same_layout(a, b) && a->copy == b->copy &&
         a->destroy == b->destroy && a->user == b->user;
}

ds_schema_service_t ds_schema_transfer(const ds_schema_t *from,
                                       const ds_schema_t *to) {
  if (ds_schema_equal(from, to)) {
    return DS_SCHEMA_MOVE;
  }
  if (ds_schema_same_layout(from, to) &&
      ds_schema_policy(from) != DS_SCHEMA_INVALID &&
      ds_schema_policy(to) != DS_SCHEMA_INVALID) {
    return DS_SCHEMA_COPY;
  }
  return DS_SCHEMA_NONE;
}

ds_status_t ds_schema_copy(const ds_schema_t *schema, void *dst,
                           const void *src) {
  if (schema == NULL || (schema->size > 0 && (dst == NULL || src == NULL))) {
    return DS_ERR_NULL;
  }
  if (schema->copy != NULL) {
    return schema->copy(dst, src, schema->user) ? DS_OK : DS_ERR_CALLBACK;
  }
  if (schema->size > 0) {
    memcpy(dst, src, schema->size);
  }
  return DS_OK;
}

void ds_schema_destroy(const ds_schema_t *schema, void *elem) {
  if (schema != NULL && schema->destroy != NULL && elem != NULL) {
    schema->destroy(elem, schema->user);
  }
}
