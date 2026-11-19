#include <dsa/structures/str_io.h>
#include <limits.h>
#include <string.h>

/* p + i, without arithmetic on a null pointer (an empty view's data may
 * be NULL) */
static const char *io_at(const char *p, size_t i) {
  return i == 0 ? p : p + i;
}

int ds_str_write_all(ds_str_t *s, ds_write_cb cb, void *ud) {
  size_t off = 0u, n;
  long w;
  if (!s || !cb) {
    return -1;
  }
  n = FUNC_str_len(s);
  while (off < n) {
    /* ask at most what the returned long can count */
    size_t ask = n - off > (size_t)LONG_MAX ? (size_t)LONG_MAX : n - off;
    w = cb(ud, s->buf + off, ask);
    if (w <= 0 || (size_t)w > ask) {
      return -1;
    }
    off += (size_t)w;
  }
  return 0;
}

/* Writes bufs[0..count) in full: a short write resumes where it stopped
 * (the first buffer then starts inside), and each call asks for at most
 * LONG_MAX bytes, so that the count it returns is exact. bufs and lens
 * are the caller's scratch copies. */
static int io_write_batch(const void **bufs, size_t *lens, size_t count,
                          ds_writev_cb vcb, void *ud) {
  size_t first = 0;
  for (;;) {
    size_t asked = 0, n = 0, keep = 0, done;
    long w;
    while (first < count && lens[first] == 0) {
      first++;
    }
    if (first == count) {
      return 0;
    }
    while (first + n < count && lens[first + n] <= (size_t)LONG_MAX - asked) {
      asked += lens[first + n];
      n++;
    }
    if (n == 0) { /* the first buffer alone is longer: ask for part */
      keep = lens[first];
      lens[first] = (size_t)LONG_MAX;
      asked = (size_t)LONG_MAX;
      n = 1;
    }
    w = vcb(ud, bufs + first, lens + first, n);
    if (keep != 0) {
      lens[first] = keep;
    }
    if (w <= 0 || (size_t)w > asked) {
      return -1;
    }
    done = (size_t)w;
    while (done > 0 && done >= lens[first]) {
      done -= lens[first];
      first++;
    }
    if (done > 0) {
      bufs[first] = (const unsigned char *)bufs[first] + done;
      lens[first] -= done;
    }
  }
}

int ds_str_writev_all(ds_str_t **arr, size_t count, ds_writev_cb vcb,
                      void *ud) {
  size_t i = 0;
  if (!vcb) {
    return -1;
  }
  while (i < count) {
    const void *bufs[16];
    size_t lens[16];
    size_t j, batch = count - i;
    if (batch > 16) {
      batch = 16;
    }
    for (j = 0; j < batch; ++j) {
      ds_str_t *s = arr[i + j];
      bufs[j] = s && s->buf ? (const void *)s->buf : (const void *)"";
      lens[j] = s ? s->len : 0u;
    }
    if (io_write_batch(bufs, lens, batch, vcb, ud) != 0) {
      return -1;
    }
    i += batch;
  }
  return 0;
}

int ds_str_writev_all_cb(ds_str_t **arr, size_t count, ds_write_cb cb,
                         void *ud) {
  size_t i;
  if (!cb) {
    return -1;
  }
  for (i = 0; i < count; ++i) {
    if (ds_str_write_all(arr[i], cb, ud) != 0) {
      return -1;
    }
  }
  return 0;
}

int ds_str_views_writev_all(const ds_str_view_t *views, size_t count,
                            ds_writev_cb vcb, void *ud) {
  size_t i = 0;
  if (!vcb) {
    return -1;
  }
  while (i < count) {
    const void *bufs[16];
    size_t lens[16];
    size_t j, batch = count - i;
    if (batch > 16) {
      batch = 16;
    }
    for (j = 0; j < batch; ++j) {
      bufs[j] = views[i + j].data != NULL ? (const void *)views[i + j].data
                                          : (const void *)"";
      lens[j] = views[i + j].len;
    }
    if (io_write_batch(bufs, lens, batch, vcb, ud) != 0) {
      return -1;
    }
    i += batch;
  }
  return 0;
}

int ds_str_view(ds_str_t *s, size_t pos, size_t n, ds_str_view_t *out) {
  size_t len;
  if (!out) {
    return -1;
  }
  len = FUNC_str_len(s);
  if (pos > len) {
    pos = len;
  }
  if (n > len - pos) {
    n = len - pos;
  }
  out->data = (s && s->buf) ? s->buf + pos : (const char *)"";
  out->len = n;
  return 0;
}

int ds_str_view_sub(ds_str_view_t v, size_t pos, size_t n,
                    ds_str_view_t *out) {
  if (!out) {
    return -1;
  }
  if (pos > v.len) {
    pos = v.len;
  }
  if (n > v.len - pos) {
    n = v.len - pos;
  }
  out->data = io_at(v.data, pos);
  out->len = n;
  return 0;
}

