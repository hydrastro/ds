#ifndef DS_STR_H
#define DS_STR_H

#include "common.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct ds_str {
  char *buf;
  size_t len;
  size_t cap;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_str_t;

#define FUNC_str_len(s) ((s) ? (s)->len : 0u)
#define FUNC_str_cap(s) ((s) ? (s)->cap : 0u)
#define FUNC_str_cstr(s) (((s) && (s)->buf) ? (s)->buf : "")
#define FUNC_str_data(s) ((s) ? (s)->buf : (char *)"")

ds_str_t *FUNC(str_create)(void);
ds_str_t *FUNC(str_create_alloc)(void *(*allocator)(size_t),
                                 void (*deallocator)(void *));
ds_str_t *FUNC(str_with_capacity)(size_t cap);
ds_str_t *FUNC(str_from)(const void *data, size_t n);
void FUNC(str_destroy)(ds_str_t *s);
void FUNC(str_clear)(ds_str_t *s);
void FUNC(str_delete)(ds_str_t *s);
int FUNC(str_reserve)(ds_str_t *s, size_t need);
int FUNC(str_shrink_to_fit)(ds_str_t *s);
int FUNC(str_append)(ds_str_t *s, const void *data, size_t n);
int FUNC(str_append_cstr)(ds_str_t *s, const char *cstr);
int FUNC(str_pushc)(ds_str_t *s, int c);
int FUNC(str_insert)(ds_str_t *s, size_t pos, const void *data, size_t n);
int FUNC(str_erase)(ds_str_t *s, size_t pos, size_t n);
ds_str_t *FUNC(str_clone)(ds_str_t *s);
ds_str_t *FUNC(str_clone_cap)(ds_str_t *s);
int FUNC(str_cmp)(ds_str_t *a, ds_str_t *b);
int FUNC(str_eq)(ds_str_t *a, ds_str_t *b);
long FUNC(str_find)(ds_str_t *s, const void *needle, size_t n, size_t start);
long FUNC(str_rfind)(ds_str_t *s, const void *needle, size_t n, size_t start);
void FUNC(str_to_upper)(ds_str_t *s);
void FUNC(str_to_lower)(ds_str_t *s);
void FUNC(str_swap)(ds_str_t *a, ds_str_t *b);
int FUNC(str_clear_freebuf)(ds_str_t *s);

int FUNC(str_append_vfmt)(ds_str_t *s, const char *fmt, va_list ap);
int FUNC(str_append_fmt)(ds_str_t *s, const char *fmt, ...);

#endif /* DS_STR_H */
