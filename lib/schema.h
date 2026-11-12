#ifndef DS_SCHEMA_H
#define DS_SCHEMA_H

/*
 * Element schemas: one descriptor for what a container needs to know about
 * the values it stores, so that containers built from the same schema can
 * be checked for compatibility instead of trusting matching sizes.
 *
 *   size, align   layout of one element; align 0 derives the largest power
 *                 of two dividing size, at most DS_MAX_ALIGN
 *   copy          clone src into uninitialized dst, false on failure;
 *                 NULL copies bytes
 *   destroy       release what an element owns; NULL: nothing is owned
 *   user          passed to both callbacks
 *   id            nonzero names the representation, so that two schemas
 *                 with the same id promise the same meaning of the bytes;
 *                 0 is anonymous
 *
 * Ownership policies follow from the callbacks:
 *
 *   plain       no callbacks: bytes are copied and nothing is released
 *   copy only   copy without destroy: copies are made by the callback
 *               (deep copies of borrowed data, retain counts released
 *               elsewhere) and nothing is released per element
 *   owned       copy and destroy: every copy is a clone that is released
 *               exactly once
 *   destroy without copy is rejected (DS_ERR_INVALID): a structure that
 *   shares or returns elements could not give out an owned copy.
 *
 * Cross-container operations choose among three services
 * (ds_schema_transfer):
 *
 *   move     equal schemas (ds_schema_equal): nodes or bytes move as they
 *            are, ownership travels with them (the memory side is
 *            ds_context_compatible)
 *   copy     equal layout and id but different callbacks: each element is
 *            cloned with the destination's copy and released with the
 *            source's destroy
 *   none     different layouts: a conversion is the caller's job
 *
 * Containers copy the schema at initialization; changing it afterwards
 * does not affect them. ds_pvec_config_from_schema and
 * ds_ftree_config_set_schema fill those structures' configurations from a
 * schema.
 */

#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ds_schema_copy_fn)(void *dst, const void *src, void *user);
typedef void (*ds_schema_destroy_fn)(void *elem, void *user);

typedef struct ds_schema {
  size_t size;
  size_t align;
  ds_schema_copy_fn copy;
  ds_schema_destroy_fn destroy;
  void *user;
  unsigned long id;
} ds_schema_t;

typedef enum ds_schema_policy {
  DS_SCHEMA_PLAIN,
  DS_SCHEMA_COPY_ONLY,
  DS_SCHEMA_OWNED,
  DS_SCHEMA_INVALID
} ds_schema_policy_t;

typedef enum ds_schema_service {
  DS_SCHEMA_MOVE,
  DS_SCHEMA_COPY,
  DS_SCHEMA_NONE
} ds_schema_service_t;

/* plain elements of the given size, anonymous */
void ds_schema_init(ds_schema_t *schema, size_t size);
ds_schema_policy_t ds_schema_policy(const ds_schema_t *schema);
/* DS_OK, or DS_ERR_INVALID for destroy without copy or a bad alignment
 * (not a power of two, above DS_MAX_ALIGN, or not dividing a nonzero
 * size) */
ds_status_t ds_schema_validate(const ds_schema_t *schema);
/* the effective alignment (derived when align is 0) */
size_t ds_schema_align(const ds_schema_t *schema);
/* bytes of count consecutive elements, overflow-checked */
ds_status_t ds_schema_array_bytes(const ds_schema_t *schema, size_t count,
                                  size_t *out_bytes);
/* same layout, id and callbacks: elements may move between containers */
bool ds_schema_equal(const ds_schema_t *a, const ds_schema_t *b);
/* same size, alignment and id: elements mean the same bytes */
bool ds_schema_same_layout(const ds_schema_t *a, const ds_schema_t *b);
/* which service moves elements from a container of `from` to one of `to` */
ds_schema_service_t ds_schema_transfer(const ds_schema_t *from,
                                       const ds_schema_t *to);
/* clone src into uninitialized dst: DS_OK, or DS_ERR_CALLBACK when the
 * copy callback fails (dst is then uninitialized) */
ds_status_t ds_schema_copy(const ds_schema_t *schema, void *dst,
                           const void *src);
/* release elem's resources (nothing for plain and copy-only schemas) */
void ds_schema_destroy(const ds_schema_t *schema, void *elem);

#ifdef __cplusplus
}
#endif

#endif /* DS_SCHEMA_H */
