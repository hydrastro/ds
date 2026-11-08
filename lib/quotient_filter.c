#include "quotient_filter.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define QF_W (sizeof(unsigned long) * CHAR_BIT)
#define QF_OCC 1U
#define QF_CONT 2U
#define QF_SHIFT 4U

static size_t qf_inc(const ds_quotient_filter_t *f, size_t i) {
  return (i + 1) & (f->slots - 1);
}

static size_t qf_dec(const ds_quotient_filter_t *f, size_t i) {
  return (i - 1) & (f->slots - 1);
}

static unsigned long qf_get_rem(const ds_quotient_filter_t *f, size_t i) {
  size_t pos = i * f->r, w = pos / QF_W;
  unsigned off = (unsigned)(pos % QF_W);
  unsigned long v = f->rem[w] >> off;
  if (off + f->r > QF_W) {
    v |= f->rem[w + 1] << (QF_W - off);
  }
  return f->r == QF_W ? v : v & ((1UL << f->r) - 1UL);
}

static void qf_set_rem(ds_quotient_filter_t *f, size_t i, unsigned long v) {
  size_t pos = i * f->r, w = pos / QF_W;
  unsigned off = (unsigned)(pos % QF_W);
  unsigned long mask = f->r == QF_W ? ~0UL : (1UL << f->r) - 1UL;
  f->rem[w] = (f->rem[w] & ~(mask << off)) | ((v & mask) << off);
  if (off + f->r > QF_W) {
    unsigned hi = (unsigned)(off + f->r - QF_W);
    unsigned long hmask = (1UL << hi) - 1UL;
    f->rem[w + 1] = (f->rem[w + 1] & ~hmask) | ((v & mask) >> (QF_W - off));
  }
}

static bool qf_empty(const ds_quotient_filter_t *f, size_t i) {
  return (f->meta[i] & (QF_OCC | QF_CONT | QF_SHIFT)) == 0;
}

ds_status_t ds_quotient_filter_init(ds_quotient_filter_t *f, unsigned q,
                                    unsigned r, ds_context_t *context) {
  size_t bits, words;
  if (f == NULL) {
    return DS_ERR_NULL;
  }
  memset(f, 0, sizeof(*f));
  if (q == 0 || r == 0 || q + r > QF_W || q >= sizeof(size_t) * CHAR_BIT - 1) {
    return DS_ERR_INVALID;
  }
  f->q = q;
  f->r = r;
  f->slots = (size_t)1 << q;
  if (!ds_size_mul(f->slots, r, &bits)) {
    return DS_ERR_OVERFLOW;
  }
  words = bits / QF_W + 2;
  f->rem =
      (unsigned long *)ds_context_calloc(context, words, sizeof(unsigned long));
  f->meta = (unsigned char *)ds_context_calloc(context, f->slots, 1);
  if (f->rem == NULL || f->meta == NULL) {
    ds_context_free(context, f->rem);
    ds_context_free(context, f->meta);
    f->rem = NULL;
    f->meta = NULL;
    return DS_ERR_ALLOC;
  }
  f->context = context;
  return DS_OK;
}

void ds_quotient_filter_destroy(ds_quotient_filter_t *f) {
  if (f == NULL) {
    return;
  }
  ds_context_free(f->context, f->rem);
  ds_context_free(f->context, f->meta);
  ds_context_free(f->context, f->scratch);
  memset(f, 0, sizeof(*f));
}

static void qf_split(const ds_quotient_filter_t *f, unsigned long hash,
                     size_t *fq, unsigned long *fr) {
  unsigned long fp =
      f->q + f->r == QF_W ? hash : hash & ((1UL << (f->q + f->r)) - 1UL);
  *fq = (size_t)(fp >> f->r);
  *fr = f->r == QF_W ? fp : fp & ((1UL << f->r) - 1UL);
}

/* start of the run for quotient fq (occupied) */
static size_t qf_run_start(const ds_quotient_filter_t *f, size_t fq) {
  size_t b = fq, s;
  while (f->meta[b] & QF_SHIFT) {
    b = qf_dec(f, b);
  }
  s = b;
  while (b != fq) {
    do {
      s = qf_inc(f, s);
    } while (f->meta[s] & QF_CONT);
    do {
      b = qf_inc(f, b);
    } while (!(f->meta[b] & QF_OCC));
  }
  return s;
}

bool ds_quotient_filter_contains(const ds_quotient_filter_t *f,
                                 unsigned long hash) {
  size_t fq, s;
  unsigned long fr;
  if (f == NULL || f->meta == NULL) {
    return false;
  }
  qf_split(f, hash, &fq, &fr);
  if (!(f->meta[fq] & QF_OCC)) {
    return false;
  }
  s = qf_run_start(f, fq);
  do {
    if (qf_get_rem(f, s) == fr) {
      return true;
    }
    s = qf_inc(f, s);
  } while (f->meta[s] & QF_CONT);
  return false;
}

/* decode the stretch [b, e) (b: a run starting at its canonical slot, e:
 * the first empty slot) into fingerprints (quotient offset from b in the
 * high part, remainder low), in order */
