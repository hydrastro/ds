#include "str_unicode.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

int ds__snapshot_src(const ds_str_t *src, char **out_buf, size_t *out_len) {
  char *tmp;
  size_t n;

  if (!src || !out_buf || !out_len) return -1;

#ifdef DS_THREAD_SAFE
  #if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wcast-qual"
  #endif
  if (src->is_thread_safe) mutex_lock(&((ds_str_t*)src)->lock);
  #if defined(__GNUC__)
  #pragma GCC diagnostic pop
  #endif
#endif

  n = src->len;
  tmp = (char*)malloc(n ? n : 1u);
  if (!tmp) {
#ifdef DS_THREAD_SAFE
  #if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wcast-qual"
  #endif
    if (src->is_thread_safe) mutex_unlock(&((ds_str_t*)src)->lock);
  #if defined(__GNUC__)
  #pragma GCC diagnostic pop
  #endif
#endif
    return -1;
  }
  if (n) memcpy(tmp, src->buf, n);

#ifdef DS_THREAD_SAFE
  #if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wcast-qual"
  #endif
  if (src->is_thread_safe) mutex_unlock(&((ds_str_t*)src)->lock);
  #if defined(__GNUC__)
  #pragma GCC diagnostic pop
  #endif
#endif

  *out_buf = tmp;
  *out_len = n;
  return 0;
}

int ds__is_nonchar(unsigned long cp) {
  unsigned long lo16 = cp & 0xFFFFul;
  if ((cp >= 0xFDD0ul && cp <= 0xFDEFul) ||
      lo16 == 0xFFFEul || lo16 == 0xFFFFul) return 1;
  return 0;
}

size_t ds__u8_decode(const unsigned char *p, const unsigned char *end,
                            unsigned long *out_cp, unsigned int flags) {
  unsigned char b0;
  unsigned long cp;
  size_t need;

  if (p >= end) return 0;
  b0 = *p;
  if (b0 < 0x80u) {
    cp = (unsigned long)b0;
    need = 1u;
  } else if ((b0 >> 5) == 0x6u) {
    if (end - p < 2) return 0;
    if ((p[1] & 0xC0u) != 0x80u) return 0;
    cp = ((unsigned long)(b0 & 0x1Fu) << 6) |
         ((unsigned long)(p[1] & 0x3Fu));
    if ((flags & DS_U8_REJECT_OVERLONG) && cp < 0x80ul) return 0;
    need = 2u;
  } else if ((b0 >> 4) == 0xEu) {
    if (end - p < 3) return 0;
    if ((p[1] & 0xC0u) != 0x80u || (p[2] & 0xC0u) != 0x80u) return 0;
    cp = ((unsigned long)(b0 & 0x0Fu) << 12) |
         ((unsigned long)(p[1] & 0x3Fu) << 6) |
         ((unsigned long)(p[2] & 0x3Fu));
    if ((flags & DS_U8_REJECT_OVERLONG) && cp < 0x800ul) return 0;
    if ((flags & DS_U8_REJECT_SURROGATE) && (cp >= 0xD800ul && cp <= 0xDFFFul)) return 0;
    need = 3u;
  } else if ((b0 >> 3) == 0x1Eu) {
    if (end - p < 4) return 0;
    if ((p[1] & 0xC0u) != 0x80u || (p[2] & 0xC0u) != 0x80u || (p[3] & 0xC0u) != 0x80u) return 0;
    cp = ((unsigned long)(b0 & 0x07u) << 18) |
         ((unsigned long)(p[1] & 0x3Fu) << 12) |
         ((unsigned long)(p[2] & 0x3Fu) << 6) |
         ((unsigned long)(p[3] & 0x3Fu));
    if ((flags & DS_U8_REJECT_OVERLONG) && cp < 0x10000ul) return 0;
    if ((flags & DS_U8_REJECT_OUT_OF_RANGE) && cp > 0x10FFFFul) return 0;
    need = 4u;
  } else {
    return 0;
  }

  if ((flags & DS_U8_REJECT_OUT_OF_RANGE) && cp > 0x10FFFFul) return 0;
  if ((flags & DS_U8_REJECT_NONCHAR) && ds__is_nonchar(cp)) return 0;

  if (out_cp) *out_cp = cp;
  return need;
}

