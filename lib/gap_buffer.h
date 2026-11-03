#ifndef DS_GAP_BUFFER_H
#define DS_GAP_BUFFER_H

/*
 * Gap buffer: a byte sequence with a movable "gap" of free space at the
 * cursor, the classic text-editor buffer (Emacs). Inserting or deleting at
 * the cursor is O(1); moving the cursor by d positions costs O(d) because
 * the gap travels with it. Edits are therefore cheap when they are local,
 * which is how people type.
 *
 * Positions are byte offsets in [0, length]. The text is not
 * NUL-terminated; ds_gap_buffer_copy() extracts a range.
 *
 * Storage: dynamic (ds_gap_buffer_init, grows through a ds_context_t) or a
 * caller buffer (ds_gap_buffer_init_buffer, DS_ERR_OVERFLOW when full).
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_gap_buffer {
  unsigned char *data;
  size_t capacity;
  size_t gap_start; /* == cursor */
  size_t gap_end;
  ds_context_t *context;
  bool fixed;
} ds_gap_buffer_t;

ds_status_t ds_gap_buffer_init(ds_gap_buffer_t *gb, size_t capacity,
                               ds_context_t *context);
ds_status_t ds_gap_buffer_init_buffer(ds_gap_buffer_t *gb, void *buffer,
                                      size_t capacity);
void ds_gap_buffer_destroy(ds_gap_buffer_t *gb);

size_t ds_gap_buffer_length(const ds_gap_buffer_t *gb);
size_t ds_gap_buffer_cursor(const ds_gap_buffer_t *gb);
ds_status_t ds_gap_buffer_move_to(ds_gap_buffer_t *gb, size_t position);
ds_status_t ds_gap_buffer_insert(ds_gap_buffer_t *gb, const void *text,
                                 size_t size);
size_t ds_gap_buffer_delete_backward(ds_gap_buffer_t *gb, size_t count);
size_t ds_gap_buffer_delete_forward(ds_gap_buffer_t *gb, size_t count);
int ds_gap_buffer_at(const ds_gap_buffer_t *gb, size_t position);
size_t ds_gap_buffer_copy(const ds_gap_buffer_t *gb, size_t position,
                          size_t count, void *out);
void ds_gap_buffer_clear(ds_gap_buffer_t *gb);

#ifdef __cplusplus
}
#endif

#endif /* DS_GAP_BUFFER_H */
