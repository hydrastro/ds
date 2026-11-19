#include <dsa/structures/str.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int ds__ensure_exact(ds_str_t *s, size_t need_cap) {
  char *nbuf;
  size_t bytes;
  if (!s) {
    return -1;
  }
  if (s->cap >= need_cap) {
    return 0;
  }
  if (need_cap > SIZE_MAX - 1u) {
    return -1;
  }
  bytes = need_cap + 1u;
  if (s->buf == NULL) {
    nbuf = (char *)ds_context_alloc(s->context, bytes);
    if (!nbuf) {
      return -1;
    }
    nbuf[0] = '\0';
  } else {
    char *tmp = (char *)ds_context_alloc(s->context, bytes);
    size_t copy = s->len;
    if (!tmp) {
      return -1;
    }
    if (copy) {
      memcpy(tmp, s->buf, copy);
    }
    tmp[copy] = '\0';
    ds_context_free(s->context, s->buf);
    nbuf = tmp;
  }
  s->buf = nbuf;
  s->cap = need_cap;
  return 0;
}

size_t ds__grow_cap(size_t cur, size_t need) {
  size_t cap = cur ? cur : 16u;
  while (cap < need) {
    if (cap > SIZE_MAX / 2u) {
      cap = need;
      break;
    }
    cap *= 2u;
  }
  return cap;
}

int ds__ensure(ds_str_t *s, size_t need_cap) {
  char *nbuf;
  size_t bytes;
  if (!s) {
    return -1;
  }
  if (s->cap >= need_cap) {
    return 0;
  }
  need_cap = ds__grow_cap(s->cap, need_cap);
  if (need_cap > SIZE_MAX - 1u) {
    return -1;
  }
  bytes = need_cap + 1u;
  if (s->buf == NULL) {
    nbuf = (char *)ds_context_alloc(s->context, bytes);
    if (!nbuf) {
      return -1;
    }
    nbuf[0] = '\0';
  } else {
    char *tmp = (char *)ds_context_alloc(s->context, bytes);
    size_t copy = s->len;
    if (!tmp) {
      return -1;
    }
    if (copy) {
      memcpy(tmp, s->buf, copy);
    }
    tmp[copy] = '\0';
    ds_context_free(s->context, s->buf);
    nbuf = tmp;
  }
  s->buf = nbuf;
  s->cap = need_cap;
  return 0;
}

