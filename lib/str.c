#include "str.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
  if (!s)
    return -1;
  if (s->cap >= need_cap)
    return 0;
  need_cap = ds__grow_cap(s->cap, need_cap);
  if (need_cap > SIZE_MAX - 1u)
    return -1;
  bytes = need_cap + 1u;
  if (s->buf == NULL) {
    nbuf = (char *)s->allocator(bytes);
    if (!nbuf)
      return -1;
    nbuf[0] = '\0';
  } else {
    char *tmp = (char *)s->allocator(bytes);
    size_t copy = s->len;
    if (!tmp)
      return -1;
    if (copy)
      memcpy(tmp, s->buf, copy);
    tmp[copy] = '\0';
    s->deallocator(s->buf);
    nbuf = tmp;
  }
  s->buf = nbuf;
  s->cap = need_cap;
  return 0;
}

ds_str_t *FUNC(str_create)(void) {
  return FUNC(str_create_alloc)(malloc, free);
}

ds_str_t *FUNC(str_create_alloc)(void *(*allocator)(size_t),
                                 void (*deallocator)(void *)) {
  ds_str_t *s = (ds_str_t *)allocator(sizeof(ds_str_t));
  if (!s)
    return NULL;
  s->allocator = allocator;
  s->deallocator = deallocator;
  s->buf = NULL;
  s->len = 0u;
  s->cap = 0u;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(s)
#endif
  return s;
}

ds_str_t *FUNC(str_with_capacity)(size_t cap) {
  ds_str_t *s = FUNC(str_create)();
  if (!s)
    return NULL;
  if (ds__ensure(s, cap) != 0) {
    FUNC(str_destroy)(s);
    return NULL;
  }
  return s;
}

ds_str_t *FUNC(str_from)(const void *data, size_t n) {
  ds_str_t *s = FUNC(str_with_capacity)(n);
  if (!s)
    return NULL;
  if (n)
    memcpy(s->buf, data, n);
  s->len = n;
  s->buf[n] = '\0';
  return s;
}

void FUNC(str_destroy)(ds_str_t *s) {
  if (!s)
    return;
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(s);
#endif
  if (s->buf)
    s->deallocator(s->buf);
  s->deallocator(s);
}

void FUNC(str_clear)(ds_str_t *s) {
  if (!s)
    return;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  s->len = 0u;
  if (s->buf)
    s->buf[0] = '\0';
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
}

void FUNC(str_delete)(ds_str_t *s) { FUNC(str_clear)(s); }

int FUNC(str_reserve)(ds_str_t *s, size_t need) {
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  {
    int r = ds__ensure(s, need);
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return r;
  }
}

int FUNC(str_shrink_to_fit)(ds_str_t *s) {
  char *nbuf;
  size_t bytes;
  if (!s)
    return -1;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  if (s->cap == s->len) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return 0;
  }
  bytes = s->len + 1u;
  if (bytes == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return -1;
  }
  nbuf = (char *)s->allocator(bytes);
  if (!nbuf) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return -1;
  }
  if (s->len)
    memcpy(nbuf, s->buf, s->len);
  nbuf[s->len] = '\0';
  if (s->buf)
    s->deallocator(s->buf);
  s->buf = nbuf;
  s->cap = s->len;
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return 0;
}

int FUNC(str_append)(ds_str_t *s, const void *data, size_t n) {
  if (!s || (!data && n))
    return -1;
  if (n == 0u)
    return 0;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  {
    size_t need_len = s->len + n;
    if (need_len < s->len) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return -1;
    }
    if (ds__ensure(s, need_len) != 0) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return -1;
    }
    memcpy(s->buf + s->len, data, n);
    s->len = need_len;
    s->buf[s->len] = '\0';
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return 0;
  }
}

int FUNC(str_append_cstr)(ds_str_t *s, const char *cstr) {
  size_t n = cstr ? strlen(cstr) : 0u;
  return FUNC(str_append)(s, cstr, n);
}

