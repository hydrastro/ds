#include "str_utf8.h"
#include <string.h>

int ds__u8_trail_ok(unsigned char c) { return (c & 0xC0u) == 0x80u; }

int FUNC(str_u8_encode)(uint32_t cp, char out[4]) {
  if (cp <= 0x7Fu) { out[0] = (char)cp; return 1; }
  if (cp <= 0x7FFu) { out[0] = (char)(0xC0u | (cp >> 6)); out[1] = (char)(0x80u | (cp & 0x3Fu)); return 2; }
  if (cp <= 0xFFFFu) {
    if (cp >= 0xD800u && cp <= 0xDFFFu) return DS_U8_EINVAL;
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
  }
  if (cp <= 0x10FFFFu) {
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
  }
  return DS_U8_EINVAL;
}

int FUNC(str_u8_decode_at)(ds_str_t *s, size_t i, uint32_t *out_cp, size_t *adv) {
  const unsigned char *p; size_t n; unsigned char c0; uint32_t cp; size_t need;
  if (!s || !s->buf || i >= s->len) return DS_U8_EINVAL;
  p = (const unsigned char*)s->buf + i; n = s->len - i; c0 = p[0];

  if (c0 < 0x80u) {
    if (out_cp) *out_cp = c0;
    if (adv) *adv = 1u;
    return DS_U8_OK;
  }
  if ((c0 & 0xE0u) == 0xC0u) {
    need = 2; if (n < need) return DS_U8_EOVERFLOW;
    if (!ds__u8_trail_ok(p[1])) return DS_U8_EINVAL;
    cp = ((c0 & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    if (cp < 0x80u) return DS_U8_EINVAL;
    if (out_cp) *out_cp = cp;
    if (adv) *adv = 2u;
    return DS_U8_OK;
  }
  if ((c0 & 0xF0u) == 0xE0u) {
    need = 3; if (n < need) return DS_U8_EOVERFLOW;
    if (!ds__u8_trail_ok(p[1]) || !ds__u8_trail_ok(p[2])) return DS_U8_EINVAL;
    cp = ((c0 & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) return DS_U8_EINVAL;
    if (out_cp) *out_cp = cp;
    if (adv) *adv = 3u;
    return DS_U8_OK;
  }
  if ((c0 & 0xF8u) == 0xF0u) {
    need = 4; if (n < need) return DS_U8_EOVERFLOW;
    if (!ds__u8_trail_ok(p[1]) || !ds__u8_trail_ok(p[2]) || !ds__u8_trail_ok(p[3])) return DS_U8_EINVAL;
    cp = ((c0 & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    if (cp < 0x10000u || cp > 0x10FFFFu) return DS_U8_EINVAL;
    if (out_cp) *out_cp = cp;
    if (adv) *adv = 4u;
    return DS_U8_OK;
  }
  return DS_U8_EINVAL;
}

int FUNC(str_u8_is_valid)(ds_str_t *s) {
  size_t i = 0u; size_t step; uint32_t cp; int r;
  if (!s) return DS_U8_EINVAL;
  while (i < s->len) {
    r = FUNC(str_u8_decode_at)(s, i, &cp, &step);
    if (r != DS_U8_OK) return r;
    i += step;
  }
  return DS_U8_OK;
}

long FUNC(str_u8_len)(ds_str_t *s) {
  size_t i = 0u; size_t step; long count = 0; int r; uint32_t cp;
  if (!s) return -1;
  while (i < s->len) {
    r = FUNC(str_u8_decode_at)(s, i, &cp, &step);
    if (r != DS_U8_OK) return -1;
    i += step;
    ++count;
  }
  return count;
}

size_t FUNC(str_u8_next)(ds_str_t *s, size_t i) {
  size_t step; uint32_t cp; if (!s) return 0u; if (i >= s->len) return s->len;
  if (FUNC(str_u8_decode_at)(s, i, &cp, &step) != DS_U8_OK) return i + 1;
  return i + step;
}

size_t FUNC(str_u8_prev)(ds_str_t *s, size_t i) {
  size_t j = (i == 0 || !s) ? 0u : i - 1; int k = 0;
  if (!s) return 0u;
  while (j > 0 && k < 3 && ((s->buf[j] & 0xC0) == 0x80)) { --j; ++k; }
  {
    size_t adv; uint32_t cp;
    if (FUNC(str_u8_decode_at)(s, j, &cp, &adv) == DS_U8_OK && j + adv == i) return j;
  }
  return (i > 0) ? i - 1 : 0;
}

int ds__u8_cp_index_to_byte(ds_str_t *s, size_t cp_index, size_t *out_byte) {
  size_t i = 0u, idx = 0u, adv; uint32_t cp; int r;
  while (i < s->len) {
    if (idx == cp_index) { *out_byte = i; return DS_U8_OK; }
    r = FUNC(str_u8_decode_at)(s, i, &cp, &adv);
    if (r != DS_U8_OK) return DS_U8_EINVAL;
    i += adv; ++idx;
  }
  if (idx == cp_index) { *out_byte = i; return DS_U8_OK; }
  return DS_U8_EINVAL;
}

int FUNC(str_u8_insert_cp)(ds_str_t *s, size_t cp_index, uint32_t cp) {
  char enc[4]; int nb; size_t off; int r;
  nb = FUNC(str_u8_encode)(cp, enc);
  if (nb < 0) return nb;
  if (!s) return DS_U8_EINVAL;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  r = ds__u8_cp_index_to_byte(s, cp_index, &off);
  if (r != DS_U8_OK) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return DS_U8_EINVAL;
  }
  r = FUNC(str_insert)(s, off, enc, (size_t)nb);
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return r;
}

int FUNC(str_u8_erase_cprange)(ds_str_t *s, size_t cp_index, size_t cp_count) {
  size_t off, i = 0u, adv, end; uint32_t cp; int r;
  if (!s) return DS_U8_EINVAL;
#ifdef DS_THREAD_SAFE
  LOCK(s)
#endif
  r = ds__u8_cp_index_to_byte(s, cp_index, &off);
  if (r != DS_U8_OK) {
#ifdef DS_THREAD_SAFE
    UNLOCK(s)
#endif
    return DS_U8_EINVAL;
  }
  end = off;
  while (i < cp_count && end < s->len) {
    if (FUNC(str_u8_decode_at)(s, end, &cp, &adv) != DS_U8_OK) break;
    end += adv;
    ++i;
  }
  r = FUNC(str_erase)(s, off, end - off);
#ifdef DS_THREAD_SAFE
  UNLOCK(s)
#endif
  return r;
}