static ds_str_t *str_create_internal(ds_context_t *context) {
  ds_str_t *s;
  s = (ds_str_t *)ds_context_alloc(context, sizeof(*s));
  if (s == NULL) {
    return NULL;
  }
  s->context = context != NULL ? context : ds_default_context();

  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
  return s;
}
ds_str_t *ds_str_create(ds_context_t *context) {
  return str_create_internal(context);
}
ds_status_t ds_str_create_context(ds_context_t *context, ds_str_t **out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = str_create_internal(context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

ds_str_t *ds_str_with_capacity(size_t cap, ds_context_t *context) {
  ds_str_t *s = ds_str_create(context);
  if (!s) {
    return NULL;
  }
  if (cap == 0) {
    if (ds__ensure(s, 0) != 0) {
      ds_str_destroy(s);
      return NULL;
    }
    if (!s->buf) {
      char *p = (char *)ds_context_alloc(s->context, 1u);
      if (!p) {
        ds_str_destroy(s);
        return NULL;
      }
      p[0] = '\0';
      s->buf = p;
      s->cap = 0;
    }
    return s;
  }
  if (ds__ensure(s, cap) != 0) {
    ds_str_destroy(s);
    return NULL;
  }
  return s;
}

ds_str_t *ds_str_from(const void *data, size_t n, ds_context_t *context) {
  ds_str_t *s;
  if (data == NULL && n != 0) {
    return NULL;
  }
  s = ds_str_with_capacity(n, context);
  if (!s) {
    return NULL;
  }
  if (s->buf == NULL) {
    ds_str_destroy(s);
    return NULL;
  }
  if (n) {
    memcpy(s->buf, data, n);
  }
  s->len = n;
  s->buf[n] = '\0';
  return s;
}

void ds_str_destroy(ds_str_t *s) {
  if (!s) {
    return;
  }
  if (s->buf) {
    ds_context_free(s->context, s->buf);
  }
  ds_context_free(s->context, s);
}

void ds_str_clear(ds_str_t *s) {
  if (!s) {
    return;
  }
  s->len = 0u;
  if (s->buf) {
    s->buf[0] = '\0';
  }
}

void ds_str_delete(ds_str_t *s) { ds_str_clear(s); }

int ds_str_reserve(ds_str_t *s, size_t need) {
  int r;
  r = ds__ensure(s, need);
  return r;
}

int ds_str_shrink_to_fit(ds_str_t *s) {
  char *nbuf;
  size_t bytes;
  if (!s) {
    return -1;
  }
  if (s->cap == s->len) {
    return 0;
  }
  if (s->len == SIZE_MAX) {
    return -1;
  }
  bytes = s->len + 1u;
  nbuf = (char *)ds_context_alloc(s->context, bytes);
  if (!nbuf) {
    return -1;
  }
  if (s->len) {
    memcpy(nbuf, s->buf, s->len);
  }
  nbuf[s->len] = '\0';
  if (s->buf) {
    ds_context_free(s->context, s->buf);
  }
  s->buf = nbuf;
  s->cap = s->len;
  return 0;
}

/* Grows s like ds__ensure, but hands the old buffer to the caller in *old
 * (NULL when the buffer did not move) instead of freeing it, so that data
 * read from the string itself stays valid until the caller releases it
 * with str_release. */
static int str_grow_keep(ds_str_t *s, size_t need_cap, char **old) {
  char *nbuf;
  *old = NULL;
  if (s->cap >= need_cap) {
    return 0;
  }
  need_cap = ds__grow_cap(s->cap, need_cap);
  if (need_cap > SIZE_MAX - 1u) {
    return -1;
  }
  nbuf = (char *)ds_context_alloc(s->context, need_cap + 1u);
  if (!nbuf) {
    return -1;
  }
  if (s->len) {
    memcpy(nbuf, s->buf, s->len);
  }
  nbuf[s->len] = '\0';
  *old = s->buf;
  s->buf = nbuf;
  s->cap = need_cap;
  return 0;
}

static void str_release(ds_str_t *s, char *old) {
  if (old) {
    ds_context_free(s->context, old);
  }
}

static void str_reverse(char *p, size_t n) {
  char t;
  size_t i = 0;
  while (n > i + 1u) {
    n--;
    t = p[i];
    p[i] = p[n];
    p[n] = t;
    i++;
  }
}

#define STR_STAGE_BYTES 256u

/* data may be a part of s itself: the old buffer outlives the copy, and in
 * place the bytes written are past the end of the string */
int ds_str_append(ds_str_t *s, const void *data, size_t n) {
  size_t need_len;
  char *old;
  if (!s || (!data && n)) {
    return -1;
  }
  if (n == 0u) {
    return 0;
  }
  need_len = s->len + n;
  if (need_len < s->len) {
    return -1;
  }
  if (str_grow_keep(s, need_len, &old) != 0) {
    return -1;
  }
  memcpy(s->buf + s->len, data, n);
  s->len = need_len;
  s->buf[s->len] = '\0';
  str_release(s, old);
  return 0;
}

int ds_str_append_cstr(ds_str_t *s, const char *cstr) {
  size_t n = cstr ? strlen(cstr) : 0u;
  return ds_str_append(s, cstr, n);
}

int ds_str_pushc(ds_str_t *s, int c) {
  unsigned char ch = (unsigned char)(c & 0xFF);
  return ds_str_append(s, &ch, 1u);
}

/* data may be a part of s itself (see ds_str_append) */
int ds_str_insert(ds_str_t *s, size_t pos, const void *data, size_t n) {
  size_t len;
  char *old;
  if (!s || (!data && n)) {
    return -1;
  }
  len = s->len;
  if (pos > len) {
    pos = len;
  }
  if (n == 0u) {
    return 0;
  }
  if (len + n < len) {
    return -1;
  }
  if (str_grow_keep(s, len + n, &old) != 0) {
    return -1;
  }
  if (old || pos == len) {
    /* data is intact: in the old buffer, or before the bytes that move */
    memmove(s->buf + pos + n, s->buf + pos, len - pos);
    memcpy(s->buf + pos, data, n);
  } else if (n <= STR_STAGE_BYTES) {
    char stage[STR_STAGE_BYTES];
    memcpy(stage, data, n);
    memmove(s->buf + pos + n, s->buf + pos, len - pos);
    memcpy(s->buf + pos, stage, n);
  } else {
    /* staged past the end, then rotated in front of the tail */
    memcpy(s->buf + len, data, n);
    str_reverse(s->buf + pos, len - pos);
    str_reverse(s->buf + len, n);
    str_reverse(s->buf + pos, len - pos + n);
  }
  s->len = len + n;
  s->buf[s->len] = '\0';
  str_release(s, old);
  return 0;
}

int ds_str_erase(ds_str_t *s, size_t pos, size_t n) {
  size_t len;
  if (!s) {
    return -1;
  }
  len = s->len;
  if (pos > len) {
    return 0;
  }
  if (n > len - pos) {
    n = len - pos;
  }
  if (n == 0u) {
    return 0;
  }
  memmove(s->buf + pos, s->buf + pos + n, len - pos - n);
  s->len = len - n;
  s->buf[s->len] = '\0';
  return 0;
}

ds_str_t *ds_str_clone(ds_str_t *s) {
  ds_str_t *n;
  if (!s) {
    return NULL;
  }
  n = str_create_internal(s->context);
  if (!n) {
    return NULL;
  }
  if (ds__ensure(n, s->len) != 0) {
    ds_str_destroy(n);
    return NULL;
  }
  if (s->len) {
    memcpy(n->buf, s->buf, s->len);
  }
  n->len = s->len;
  if (n->buf) { /* an empty string has no buffer yet, like a new one */
    n->buf[n->len] = '\0';
  }
  return n;
}

ds_str_t *ds_str_clone_cap(ds_str_t *s) {
  ds_str_t *n;
  size_t want;
  if (!s) {
    return NULL;
  }
  n = str_create_internal(s->context);
  if (!n) {
    return NULL;
  }
  want = s->cap ? s->cap : s->len;
  if (want && ds__ensure(n, want) != 0) {
    ds_str_destroy(n);
    return NULL;
  }
  if (s->len) {
    memcpy(n->buf, s->buf, s->len);
  }
  n->len = s->len;
  if (n->buf) {
    n->buf[n->len] = '\0';
  }
  return n;
}

int ds_str_cmp(ds_str_t *a, ds_str_t *b) {
  size_t la, lb, lm;
  int r;
  if (a == b) {
    return 0;
  }
  la = a ? a->len : 0u;
  lb = b ? b->len : 0u;
  lm = la < lb ? la : lb;
  r = lm ? memcmp(a ? a->buf : "", b ? b->buf : "", lm) : 0;
  if (r != 0) {
    return r;
  }
  if (la < lb) {
    return -1;
  }
  if (la > lb) {
    return 1;
  }
  return 0;
}

int ds_str_eq(ds_str_t *a, ds_str_t *b) { return ds_str_cmp(a, b) == 0; }

long ds_str_find(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h;
  const unsigned char *nd = (const unsigned char *)needle;
  size_t i, len;
  const void *p;
  long ret = -1;
  if (!s || !needle || n == 0u) {
    return -1;
  }
  len = s->len;
  if (start > len) {
    return -1;
  }
  h = (const unsigned char *)s->buf;
  if (start + n > len) { /* no room; also an empty string's NULL buffer */
    return -1;
  }
  if (n == 1u) {
    p = memchr(h + start, *nd, len - start);
    ret = p ? (long)((const unsigned char *)p - h) : -1;
    return ret;
  }
  for (i = start; i + n <= len; ++i) {
    if (memcmp(h + i, nd, n) == 0) {
      ret = (long)i;
      break;
    }
  }
  return ret;
}

long ds_str_rfind(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h, *nd = (const unsigned char *)needle;
  size_t i, len;
  long ret = -1;
  if (!s || !needle || n == 0u) {
    return -1;
  }
  len = s->len;
  if (start > len) {
    start = len;
  }
  if (start < n) {
    return -1;
  }
  h = (const unsigned char *)s->buf;
  for (i = start - n + 1; i-- > 0;) {
    if (memcmp(h + i, nd, n) == 0) {
      ret = (long)i;
      break;
    }
  }
  return ret;
}

void ds_str_to_upper(ds_str_t *s) {
  size_t i, n;
  if (!s) {
    return;
  }
  n = s->len;
  for (i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)s->buf[i];
    if (c >= 'a' && c <= 'z') {
      s->buf[i] = (char)(c - 'a' + 'A');
    }
  }
}

void ds_str_to_lower(ds_str_t *s) {
  size_t i, n;
  if (!s) {
    return;
  }
  n = s->len;
  for (i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)s->buf[i];
    if (c >= 'A' && c <= 'Z') {
      s->buf[i] = (char)(c - 'A' + 'a');
    }
  }
}

