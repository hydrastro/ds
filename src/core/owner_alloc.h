#ifndef DS_OWNER_ALLOC_H
#define DS_OWNER_ALLOC_H

/* Allocate through the container owner. A non-NULL context owns every
 * allocation; otherwise the explicit allocator/deallocator pair is used. */
#include <dsa/core/context.h>

#define DS_OWNER_ALLOCATE(ctx, allocator, bytes)                               \
  ((ctx) != NULL ? ds_context_alloc((ctx), (bytes)) : (allocator)((bytes)))
#define DS_OWNER_RELEASE(ctx, deallocator, pointer)                            \
  ((ctx) != NULL ? ds_context_free((ctx), (pointer)) : (deallocator)((pointer)))
#define DS_OWNER_ALLOC(object, bytes)                                          \
  DS_OWNER_ALLOCATE((object)->context, (object)->allocator, (bytes))
#define DS_OWNER_FREE(object, pointer)                                         \
  DS_OWNER_RELEASE((object)->context, (object)->deallocator, (pointer))

#endif /* DS_OWNER_ALLOC_H */
