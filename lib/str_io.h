#ifndef DS_STR_IO_H
#define DS_STR_IO_H

#include "common.h"
#include "str.h"
#include <stddef.h>

typedef struct ds_str_view {
  const char *data;
  size_t len;
} ds_str_view_t;

typedef long (*ds_write_cb)(void *ud, const void *buf, size_t n);
typedef long (*ds_writev_cb)(void *ud, const void *const *bufs,
                             const size_t *lens, size_t count);

int FUNC(str_write_all)(ds_str_t *s, ds_write_cb cb, void *ud);
int FUNC(str_writev_all)(ds_str_t **arr, size_t count, ds_writev_cb vcb,
                         void *ud);
int FUNC(str_writev_all_cb)(ds_str_t **arr, size_t count, ds_write_cb cb,
                            void *ud);
int FUNC(str_views_writev_all)(const ds_str_view_t *views, size_t count,
                               ds_writev_cb vcb, void *ud);

int FUNC(str_view)(ds_str_t *s, size_t pos, size_t n, ds_str_view_t *out);
int FUNC(str_view_sub)(ds_str_view_t v, size_t pos, size_t n,
                       ds_str_view_t *out);

int FUNC(str_view_eq)(ds_str_view_t a, ds_str_view_t b);
long FUNC(str_view_find)(ds_str_view_t v, const void *needle, size_t n,
                         size_t start);

#endif /* DS_STR_IO_H */