ds_status_t ds_str_swap_ex(ds_str_t *a, ds_str_t *b) {
  char *abuf, *bbuf, *tmp;
  size_t len, cap;
  bool compatible;
  if (a == NULL || b == NULL) {
    return DS_ERR_NULL;
  }
  if (a == b) {
    return DS_OK;
  }
  compatible = ds_context_compatible(a->context, b->context);
  if (compatible) {
    tmp = a->buf;
    a->buf = b->buf;
    b->buf = tmp;
  } else {
    abuf = NULL;
    bbuf = NULL;
    if (a->cap == (size_t)-1 || b->cap == (size_t)-1) {
      return DS_ERR_OVERFLOW;
    }
    if (b->buf != NULL) {
      abuf = (char *)ds_context_alloc(a->context, b->cap + 1);
      if (abuf == NULL) {
        return DS_ERR_ALLOC;
      }
      memcpy(abuf, b->buf, b->len + 1);
    }
    if (a->buf != NULL) {
      bbuf = (char *)ds_context_alloc(b->context, a->cap + 1);
      if (bbuf == NULL) {
        if (abuf != NULL) {
          ds_context_free(a->context, abuf);
        }
        return DS_ERR_ALLOC;
      }
      memcpy(bbuf, a->buf, a->len + 1);
    }
    if (a->buf != NULL) {
      ds_context_free(a->context, a->buf);
    }
    if (b->buf != NULL) {
      ds_context_free(b->context, b->buf);
    }
    a->buf = abuf;
    b->buf = bbuf;
  }
  len = a->len;
  a->len = b->len;
  b->len = len;
  cap = a->cap;
  a->cap = b->cap;
  b->cap = cap;
  return DS_OK;
}
void ds_str_swap(ds_str_t *a, ds_str_t *b) { (void)ds_str_swap_ex(a, b); }

