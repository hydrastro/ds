#ifndef DS_CACTUS_STACK_H
#define DS_CACTUS_STACK_H

/*
 * Cactus stacks (saguaro stacks, spaghetti stacks): a tree of frames in
 * which every frame points to the frame below it, so many stacks share
 * their common bottoms. A stack is a handle to its top frame; pushing onto
 * a handle creates a child frame and leaves the old handle valid (forking
 * a stack is O(1)), which is how parallel calls and closures keep their
 * environments.
 *
 * Frames are stored by value in arrays and reference counted: each handle
 * owns one reference and each frame owns one on its parent, so a frame
 * lives while any stack can still reach it. Releasing a frame frees the
 * chain that becomes unreachable, iteratively.
 *
 * Concurrency: a cactus stack may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_CACTUS_EMPTY (~(size_t)0)

typedef struct ds_cactus_stack {
  unsigned char *data;
  size_t *parent;
  size_t *refs;
  size_t *depth;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t live;
  size_t elem_size;
  ds_context_t *context;
} ds_cactus_stack_t;

ds_status_t ds_cactus_stack_init(ds_cactus_stack_t *cs, size_t elem_size,
                                 ds_context_t *context);
void ds_cactus_stack_destroy(ds_cactus_stack_t *cs);
/* New frame on top of `top` (DS_CACTUS_EMPTY: a new stack); the result is
 * a handle owning one reference, and `top` stays valid. */
ds_status_t ds_cactus_stack_push(ds_cactus_stack_t *cs, size_t top,
                                 const void *elem, size_t *out_top);
/* Handle to the stack below top (retained), releasing top. */
ds_status_t ds_cactus_stack_pop(ds_cactus_stack_t *cs, size_t top,
                                void *out_elem, size_t *out_below);
/* Another handle to the same stack (a fork). */
ds_status_t ds_cactus_stack_retain(ds_cactus_stack_t *cs, size_t top);
/* Drops a handle; frames nobody reaches any more are freed. */
ds_status_t ds_cactus_stack_release(ds_cactus_stack_t *cs, size_t top);
void *ds_cactus_stack_peek(const ds_cactus_stack_t *cs, size_t top);
size_t ds_cactus_stack_below(const ds_cactus_stack_t *cs, size_t top);
/* Frames in the stack (0 for DS_CACTUS_EMPTY). */
size_t ds_cactus_stack_depth(const ds_cactus_stack_t *cs, size_t top);
/* Frames currently alive in the whole cactus. */
size_t ds_cactus_stack_live(const ds_cactus_stack_t *cs);
/* Top to bottom; return false to stop. */
void ds_cactus_stack_walk(const ds_cactus_stack_t *cs, size_t top,
                          bool (*visit)(const void *elem, void *user),
                          void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_CACTUS_STACK_H */