int FUNC(str_pushc)(ds_str_t *s, int c) {
  unsigned char ch = (unsigned char)(c & 0xFF);
  return FUNC(str_append)(s, &ch, 1u);
}

int FUNC(str_insert)(ds_str_t *s, size_t pos, const void *data, size_t n) {
  if (!s || (!data && n))
    return -1;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  {
    size_t len = s->len;
    if (pos > len)
      pos = len;
    if (n == 0u) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return 0;
    }
    if (ds__ensure(s, len + n) != 0) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return -1;
    }
    memmove(s->buf + pos + n, s->buf + pos, len - pos);
    memcpy(s->buf + pos, data, n);
    s->len = len + n;
    s->buf[s->len] = '\0';
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return 0;
  }
}

int FUNC(str_erase)(ds_str_t *s, size_t pos, size_t n) {
  if (!s)
    return -1;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  {
    size_t len = s->len;
    if (pos > len) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return 0;
    }
    if (n > len - pos)
      n = len - pos;
    if (n == 0u) {
#ifdef DS_THREAD_SAFE
      UNLOCK(s)
#endif
      return 0;
    }
    memmove(s->buf + pos, s->buf + pos + n, len - pos - n);
    s->len = len - n;
    s->buf[s->len] = '\0';
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return 0;
  }
}

ds_str_t *FUNC(str_clone)(ds_str_t *s) {
  ds_str_t *n;
  if (!s)
    return NULL;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  n = FUNC(str_create_alloc)(s->allocator, s->deallocator);
  if (!n) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return NULL;
  }
  if (ds__ensure(n, s->len) != 0) {
    FUNC(str_destroy)(n);
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return NULL;
  }
  if (s->len)
    memcpy(n->buf, s->buf, s->len);
  n->len = s->len;
  if (n->buf)
    n->buf[n->len] = '\0';
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return n;
}

int FUNC(str_cmp)(ds_str_t *a, ds_str_t *b) {
  size_t la = a ? a->len : 0u, lb = b ? b->len : 0u, lm = la < lb ? la : lb;
  int r = lm ? memcmp(a ? a->buf : "", b ? b->buf : "", lm) : 0;
  if (r != 0)
    return r;
  if (la < lb)
    return -1;
  if (la > lb)
    return 1;
  return 0;
}
int FUNC(str_eq)(ds_str_t *a, ds_str_t *b) { return FUNC(str_cmp)(a, b) == 0; }

long FUNC(str_find)(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h;
  const unsigned char *nd = (const unsigned char *)needle;
  size_t i, len;
  if (!s || !needle || n == 0u)
    return -1;
  len = s->len;
  if (start > len)
    return -1;
  h = (const unsigned char *)s->buf;
  for (i = start; i + n <= len; ++i) {
    if (memcmp(h + i, nd, n) == 0)
      return (long)i;
  }
  return -1;
}
long FUNC(str_rfind)(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h;
  long i;
  size_t len;
  if (!s || !needle || n == 0u)
    return -1;
  len = s->len;
  if (start > len)
    start = len;
  if (start < n)
    return -1;
  h = (const unsigned char *)s->buf;
  for (i = (long)start - (long)n; i >= 0; --i) {
    if (memcmp(h + (size_t)i, needle, n) == 0)
      return i;
  }
  return -1;
}

void FUNC(str_to_upper)(ds_str_t *s) {
  size_t i, n;
  if (!s)
    return;
  n = s->len;
  for (i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)s->buf[i];
    if (c >= 'a' && c <= 'z')
      s->buf[i] = (char)(c - 'a' + 'A');
  }
}
void FUNC(str_to_lower)(ds_str_t *s) {
  size_t i, n;
  if (!s)
    return;
  n = s->len;
  for (i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)s->buf[i];
    if (c >= 'A' && c <= 'Z')
      s->buf[i] = (char)(c - 'A' + 'a');
  }
}