int ds_str_clear_freebuf(ds_str_t *s) {
  if (!s) {
    return -1;
  }
  if (s->buf) {
    ds_context_free(s->context, s->buf);
  }
  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
  return 0;
}

int ds_str_append_vfmt(ds_str_t *s, const char *fmt, va_list ap) {
  va_list ap2;
  int need;
  if (!s || !fmt) {
    return -1;
  }

  DS_VA_COPY(ap2, ap);
  need = vsnprintf(NULL, 0, fmt, ap2);
  va_end(ap2);
  if (need < 0) {
    return -1;
  }

  /* formatted apart, then appended: an argument may point into s */
  if ((size_t)need < STR_STAGE_BYTES) {
    char stage[STR_STAGE_BYTES];
    if (vsnprintf(stage, sizeof(stage), fmt, ap) != need) {
      return -1;
    }
    return ds_str_append(s, stage, (size_t)need);
  } else {
    char *tmp = (char *)ds_context_alloc(s->context, (size_t)need + 1u);
    int r;
    if (!tmp) {
      return -1;
    }
    r = vsnprintf(tmp, (size_t)need + 1u, fmt, ap) == need
            ? ds_str_append(s, tmp, (size_t)need)
            : -1;
    ds_context_free(s->context, tmp);
    return r;
  }
}

int ds_str_reserve_exact(ds_str_t *s, size_t cap) {
  int r;
  if (!s) {
    return -1;
  }
  r = ds__ensure_exact(s, cap);
  return r;
}

int ds_str_append_fmt(ds_str_t *s, const char *fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = ds_str_append_vfmt(s, fmt, ap);
  va_end(ap);
  return r;
}

int ds_str_assign(ds_str_t *s, const void *data, size_t n) {
  int r;
  char *old;
  if (!s) {
    return -1;
  }
  if (!data && n) {
    return -1;
  }
  r = str_grow_keep(s, n, &old);
  if (r != 0) {
    return -1;
  }
  if (n) { /* data may be a part of s itself */
    memmove(s->buf, data, n);
  }
  s->len = n;
  if (s->buf) { /* assigning "" to a string that never had a buffer */
    s->buf[s->len] = '\0';
  }
  str_release(s, old);
  return 0;
}

int ds_str_pop(ds_str_t *s, int *out_ch) {
  unsigned char c;
  if (!s) {
    return -1;
  }
  if (s->len == 0u) {
    return -1;
  }
  c = (unsigned char)s->buf[s->len - 1u];
  s->len -= 1u;
  s->buf[s->len] = '\0';
  if (out_ch) {
    *out_ch = (int)c;
  }
  return 0;
}

ds_str_t *ds_str_slice(ds_str_t *s, size_t pos, size_t n) {
  ds_str_t *out;
  size_t len, start, take;
  if (!s) {
    return NULL;
  }
  len = s->len;
  start = pos > len ? len : pos;
  take = (n > len - start) ? (len - start) : n;
  out = str_create_internal(s->context);
  if (!out) {
    return NULL;
  }

  if (ds_str_assign(out, take != 0 ? s->buf + start : NULL, take) != 0) {
    ds_str_destroy(out);
    return NULL;
  }
  len = s->len;
  if (start > len) {
    start = len;
  }
  if (take > len - start) {
    take = len - start;
  }
  if (take) {
    memcpy(out->buf, s->buf + start, take);
  }

  out->len = take;
  if (out->buf != NULL) {
    out->buf[take] = '\0';
  }
  return out;
}

