#include "compress.h"
#include "sort.h"
#include <string.h>

/* ---- arithmetic coding ---------------------------------------------- */

#define AC_TOP 0xFFFFUL
#define AC_FIRST_QTR 0x4000UL
#define AC_HALF 0x8000UL
#define AC_THIRD_QTR 0xC000UL

static ds_status_t ac_bit_plus_pending(ds_arith_encoder_t *e,
                                       unsigned long bit) {
  ds_status_t st = ds_bit_writer_put(e->out, bit, 1);
  while (st == DS_OK && e->pending > 0) {
    st = ds_bit_writer_put(e->out, bit ^ 1UL, 1);
    e->pending--;
  }
  return st;
}

void ds_arith_encoder_init(ds_arith_encoder_t *e, ds_bit_writer_t *out) {
  e->out = out;
  e->low = 0;
  e->high = AC_TOP;
  e->pending = 0;
}

ds_status_t ds_arith_encode(ds_arith_encoder_t *e,
                            const ds_arith_model_t *model, size_t symbol) {
  unsigned long lo;
  unsigned long hi;
  unsigned long total;
  unsigned long range;
  ds_status_t st;
  if (e == NULL || model == NULL) {
    return DS_ERR_NULL;
  }
  model->range(symbol, &lo, &hi, model->state);
  total = model->total(model->state);
  if (total == 0 || total > DS_ARITH_MAX_TOTAL || lo >= hi || hi > total) {
    return DS_ERR_INVALID;
  }
  range = e->high - e->low + 1;
  e->high = e->low + range * hi / total - 1;
  e->low = e->low + range * lo / total;
  for (;;) {
    if (e->high < AC_HALF) {
      st = ac_bit_plus_pending(e, 0);
    } else if (e->low >= AC_HALF) {
      st = ac_bit_plus_pending(e, 1);
      e->low -= AC_HALF;
      e->high -= AC_HALF;
    } else if (e->low >= AC_FIRST_QTR && e->high < AC_THIRD_QTR) {
      e->pending++;
      e->low -= AC_FIRST_QTR;
      e->high -= AC_FIRST_QTR;
      st = DS_OK;
    } else {
      break;
    }
    if (st != DS_OK) {
      return st;
    }
    e->low = 2 * e->low;
    e->high = 2 * e->high + 1;
  }
  if (model->update != NULL) {
    model->update(symbol, model->state);
  }
  return DS_OK;
}

ds_status_t ds_arith_encoder_finish(ds_arith_encoder_t *e) {
  if (e == NULL) {
    return DS_ERR_NULL;
  }
  e->pending++;
  return ac_bit_plus_pending(e, e->low < AC_FIRST_QTR ? 0UL : 1UL);
}

static unsigned long ac_next_bit(ds_arith_decoder_t *d) {
  unsigned long b;
  if (ds_bit_reader_get(d->in, 1, &b) != DS_OK) {
    return 0;
  }
  return b;
}

void ds_arith_decoder_init(ds_arith_decoder_t *d, ds_bit_reader_t *in) {
  int i;
  d->in = in;
  d->low = 0;
  d->high = AC_TOP;
  d->value = 0;
  for (i = 0; i < 16; i++) {
    d->value = 2 * d->value + ac_next_bit(d);
  }
}

ds_status_t ds_arith_decode(ds_arith_decoder_t *d,
                            const ds_arith_model_t *model, size_t *out_symbol) {
  unsigned long range;
  unsigned long total;
  unsigned long cum;
  unsigned long lo;
  unsigned long hi;
  size_t sym;
  if (d == NULL || model == NULL || out_symbol == NULL) {
    return DS_ERR_NULL;
  }
  total = model->total(model->state);
  if (total == 0 || total > DS_ARITH_MAX_TOTAL) {
    return DS_ERR_INVALID;
  }
  range = d->high - d->low + 1;
  cum = ((d->value - d->low + 1) * total - 1) / range;
  if (cum >= total) {
    return DS_ERR_INVALID;
  }
  sym = model->find(cum, &lo, &hi, model->state);
  d->high = d->low + range * hi / total - 1;
  d->low = d->low + range * lo / total;
  for (;;) {
    if (d->high < AC_HALF) {
      /* nothing */
    } else if (d->low >= AC_HALF) {
      d->value -= AC_HALF;
      d->low -= AC_HALF;
      d->high -= AC_HALF;
    } else if (d->low >= AC_FIRST_QTR && d->high < AC_THIRD_QTR) {
      d->value -= AC_FIRST_QTR;
      d->low -= AC_FIRST_QTR;
      d->high -= AC_FIRST_QTR;
    } else {
      break;
    }
    d->low = 2 * d->low;
    d->high = 2 * d->high + 1;
    d->value = 2 * d->value + ac_next_bit(d);
  }
  if (model->update != NULL) {
    model->update(sym, model->state);
  }
  *out_symbol = sym;
  return DS_OK;
}

