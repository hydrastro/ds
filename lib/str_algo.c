#include "str_algo.h"
#include <stdlib.h>
#include <string.h>

int ds__in_set(unsigned char c, const unsigned char *set, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i)
    if (c == set[i])
      return 1;
  return 0;
}

/*
 * Two-way string matching (Crochemore and Perrin, 1991). The needle is cut
 * at a critical factorization x = u v, found as the later of the maximal
 * suffixes of x under the byte order and under its reverse; v is matched
 * left to right, then u right to left. When u is a suffix of a prefix of
 * v's period (the "periodic" case), a mismatch can shift only by the
 * period, and the matched periods are remembered; otherwise any mismatch
 * shifts past the longer half. Linear time, constant space.
 */

/* the maximal suffix of x[0..m) under the byte order (reverse != 0: the
 * reversed order): where it starts, and its period */
static void tw_max_suffix(const unsigned char *x, size_t m, int reverse,
                          size_t *start, size_t *period) {
  size_t best = 0; /* start of the largest suffix so far */
  size_t cand = 1; /* start of the suffix compared with it */
  size_t off = 0;  /* how far the two agree */
  size_t per = 1;  /* period of the best suffix so far */
  while (cand + off < m) {
    unsigned char a = x[cand + off];
    unsigned char b = x[best + off];
    if (a == b) {
      if (off + 1 == per) { /* a whole period matched: skip it */
        cand += per;
        off = 0;
      } else {
        off += 1;
      }
    } else if ((a < b) != (reverse != 0)) {
      /* the candidate is smaller, and so is every start up to it */
      cand += off + 1;
      off = 0;
      per = cand - best;
    } else {
      /* the candidate is larger: it becomes the best */
      best = cand;
      cand = best + 1;
      off = 0;
      per = 1;
    }
  }
  *start = best;
  *period = per;
}

void ds__maximal_suffix(const unsigned char *x, size_t m, int *ms, int *p) {
  size_t start, period;
  tw_max_suffix(x, m, 0, &start, &period);
  *ms = (int)start;
  *p = (int)period;
}

void ds__maximal_suffix_rev(const unsigned char *x, size_t m, int *ms, int *p) {
  size_t start, period;
  tw_max_suffix(x, m, 1, &start, &period);
  *ms = (int)start;
  *p = (int)period;
}

long ds_str_find_twoway(ds_str_t *s, const void *needle, size_t m,
                        size_t start) {
  const unsigned char *y;
  const unsigned char *x = (const unsigned char *)needle;
  size_t n, ell, per, s1, p1, s2, p2, i, j;
  if (!s || !needle || m == 0u)
    return -1;
  n = s->len;
  if (start > n) {
    return -1;
  }
  if (m > n - start) { /* no room; also an empty string's NULL buffer */
    return -1;
  }
  y = (const unsigned char *)s->buf;
  if (m == 1u) {
    const void *p = memchr(y + start, x[0], n - start);
    return p ? (long)((const unsigned char *)p - y) : -1;
  }
  /* the critical factorization: the later of the two maximal suffixes */
  tw_max_suffix(x, m, 0, &s1, &p1);
  tw_max_suffix(x, m, 1, &s2, &p2);
  if (s1 >= s2) {
    ell = s1;
    per = p1;
  } else {
    ell = s2;
    per = p2;
  }
  j = start;
  if (memcmp(x, x + per, ell) == 0) {
    /* periodic: shifts are by the period, and after one the first
     * (m - per) bytes are known to match */
    size_t memory = 0;
    while (j <= n - m) {
      i = ell > memory ? ell : memory;
      while (i < m && x[i] == y[j + i]) {
        i += 1;
      }
      if (i >= m) {
        i = ell;
        while (i > memory && x[i - 1] == y[j + i - 1]) {
          i -= 1;
        }
        if (i <= memory) {
          return (long)j;
        }
        j += per;
        memory = m - per;
      } else {
        j += i - ell + 1;
        memory = 0;
      }
    }
  } else {
    /* the halves differ: a full right half match that fails on the left
     * shifts past the longer half */
    per = (ell > m - ell ? ell : m - ell) + 1;
    while (j <= n - m) {
      i = ell;
      while (i < m && x[i] == y[j + i]) {
        i += 1;
      }
      if (i >= m) {
        i = ell;
        while (i > 0 && x[i - 1] == y[j + i - 1]) {
          i -= 1;
        }
        if (i == 0) {
          return (long)j;
        }
        j += per;
      } else {
        j += i - ell + 1;
      }
    }
  }
  return -1;
}