int ds_str_view_eq(ds_str_view_t a, ds_str_view_t b) {
  if (a.len != b.len) {
    return 0;
  }
  if (a.len == 0) {
    return 1;
  }
  return memcmp(a.data, b.data, a.len) == 0;
}

long ds_str_view_find(ds_str_view_t v, const void *needle, size_t n,
                      size_t start) {
  return ds_mem_find(v.data, v.len, needle, n, start);
}

long ds_str_view_count(ds_str_view_t v, const void *needle, size_t n) {
  size_t pos = 0u;
  long c = 0;
  long at;
  if (!needle || n == 0u) {
    return 0;
  }
  while (1) {
    at = ds_str_view_find(v, needle, n, pos);
    if (at < 0) {
      break;
    }
    ++c;
    pos = (size_t)at + n;
    if (pos > v.len) {
      break;
    }
  }
  return c;
}

void ds_str_view_split_each_substr(ds_str_view_t v, const void *sep,
                                   size_t nsep, ds_str_token_cb cb, void *ud) {
  size_t pos = 0u;
  long at;
  if (!cb || !sep || nsep == 0u) {
    return;
  }
  for (;;) {
    at = ds_str_view_find(v, sep, nsep, pos);
    if (at < 0) {
      cb(io_at(v.data, pos), v.len - pos, ud);
      break;
    }
    cb(io_at(v.data, pos), (size_t)at - pos, ud);
    pos = (size_t)at + nsep;
    if (pos > v.len) {
      pos = v.len;
    }
  }
}

int ds__view_in_set(unsigned char c, const unsigned char *set, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    if (c == set[i]) {
      return 1;
    }
  }
  return 0;
}

int ds_str_view_starts_with(ds_str_view_t v, const void *prefix, size_t n) {
  if (!prefix) {
    return 0;
  }
  if (v.len < n) {
    return 0;
  }
  return n == 0 || memcmp(v.data, prefix, n) == 0;
}

int ds_str_view_ends_with(ds_str_view_t v, const void *suffix, size_t n) {
  if (!suffix) {
    return 0;
  }
  if (v.len < n) {
    return 0;
  }
  return n == 0 || memcmp(v.data + (v.len - n), suffix, n) == 0;
}

int ds_str_view_ltrim_set(ds_str_view_t v, const unsigned char *set,
                          size_t set_n, ds_str_view_t *out) {
  size_t i = 0u;
  if (!out) {
    return -1;
  }
  while (i < v.len && ds__view_in_set((unsigned char)v.data[i], set, set_n)) {
    ++i;
  }
  out->data = io_at(v.data, i);
  out->len = v.len - i;
  return 0;
}

int ds_str_view_rtrim_set(ds_str_view_t v, const unsigned char *set,
                          size_t set_n, ds_str_view_t *out) {
  size_t len;
  if (!out) {
    return -1;
  }
  len = v.len;
  while (len && ds__view_in_set((unsigned char)v.data[len - 1], set, set_n)) {
    --len;
  }
  out->data = v.data;
  out->len = len;
  return 0;
}

int ds_str_view_trim_set(ds_str_view_t v, const unsigned char *set,
                         size_t set_n, ds_str_view_t *out) {
  ds_str_view_t tmp;
  if (ds_str_view_rtrim_set(v, set, set_n, &tmp) != 0) {
    return -1;
  }
  return ds_str_view_ltrim_set(tmp, set, set_n, out);
}

int ds_str_view_ltrim_pred(ds_str_view_t v, ds_view_pred pred,
                           ds_str_view_t *out) {
  size_t i = 0u;
  if (!pred || !out) {
    return -1;
  }
  while (i < v.len && pred((unsigned char)v.data[i])) {
    ++i;
  }
  out->data = io_at(v.data, i);
  out->len = v.len - i;
  return 0;
}

int ds_str_view_rtrim_pred(ds_str_view_t v, ds_view_pred pred,
                           ds_str_view_t *out) {
  size_t len;
  if (!pred || !out) {
    return -1;
  }
  len = v.len;
  while (len && pred((unsigned char)v.data[len - 1])) {
    --len;
  }
  out->data = v.data;
  out->len = len;
  return 0;
}

int ds_str_view_trim_pred(ds_str_view_t v, ds_view_pred pred,
                          ds_str_view_t *out) {
  ds_str_view_t tmp;
  if (!pred || !out) {
    return -1;
  }
  if (ds_str_view_rtrim_pred(v, pred, &tmp) != 0) {
    return -1;
  }
  return ds_str_view_ltrim_pred(tmp, pred, out);
}

void ds_str_view_split_each_c(ds_str_view_t v, unsigned char sep,
                              ds_view_token_cb cb, void *ud) {
  size_t i = 0u, start = 0u;
  if (!cb) {
    return;
  }
  for (i = 0; i < v.len; ++i) {
    if ((unsigned char)v.data[i] == sep) {
      cb(v.data + start, i - start, ud);
      start = i + 1;
    }
  }
  cb(io_at(v.data, start), v.len - start, ud);
}