/* Fenwick tree, 1-based. */
static unsigned long am_prefix(const ds_arith_adaptive_model_t *m, size_t i) {
  unsigned long s = 0;
  while (i > 0) {
    s += m->tree[i];
    i -= i & (~i + 1);
  }
  return s;
}

static void am_add(ds_arith_adaptive_model_t *m, size_t i, unsigned long v) {
  i++;
  while (i <= m->symbols) {
    m->tree[i] += v;
    i += i & (~i + 1);
  }
}

static void am_range(size_t symbol, unsigned long *low, unsigned long *high,
                     void *state) {
  const ds_arith_adaptive_model_t *m = (const ds_arith_adaptive_model_t *)state;
  *low = am_prefix(m, symbol);
  *high = am_prefix(m, symbol + 1);
}

static size_t am_find(unsigned long target, unsigned long *low,
                      unsigned long *high, void *state) {
  const ds_arith_adaptive_model_t *m = (const ds_arith_adaptive_model_t *)state;
  size_t pos = 0;
  size_t step = m->top_bit;
  unsigned long acc = 0;
  /* largest pos with prefix(pos) <= target */
  while (step > 0) {
    if (pos + step <= m->symbols && acc + m->tree[pos + step] <= target) {
      pos += step;
      acc += m->tree[pos];
    }
    step >>= 1;
  }
  *low = acc;
  *high = am_prefix(m, pos + 1);
  return pos;
}

static unsigned long am_total(void *state) {
  return ((const ds_arith_adaptive_model_t *)state)->total;
}

static void am_update(size_t symbol, void *state) {
  ds_arith_adaptive_model_t *m = (ds_arith_adaptive_model_t *)state;
  size_t i;
  unsigned long c;
  unsigned long *cnt;
  am_add(m, symbol, 1);
  m->total++;
  if (m->total < DS_ARITH_MAX_TOTAL) {
    return;
  }
  /* halve every count (keeping at least 1) and rebuild */
  cnt = m->tree + m->symbols + 1; /* scratch area */
  for (i = 0; i < m->symbols; i++) {
    cnt[i] = am_prefix(m, i + 1) - am_prefix(m, i);
  }
  memset(m->tree, 0, (m->symbols + 1) * sizeof(unsigned long));
  m->total = 0;
  for (i = 0; i < m->symbols; i++) {
    c = (cnt[i] + 1) / 2;
    am_add(m, i, c);
    m->total += c;
  }
}

ds_status_t ds_arith_adaptive_model_init(ds_arith_adaptive_model_t *m,
                                         size_t symbols,
                                         ds_context_t *context) {
  size_t i;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  if (symbols == 0 || symbols > 4096) {
    return DS_ERR_INVALID;
  }
  m->tree = (unsigned long *)ds_context_calloc(context, 2 * symbols + 1,
                                               sizeof(unsigned long));
  if (m->tree == NULL) {
    return DS_ERR_ALLOC;
  }
  m->symbols = symbols;
  m->total = 0;
  m->context = context;
  m->top_bit = 1;
  while (m->top_bit * 2 <= symbols) {
    m->top_bit *= 2;
  }
  for (i = 0; i < symbols; i++) {
    am_add(m, i, 1);
  }
  m->total = (unsigned long)symbols;
  return DS_OK;
}