char *ds_str_detach(ds_str_t *s, size_t *len_out, size_t *cap_out) {
  char *p;
  if (!s) {
    return NULL;
  }
  p = s->buf;
  if (len_out) {
    *len_out = s->len;
  }
  if (cap_out) {
    *cap_out = s->cap;
  }
  s->buf = NULL;
  s->len = 0u;
  s->cap = 0u;
  return p;
}

int ds_str_adopt(ds_str_t *s, char *buf, size_t len, size_t cap) {
  if (!s) {
    return -1;
  }
  if (!buf && (len != 0u || cap != 0u)) {
    return -1;
  }
  if (buf && cap < len) {
    return -1;
  }
  if (cap == (size_t)-1 || (buf == s->buf && cap > s->cap)) {
    return -1;
  }
  if (s->buf && s->buf != buf) {
    ds_context_free(s->context, s->buf);
  }
  s->buf = buf;
  s->len = len;
  s->cap = cap;
  if (s->buf) {
    s->buf[s->len] = '\0';
  }
  return 0;
}

void ds_str_free_external(ds_str_t *owner, void *p) {
  if (!owner || !p) {
    return;
  }
  ds_context_free(owner->context, p);
}

int ds_str_adopt_safe(ds_str_t *s, char *buf, size_t len, size_t cap,
                      int has_room_for_nul) {
  if (s == NULL || (buf == NULL && (len != 0 || cap != 0)) || cap < len) {
    return -1;
  }
  if (buf != NULL && !has_room_for_nul && len == cap) {
    return ds_str_assign(s, buf,
                         len); /* copies; caller retains external buf */
  }
  return ds_str_adopt(s, buf, len, cap);
}

ds_status_t ds_str_reserve_ex(ds_str_t *s, size_t capacity) {
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  return ds_str_reserve(s, capacity) == 0 ? DS_OK : DS_ERR_ALLOC;
}
ds_status_t ds_str_assign_ex(ds_str_t *s, const void *data, size_t length) {
  if (s == NULL || (data == NULL && length != 0)) {
    return DS_ERR_NULL;
  }
  if (length == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  return ds_str_assign(s, data, length) == 0 ? DS_OK : DS_ERR_ALLOC;
}
ds_status_t ds_str_append_ex(ds_str_t *s, const void *data, size_t length) {
  if (s == NULL || (data == NULL && length != 0)) {
    return DS_ERR_NULL;
  }
  if (length >= (size_t)-1 - s->len) {
    return DS_ERR_OVERFLOW;
  }
  return ds_str_append(s, data, length) == 0 ? DS_OK : DS_ERR_ALLOC;
}
ds_status_t ds_str_insert_ex(ds_str_t *s, size_t position, const void *data,
                             size_t length) {
  if (s == NULL || (data == NULL && length != 0)) {
    return DS_ERR_NULL;
  }
  if (position > s->len) {
    return DS_ERR_RANGE;
  }
  if (length >= (size_t)-1 - s->len) {
    return DS_ERR_OVERFLOW;
  }
  return ds_str_insert(s, position, data, length) == 0 ? DS_OK : DS_ERR_ALLOC;
}
ds_status_t ds_str_erase_ex(ds_str_t *s, size_t position, size_t length) {
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  if (position > s->len || length > s->len - position) {
    return DS_ERR_RANGE;
  }
  (void)ds_str_erase(s, position, length);
  return DS_OK;
}
ds_status_t ds_str_pop_ex(ds_str_t *s, int *out) {
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (s->len == 0) {
    return DS_EMPTY;
  }
  (void)ds_str_pop(s, out);
  return DS_OK;
}
ds_status_t ds_str_clone_with(const ds_str_t *s, bool keep_capacity,
                              ds_context_t *context, ds_str_t **out) {
  ds_str_t *copy;
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  copy = str_create_internal(context != NULL ? context : s->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_str_reserve_ex(copy, keep_capacity ? s->cap : s->len);
  if (st == DS_OK) {
    st = ds_str_assign_ex(copy, s->buf, s->len);
  }
  if (st != DS_OK) {
    ds_str_destroy(copy);
    return st;
  }
  *out = copy;
  return DS_OK;
}