long ds_str_find_bmh(ds_str_t *s, const void *needle, size_t n, size_t start) {
  const unsigned char *h, *pat = (const unsigned char *)needle;
  size_t i, len;
  unsigned char skip[256];
  long ret = -1;
  if (!s || !needle || n == 0u)
    return -1;
  len = s->len;
  if (start > len || n > len - start) { /* also an empty string's NULL buffer */
    return -1;
  }
  if (n == 1u) {
    const void *p = memchr(s->buf + start, *pat, len - start);
    ret = p ? (long)((const unsigned char *)p - (const unsigned char *)s->buf)
            : -1;
    return ret;
  }
  if (n > 255u) {
    return ds_str_find_twoway(s, needle, n, start);
  }
  for (i = 0; i < 256; ++i)
    skip[i] = (unsigned char)n;
  for (i = 0; i < n - 1; ++i)
    skip[pat[i]] = (unsigned char)(n - 1 - i);
  h = (const unsigned char *)s->buf;
  i = start;
  while (i + n <= len) {
    unsigned char last = h[i + n - 1];
    if (last == pat[n - 1] && memcmp(h + i, pat, n) == 0) {
      ret = (long)i;
      break;
    }
    i += skip[last];
  }
  return ret;
}

int ds_str_ltrim_set(ds_str_t *s, const unsigned char *set, size_t set_n) {
  size_t i = 0u, len;
  if (!s)
    return -1;
  len = s->len;
  while (i < len && ds__in_set((unsigned char)s->buf[i], set, set_n))
    ++i;
  if (i) {
    memmove(s->buf, s->buf + i, len - i);
    s->len = len - i;
    s->buf[s->len] = '\0';
  }
  return 0;
}

int ds_str_rtrim_set(ds_str_t *s, const unsigned char *set, size_t set_n) {
  size_t len;
  if (!s)
    return -1;
  len = s->len;
  while (len && ds__in_set((unsigned char)s->buf[len - 1], set, set_n))
    --len;
  if (len != s->len) { /* an empty string may have no buffer at all */
    s->len = len;
    s->buf[len] = '\0';
  }
  return 0;
}

int ds_str_trim_set(ds_str_t *s, const unsigned char *set, size_t set_n) {
  if (ds_str_rtrim_set(s, set, set_n) != 0)
    return -1;
  return ds_str_ltrim_set(s, set, set_n);
}

int ds_str_ltrim_pred(ds_str_t *s, ds_byte_pred pred) {
  size_t i = 0u, len;
  if (!s || !pred)
    return -1;
  len = s->len;
  while (i < len && pred((unsigned char)s->buf[i]))
    ++i;
  if (i) {
    memmove(s->buf, s->buf + i, len - i);
    s->len = len - i;
    s->buf[s->len] = '\0';
  }
  return 0;
}

int ds_str_rtrim_pred(ds_str_t *s, ds_byte_pred pred) {
  size_t len;
  if (!s || !pred)
    return -1;
  len = s->len;
  while (len && pred((unsigned char)s->buf[len - 1]))
    --len;
  if (len != s->len) { /* an empty string may have no buffer at all */
    s->len = len;
    s->buf[len] = '\0';
  }
  return 0;
}

int ds_str_trim_pred(ds_str_t *s, ds_byte_pred pred) {
  if (ds_str_rtrim_pred(s, pred) != 0)
    return -1;
  return ds_str_ltrim_pred(s, pred);
}

void ds_str_split_each_c(ds_str_t *s, unsigned char sep, ds_str_token_cb cb,
                         void *ud) {
  size_t i = 0u, start = 0u, len;
  if (!s || !cb)
    return;
  len = s->len;
  for (i = 0; i < len; ++i) {
    if ((unsigned char)s->buf[i] == sep) {
      cb(s->buf + start, i - start, ud);
      start = i + 1;
    }
  }
  cb(s->buf + start, len - start, ud);
}