void ds_arith_adaptive_model_destroy(ds_arith_adaptive_model_t *m) {
  if (m != NULL) {
    ds_context_free(m->context, m->tree);
    m->tree = NULL;
  }
}

void ds_arith_adaptive_model_bind(ds_arith_adaptive_model_t *m,
                                  ds_arith_model_t *out) {
  out->range = am_range;
  out->find = am_find;
  out->total = am_total;
  out->update = am_update;
  out->state = m;
}

static void cz_put32(unsigned char *p, size_t v) {
  p[0] = (unsigned char)((v >> 24) & 0xFFu);
  p[1] = (unsigned char)((v >> 16) & 0xFFu);
  p[2] = (unsigned char)((v >> 8) & 0xFFu);
  p[3] = (unsigned char)(v & 0xFFu);
}

static size_t cz_get32(const unsigned char *p) {
  return ((size_t)p[0] << 24) | ((size_t)p[1] << 16) | ((size_t)p[2] << 8) |
         (size_t)p[3];
}

size_t ds_arith_compress_bound(size_t n) {
  if (n > (((size_t)-1) - 32) / 2) {
    return (size_t)-1;
  }
  return 2 * n + 32;
}

ds_status_t ds_arith_compress(const unsigned char *in, size_t n,
                              unsigned char *out, size_t capacity,
                              size_t *out_length, ds_context_t *context) {
  ds_arith_adaptive_model_t am;
  ds_arith_model_t model;
  ds_arith_encoder_t enc;
  ds_bit_writer_t w;
  size_t i;
  ds_status_t st;
  if ((in == NULL && n > 0) || out == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n > 0xFFFFFFFFUL) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity < 4) {
    return DS_ERR_OVERFLOW;
  }
  cz_put32(out, n);
  st = ds_arith_adaptive_model_init(&am, 256, context);
  if (st != DS_OK) {
    return st;
  }
  ds_arith_adaptive_model_bind(&am, &model);
  ds_bit_writer_init(&w, out + 4, capacity - 4);
  ds_arith_encoder_init(&enc, &w);
  for (i = 0; i < n && st == DS_OK; i++) {
    st = ds_arith_encode(&enc, &model, in[i]);
  }
  if (st == DS_OK) {
    st = ds_arith_encoder_finish(&enc);
  }
  ds_arith_adaptive_model_destroy(&am);
  if (st == DS_OK) {
    *out_length = 4 + ds_bit_writer_bytes(&w);
  }
  return st;
}

