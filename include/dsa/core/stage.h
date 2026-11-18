#ifndef DS_STAGE_H
#define DS_STAGE_H

/*
 * Arguments copied aside before a container changes.
 *
 * A container that copies caller data in, such as a map's put, may be given
 * data it holds itself: another entry's value, used as the new value or as
 * the new key. Growth frees the block such data lies in, and moving entries
 * in place overwrites it. Staging copies the arguments first, so the
 * container reads them as they were before the call. A pointer cannot tell
 * whether it points into a container without comparing unrelated objects,
 * so a container stages whatever it is given.
 *
 * Up to DS_STAGE_BYTES (key and value together, the value starting at a
 * multiple of DS_MAX_ALIGN) stay inside the ds_stage_t, maximally aligned,
 * so a staged key reads like the original; larger arguments take one block
 * from the context (DS_ERR_ALLOC when refused). A container in fixed
 * storage, which must not allocate, passes may_allocate false: larger
 * arguments are then not copied, and stage->key and stage->value are the
 * originals.
 *
 *   ds_stage_t st;
 *   if (ds_stage_copy(&st, context, true, key, key_size, value,
 *                     value_size) != DS_OK) { ... }
 *   ... use st.key and st.value ...
 *   ds_stage_release(&st);
 */

#include <dsa/core/checked.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DS_STAGE_BYTES
#define DS_STAGE_BYTES 256
#endif

typedef struct ds_stage {
  union {
    unsigned char bytes[DS_STAGE_BYTES];
    ds_max_align_t align;
  } room;
  unsigned char *block; /* from the context, or NULL */
  ds_context_t *context;
  const void *key;   /* the staged key, the original, or NULL */
  const void *value; /* the staged value, the original, or NULL */
} ds_stage_t;

/* key or value may be NULL (nothing to stage), or of size 0 */
ds_status_t ds_stage_copy(ds_stage_t *stage, ds_context_t *context,
                          bool may_allocate, const void *key, size_t key_size,
                          const void *value, size_t value_size);
void ds_stage_release(ds_stage_t *stage);

#ifdef __cplusplus
}
#endif

#endif /* DS_STAGE_H */
