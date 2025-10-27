#include "str_io.h"

int FUNC(str_write_all)(ds_str_t *s, ds_write_cb cb, void *ud) {
  size_t off = 0u, n;
  long w;
  if (!s || !cb) return -1;
  n = FUNC_str_len(s);
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  while (off < n) {
    w = cb(ud, s->buf + off, n - off);
    if (w <= 0) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return -1;
    }
    off += (size_t)w;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return 0;
}

int FUNC(str_writev_all)(ds_str_t **arr, size_t count, ds_writev_cb vcb, void *ud) {
  size_t i = 0;
  if (!vcb) return -1;
  if (count == 0) return 0;
  while (i < count) {
    const void *bufs[16];
    size_t lens[16];
    size_t j, batch = count - i;
    long w;
    if (batch > 16) batch = 16;
    for (j = 0; j < batch; ++j) {
      ds_str_t *s = arr[i + j];
      bufs[j] = s ? (const void*)s->buf : (const void*)"";
      lens[j] = s ? s->len : 0u;
    }
    w = vcb(ud, bufs, lens, batch);
    if (w < 0) return -1;
    i += batch;
  }
  return 0;
}

int FUNC(str_writev_all_cb)(ds_str_t **arr, size_t count, ds_write_cb cb, void *ud) {
  size_t i;
  if (!cb) return -1;
  for (i = 0; i < count; ++i) {
    if (FUNC(str_write_all)(arr[i], cb, ud) != 0) return -1;
  }
  return 0;
}

int FUNC(str_views_writev_all)(const ds_str_view_t *views, size_t count, ds_writev_cb vcb, void *ud) {
  size_t i = 0;
  if (!vcb) return -1;
  if (count == 0) return 0;
  while (i < count) {
    const void *bufs[16];
    size_t lens[16];
    size_t j, batch = count - i;
    long w;
    if (batch > 16) batch = 16;
    for (j = 0; j < batch; ++j) {
      bufs[j] = views[i + j].data;
      lens[j] = views[i + j].len;
    }
    w = vcb(ud, bufs, lens, batch);
    if (w < 0) return -1;
    i += batch;
  }
  return 0;
}

ds_str_view_t FUNC(str_view)(ds_str_t *s, size_t pos, size_t n) {
  ds_str_view_t v;
  size_t len = FUNC_str_len(s);
  if (pos > len) pos = len;
  if (n > len - pos) n = len - pos;
  v.data = (s && s->buf) ? s->buf + pos : (const char *)"";
  v.len = n;
  return v;
}

int FUNC(str_view_eq)(ds_str_view_t a, ds_str_view_t b) {
  if (a.len != b.len) return 0;
  if (a.len == 0) return 1;
  return memcmp(a.data, b.data, a.len) == 0;
}

ds_str_view_t FUNC(str_view_sub)(ds_str_view_t v, size_t pos, size_t n) {
  ds_str_view_t out;
  if (pos > v.len) pos = v.len;
  if (n > v.len - pos) n = v.len - pos;
  out.data = v.data + pos; out.len = n; return out;
}

long FUNC(str_view_find)(ds_str_view_t v, const void *needle, size_t n, size_t start) {
  size_t i;
  const unsigned char *h = (const unsigned char*)v.data;
  const unsigned char *nd = (const unsigned char*)needle;
  if (!needle || n == 0u) return -1;
  if (start > v.len) return -1;
  if (n == 1u) {
    const void *p = memchr(h + start, *nd, v.len - start);
    return p ? (long)((const unsigned char*)p - h) : -1;
  }
  for (i = start; i + n <= v.len; ++i) if (memcmp(h + i, nd, n) == 0) return (long)i;
  return -1;
}