ds_status_t ds_arith_decompress(const unsigned char *in, size_t n,
                                unsigned char *out, size_t capacity,
                                size_t *out_length, ds_context_t *context) {
  ds_arith_adaptive_model_t am;
  ds_arith_model_t model;
  ds_arith_decoder_t dec;
  ds_bit_reader_t r;
  size_t count;
  size_t i;
  size_t sym;
  ds_status_t st;
  if (in == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 4) {
    return DS_ERR_INVALID;
  }
  count = cz_get32(in);
  *out_length = count;
  if (count > capacity) {
    return DS_ERR_OVERFLOW;
  }
  if (count > 0 && out == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_arith_adaptive_model_init(&am, 256, context);
  if (st != DS_OK) {
    return st;
  }
  ds_arith_adaptive_model_bind(&am, &model);
  ds_bit_reader_init(&r, in + 4, n - 4);
  ds_arith_decoder_init(&dec, &r);
  for (i = 0; i < count && st == DS_OK; i++) {
    st = ds_arith_decode(&dec, &model, &sym);
    out[i] = (unsigned char)sym;
  }
  ds_arith_adaptive_model_destroy(&am);
  return st;
}

/* ---- LZW ------------------------------------------------------------ */

#define LZW_CLEAR 256UL
#define LZW_END 257UL
#define LZW_FIRST 258UL
#define LZW_EMPTY 0xFFFFFFFFUL

static unsigned lzw_width(unsigned long d) {
  /* bits to write values up to d - 1, at least 9 */
  unsigned b = 9;
  while ((1UL << b) < d) {
    b++;
  }
  return b;
}

size_t ds_lzw_compress_bound(size_t n) {
  if (n > (((size_t)-1) - 16) / 3) {
    return (size_t)-1;
  }
  return 3 * n + 16;
}

typedef struct lzw_enc {
  unsigned long *keys;
  unsigned long *vals;
  size_t mask;
  unsigned long next;
  unsigned long max;
} lzw_enc_t;

static size_t lzw_slot(const lzw_enc_t *z, unsigned long key) {
  size_t h = (size_t)((key * 2654435761UL) & 0xFFFFFFFFUL) & z->mask;
  while (z->keys[h] != LZW_EMPTY && z->keys[h] != key) {
    h = (h + 1) & z->mask;
  }
  return h;
}

static void lzw_enc_reset(lzw_enc_t *z) {
  size_t i;
  for (i = 0; i <= z->mask; i++) {
    z->keys[i] = LZW_EMPTY;
  }
  z->next = LZW_FIRST;
}

ds_status_t ds_lzw_compress(const unsigned char *in, size_t n,
                            unsigned max_bits, ds_lzw_full_policy_t policy,
                            unsigned char *out, size_t capacity,
                            size_t *out_length, ds_context_t *context) {
  lzw_enc_t z;
  ds_bit_writer_t w;
  unsigned long cur;
  unsigned long key;
  size_t slot;
  size_t i;
  size_t tsize;
  ds_status_t st = DS_OK;
  if ((in == NULL && n > 0) || out == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (max_bits < 9 || max_bits > 16 ||
      (policy != DS_LZW_FREEZE && policy != DS_LZW_RESET)) {
    return DS_ERR_INVALID;
  }
  if (capacity < 2) {
    return DS_ERR_OVERFLOW;
  }
  out[0] = (unsigned char)max_bits;
  out[1] = (unsigned char)policy;
  z.max = 1UL << max_bits;
  tsize = (size_t)1 << (max_bits + 1);
  z.mask = tsize - 1;
  z.keys = (unsigned long *)ds_context_alloc(context,
                                             2 * tsize * sizeof(unsigned long));
  if (z.keys == NULL) {
    return DS_ERR_ALLOC;
  }
  z.vals = z.keys + tsize;
  lzw_enc_reset(&z);
  ds_bit_writer_init(&w, out + 2, capacity - 2);
  if (n > 0) {
    cur = in[0];
    for (i = 1; i < n && st == DS_OK; i++) {
      key = (cur << 8) | in[i];
      slot = lzw_slot(&z, key);
      if (z.keys[slot] == key) {
        cur = z.vals[slot];
        continue;
      }
      st = ds_bit_writer_put(&w, cur, lzw_width(z.next));
      if (st != DS_OK) {
        break;
      }
      if (z.next < z.max) {
        z.keys[slot] = key;
        z.vals[slot] = z.next++;
        if (z.next == z.max && policy == DS_LZW_RESET) {
          st = ds_bit_writer_put(&w, LZW_CLEAR, lzw_width(z.next));
          lzw_enc_reset(&z);
        }
      }
      cur = in[i];
    }
    if (st == DS_OK) {
      st = ds_bit_writer_put(&w, cur, lzw_width(z.next));
    }
  }
  if (st == DS_OK) {
    /* the decoder has added the entry for the last code by now */
    st = ds_bit_writer_put(
        &w, LZW_END,
        lzw_width(n == 0 ? z.next : (z.next + 1 < z.max ? z.next + 1 : z.max)));
  }
  ds_context_free(context, z.keys);
  if (st == DS_OK) {
    *out_length = 2 + ds_bit_writer_bytes(&w);
  }
  return st;
}

ds_status_t ds_lzw_decompress(const unsigned char *in, size_t n,
                              unsigned char *out, size_t capacity,
                              size_t *out_length, ds_context_t *context) {
  unsigned long *prefix;
  unsigned char *suffix;
  unsigned char *stack;
  ds_bit_reader_t r;
  unsigned max_bits;
  int policy;
  unsigned long max;
  unsigned long next = LZW_FIRST;
  unsigned long code;
  unsigned long prev = LZW_EMPTY;
  unsigned long c;
  unsigned long d;
  size_t o = 0;
  size_t sp;
  size_t i;
  unsigned char first;
  int fresh = 1;
  ds_status_t st = DS_OK;
  if (in == NULL || out_length == NULL || (out == NULL && capacity > 0)) {
    return DS_ERR_NULL;
  }
  if (n < 2) {
    return DS_ERR_INVALID;
  }
  max_bits = in[0];
  policy = in[1];
  if (max_bits < 9 || max_bits > 16 || policy > 1) {
    return DS_ERR_INVALID;
  }
  max = 1UL << max_bits;
  prefix = (unsigned long *)ds_context_alloc(
      context, (size_t)max * sizeof(unsigned long));
  suffix = (unsigned char *)ds_context_alloc(context, 2 * (size_t)max + 1);
  if (prefix == NULL || suffix == NULL) {
    ds_context_free(context, prefix);
    ds_context_free(context, suffix);
    return DS_ERR_ALLOC;
  }
  stack = suffix + max;
  ds_bit_reader_init(&r, in + 2, n - 2);
  for (;;) {
    d = fresh ? LZW_FIRST : (next + 1 < max ? next + 1 : max);
    if (ds_bit_reader_get(&r, lzw_width(d), &code) != DS_OK) {
      st = DS_ERR_INVALID;
      break;
    }
    if (code == LZW_END) {
      break;
    }
    if (code == LZW_CLEAR) {
      next = LZW_FIRST;
      fresh = 1;
      prev = LZW_EMPTY;
      continue;
    }
    if (fresh) {
      if (code >= 256) {
        st = DS_ERR_INVALID;
        break;
      }
      sp = 0;
      stack[sp++] = (unsigned char)code;
    } else {
      if (code > next || (code == next && next >= max) ||
          (code >= 256 && code < LZW_FIRST)) {
        st = DS_ERR_INVALID;
        break;
      }
      /* string of code (or of prev for the KwKwK case), reversed */
      sp = 0;
      c = code == next ? prev : code;
      while (c >= LZW_FIRST) {
        stack[sp++] = suffix[c];
        c = prefix[c];
      }
      stack[sp++] = (unsigned char)c;
      first = (unsigned char)c;
      if (code == next) {
        /* prepend the extra first character at the end of the string */
        memmove(stack + 1, stack, sp);
        stack[0] = first;
        sp++;
      }
      if (next < max) {
        prefix[next] = prev;
        suffix[next] = first;
        next++;
      }
    }
    if (o + sp > capacity) {
      st = DS_ERR_OVERFLOW;
      o += sp;
      break;
    }
    for (i = sp; i-- > 0;) {
      out[o++] = stack[i];
    }
    prev = code;
    fresh = 0;
  }
  *out_length = o;
  ds_context_free(context, prefix);
  ds_context_free(context, suffix);
  return st;
}

/* ---- move-to-front and BSTW ----------------------------------------- */

void ds_move_to_front_encode(const unsigned char *in, size_t n,
                             unsigned char *out) {
  unsigned char list[256];
  size_t i;
  unsigned j;
  unsigned char c;
  if (in == NULL || out == NULL) {
    return;
  }
  for (j = 0; j < 256; j++) {
    list[j] = (unsigned char)j;
  }
  for (i = 0; i < n; i++) {
    c = in[i];
    for (j = 0; list[j] != c; j++) {
    }
    memmove(list + 1, list, j);
    list[0] = c;
    out[i] = (unsigned char)j;
  }
}

void ds_move_to_front_decode(const unsigned char *in, size_t n,
                             unsigned char *out) {
  unsigned char list[256];
  size_t i;
  unsigned j;
  unsigned char c;
  if (in == NULL || out == NULL) {
    return;
  }
  for (j = 0; j < 256; j++) {
    list[j] = (unsigned char)j;
  }
  for (i = 0; i < n; i++) {
    j = in[i];
    c = list[j];
    memmove(list + 1, list, j);
    list[0] = c;
    out[i] = c;
  }
}

static ds_status_t bstw_gamma_put(ds_bit_writer_t *w, unsigned long p) {
  unsigned nbits = 0;
  ds_status_t st;
  while ((p >> nbits) > 1) {
    nbits++;
  }
  st = ds_bit_writer_put(w, 0, nbits);
  if (st == DS_OK) {
    st = ds_bit_writer_put(w, p, nbits + 1);
  }
  return st;
}

static ds_status_t bstw_gamma_get(ds_bit_reader_t *r, unsigned long *p) {
  unsigned zeros = 0;
  unsigned long b;
  unsigned long rest;
  ds_status_t st;
  for (;;) {
    st = ds_bit_reader_get(r, 1, &b);
    if (st != DS_OK) {
      return DS_ERR_INVALID;
    }
    if (b) {
      break;
    }
    zeros++;
    if (zeros > 16) {
      return DS_ERR_INVALID;
    }
  }
  st = ds_bit_reader_get(r, zeros, &rest);
  if (st != DS_OK) {
    return DS_ERR_INVALID;
  }
  *p = (1UL << zeros) | rest;
  return DS_OK;
}

size_t ds_bstw_compress_bound(size_t n) {
  if (n > (((size_t)-1) - 8) / 4) {
    return (size_t)-1;
  }
  return 4 * n + 8;
}

ds_status_t ds_bstw_compress(const unsigned char *in, size_t n,
                             unsigned char *out, size_t capacity,
                             size_t *out_length) {
  unsigned char list[256];
  size_t size = 0;
  size_t i;
  size_t j;
  ds_bit_writer_t w;
  ds_status_t st = DS_OK;
  if ((in == NULL && n > 0) || out == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n > 0xFFFFFFFFUL) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity < 4) {
    return DS_ERR_OVERFLOW;
  }
  cz_put32(out, n);
  ds_bit_writer_init(&w, out + 4, capacity - 4);
  for (i = 0; i < n && st == DS_OK; i++) {
    for (j = 0; j < size && list[j] != in[i]; j++) {
    }
    st = bstw_gamma_put(&w, (unsigned long)(j + 1));
    if (st == DS_OK && j == size) {
      st = ds_bit_writer_put(&w, in[i], 8);
      size++;
    }
    memmove(list + 1, list, j);
    list[0] = in[i];
  }
  if (st == DS_OK) {
    *out_length = 4 + ds_bit_writer_bytes(&w);
  }
  return st;
}

ds_status_t ds_bstw_decompress(const unsigned char *in, size_t n,
                               unsigned char *out, size_t capacity,
                               size_t *out_length) {
  unsigned char list[256];
  size_t size = 0;
  size_t count;
  size_t i;
  unsigned long p;
  unsigned long raw;
  unsigned char c;
  ds_bit_reader_t r;
  ds_status_t st;
  if (in == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 4) {
    return DS_ERR_INVALID;
  }
  count = cz_get32(in);
  *out_length = count;
  if (count > capacity) {
    return DS_ERR_OVERFLOW;
  }
  if (count > 0 && out == NULL) {
    return DS_ERR_NULL;
  }
  ds_bit_reader_init(&r, in + 4, n - 4);
  for (i = 0; i < count; i++) {
    st = bstw_gamma_get(&r, &p);
    if (st != DS_OK || p > size + 1) {
      return DS_ERR_INVALID;
    }
    if (p == size + 1) {
      if (size == 256 || ds_bit_reader_get(&r, 8, &raw) != DS_OK) {
        return DS_ERR_INVALID;
      }
      c = (unsigned char)raw;
      size++;
    } else {
      c = list[p - 1];
    }
    memmove(list + 1, list, (size_t)(p - 1));
    list[0] = c;
    out[i] = c;
  }
  return DS_OK;
}

/* ---- star encoding -------------------------------------------------- */

typedef struct star_dict {
  const unsigned char *const *words;
  const size_t *lens;
  size_t *by_word; /* indexes sorted by (length, bytes) */
  size_t *by_rank; /* indexes sorted by (length, index) */
  size_t *rank;    /* per word: rank within its length */
  size_t count;
} star_dict_t;

static int star_letter(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int star_digit(unsigned char c) {
  return c >= 'a' && c <= 'z' ? c - 'a' : c - 'A' + 26;
}

static unsigned char star_char(size_t d) {
  return (unsigned char)(d < 26 ? 'a' + d : 'A' + (d - 26));
}

static int star_cmp_word(const void *a, const void *b, void *user) {
  const star_dict_t *s = (const star_dict_t *)user;
  size_t x = *(const size_t *)a;
  size_t y = *(const size_t *)b;
  if (s->lens[x] != s->lens[y]) {
    return s->lens[x] < s->lens[y] ? -1 : 1;
  }
  return memcmp(s->words[x], s->words[y], s->lens[x]);
}

static int star_cmp_rank(const void *a, const void *b, void *user) {
  const star_dict_t *s = (const star_dict_t *)user;
  size_t x = *(const size_t *)a;
  size_t y = *(const size_t *)b;
  if (s->lens[x] != s->lens[y]) {
    return s->lens[x] < s->lens[y] ? -1 : 1;
  }
  return (x > y) - (x < y);
}

static void star_dict_free(star_dict_t *s, ds_context_t *ctx) {
  ds_context_free(ctx, s->by_word);
}

static ds_status_t star_dict_init(star_dict_t *s,
                                  const unsigned char *const *words,
                                  const size_t *lens, size_t count,
                                  ds_context_t *ctx) {
  size_t i;
  size_t j;
  s->words = words;
  s->lens = lens;
  s->count = count;
  s->by_word = NULL;
  if (count > 0 && (words == NULL || lens == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < count; i++) {
    if (lens[i] == 0 || words[i] == NULL) {
      return DS_ERR_INVALID;
    }
    for (j = 0; j < lens[i]; j++) {
      if (!star_letter(words[i][j])) {
        return DS_ERR_INVALID;
      }
    }
  }
  if (count > ((size_t)-1) / (3 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  s->by_word = (size_t *)ds_context_alloc(ctx, (count == 0 ? 1 : 3 * count) *
                                                   sizeof(size_t));
  if (s->by_word == NULL) {
    return DS_ERR_ALLOC;
  }
  s->by_rank = s->by_word + count;
  s->rank = s->by_rank + count;
  for (i = 0; i < count; i++) {
    s->by_word[i] = i;
    s->by_rank[i] = i;
  }
  ds_intro_sort(s->by_word, count, sizeof(size_t), star_cmp_word, s);
  ds_intro_sort(s->by_rank, count, sizeof(size_t), star_cmp_rank, s);
  for (i = 1; i < count; i++) {
    if (star_cmp_word(&s->by_word[i - 1], &s->by_word[i], s) == 0) {
      star_dict_free(s, ctx);
      return DS_ERR_INVALID;
    }
  }
  for (i = 0; i < count; i++) {
    s->rank[s->by_rank[i]] =
        i > 0 && lens[s->by_rank[i - 1]] == lens[s->by_rank[i]]
            ? s->rank[s->by_rank[i - 1]] + 1
            : 0;
  }
  return DS_OK;
}

static size_t star_find(const star_dict_t *s, const unsigned char *w,
                        size_t len) {
  size_t lo = 0;
  size_t hi = s->count;
  size_t mid;
  size_t x;
  int c;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    x = s->by_word[mid];
    if (s->lens[x] != len) {
      c = s->lens[x] < len ? -1 : 1;
    } else {
      c = memcmp(s->words[x], w, len);
    }
    if (c == 0) {
      return x;
    }
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return (size_t)-1;
}

typedef struct star_out {
  unsigned char *p;
  size_t cap;
  size_t len;
} star_out_t;

static void star_put(star_out_t *o, unsigned char c) {
  if (o->p != NULL && o->len < o->cap) {
    o->p[o->len] = c;
  }
  o->len++;
}

ds_status_t ds_star_encode(const unsigned char *text, size_t n,
                           const unsigned char *const *words,
                           const size_t *lens, size_t count, unsigned char *out,
                           size_t capacity, size_t *out_length,
                           ds_context_t *context) {
  star_dict_t s;
  star_out_t o;
  size_t i = 0;
  size_t j;
  size_t k;
  size_t x;
  size_t r;
  size_t digits;
  unsigned char dig[64];
  ds_status_t st;
  if ((text == NULL && n > 0) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  st = star_dict_init(&s, words, lens, count, context);
  if (st != DS_OK) {
    return st;
  }
  o.p = out;
  o.cap = capacity;
  o.len = 0;
  while (i < n) {
    if (!star_letter(text[i])) {
      if (text[i] == '*' || text[i] == '\\') {
        star_put(&o, '\\');
      }
      star_put(&o, text[i]);
      i++;
      continue;
    }
    for (j = i; j < n && star_letter(text[j]); j++) {
    }
    x = star_find(&s, text + i, j - i);
    digits = 0;
    if (x != (size_t)-1) {
      for (r = s.rank[x]; r > 0 && digits < sizeof(dig); r /= 52) {
        dig[digits++] = star_char(r % 52);
      }
    }
    if (x != (size_t)-1 && digits < j - i) {
      for (k = 0; k < j - i - digits; k++) {
        star_put(&o, '*');
      }
      for (k = digits; k-- > 0;) {
        star_put(&o, dig[k]);
      }
    } else {
      for (k = i; k < j; k++) {
        star_put(&o, text[k]);
      }
    }
    i = j;
  }
  star_dict_free(&s, context);
  *out_length = o.len;
  if (o.len > capacity || (out == NULL && o.len > 0)) {
    return DS_ERR_OVERFLOW;
  }
  return DS_OK;
}

ds_status_t ds_star_decode(const unsigned char *in, size_t n,
                           const unsigned char *const *words,
                           const size_t *lens, size_t count, unsigned char *out,
                           size_t capacity, size_t *out_length,
                           ds_context_t *context) {
  star_dict_t s;
  star_out_t o;
  size_t i = 0;
  size_t j;
  size_t k;
  size_t stars;
  size_t r;
  size_t lo;
  size_t hi;
  size_t mid;
  size_t len;
  size_t x;
  ds_status_t st;
  if ((in == NULL && n > 0) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  st = star_dict_init(&s, words, lens, count, context);
  if (st != DS_OK) {
    return st;
  }
  o.p = out;
  o.cap = capacity;
  o.len = 0;
  while (i < n && st == DS_OK) {
    if (in[i] == '\\') {
      if (i + 1 >= n) {
        st = DS_ERR_INVALID;
        break;
      }
      star_put(&o, in[i + 1]);
      i += 2;
      continue;
    }
    if (!star_letter(in[i]) && in[i] != '*') {
      star_put(&o, in[i]);
      i++;
      continue;
    }
    for (j = i; j < n && (star_letter(in[j]) || in[j] == '*'); j++) {
    }
    for (stars = 0; i + stars < j && in[i + stars] == '*'; stars++) {
    }
    if (stars == 0) {
      for (k = i; k < j; k++) {
        star_put(&o, in[k]);
      }
      i = j;
      continue;
    }
    len = j - i;
    r = 0;
    for (k = i + stars; k < j; k++) {
      if (!star_letter(in[k])) {
        st = DS_ERR_INVALID;
        break;
      }
      r = r * 52 + (size_t)star_digit(in[k]);
    }
    if (st != DS_OK) {
      break;
    }
    /* first word of this length in rank order */
    lo = 0;
    hi = s.count;
    while (lo < hi) {
      mid = lo + (hi - lo) / 2;
      if (s.lens[s.by_rank[mid]] < len) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo + r >= s.count || s.lens[s.by_rank[lo + r]] != len) {
      st = DS_ERR_INVALID;
      break;
    }
    x = s.by_rank[lo + r];
    for (k = 0; k < len; k++) {
      star_put(&o, s.words[x][k]);
    }
    i = j;
  }
  star_dict_free(&s, context);
  *out_length = o.len;
  if (st != DS_OK) {
    return st;
  }
  if (o.len > capacity || (out == NULL && o.len > 0)) {
    return DS_ERR_OVERFLOW;
  }
  return DS_OK;
}