void ds_str_split_each_substr(ds_str_t *s, const void *sep, size_t nsep,
                              ds_str_token_cb cb, void *ud) {
  size_t pos = 0u;
  long at;
  if (!s || !cb || !sep || nsep == 0u)
    return;
  for (;;) {
    at = ds_str_find_twoway(s, sep, nsep, pos);
    if (at < 0) {
      cb(s->buf + pos, s->len - pos, ud);
      break;
    }
    cb(s->buf + pos, (size_t)at - pos, ud);
    pos = (size_t)at + nsep;
    if (pos > s->len)
      pos = s->len;
  }
}

int ds_str_join_c(ds_str_t *dst, ds_str_t **parts, size_t count,
                  const void *sep, size_t nsep) {
  size_t i;
  if (!dst)
    return -1;
  if (!parts || count == 0)
    return 0;
  for (i = 0; i < count; ++i) {
    if (i) {
      if (ds_str_append(dst, sep, nsep) != 0) {
        return -1;
      }
    }
    if (parts[i] && parts[i]->len) {
      if (ds_str_append(dst, parts[i]->buf, parts[i]->len) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

int ds_str_starts_with(ds_str_t *s, const void *prefix, size_t n) {
  if (!s || !prefix)
    return 0;
  if (s->len < n)
    return 0;
  if (n == 0u) /* an empty string may have no buffer at all */
    return 1;
  return memcmp(s->buf, prefix, n) == 0;
}

int ds_str_ends_with(ds_str_t *s, const void *suffix, size_t n) {
  if (!s || !suffix)
    return 0;
  if (s->len < n)
    return 0;
  if (n == 0u)
    return 1;
  return memcmp(s->buf + (s->len - n), suffix, n) == 0;
}

long ds_str_count(ds_str_t *s, const void *needle, size_t n) {
  size_t pos = 0u;
  long c = 0;
  long at;
  if (!s || !needle || n == 0u)
    return 0;
  for (;;) {
    at = ds_str_find_twoway(s, needle, n, pos);
    if (at < 0)
      break;
    ++c;
    pos = (size_t)at + n;
    if (pos > s->len)
      break;
  }
  return c;
}

int ds_str_replace_all(ds_str_t *s, const void *from, size_t nfrom,
                       const void *to, size_t nto) {
  size_t pos = 0u;
  long at;
  if (!s || !from || nfrom == 0u)
    return -1;
  while (1) {
    at = ds_str_find_twoway(s, from, nfrom, pos);
    if (at < 0)
      break;
    if (ds_str_erase(s, (size_t)at, nfrom) != 0) {
      return -1;
    }
    if (nto) {
      if (ds_str_insert(s, (size_t)at, to, nto) != 0) {
        return -1;
      }
      pos = (size_t)at + nto;
    } else {
      pos = (size_t)at;
    }
  }
  return 0;
}

int ds_str_replace_all_builder(ds_str_t *s, const void *from, size_t nfrom,
                               const void *to, size_t nto) {
  size_t pos = 0u;
  long at;
  ds_str_t *out;
  size_t last = 0u;

  if (!s || !from || nfrom == 0u)
    return -1;

  out = ds_str_create_alloc(s->allocator, s->deallocator);
  if (!out) {
    return -1;
  }

  if (s->len > 0u) {
    if (ds_str_reserve(out, s->len) != 0) {
      ds_str_destroy(out);
      return -1;
    }
  }

  while (1) {
    at = ds_str_find_twoway(s, from, nfrom, pos);
    if (at < 0) {
      if (last < s->len) {
        if (ds_str_append(out, s->buf + last, s->len - last) != 0) {
          ds_str_destroy(out);
          return -1;
        }
      }
      break;
    }

    if ((size_t)at > last) {
      if (ds_str_append(out, s->buf + last, (size_t)at - last) != 0) {
        ds_str_destroy(out);
        return -1;
      }
    }

    if (nto) {
      if (ds_str_append(out, to, nto) != 0) {
        ds_str_destroy(out);
        return -1;
      }
    }

    pos = (size_t)at + nfrom;
    last = pos;
    if (pos > s->len)
      pos = s->len;
  }

  if (s->buf)
    s->deallocator(s->buf);
  s->buf = out->buf;
  s->len = out->len;
  s->cap = out->cap;
  out->buf = NULL;
  out->len = 0u;
  out->cap = 0u;
  ds_str_destroy(out);
  if (s->buf)
    s->buf[s->len] = '\0';
  return 0;
}
