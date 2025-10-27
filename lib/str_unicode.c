#include "str_unicode.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

extern int ds__ucd_normalize(ds_str_t *dst, const ds_str_t *src, int compat, int compose);
extern int ds__ucd_casefold(ds_str_t *dst, const ds_str_t *src);
extern int ds__ucd_tolower(ds_str_t *dst, const ds_str_t *src);
extern int ds__ucd_toupper(ds_str_t *dst, const ds_str_t *src);
extern long ds__ucd_grapheme_len(const ds_str_t *s);

int ds__is_nonchar(unsigned long cp) {
  if (cp >= 0xFDD0ul && cp <= 0xFDEFul) return 1;
  if ((cp & 0xFFFEul) == 0xFFFEul && cp <= 0x10FFFFul) return 1;
  return 0;
}

int ds__decode_one(const unsigned char *s, size_t n, size_t *adv,
                          unsigned long *out, unsigned int flags)
{
  unsigned long cp; size_t need;

  if (n == 0) return 0;
  if (s[0] < 0x80u) {
    *adv = 1; cp = s[0];
  } else if ((s[0] & 0xE0u) == 0xC0u) {
    if (n < 2 || (s[1] & 0xC0u) != 0x80u) return -1;
    cp = ((unsigned long)(s[0] & 0x1Fu) << 6) | (unsigned long)(s[1] & 0x3Fu);
    *adv = 2; if ((flags & DS_U8_REJECT_OVERLONG) && cp < 0x80ul) return -1;
  } else if ((s[0] & 0xF0u) == 0xE0u) {
    if (n < 3 || (s[1] & 0xC0u) != 0x80u || (s[2] & 0xC0u) != 0x80u) return -1;
    cp = ((unsigned long)(s[0] & 0x0Fu) << 12)
       | ((unsigned long)(s[1] & 0x3Fu) << 6)
       |  (unsigned long)(s[2] & 0x3Fu);
    *adv = 3;
    if ((flags & DS_U8_REJECT_OVERLONG) && cp < 0x800ul) return -1;
    if ((flags & DS_U8_REJECT_SURROGATE) && cp >= 0xD800ul && cp <= 0xDFFFul) return -1;
  } else if ((s[0] & 0xF8u) == 0xF0u) {
    if (n < 4 || (s[1] & 0xC0u) != 0x80u || (s[2] & 0xC0u) != 0x80u || (s[3] & 0xC0u) != 0x80u) return -1;
    cp = ((unsigned long)(s[0] & 0x07u) << 18)
       | ((unsigned long)(s[1] & 0x3Fu) << 12)
       | ((unsigned long)(s[2] & 0x3Fu) << 6)
       |  (unsigned long)(s[3] & 0x3Fu);
    *adv = 4;
    if ((flags & DS_U8_REJECT_OVERLONG) && cp < 0x10000ul) return -1;
    if ((flags & DS_U8_REJECT_OUT_OF_RANGE) && cp > 0x10FFFFul) return -1;
  } else {
    return -1;
  }

  if ((flags & DS_U8_REJECT_OUT_OF_RANGE) && cp > 0x10FFFFul) return -1;
  if ((flags & DS_U8_REJECT_NONCHAR) && ds__is_nonchar(cp)) return -1;

  if (out) *out = cp;
  (void)need;
  return 1;
}