size_t ds__u8_encode(unsigned long cp, unsigned char out[4]) {
  if (cp <= 0x7Ful) {
    out[0] = (unsigned char)cp; return 1u;
  } else if (cp <= 0x7FFul) {
    out[0] = (unsigned char)(0xC0u | ((cp >> 6) & 0x1Fu));
    out[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 2u;
  } else if (cp >= 0xD800ul && cp <= 0xDFFFul) {
    return 0;
  } else if (cp <= 0xFFFFul) {
    out[0] = (unsigned char)(0xE0u | ((cp >> 12) & 0x0Fu));
    out[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 3u;
  } else if (cp <= 0x10FFFFul) {
    out[0] = (unsigned char)(0xF0u | ((cp >> 18) & 0x07u));
    out[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 4u;
  }
  return 0;
}

size_t ds__u8_back_to_start(const char *buf, size_t len, size_t i) {
  size_t k;
  if (i > len) i = len;
  k = i;
  while (k > 0 && ((unsigned char)buf[k - 1] & 0xC0u) == 0x80u) {
    --k;
    if (i - k >= 3u) break;
  }
  return k;
}

int FUNC(str_u8_push_cp)(ds_str_t *s, unsigned long cp) {
  unsigned char tmp[4];
  size_t n;
  if (!s) return -1;
  n = ds__u8_encode(cp, tmp);
  if (n == 0u) return -1;
  return FUNC(str_append)(s, tmp, n);
}

int FUNC(str_u8_pop_cp)(ds_str_t *s, unsigned long *out_cp) {
  size_t i, start;
  unsigned long cp;
  size_t n;
  const unsigned char *p;
  if (!s) return -1;

#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  if (s->len == 0u) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return -1;
  }

  i = s->len;
  start = ds__u8_back_to_start(s->buf, s->len, i);
  if (start >= s->len) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return -1;
  }
  p = (const unsigned char*)s->buf + start;
  n = ds__u8_decode(p, (const unsigned char*)s->buf + s->len, &cp, DS_U8_STRICT);
  if (n == 0u || start + n != s->len) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return -1;
  }
  s->len = start;
  s->buf[s->len] = '\0';
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  if (out_cp) *out_cp = cp;
  return 0;
}

size_t FUNC(str_u8_count)(const ds_str_t *s, unsigned int flags, int *valid_out) {
  char *b;
  size_t n, off = 0u, count = 0u;
  int valid = 1;
  size_t used;

  if (!s) {
    if (valid_out) *valid_out = 1;
    return 0u;
  }
  if (ds__snapshot_src(s, &b, &n) != 0) {
    if (valid_out) *valid_out = 0;
    return 0u;
  }
  while (off < n) {
    used = ds__u8_decode((const unsigned char*)b + off, (const unsigned char*)b + n, NULL, flags);
    if (used == 0u) { valid = 0; break; }
    off += used;
    ++count;
  }
  free(b);
  if (valid_out) *valid_out = valid;
  return count;
}

size_t FUNC(str_view_u8_count)(ds_str_view_t v, unsigned int flags, int *valid_out) {
  size_t off = 0u, count = 0u, used;
  int valid = 1;
  while (off < v.len) {
    used = ds__u8_decode((const unsigned char*)v.data + off, (const unsigned char*)v.data + v.len, NULL, flags);
    if (used == 0u) { valid = 0; break; }
    off += used;
    ++count;
  }
  if (valid_out) *valid_out = valid;
  return count;
}

int FUNC(str_u8_validate)(const ds_str_t *s, unsigned int flags) {
  int valid;
  (void)FUNC(str_u8_count)(s, flags, &valid);
  return valid;
}

