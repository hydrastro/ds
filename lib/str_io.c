#include "str_io.h"

int FUNC(str_write_all)(ds_str_t *s, ds_write_cb cb, void *ud) {
  size_t off = 0u, n = FUNC_str_len(s);
  long w;
  if (!s || !cb) return -1;
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
    if (batch > 16) batch = 16;
    for (j = 0; j < batch; ++j) {
      ds_str_t *s = arr[i + j];
      bufs[j] = s ? (const void*)s->buf : (const void*)"";
      lens[j] = s ? s->len : 0u;
    }
    {
      long w = vcb(ud, bufs, lens, batch);
      if (w < 0) return -1;
    }
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
    if (batch > 16) batch = 16;
    for (j = 0; j < batch; ++j) {
      bufs[j] = views[i + j].data;
      lens[j] = views[i + j].len;
    }
    {
      long w = vcb(ud, bufs, lens, batch);
      if (w < 0) return -1;
    }
    i += batch;
  }
  return 0;
}
