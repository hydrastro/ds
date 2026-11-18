#ifndef DS_LEGACY_ALLOC_H
#define DS_LEGACY_ALLOC_H

/* Internal bridge for containers retaining their original allocator fields.
 * A non-NULL context owns every container allocation. Legacy constructors
 * leave context NULL and keep using the original function pair. */
#include "context.h"

#define DS_LEGACY_ALLOCATE(ctx, allocator, bytes)                              \
  ((ctx) != NULL ? ds_context_alloc((ctx), (bytes)) : (allocator)((bytes)))
#define DS_LEGACY_RELEASE(ctx, deallocator, pointer)                           \
  ((ctx) != NULL ? ds_context_free((ctx), (pointer)) : (deallocator)((pointer)))
#define DS_LEGACY_ALLOC(object, bytes)                                         \
  DS_LEGACY_ALLOCATE((object)->context, (object)->allocator, (bytes))
#define DS_LEGACY_FREE(object, pointer)                                        \
  DS_LEGACY_RELEASE((object)->context, (object)->deallocator, (pointer))

#endif /* DS_LEGACY_ALLOC_H */
