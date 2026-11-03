#include "scan.h"
#include <string.h>

typedef union scan_align {
  long l;
  double d;
  void *p;
  long double ld;
} scan_align_t;

#define SCAN_STACK_BYTES 192
#define AT(base, i, es) ((unsigned char *)(base) + (i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (i) * (es))

typedef struct scan_temps {
  scan_align_t stack[SCAN_STACK_BYTES / sizeof(scan_align_t) + 1];
  unsigned char *t;
  bool owned;
} scan_temps_t;

static ds_status_t scan_check(const ds_monoid_t *m) {
  if (m == NULL || m->combine == NULL || m->identity == NULL) {
    return DS_ERR_NULL;
  }
  if (m->elem_size == 0) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

static ds_status_t scan_temps_get(const ds_monoid_t *m, scan_temps_t *tt) {
  tt->owned = false;
  if (m->scratch != NULL) {
    tt->t = (unsigned char *)m->scratch;
    return DS_OK;
  }
  if (m->elem_size <= SCAN_STACK_BYTES / DS_SCAN_SCRATCH_ELEMS) {
    tt->t = (unsigned char *)tt->stack;
    return DS_OK;
  }
  if (m->elem_size > ((size_t)-1) / DS_SCAN_SCRATCH_ELEMS) {
    return DS_ERR_OVERFLOW;
  }
  tt->t = (unsigned char *)ds_context_alloc(
      m->context, m->elem_size * DS_SCAN_SCRATCH_ELEMS);
  if (tt->t == NULL) {
    return DS_ERR_ALLOC;
  }
  tt->owned = true;
  return DS_OK;
}

static void scan_temps_release(const ds_monoid_t *m, scan_temps_t *tt) {
  if (tt->owned) {
    ds_context_free(m->context, tt->t);
  }
}

static ds_status_t scan_args(const ds_monoid_t *m, const void *in,
                             const void *out, size_t count) {
  ds_status_t st = scan_check(m);
  if (st != DS_OK) {
    return st;
  }
  if (count > 0 && (in == NULL || out == NULL)) {
    return DS_ERR_NULL;
  }
  return DS_OK;
}

ds_status_t ds_scan_inclusive(const ds_monoid_t *m, const void *in, void *out,
                              size_t count) {
  size_t es;
  size_t i;
  ds_status_t st = scan_args(m, in, out, count);
  if (st != DS_OK || count == 0) {
    return st;
  }
  es = m->elem_size;
  if (out != in) {
    memcpy(out, in, es);
  }
  for (i = 1; i < count; i++) {
    m->combine(AT(out, i, es), CAT(out, i - 1, es), CAT(in, i, es), m->user);
  }
  return DS_OK;
}

ds_status_t ds_scan_exclusive(const ds_monoid_t *m, const void *in, void *out,
                              size_t count, void *out_total) {
  scan_temps_t tt;
  unsigned char *acc;
  unsigned char *cur;
  size_t es;
  size_t i;
  ds_status_t st = scan_args(m, in, out, count);
  if (st != DS_OK) {
    return st;
  }
  st = scan_temps_get(m, &tt);
  if (st != DS_OK) {
    return st;
  }
  es = m->elem_size;
  acc = tt.t;
  cur = tt.t + es;
  memcpy(acc, m->identity, es);
  for (i = 0; i < count; i++) {
    memcpy(cur, CAT(in, i, es), es);
    memcpy(AT(out, i, es), acc, es);
    m->combine(acc, acc, cur, m->user);
  }
  if (out_total != NULL) {
    memcpy(out_total, acc, es);
  }
  scan_temps_release(m, &tt);
  return DS_OK;
}

ds_status_t ds_scan_suffix(const ds_monoid_t *m, const void *in, void *out,
                           size_t count) {
  size_t es;
  size_t i;
  ds_status_t st = scan_args(m, in, out, count);
  if (st != DS_OK || count == 0) {
    return st;
  }
  es = m->elem_size;
  if (out != in) {
    memcpy(AT(out, count - 1, es), CAT(in, count - 1, es), es);
  }
  for (i = count - 1; i-- > 0;) {
    m->combine(AT(out, i, es), CAT(in, i, es), CAT(out, i + 1, es), m->user);
  }
  return DS_OK;
}

ds_status_t ds_scan_segmented(const ds_monoid_t *m, const void *in, void *out,
                              size_t count, const unsigned char *flags,
                              bool inclusive) {
  scan_temps_t tt;
  unsigned char *acc;
  unsigned char *cur;
  size_t es;
  size_t i;
  ds_status_t st = scan_args(m, in, out, count);
  if (st != DS_OK) {
    return st;
  }
  if (count > 0 && flags == NULL) {
    return DS_ERR_NULL;
  }
  st = scan_temps_get(m, &tt);
  if (st != DS_OK) {
    return st;
  }
  es = m->elem_size;
  acc = tt.t;
  cur = tt.t + es;
  for (i = 0; i < count; i++) {
    if (i == 0 || flags[i]) {
      memcpy(acc, m->identity, es);
    }
    memcpy(cur, CAT(in, i, es), es);
    if (inclusive) {
      m->combine(acc, acc, cur, m->user);
      memcpy(AT(out, i, es), acc, es);
    } else {
      memcpy(AT(out, i, es), acc, es);
      m->combine(acc, acc, cur, m->user);
    }
  }
  scan_temps_release(m, &tt);
  return DS_OK;
}

ds_status_t ds_multiprefix(const ds_monoid_t *m, const void *in,
                           const size_t *labels, size_t label_count, void *out,
                           size_t count, void *totals) {
  scan_temps_t tt;
  unsigned char *cur;
  size_t es;
  size_t i;
  size_t lab;
  ds_status_t st = scan_args(m, in, out, count);
  if (st != DS_OK) {
    return st;
  }
  if ((count > 0 && labels == NULL) || (label_count > 0 && totals == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < count; i++) {
    if (labels[i] >= label_count) {
      return DS_ERR_RANGE;
    }
  }
  st = scan_temps_get(m, &tt);
  if (st != DS_OK) {
    return st;
  }
  es = m->elem_size;
  cur = tt.t;
  for (i = 0; i < label_count; i++) {
    memcpy(AT(totals, i, es), m->identity, es);
  }
  for (i = 0; i < count; i++) {
    lab = labels[i];
    memcpy(cur, CAT(in, i, es), es);
    memcpy(AT(out, i, es), CAT(totals, lab, es), es);
    m->combine(AT(totals, lab, es), CAT(totals, lab, es), cur, m->user);
  }
  scan_temps_release(m, &tt);
  return DS_OK;
}

ds_status_t ds_scan_hillis_steele(const ds_monoid_t *m, const void *in,
                                  void *out, size_t count, void *scratch) {
  unsigned char *src;
  unsigned char *dst;
  unsigned char *tmp;
  size_t es;
  size_t d;
  size_t i;
  ds_status_t st = scan_args(m, in, out, count);
  if (st != DS_OK || count == 0) {
    return st;
  }
  if (scratch == NULL) {
    return DS_ERR_NULL;
  }
  es = m->elem_size;
  if (out != in) {
    memmove(out, in, count * es);
  }
  src = (unsigned char *)out;
  dst = (unsigned char *)scratch;
  /* round d: every position i >= d reads i - d and i of the previous
   * round, as the parallel algorithm does in one step */
  for (d = 1; d < count; d *= 2) {
    for (i = 0; i < count; i++) {
      if (i >= d) {
        m->combine(AT(dst, i, es), CAT(src, i - d, es), CAT(src, i, es),
                   m->user);
      } else {
        memcpy(AT(dst, i, es), CAT(src, i, es), es);
      }
    }
    tmp = src;
    src = dst;
    dst = tmp;
    if (d > count / 2) {
      break;
    }
  }
  if (src != (unsigned char *)out) {
    memcpy(out, src, count * es);
  }
  return DS_OK;
}

/* Exclusive Blelloch scan of a power-of-two block [s, s + len). */
static void scan_blelloch_block(const ds_monoid_t *m, unsigned char *a,
                                size_t s, size_t len, unsigned char *t) {
  size_t es = m->elem_size;
  size_t d;
  size_t i;
  for (d = 1; d < len; d *= 2) {
    for (i = 2 * d - 1; i < len; i += 2 * d) {
      m->combine(AT(a, s + i, es), CAT(a, s + i - d, es), CAT(a, s + i, es),
                 m->user);
    }
  }
  memcpy(AT(a, s + len - 1, es), m->identity, es);
  for (d = len / 2; d >= 1; d /= 2) {
    for (i = 2 * d - 1; i < len; i += 2 * d) {
      memcpy(t, CAT(a, s + i - d, es), es);
      memcpy(AT(a, s + i - d, es), CAT(a, s + i, es), es);
      m->combine(AT(a, s + i, es), CAT(a, s + i, es), t, m->user);
    }
  }
}

ds_status_t ds_scan_blelloch(const ds_monoid_t *m, void *data, size_t count,
                             void *out_total) {
  scan_temps_t tt;
  unsigned char *a = (unsigned char *)data;
  unsigned char *acc;
  unsigned char *last;
  unsigned char *t;
  size_t es;
  size_t s = 0;
  size_t len;
  size_t j;
  ds_status_t st = scan_args(m, data, data, count);
  if (st != DS_OK) {
    return st;
  }
  st = scan_temps_get(m, &tt);
  if (st != DS_OK) {
    return st;
  }
  es = m->elem_size;
  acc = tt.t;
  last = tt.t + es;
  t = tt.t + 2 * es;
  memcpy(acc, m->identity, es);
  /* blocks of decreasing powers of two (the binary digits of count) */
  len = (size_t)1;
  while (len <= count / 2) {
    len *= 2;
  }
  while (s < count) {
    while (len > count - s) {
      len /= 2;
    }
    memcpy(last, CAT(a, s + len - 1, es), es);
    scan_blelloch_block(m, a, s, len, t);
    /* block total = exclusive prefix of its last element . that element */
    m->combine(last, CAT(a, s + len - 1, es), last, m->user);
    if (s > 0) {
      for (j = s; j < s + len; j++) {
        m->combine(AT(a, j, es), acc, CAT(a, j, es), m->user);
      }
    }
    m->combine(acc, acc, last, m->user);
    s += len;
  }
  if (out_total != NULL) {
    memcpy(out_total, acc, es);
  }
  scan_temps_release(m, &tt);
  return DS_OK;
}

ds_status_t ds_reduce(const ds_monoid_t *m, const void *in, size_t count,
                      void *out) {
  size_t es;
  size_t i;
  ds_status_t st = scan_check(m);
  if (st != DS_OK) {
    return st;
  }
  if (out == NULL || (count > 0 && in == NULL)) {
    return DS_ERR_NULL;
  }
  es = m->elem_size;
  memcpy(out, m->identity, es);
  for (i = 0; i < count; i++) {
    m->combine(out, out, CAT(in, i, es), m->user);
  }
  return DS_OK;
}

void ds_prefix_sums_long(const long *in, long *out, size_t count) {
  size_t i;
  if (count == 0 || in == NULL || out == NULL) {
    return;
  }
  out[0] = in[0];
  for (i = 1; i < count; i++) {
    out[i] = out[i - 1] + in[i];
  }
}

void ds_prefix_sums_ulong(const unsigned long *in, unsigned long *out,
                          size_t count) {
  size_t i;
  if (count == 0 || in == NULL || out == NULL) {
    return;
  }
  out[0] = in[0];
  for (i = 1; i < count; i++) {
    out[i] = out[i - 1] + in[i];
  }
}

void ds_prefix_sums_size(const size_t *in, size_t *out, size_t count) {
  size_t i;
  if (count == 0 || in == NULL || out == NULL) {
    return;
  }
  out[0] = in[0];
  for (i = 1; i < count; i++) {
    out[i] = out[i - 1] + in[i];
  }
}

void ds_prefix_sums_double(const double *in, double *out, size_t count) {
  size_t i;
  double sum = 0.0;
  double comp = 0.0;
  double x;
  double t;
  if (count == 0 || in == NULL || out == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    x = in[i];
    t = sum + x;
    if ((sum < 0 ? -sum : sum) >= (x < 0 ? -x : x)) {
      comp += (sum - t) + x;
    } else {
      comp += (x - t) + sum;
    }
    sum = t;
    out[i] = sum + comp;
  }
}