int FUNC(str_view_u8_validate)(ds_str_view_t v, unsigned int flags) {
  int valid;
  (void)FUNC(str_view_u8_count)(v, flags, &valid);
  return valid;
}

int FUNC(str_u8_next)(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t off, used;
  if (!buf || !ioff) return 0;
  off = *ioff;
  if (off >= len) return 0;
  used = ds__u8_decode((const unsigned char*)buf + off, (const unsigned char*)buf + len, out_cp, flags);
  if (used == 0u) return 0;
  *ioff = off + used;
  return 1;
}

int FUNC(str_u8_prev)(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t i, start, used;
  if (!buf || !ioff) return 0;
  i = *ioff;
  if (i == 0u || i > len) return 0;
  start = ds__u8_back_to_start(buf, len, i);
  if (start >= i) return 0;
  used = ds__u8_decode((const unsigned char*)buf + start, (const unsigned char*)buf + len, out_cp, flags);
  if (used == 0u || start + used != i) return 0;
  *ioff = start;
  return 1;
}

size_t FUNC(str_u8_cp_to_byte)(const ds_str_t *s, size_t cp_index, unsigned int flags) {
  char *b;
  size_t n, off = 0u, used, i = 0u;

  if (!s) return 0u;
  if (ds__snapshot_src(s, &b, &n) != 0) return 0u;

  while (off < n && i < cp_index) {
    used = ds__u8_decode((const unsigned char*)b + off, (const unsigned char*)b + n, NULL, flags);
    if (used == 0u) break;
    off += used;
    ++i;
  }
  if (i < cp_index) off = n;
  free(b);
  return off;
}

int FUNC(str_u8_slice_cp)(ds_str_t *dst, const ds_str_t *src, size_t start_cp, size_t count_cp, unsigned int flags) {
  char *b;
  size_t n, off, used, i, start_byte, end_byte;

  if (!dst || !src) return -1;
  if (ds__snapshot_src(src, &b, &n) != 0) return -1;

  off = 0u; i = 0u;
  while (off < n && i < start_cp) {
    used = ds__u8_decode((const unsigned char*)b + off, (const unsigned char*)b + n, NULL, flags);
    if (used == 0u) break;
    off += used; ++i;
  }
  start_byte = off;

  if (count_cp == (size_t)SIZE_MAX) {
    end_byte = n;
  } else {
    i = 0u;
    while (off < n && i < count_cp) {
      used = ds__u8_decode((const unsigned char*)b + off, (const unsigned char*)b + n, NULL, flags);
      if (used == 0u) break;
      off += used; ++i;
    }
    end_byte = off;
  }

  if (start_byte > n) start_byte = n;
  if (end_byte > n) end_byte = n;
  if (end_byte < start_byte) end_byte = start_byte;

  if (FUNC(str_assign)(dst, b + start_byte, end_byte - start_byte) != 0) {
    free(b);
    return -1;
  }
  free(b);
  return 0;
}

int FUNC(str_u8_sanitize)(ds_str_t *dst, const ds_str_t *src, unsigned int flags) {
  char *b;
  size_t n, off = 0u;
  ds_str_t *out;
  unsigned char enc[4];
  unsigned long cp;
  size_t used, w;

  if (!dst || !src) return -1;
  if (ds__snapshot_src(src, &b, &n) != 0) return -1;

  out = FUNC(str_create_alloc)(src->allocator, src->deallocator);
  if (!out) { free(b); return -1; }
  if (n && FUNC(str_reserve)(out, n) != 0) { FUNC(str_destroy)(out); free(b); return -1; }

  while (off < n) {
    used = ds__u8_decode((const unsigned char*)b + off, (const unsigned char*)b + n, &cp, flags);
    if (used == 0u) {
      w = ds__u8_encode(DS_U8_REPLACEMENT_CHAR, enc);
      if (!w || FUNC(str_append)(out, enc, w) != 0) { FUNC(str_destroy)(out); free(b); return -1; }
      ++off;
      continue;
    }
    if (FUNC(str_append)(out, b + off, used) != 0) { FUNC(str_destroy)(out); free(b); return -1; }
    off += used;
  }

#ifdef DS_THREAD_SAFE
  LOCK(dst)
#endif
  if (dst->buf) dst->deallocator(dst->buf);
  dst->buf = out->buf;
  dst->len = out->len;
  dst->cap = out->cap;
  if (dst->buf) dst->buf[dst->len] = '\0';
  out->buf = NULL; out->len = 0u; out->cap = 0u;
#ifdef DS_THREAD_SAFE
  UNLOCK(dst)
#endif
  FUNC(str_destroy)(out);
  free(b);
  return 0;
}