static ds_status_t qf_decode(ds_quotient_filter_t *f, size_t b, size_t extra,
                             size_t *out_n, size_t *out_len) {
  size_t s = b, len = 0, n = 0, q = b;
  bool first = true;
  while (!qf_empty(f, s) && len < f->slots) {
    len++;
    s = qf_inc(f, s);
  }
  if (len + extra > f->scratch_cap) {
    size_t cap = len + extra + 16, bytes;
    unsigned long *p;
    if (!ds_size_mul(cap, sizeof(unsigned long), &bytes)) {
      return DS_ERR_OVERFLOW;
    }
    p = (unsigned long *)ds_context_realloc(f->context, f->scratch, bytes);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    f->scratch = p;
    f->scratch_cap = cap;
  }
  for (s = b; n < len; s = qf_inc(f, s)) {
    if (!(f->meta[s] & QF_CONT)) {
      /* a new run: its quotient is the next occupied canonical slot */
      if (!first) {
        do {
          q = qf_inc(f, q);
        } while (!(f->meta[q] & QF_OCC));
      }
      first = false;
    }
    f->scratch[n++] =
        ((unsigned long)((q - b) & (f->slots - 1)) << f->r) | qf_get_rem(f, s);
  }
  *out_n = n;
  *out_len = len;
  return DS_OK;
}

/* clear [b, b + len) and write fingerprints scratch[0, n) back from b */
static void qf_encode(ds_quotient_filter_t *f, size_t b, size_t len, size_t n) {
  size_t i, s = b, pos = 0, prevq = (size_t)-1;
  unsigned long rmask = f->r == QF_W ? ~0UL : (1UL << f->r) - 1UL;
  for (i = 0; i < len; i++) {
    f->meta[s] = 0;
    s = qf_inc(f, s);
  }
  for (i = 0; i < n; i++) {
    size_t qoff = (size_t)(f->scratch[i] >> f->r);
    size_t slot;
    unsigned char m = 0;
    if (pos < qoff) {
      pos = qoff;
    }
    slot = (b + pos) & (f->slots - 1);
    if (qoff == prevq) {
      m |= QF_CONT;
    }
    if (pos != qoff) {
      m |= QF_SHIFT;
    }
    f->meta[slot] = (unsigned char)((f->meta[slot] & QF_OCC) | m);
    f->meta[(b + qoff) & (f->slots - 1)] |= QF_OCC;
    qf_set_rem(f, slot, f->scratch[i] & rmask);
    prevq = qoff;
    pos++;
  }
}

ds_status_t ds_quotient_filter_insert(ds_quotient_filter_t *f,
                                      unsigned long hash) {
  size_t fq, b, n, len, i;
  unsigned long fr, key;
  ds_status_t st;
  if (f == NULL || f->meta == NULL) {
    return DS_ERR_NULL;
  }
  if (ds_quotient_filter_contains(f, hash)) {
    return DS_EXISTS;
  }
  if (f->count >= f->slots) {
    return DS_ERR_OVERFLOW;
  }
  qf_split(f, hash, &fq, &fr);
  if (qf_empty(f, fq)) {
    f->meta[fq] = QF_OCC;
    qf_set_rem(f, fq, fr);
    f->count++;
    return DS_OK;
  }
  b = fq;
  while (f->meta[b] & QF_SHIFT) {
    b = qf_dec(f, b);
  }
  st = qf_decode(f, b, 1, &n, &len);
  if (st != DS_OK) {
    return st;
  }
  key = ((unsigned long)((fq - b) & (f->slots - 1)) << f->r) | fr;
  i = n;
  while (i > 0 && f->scratch[i - 1] > key) {
    f->scratch[i] = f->scratch[i - 1];
    i--;
  }
  f->scratch[i] = key;
  qf_encode(f, b, len, n + 1);
  f->count++;
  return DS_OK;
}

ds_status_t ds_quotient_filter_remove(ds_quotient_filter_t *f,
                                      unsigned long hash) {
  size_t fq, b, n, len, i, j;
  unsigned long fr, key;
  ds_status_t st;
  if (f == NULL || f->meta == NULL) {
    return DS_ERR_NULL;
  }
  if (!ds_quotient_filter_contains(f, hash)) {
    return DS_NOT_FOUND;
  }
  qf_split(f, hash, &fq, &fr);
  b = fq;
  while (f->meta[b] & QF_SHIFT) {
    b = qf_dec(f, b);
  }
  st = qf_decode(f, b, 0, &n, &len);
  if (st != DS_OK) {
    return st;
  }
  key = ((unsigned long)((fq - b) & (f->slots - 1)) << f->r) | fr;
  for (i = 0; i < n && f->scratch[i] != key; i++) {
  }
  for (j = i; j + 1 < n; j++) {
    f->scratch[j] = f->scratch[j + 1];
  }
  /* occupied bits of the stretch are rebuilt by the encoder */
  for (j = 0; j < len; j++) {
    f->meta[(b + j) & (f->slots - 1)] &= (unsigned char)~QF_OCC;
  }
  qf_encode(f, b, len, n - 1);
  f->count--;
  return DS_OK;
}

size_t ds_quotient_filter_count(const ds_quotient_filter_t *f) {
  return f == NULL ? 0 : f->count;
}

size_t ds_quotient_filter_bytes(const ds_quotient_filter_t *f) {
  if (f == NULL) {
    return 0;
  }
  return sizeof(*f) + (f->slots * f->r / QF_W + 2) * sizeof(unsigned long) +
         f->slots;
}

ds_status_t ds_quotient_filter_visit(const ds_quotient_filter_t *f,
                                     ds_quotient_filter_visit_t visit,
                                     void *user) {
  size_t q, s;
  if (f == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (f->meta == NULL) {
    return DS_OK;
  }
  for (q = 0; q < f->slots; q++) {
    if (!(f->meta[q] & QF_OCC)) {
      continue;
    }
    s = qf_run_start(f, q);
    do {
      if (!visit(((unsigned long)q << f->r) | qf_get_rem(f, s), user)) {
        return DS_STOP;
      }
      s = qf_inc(f, s);
    } while (f->meta[s] & QF_CONT);
  }
  return DS_OK;
}