size_t ds__encode_one(unsigned long cp, unsigned char out[4]) {
  if (cp <= 0x7Ful) { out[0] = (unsigned char)cp; return 1; }
  if (cp <= 0x7FFul) {
    out[0] = (unsigned char)(0xC0u | (cp >> 6));
    out[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 2;
  }
  if (cp <= 0xFFFFul) {
    out[0] = (unsigned char)(0xE0u | (cp >> 12));
    out[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 3;
  }
  out[0] = (unsigned char)(0xF0u | (cp >> 18));
  out[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
  out[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
  out[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
  return 4;
}

int FUNC(str_u8_push_cp)(ds_str_t *s, unsigned long cp) {
  unsigned char buf[4]; size_t k;
  if (!s) return -1;
  if (cp > 0x10FFFFul || (cp >= 0xD800ul && cp <= 0xDFFFul)) cp = DS_U8_REPLACEMENT_CHAR;
  k = ds__encode_one(cp, buf);
  return FUNC(str_append)(s, buf, k);
}

int FUNC(str_u8_pop_cp)(ds_str_t *s, unsigned long *out_cp) {
  size_t len; const unsigned char *p; size_t i = 0;
  if (!s || s->len == 0) return -1;
  len = s->len; p = (const unsigned char*)s->buf;
  i = len;
  while (i > 0) {
    unsigned char c = p[i-1];
    if ((c & 0xC0u) != 0x80u) {
      size_t adv; unsigned long cp;
      if (ds__decode_one(p + (i-1), len - (i-1), &adv, &cp, DS_U8_STRICT) <= 0) return -1;
      if (i - 1 + adv != len) return -1;
      if (out_cp) *out_cp = cp;
      return FUNC(str_erase)(s, i-1, adv);
    }
    --i;
    if (len - i > 4) break;
  }
  return -1;
}

size_t FUNC(str_u8_count)(const ds_str_t *s, unsigned int flags, int *valid_out) {
  size_t i = 0, n; size_t count = 0; const unsigned char *p;
  int ok = 1;
  if (!s) { if (valid_out) *valid_out = 0; return 0; }
  n = s->len; p = (const unsigned char*)s->buf;
  while (i < n) {
    size_t adv; int r = ds__decode_one(p + i, n - i, &adv, NULL, flags);
    if (r <= 0) { ok = 0; break; }
    ++count; i += adv;
  }
  if (valid_out) *valid_out = ok;
  return ok ? count : 0;
}

size_t FUNC(str_view_u8_count)(ds_str_view_t v, unsigned int flags, int *valid_out) {
  const unsigned char *p = (const unsigned char *)v.data;
  size_t i = 0, n = v.len, count = 0;
  int ok = 1;

  while (i < n) {
    size_t adv;
    int r = ds__decode_one(p + i, n - i, &adv, NULL, flags);
    if (r <= 0) { ok = 0; break; }
    ++count; i += adv;
  }
  if (valid_out) *valid_out = ok;
  return ok ? count : 0;
}

int FUNC(str_u8_validate)(const ds_str_t *s, unsigned int flags) {
  int ok; (void)FUNC(str_u8_count)(s, flags, &ok); return ok;
}

int FUNC(str_view_u8_validate)(ds_str_view_t v, unsigned int flags) {
  int ok; (void)FUNC(str_view_u8_count)(v, flags, &ok); return ok;
}

int FUNC(str_u8_next)(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t i = ioff ? *ioff : 0; size_t adv; int r;
  if (!buf) return -1;
  if (i >= len) return 0;
  r = ds__decode_one((const unsigned char*)buf + i, len - i, &adv, out_cp, flags);
  if (r <= 0) return -1;
  if (ioff) *ioff = i + adv;
  return 1;
}

int FUNC(str_u8_prev)(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t i = ioff ? *ioff : len;
  const unsigned char *p = (const unsigned char*)buf;
  if (!buf) return -1;
  if (i == 0) return 0;
  {
    size_t j = i;
    size_t start = j;
    size_t adv; unsigned long cp;
    if (j > len) j = len;
    while (j > 0 && (p[j-1] & 0xC0u) == 0x80u) --j;
    if (j == 0) return -1;
    start = j;
    if (ds__decode_one(p + start, len - start, &adv, &cp, flags) <= 0) return -1;
    if (start + adv != i) return -1;
    if (out_cp) *out_cp = cp;
    if (ioff) *ioff = start;
  }
  return 1;
}

size_t FUNC(str_u8_cp_to_byte)(const ds_str_t *s, size_t cp_index, unsigned int flags) {
  size_t i = 0, n; size_t cp = 0; const unsigned char *p;
  if (!s) return SIZE_MAX;
  n = s->len; p = (const unsigned char*)s->buf;
  while (i < n) {
    size_t adv; int r = ds__decode_one(p + i, n - i, &adv, NULL, flags);
    if (r <= 0) return SIZE_MAX;
    if (cp == cp_index) return i;
    ++cp; i += adv;
  }
  return (cp_index == cp) ? i : SIZE_MAX;
}

int FUNC(str_u8_slice_cp)(ds_str_t *dst, const ds_str_t *src, size_t start_cp, size_t count_cp, unsigned int flags) {
  size_t a, b;
  size_t start_byte = FUNC(str_u8_cp_to_byte)(src, start_cp, flags);
  if (start_byte == SIZE_MAX) return -1;

  if (count_cp == (size_t)-1) {
    a = start_byte; b = src->len;
  } else {
    size_t end_cp = start_cp + count_cp;
    size_t end_byte = FUNC(str_u8_cp_to_byte)(src, end_cp, flags);
    if (end_byte == SIZE_MAX) return -1;
    a = start_byte; b = end_byte;
  }

  if (dst == src) {
    ds_str_t *tmp = FUNC(str_from)(src->buf + a, b - a);
    if (!tmp) return -1;
    FUNC(str_clear)(dst);
    (void)FUNC(str_append)(dst, tmp->buf, tmp->len);
    FUNC(str_destroy)(tmp);
    return 0;
  }
  return FUNC(str_assign)(dst, src->buf + a, b - a);
}

int FUNC(str_u8_sanitize)(ds_str_t *dst, const ds_str_t *src, unsigned int flags) {
  size_t i = 0, n; const unsigned char *p; unsigned char buf[4];
  if (!dst || !src) return -1;
#ifdef DS_THREAD_SAFE
  if (dst) LOCK(dst);
#endif
  (void)FUNC(str_clear)(dst);
  n = src->len; p = (const unsigned char*)src->buf;
  while (i < n) {
    unsigned long cp; size_t adv; int r = ds__decode_one(p + i, n - i, &adv, &cp, flags);
    if (r <= 0) {
      size_t k = ds__encode_one(DS_U8_REPLACEMENT_CHAR, buf);
      if (FUNC(str_append)(dst, buf, k) != 0) { 
#ifdef DS_THREAD_SAFE
        if (dst) UNLOCK(dst);
#endif
        return -1; 
      }
      i += 1;
    } else {
      size_t k = ds__encode_one(cp, buf);
      if (FUNC(str_append)(dst, buf, k) != 0) { 
#ifdef DS_THREAD_SAFE
        if (dst) UNLOCK(dst);
#endif
        return -1; 
      }
      i += adv;
    }
  }
#ifdef DS_THREAD_SAFE
  if (dst) UNLOCK(dst);
#endif
  return 0;
}

int FUNC(str_u8_strip_bom)(ds_str_t *s) {
  const unsigned char bom[3] = {0xEFu, 0xBBu, 0xBFu};
  if (!s || s->len < 3) return 0;
  if ((unsigned char)s->buf[0] == bom[0] &&
      (unsigned char)s->buf[1] == bom[1] &&
      (unsigned char)s->buf[2] == bom[2]) {
    (void)FUNC(str_erase)(s, 0u, 3u);
    return 1;
  }
  return 0;
}

unsigned char ds__ascii_lower(unsigned char c) { return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32u) : c; }
unsigned char ds__ascii_upper(unsigned char c) { return (c >= 'a' && c <= 'z') ? (unsigned char)(c - 32u) : c; }

int FUNC(str_u8_ascii_tolower)(ds_str_t *dst, const ds_str_t *src) {
  size_t i, n;
  if (!dst || !src) return -1;
#ifdef DS_THREAD_SAFE
  if (dst) LOCK(dst);
#endif
  if (FUNC(str_reserve)(dst, src->len) != 0) { 
#ifdef DS_THREAD_SAFE
    if (dst) UNLOCK(dst);
#endif
    return -1; 
  }
  for (i = 0, n = src->len; i < n; ++i) dst->buf[i] = (char)ds__ascii_lower((unsigned char)src->buf[i]);
  dst->len = src->len; dst->buf[dst->len] = '\0';
#ifdef DS_THREAD_SAFE
  if (dst) UNLOCK(dst);
#endif
  return 0;
}

int FUNC(str_u8_ascii_toupper)(ds_str_t *dst, const ds_str_t *src) {
  size_t i, n;
  if (!dst || !src) return -1;
#ifdef DS_THREAD_SAFE
  if (dst) LOCK(dst);
#endif
  if (FUNC(str_reserve)(dst, src->len) != 0) { 
#ifdef DS_THREAD_SAFE
    if (dst) UNLOCK(dst);
#endif
    return -1; 
  }
  for (i = 0, n = src->len; i < n; ++i) dst->buf[i] = (char)ds__ascii_upper((unsigned char)src->buf[i]);
  dst->len = src->len; dst->buf[dst->len] = '\0';
#ifdef DS_THREAD_SAFE
  if (dst) UNLOCK(dst);
#endif
  return 0;
}

int FUNC(str_u8_ascii_casefold)(ds_str_t *dst, const ds_str_t *src) {
  return FUNC(str_u8_ascii_tolower)(dst, src);
}

unsigned char ds__ascii_fold(unsigned char c) { return ds__ascii_lower(c); }

long FUNC(str_find_ascii_folded)(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h, *nd = (const unsigned char*)needle; size_t i, len;
  if (!s || !needle || n == 0u) return -1;
  len = s->len; if (start > len) return -1;
  h = (const unsigned char*)s->buf;
  if (n == 1u) {
    unsigned char t = ds__ascii_fold(nd[0]);
    for (i = start; i < len; ++i) if (ds__ascii_fold(h[i]) == t) return (long)i;
    return -1;
  }
  for (i = start; i + n <= len; ++i) {
    size_t k = 0;
    while (k < n && ds__ascii_fold(h[i+k]) == ds__ascii_fold(nd[k])) ++k;
    if (k == n) return (long)i;
  }
  return -1;
}

int FUNC(str_u8_normalize)(ds_str_t *dst, const ds_str_t *src, ds_u8_norm_form form) {
  int compat, compose;
  (void)form;
  compat = (form == DS_U8_NFKC || form == DS_U8_NFKD);
  compose = (form == DS_U8_NFC  || form == DS_U8_NFKC);
  return ds__ucd_normalize(dst, src, compat, compose);
}

int FUNC(str_u8_casefold)(ds_str_t *dst, const ds_str_t *src) {
  return ds__ucd_casefold(dst, src);
}

int FUNC(str_u8_tolower)(ds_str_t *dst, const ds_str_t *src) {
  return ds__ucd_tolower(dst, src);
}

int FUNC(str_u8_toupper)(ds_str_t *dst, const ds_str_t *src) {
  return ds__ucd_toupper(dst, src);
}

long FUNC(str_u8_len_graphemes)(const ds_str_t *s) {
  return ds__ucd_grapheme_len(s);
}


int FUNC(str_view_u8_next)(ds_str_view_t v, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t i = ioff ? *ioff : 0, adv; int r;
  if (i >= v.len) return 0;
  r = ds__decode_one((const unsigned char*)v.data + i, v.len - i, &adv, out_cp, flags);
  if (r <= 0) return -1;
  if (ioff) *ioff = i + adv;
  return 1;
}

int FUNC(str_view_u8_prev)(ds_str_view_t v, size_t *ioff, unsigned long *out_cp, unsigned int flags) {
  size_t i = ioff ? *ioff : v.len;
  const unsigned char *p = (const unsigned char*)v.data;
  if (i == 0) return 0;
  {
    size_t j = i, start, adv; unsigned long cp;
    while (j > 0 && (p[j-1] & 0xC0u) == 0x80u) --j;
    if (j == 0) return -1;
    start = j;
    if (ds__decode_one(p + start, v.len - start, &adv, &cp, flags) <= 0) return -1;
    if (start + adv != i) return -1;
    if (out_cp) *out_cp = cp;
    if (ioff) *ioff = start;
  }
  return 1;
}
