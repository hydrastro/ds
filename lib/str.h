#ifndef DS_STR_H
#define DS_STR_H

#include "common.h"
#include "context.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_str {
  char *buf;
  size_t len;
  size_t cap;
  ds_context_t *context; /* NULL for legacy allocators */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_str_t;

#define FUNC_str_len(s) ((s) ? (s)->len : 0u)
#define FUNC_str_cap(s) ((s) ? (s)->cap : 0u)
#define FUNC_str_cstr(s) (((s) && (s)->buf) ? (s)->buf : "")
#define FUNC_str_data(s) ((s) ? (s)->buf : (char *)"")

int ds__ensure(ds_str_t *s, size_t need_cap);
int ds__ensure_exact(ds_str_t *s, size_t need_cap);

size_t ds__grow_cap(size_t cur, size_t need);

ds_str_t *ds_str_create(void);
ds_str_t *ds_str_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *));
ds_str_t *ds_str_with_capacity(size_t cap);
ds_str_t *ds_str_from(const void *data, size_t n);
void ds_str_destroy(ds_str_t *s);
void ds_str_clear(ds_str_t *s);
void ds_str_delete(ds_str_t *s);
int ds_str_reserve(ds_str_t *s, size_t need);
int ds_str_shrink_to_fit(ds_str_t *s);
/* The bytes given to append, insert and assign, and the arguments of the
 * formatted appends, may lie in the string itself (a copy of a part of it):
 * they are read as they were before the call. */
int ds_str_append(ds_str_t *s, const void *data, size_t n);
int ds_str_append_cstr(ds_str_t *s, const char *cstr);
int ds_str_pushc(ds_str_t *s, int c);
int ds_str_insert(ds_str_t *s, size_t pos, const void *data, size_t n);
int ds_str_erase(ds_str_t *s, size_t pos, size_t n);
ds_str_t *ds_str_clone(ds_str_t *s);
ds_str_t *ds_str_clone_cap(ds_str_t *s);
int ds_str_cmp(ds_str_t *a, ds_str_t *b);
int ds_str_eq(ds_str_t *a, ds_str_t *b);
long ds_str_find(ds_str_t *s, const void *needle, size_t n, size_t start);
long ds_str_rfind(ds_str_t *s, const void *needle, size_t n, size_t start);
void ds_str_to_upper(ds_str_t *s);
void ds_str_to_lower(ds_str_t *s);
void ds_str_swap(ds_str_t *a, ds_str_t *b);
int ds_str_clear_freebuf(ds_str_t *s);

/* printf formats, checked against their arguments where the compiler can
 * (DS_FORMAT_PRINTF) */
int ds_str_append_vfmt(ds_str_t *s, const char *fmt, va_list ap)
    DS_FORMAT_PRINTF(2, 0);
int ds_str_append_fmt(ds_str_t *s, const char *fmt, ...) DS_FORMAT_PRINTF(2, 3);

int ds_str_reserve_exact(ds_str_t *s, size_t cap);
int ds_str_assign(ds_str_t *s, const void *data, size_t n);
int ds_str_pop(ds_str_t *s, int *out_ch);
ds_str_t *ds_str_slice(ds_str_t *s, size_t pos, size_t n);

char *ds_str_detach(ds_str_t *s, size_t *len_out, size_t *cap_out);
int ds_str_adopt(ds_str_t *s, char *buf, size_t len, size_t cap);
void ds_str_free_external(ds_str_t *owner, void *p);

int ds_str_adopt_safe(ds_str_t *s, char *buf, size_t len, size_t cap,
                      int has_room_for_nul);

/* Checked core operations distinguish invalid input, range, size overflow and
 * allocation failure. Existing integer-returning modifiers keep 0/-1 contracts.
 * Byte edits and cross-owner swap are atomic on failure; borrowed source bytes
 * may lie in the destination. Checked erase/insert use strict ranges. */
ds_status_t ds_str_create_context(ds_context_t *context, ds_str_t **out);
ds_status_t ds_str_reserve_ex(ds_str_t *s, size_t capacity);
ds_status_t ds_str_assign_ex(ds_str_t *s, const void *data, size_t length);
ds_status_t ds_str_append_ex(ds_str_t *s, const void *data, size_t length);
ds_status_t ds_str_insert_ex(ds_str_t *s, size_t position, const void *data,
                             size_t length);
ds_status_t ds_str_erase_ex(ds_str_t *s, size_t position, size_t length);
ds_status_t ds_str_pop_ex(ds_str_t *s, int *out);
/* Same-owner swap is O(1). Incompatible owners stage copies (O(capacity) space)
 * while preserving each string header's owner. Legacy swap ignores failures. */
ds_status_t ds_str_swap_ex(ds_str_t *a, ds_str_t *b);
/* NULL context inherits the source owner; failed creation/clone sets out NULL.
 * The context must outlive all strings and detached buffers using it. */
ds_status_t ds_str_clone_with(const ds_str_t *s, bool keep_capacity,
                              ds_context_t *context, ds_str_t **out);
/* Detach transfers the buffer, including its owner policy. Free it through
 * ds_str_free_external while the original owner lives, or that same context /
 * allocator afterwards. Adopt requires compatible storage with cap+1 bytes;
 * interior aliases into the current buffer are forbidden. Safe adopt copies
 * when no NUL room exists (len == cap), leaving that external buffer owned by
 * its caller; otherwise it transfers ownership. Exact self-adoption is safe. */

#ifdef __cplusplus
}
#endif

#endif /* DS_STR_H */