int FUNC(str_u8_strip_bom)(ds_str_t *s) {
  int stripped = 0;
  if (!s || s->len < 3u) return 0;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  if (s->len >= 3u &&
      (unsigned char)s->buf[0] == 0xEFu &&
      (unsigned char)s->buf[1] == 0xBBu &&
      (unsigned char)s->buf[2] == 0xBFu) {
    memmove(s->buf, s->buf + 3, s->len - 3u);
    s->len -= 3u;
    s->buf[s->len] = '\0';
    stripped = 1;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return stripped;
}

int ds__ascii_map_into(ds_str_t *dst, const ds_str_t *src, int tolower_mode) {
  char *b;
  size_t n, i;
  ds_str_t *out;

  if (!dst || !src) return -1;
  if (ds__snapshot_src(src, &b, &n) != 0) return -1;

  out = FUNC(str_create_alloc)(src->allocator, src->deallocator);
  if (!out) { free(b); return -1; }
  if (n && FUNC(str_reserve)(out, n) != 0) { FUNC(str_destroy)(out); free(b); return -1; }

  for (i = 0u; i < n; ++i) {
    unsigned char c = (unsigned char)b[i];
    if (tolower_mode) {
      if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
    } else {
      if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    }
    if (FUNC(str_pushc)(out, (int)c) != 0) { FUNC(str_destroy)(out); free(b); return -1; }
  }

#ifdef DS_THREAD_SAFE
  LOCK(dst)
#endif
  if (dst->buf) dst->deallocator(dst->buf);
  dst->buf = out->buf;
  dst->len = out->len;
  dst->cap = out->cap;
  if (dst->buf) dst->buf[dst->len] = '\0';
  out->buf = NULL; out->len = 0u; out->cap = 0u;
#ifdef DS_THREAD_SAFE
  UNLOCK(dst)
#endif
  FUNC(str_destroy)(out);
  free(b);
  return 0;
}

int FUNC(str_u8_ascii_tolower)(ds_str_t *dst, const ds_str_t *src) { return ds__ascii_map_into(dst, src, 1); }
int FUNC(str_u8_ascii_toupper)(ds_str_t *dst, const ds_str_t *src) { return ds__ascii_map_into(dst, src, 0); }
int FUNC(str_u8_ascii_casefold)(ds_str_t *dst, const ds_str_t *src) { return ds__ascii_map_into(dst, src, 1); }

int FUNC(str_view_u8_next)(ds_str_view_t v, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t off, used;
  if (!ioff) return 0;
  off = *ioff;
  if (off >= v.len) return 0;
  used = ds__u8_decode((const unsigned char*)v.data + off, (const unsigned char*)v.data + v.len, out_cp, flags);
  if (used == 0u) return 0;
  *ioff = off + used;
  return 1;
}

int FUNC(str_view_u8_prev)(ds_str_view_t v, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t i, start, used;
  if (!ioff) return 0;
  i = *ioff;
  if (i == 0u || i > v.len) return 0;
  start = ds__u8_back_to_start(v.data, v.len, i);
  if (start >= i) return 0;
  used = ds__u8_decode((const unsigned char*)v.data + start, (const unsigned char*)v.data + v.len, out_cp, flags);
  if (used == 0u || start + used != i) return 0;
  *ioff = start;
  return 1;
}
