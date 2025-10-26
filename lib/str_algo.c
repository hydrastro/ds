#include "str_algo.h"
#include <string.h>
#include <stdlib.h>

int ds__in_set(unsigned char c, const unsigned char *set, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    if (c == set[i]) return 1;
  }
  return 0;
}

long FUNC(str_find_bmh)(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h, *pat = (const unsigned char*)needle;
  size_t i, len;
  unsigned char skip[256];
  if (!s || !needle || n == 0u) return -1;
  len = s->len;
  if (start > len) return -1;
  if (n == 1u) {
    const void *p = memchr(s->buf + start, *pat, len - start);
    return p ? (long)((const unsigned char*)p - (const unsigned char*)s->buf) : -1;
  }
  for (i = 0; i < 256; ++i) skip[i] = (unsigned char)n;
  for (i = 0; i < n - 1; ++i) skip[pat[i]] = (unsigned char)(n - 1 - i);
  h = (const unsigned char*)s->buf;
  i = start;
  while (i + n <= len) {
    unsigned char last = h[i + n - 1];
    if (last == pat[n - 1] && memcmp(h + i, pat, n) == 0) return (long)i;
    i += skip[last];
  }
  return -1;
}

int FUNC(str_ltrim_set)(ds_str_t *s, const unsigned char *set, size_t set_n) {
  size_t i = 0u, len;
  if (!s) return -1;
  len = s->len;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  while (i < len && ds__in_set((unsigned char)s->buf[i], set, set_n)) ++i;
  if (i) {
    memmove(s->buf, s->buf + i, len - i);
    s->len = len - i;
    s->buf[s->len] = '\0';
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return 0;
}

int FUNC(str_rtrim_set)(ds_str_t *s, const unsigned char *set, size_t set_n) {
  size_t len;
  if (!s) return -1;
  len = s->len;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  while (len && ds__in_set((unsigned char)s->buf[len - 1], set, set_n)) --len;
  s->len = len;
  s->buf[len] = '\0';
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return 0;
}

int FUNC(str_trim_set)(ds_str_t *s, const unsigned char *set, size_t set_n) {
  if (FUNC(str_rtrim_set)(s, set, set_n) != 0) return -1;
  return FUNC(str_ltrim_set)(s, set, set_n);
}

void FUNC(str_split_each_c)(ds_str_t *s, unsigned char sep, ds_str_token_cb cb, void *ud) {
  size_t i = 0u, start = 0u, len;
  if (!s || !cb) return;
  len = s->len;
  for (i = 0; i < len; ++i) {
    if ((unsigned char)s->buf[i] == sep) { cb(s->buf + start, i - start, ud); start = i + 1; }
  }
  cb(s->buf + start, len - start, ud);
}

int FUNC(str_join_c)(ds_str_t *dst, ds_str_t **parts, size_t count, const void *sep, size_t nsep) {
  size_t i;
  if (!dst) return -1;
  if (!parts || count == 0) { return 0; }
#ifdef DS_THREAD_SAFE
  LOCK(dst)
#endif
  for (i = 0; i < count; ++i) {
    if (i) {
      if (FUNC(str_append)(dst, sep, nsep) != 0) {
#ifdef DS_THREAD_SAFE
        UNLOCK(dst)
#endif
        return -1;
      }
    }
    if (parts[i] && parts[i]->len) {
      if (FUNC(str_append)(dst, parts[i]->buf, parts[i]->len) != 0) {
#ifdef DS_THREAD_SAFE
        UNLOCK(dst)
#endif
        return -1;
      }
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(dst)
#endif
  return 0;
}
