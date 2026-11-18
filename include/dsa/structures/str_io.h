#ifndef DS_STR_IO_H
#define DS_STR_IO_H

#include <dsa/algorithms/str_algo.h>
#include <dsa/core/common.h>
#include <dsa/structures/str.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A view of len bytes at data; data may be NULL when len is 0 (a zeroed
 * view is empty). Searches are two-way matching (ds_mem_find). */
typedef struct ds_str_view {
  const char *data;
  size_t len;
} ds_str_view_t;

/* Write callbacks. ds_write_cb writes some of buf[0..n) and returns how
 * many bytes it wrote: fewer than n is a short write, which the caller
 * resumes; 0 or less is a failure. ds_writev_cb does the same for the
 * buffers in order and returns the bytes it wrote across them. The _all
 * functions resume short writes until everything is written, asking each
 * call for at most LONG_MAX bytes so that the count it returns is exact,
 * and return 0, or -1 when a callback fails. */
typedef long (*ds_write_cb)(void *ud, const void *buf, size_t n);
typedef long (*ds_writev_cb)(void *ud, const void *const *bufs,
                             const size_t *lens, size_t count);

int ds_str_write_all(ds_str_t *s, ds_write_cb cb, void *ud);
int ds_str_writev_all(ds_str_t **arr, size_t count, ds_writev_cb vcb, void *ud);
int ds_str_writev_all_cb(ds_str_t **arr, size_t count, ds_write_cb cb,
                         void *ud);
int ds_str_views_writev_all(const ds_str_view_t *views, size_t count,
                            ds_writev_cb vcb, void *ud);

int ds_str_view(ds_str_t *s, size_t pos, size_t n, ds_str_view_t *out);
int ds_str_view_sub(ds_str_view_t v, size_t pos, size_t n, ds_str_view_t *out);

int ds_str_view_eq(ds_str_view_t a, ds_str_view_t b);
long ds_str_view_find(ds_str_view_t v, const void *needle, size_t n,
                      size_t start);

long ds_str_view_count(ds_str_view_t v, const void *needle, size_t n);
void ds_str_view_split_each_substr(ds_str_view_t v, const void *sep,
                                   size_t nsep, ds_str_token_cb cb, void *ud);

typedef int (*ds_view_pred)(unsigned char c);
typedef void (*ds_view_token_cb)(const char *ptr, size_t len, void *ud);

int ds__view_in_set(unsigned char c, const unsigned char *set, size_t n);

int ds_str_view_starts_with(ds_str_view_t v, const void *prefix, size_t n);
int ds_str_view_ends_with(ds_str_view_t v, const void *suffix, size_t n);

int ds_str_view_ltrim_set(ds_str_view_t v, const unsigned char *set,
                          size_t set_n, ds_str_view_t *out);
int ds_str_view_rtrim_set(ds_str_view_t v, const unsigned char *set,
                          size_t set_n, ds_str_view_t *out);
int ds_str_view_trim_set(ds_str_view_t v, const unsigned char *set,
                         size_t set_n, ds_str_view_t *out);

int ds_str_view_ltrim_pred(ds_str_view_t v, ds_view_pred pred,
                           ds_str_view_t *out);
int ds_str_view_rtrim_pred(ds_str_view_t v, ds_view_pred pred,
                           ds_str_view_t *out);
int ds_str_view_trim_pred(ds_str_view_t v, ds_view_pred pred,
                          ds_str_view_t *out);

void ds_str_view_split_each_c(ds_str_view_t v, unsigned char sep,
                              ds_view_token_cb cb, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* DS_STR_IO_H */
