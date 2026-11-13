#include "amq_iface.h"
#include "checked.h"
#include "cuckoo_filter.h"
#include "quotient_filter.h"
#include "xor_filter.h"
#include <limits.h>
#include <string.h>

#define AMQ_W (sizeof(unsigned long) * CHAR_BIT)

static unsigned long amq_mix(unsigned long h) {
#if ULONG_MAX > 0xFFFFFFFFUL
  h ^= h >> 33;
  h *= 0xFF51AFD7ED558CCDUL;
  h ^= h >> 33;
  h *= 0xC4CEB9FE1A85EC53UL;
  h ^= h >> 33;
#else
  h ^= h >> 16;
  h *= 0x85EBCA6BUL;
  h ^= h >> 13;
  h *= 0xC2B2AE35UL;
  h ^= h >> 16;
#endif
  return h;
}

/* batch insert for dynamic filters */
static ds_status_t
amq_build_loop(void *f, const unsigned long *hashes, size_t n,
               ds_status_t (*insert)(void *, unsigned long)) {
  size_t i;
  for (i = 0; i < n; i++) {
    ds_status_t st = insert(f, hashes[i]);
    if (DS_FAILED(st)) {
      return st;
    }
  }
  return DS_OK;
}

static ds_status_t amq_unsupported(void *f, unsigned long hash) {
  (void)f;
  (void)hash;
  return DS_ERR_UNSUPPORTED;
}

/* ---- image encoding ---- */

#define AMQ_MAGIC 0x51415344UL /* bytes "DSAQ" */
#define AMQ_VERSION 1u
#define AMQ_HEADER 16
#define AMQ_WB (sizeof(unsigned long)) /* bytes per word */
#define AMQ_BLOOM_MAX_K 64u

/* CRC-32C (Castagnoli, reflected 0x82F63B78), four bits at a time */
static const unsigned long amq_crc_nibble[16] = {
    0x00000000UL, 0x105EC76FUL, 0x20BD8EDEUL, 0x30E349B1UL,
    0x417B1DBCUL, 0x5125DAD3UL, 0x61C69362UL, 0x7198540DUL,
    0x82F63B78UL, 0x92A8FC17UL, 0xA24BB5A6UL, 0xB21572C9UL,
    0xC38D26C4UL, 0xD3D3E1ABUL, 0xE330A81AUL, 0xF36E6F75UL};

static unsigned long amq_crc32c(const unsigned char *p, size_t n) {
  unsigned long c = 0xFFFFFFFFUL;
  size_t i;
  for (i = 0; i < n; i++) {
    c ^= p[i];
    c = (c >> 4) ^ amq_crc_nibble[c & 15UL];
    c = (c >> 4) ^ amq_crc_nibble[c & 15UL];
  }
  return c ^ 0xFFFFFFFFUL;
}

static void amq_put32(unsigned char *p, unsigned long v) {
  p[0] = (unsigned char)(v & 0xFFUL);
  p[1] = (unsigned char)((v >> 8) & 0xFFUL);
  p[2] = (unsigned char)((v >> 16) & 0xFFUL);
  p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static unsigned long amq_get32(const unsigned char *p) {
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* 8-byte fields from size_t and unsigned long of any width */
static void amq_put_size(unsigned char *p, size_t v) {
  amq_put32(p, (unsigned long)(v & 0xFFFFFFFFUL));
  amq_put32(p + 4, (unsigned long)(((v >> 16) >> 16) & 0xFFFFFFFFUL));
}

static bool amq_get_size(const unsigned char *p, size_t *out) {
  unsigned long lo = amq_get32(p), hi = amq_get32(p + 4);
  size_t v = (size_t)lo, h = (size_t)hi;
  if ((unsigned long)v != lo || (unsigned long)h != hi ||
      (((h << 16) << 16) >> 16) >> 16 != h) {
    return false; /* does not fit in size_t */
  }
  *out = v | ((h << 16) << 16);
  return true;
}

static void amq_put_ulong(unsigned char *p, unsigned long v) {
  amq_put32(p, v & 0xFFFFFFFFUL);
  amq_put32(p + 4, ((v >> 16) >> 16) & 0xFFFFFFFFUL);
}

static bool amq_get_ulong(const unsigned char *p, unsigned long *out) {
  unsigned long hi = amq_get32(p + 4);
#if ULONG_MAX > 0xFFFFFFFFUL
  *out = amq_get32(p) | ((hi << 16) << 16);
  return true;
#else
  *out = amq_get32(p);
  return hi == 0;
#endif
}

/* bytes of a bit stream of n bits */
static size_t amq_nbytes(size_t nbits) {
  return nbits / 8 + (nbits % 8 != 0 ? 1 : 0);
}

/* the first nbits bits of a word array as a little-endian bit stream */
static void amq_bits_out(const unsigned long *w, size_t nbits,
                         unsigned char *out) {
  size_t n = amq_nbytes(nbits);
  size_t i;
  for (i = 0; i < n; i++) {
    out[i] = (unsigned char)((w[i / AMQ_WB] >> (8 * (i % AMQ_WB))) & 0xFFUL);
  }
  if (nbits % 8 != 0) {
    out[n - 1] = (unsigned char)(out[n - 1] & ((1U << (nbits % 8)) - 1U));
  }
}

/* the reverse, into zeroed words; false when a padding bit is set */
static bool amq_bits_in(const unsigned char *in, size_t nbits,
                        unsigned long *w) {
  size_t n = amq_nbytes(nbits);
  size_t i;
  if (nbits % 8 != 0 && (in[n - 1] >> (nbits % 8)) != 0) {
    return false;
  }
  for (i = 0; i < n; i++) {
    w[i / AMQ_WB] |= (unsigned long)in[i] << (8 * (i % AMQ_WB));
  }
  return true;
}

/* *len = a + b, failing on overflow */
static ds_status_t amq_payload_len(size_t a, size_t b, size_t *len) {
  return ds_size_add(a, b, len) ? DS_OK : DS_ERR_OVERFLOW;
}

/* ---- Bloom ---- */

typedef struct amq_bloom {
  unsigned long *bits;
  size_t m;
  unsigned k;
  ds_context_t *context;
} amq_bloom_t;

static ds_status_t bl_init(void *p, size_t capacity, unsigned fp_bits,
                           ds_context_t *ctx) {
  amq_bloom_t *b = (amq_bloom_t *)p;
  size_t m;
  memset(b, 0, sizeof(*b));
  if (fp_bits == 0) {
    fp_bits = 8;
  }
  if (fp_bits > AMQ_BLOOM_MAX_K) {
    fp_bits = AMQ_BLOOM_MAX_K;
  }
  /* 1.44 bits per key per halving of the false-positive rate */
  if (!ds_size_mul(capacity == 0 ? 1 : capacity, (size_t)fp_bits * 3 / 2 + 1,
                   &m)) {
    return DS_ERR_OVERFLOW;
  }
  b->m = m < 64 ? 64 : m;
  b->k = fp_bits;
  b->context = ctx;
  b->bits = (unsigned long *)ds_context_calloc(ctx, b->m / AMQ_W + 1,
                                               sizeof(unsigned long));
  return b->bits == NULL ? DS_ERR_ALLOC : DS_OK;
}

static void bl_destroy(void *p) {
  amq_bloom_t *b = (amq_bloom_t *)p;
  ds_context_free(b->context, b->bits);
  b->bits = NULL;
}

static ds_status_t bl_insert(void *p, unsigned long hash) {
  amq_bloom_t *b = (amq_bloom_t *)p;
  unsigned long h1 = amq_mix(hash), h2 = amq_mix(h1 ^ 0x9E3779B9UL) | 1UL;
  unsigned i;
  for (i = 0; i < b->k; i++) {
    size_t bit = (size_t)((h1 + (unsigned long)i * h2) % (unsigned long)b->m);
    b->bits[bit / AMQ_W] |= 1UL << (bit % AMQ_W);
  }
  return DS_OK;
}

static ds_status_t bl_build(void *p, const unsigned long *h, size_t n) {
  return amq_build_loop(p, h, n, bl_insert);
}

static bool bl_contains(const void *p, unsigned long hash) {
  const amq_bloom_t *b = (const amq_bloom_t *)p;
  unsigned long h1 = amq_mix(hash), h2 = amq_mix(h1 ^ 0x9E3779B9UL) | 1UL;
  unsigned i;
  for (i = 0; i < b->k; i++) {
    size_t bit = (size_t)((h1 + (unsigned long)i * h2) % (unsigned long)b->m);
    if (!((b->bits[bit / AMQ_W] >> (bit % AMQ_W)) & 1UL)) {
      return false;
    }
  }
  return true;
}

static size_t bl_bytes(const void *p) {
  const amq_bloom_t *b = (const amq_bloom_t *)p;
  return sizeof(*b) + (b->m / AMQ_W + 1) * sizeof(unsigned long);
}

static ds_status_t bl_save(const void *p, unsigned char *out, size_t cap,
                           size_t *len) {
  const amq_bloom_t *b = (const amq_bloom_t *)p;
  ds_status_t st = amq_payload_len(12, amq_nbytes(b->m), len);
  if (st != DS_OK || out == NULL) {
    return st;
  }
  if (cap < *len) {
    return DS_ERR_RANGE;
  }
  amq_put_size(out, b->m);
  amq_put32(out + 8, b->k);
  amq_bits_out(b->bits, b->m, out + 12);
  return DS_OK;
}

static ds_status_t bl_load(void *p, const unsigned char *in, size_t len,
                           ds_context_t *ctx) {
  amq_bloom_t *b = (amq_bloom_t *)p;
  size_t m;
  unsigned long k;
  memset(b, 0, sizeof(*b));
  if (len < 12 || !amq_get_size(in, &m) || m < 64) {
    return DS_ERR_INVALID;
  }
  k = amq_get32(in + 8);
  if (k < 1 || k > AMQ_BLOOM_MAX_K || len - 12 != amq_nbytes(m)) {
    return DS_ERR_INVALID;
  }
  b->bits = (unsigned long *)ds_context_calloc(ctx, m / AMQ_W + 1,
                                               sizeof(unsigned long));
  if (b->bits == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!amq_bits_in(in + 12, m, b->bits)) {
    ds_context_free(ctx, b->bits);
    b->bits = NULL;
    return DS_ERR_INVALID;
  }
  b->m = m;
  b->k = (unsigned)k;
  b->context = ctx;
  return DS_OK;
}

/* ---- cuckoo filter ---- */

static ds_status_t ck_init(void *p, size_t capacity, unsigned fp_bits,
                           ds_context_t *ctx) {
  (void)fp_bits;
  return ds_cuckoo_filter_init((ds_cuckoo_filter_t *)p,
                               ds_cuckoo_filter_buckets_for(capacity), ctx);
}

static void ck_destroy(void *p) {
  ds_cuckoo_filter_destroy((ds_cuckoo_filter_t *)p);
}

static ds_status_t ck_insert(void *p, unsigned long hash) {
  return ds_cuckoo_filter_insert((ds_cuckoo_filter_t *)p, hash);
}

static ds_status_t ck_build(void *p, const unsigned long *h, size_t n) {
  return amq_build_loop(p, h, n, ck_insert);
}

static bool ck_contains(const void *p, unsigned long hash) {
  return ds_cuckoo_filter_contains((const ds_cuckoo_filter_t *)p, hash);
}

static ds_status_t ck_remove(void *p, unsigned long hash) {
  return ds_cuckoo_filter_remove((ds_cuckoo_filter_t *)p, hash);
}

static size_t ck_bytes(const void *p) {
  const ds_cuckoo_filter_t *c = (const ds_cuckoo_filter_t *)p;
  return sizeof(*c) + c->buckets * DS_CUCKOO_SLOTS * sizeof(unsigned short);
}

#define CK_HEAD 31

static ds_status_t ck_save(const void *p, unsigned char *out, size_t cap,
                           size_t *len) {
  const ds_cuckoo_filter_t *c = (const ds_cuckoo_filter_t *)p;
  size_t n = c->buckets * DS_CUCKOO_SLOTS; /* allocated, so it fits */
  size_t i;
  ds_status_t st;
  if (c->table == NULL) {
    return DS_ERR_STATE;
  }
  st = amq_payload_len(CK_HEAD, 2 * n, len);
  if (st != DS_OK || out == NULL) {
    return st;
  }
  if (cap < *len) {
    return DS_ERR_RANGE;
  }
  amq_put_size(out, c->buckets);
  amq_put_size(out + 8, c->count);
  amq_put_size(out + 16, c->victim_bucket);
  amq_put32(out + 24, c->rng & 0xFFFFFFFFUL);
  out[28] = (unsigned char)(c->victim & 0xFFU);
  out[29] = (unsigned char)((c->victim >> 8) & 0xFFU);
  out[30] = (unsigned char)DS_CUCKOO_SLOTS;
  for (i = 0; i < n; i++) {
    out[CK_HEAD + 2 * i] = (unsigned char)(c->table[i] & 0xFFU);
    out[CK_HEAD + 2 * i + 1] = (unsigned char)((c->table[i] >> 8) & 0xFFU);
  }
  return DS_OK;
}

static ds_status_t ck_load(void *p, const unsigned char *in, size_t len,
                           ds_context_t *ctx) {
  ds_cuckoo_filter_t *c = (ds_cuckoo_filter_t *)p;
  size_t buckets;
  size_t count;
  size_t vb;
  size_t need;
  size_t n;
  size_t i;
  size_t used = 0;
  unsigned long rng;
  unsigned victim;
  ds_status_t st;
  memset(c, 0, sizeof(*c));
  if (len < CK_HEAD || !amq_get_size(in, &buckets) ||
      !amq_get_size(in + 8, &count) || !amq_get_size(in + 16, &vb) ||
      in[30] != DS_CUCKOO_SLOTS) {
    return DS_ERR_INVALID;
  }
  rng = amq_get32(in + 24);
  victim = (unsigned)in[28] | ((unsigned)in[29] << 8);
  /* the table is a power of two of buckets, and the victim's bucket is
   * one of them: removing a key re-inserts the victim from there */
  if (buckets == 0 || (buckets & (buckets - 1)) != 0 || vb >= buckets ||
      rng > 0x7FFFFFFFUL || !ds_size_mul(buckets, 2 * DS_CUCKOO_SLOTS, &need) ||
      len - CK_HEAD != need) {
    return DS_ERR_INVALID;
  }
  st = ds_cuckoo_filter_init(c, buckets, ctx);
  if (st != DS_OK) {
    return st;
  }
  n = buckets * DS_CUCKOO_SLOTS;
  for (i = 0; i < n; i++) {
    unsigned v = (unsigned)in[CK_HEAD + 2 * i] |
                 ((unsigned)in[CK_HEAD + 2 * i + 1] << 8);
    c->table[i] = (unsigned short)v;
    if (v != 0) {
      used++;
    }
  }
  /* every stored copy is a fingerprint in a slot or the victim */
  if (count != used + (victim != 0 ? 1 : 0)) {
    ds_cuckoo_filter_destroy(c);
    return DS_ERR_INVALID;
  }
  c->count = count;
  c->rng = rng;
  c->victim = (unsigned short)victim;
  c->victim_bucket = vb;
  return DS_OK;
}

/* ---- quotient filter ---- */

static ds_status_t qt_init(void *p, size_t capacity, unsigned fp_bits,
                           ds_context_t *ctx) {
  unsigned q = 1;
  size_t need = capacity + capacity / 3 + 1; /* stay below 75% load */
  if (fp_bits == 0) {
    fp_bits = 8;
  }
  while (q < sizeof(size_t) * CHAR_BIT - 2 && ((size_t)1 << q) < need) {
    q++;
  }
  if (q + fp_bits > AMQ_W) {
    fp_bits = (unsigned)AMQ_W - q;
  }
  return ds_quotient_filter_init((ds_quotient_filter_t *)p, q, fp_bits, ctx);
}

static void qt_destroy(void *p) {
  ds_quotient_filter_destroy((ds_quotient_filter_t *)p);
}

static ds_status_t qt_insert(void *p, unsigned long hash) {
  /* multiset, so removing one of two colliding keys keeps the other */
  return ds_quotient_filter_insert_multi((ds_quotient_filter_t *)p,
                                         amq_mix(hash));
}

static ds_status_t qt_build(void *p, const unsigned long *h, size_t n) {
  return amq_build_loop(p, h, n, qt_insert);
}

static bool qt_contains(const void *p, unsigned long hash) {
  return ds_quotient_filter_contains((const ds_quotient_filter_t *)p,
                                     amq_mix(hash));
}

static ds_status_t qt_remove(void *p, unsigned long hash) {
  return ds_quotient_filter_remove((ds_quotient_filter_t *)p, amq_mix(hash));
}

static size_t qt_bytes(const void *p) {
  return ds_quotient_filter_bytes((const ds_quotient_filter_t *)p);
}

#define QT_HEAD 10

static ds_status_t qt_save(const void *p, unsigned char *out, size_t cap,
                           size_t *len) {
  const ds_quotient_filter_t *f = (const ds_quotient_filter_t *)p;
  size_t rb;
  size_t mb;
  size_t i;
  size_t o;
  unsigned long acc = 0;
  unsigned nb = 0;
  ds_status_t st;
  if (f->meta == NULL) {
    return DS_ERR_STATE;
  }
  /* both fit: the remainder bits are allocated and 3 slots < 2^bits */
  rb = amq_nbytes(f->slots * f->r);
  mb = amq_nbytes(3 * f->slots);
  st = amq_payload_len(QT_HEAD, rb, len);
  if (st == DS_OK) {
    st = amq_payload_len(*len, mb, len);
  }
  if (st != DS_OK || out == NULL) {
    return st;
  }
  if (cap < *len) {
    return DS_ERR_RANGE;
  }
  out[0] = (unsigned char)f->q;
  out[1] = (unsigned char)f->r;
  amq_put_size(out + 2, f->count);
  amq_bits_out(f->rem, f->slots * f->r, out + QT_HEAD);
  o = QT_HEAD + rb;
  for (i = 0; i < f->slots; i++) {
    acc |= (unsigned long)(f->meta[i] & 7U) << nb;
    nb += 3;
    if (nb >= 8) {
      out[o++] = (unsigned char)(acc & 0xFFUL);
      acc >>= 8;
      nb -= 8;
    }
  }
  if (nb > 0) {
    out[o] = (unsigned char)(acc & 0xFFUL);
  }
  return DS_OK;
}

static ds_status_t qt_load(void *p, const unsigned char *in, size_t len,
                           ds_context_t *ctx) {
  ds_quotient_filter_t *f = (ds_quotient_filter_t *)p;
  unsigned q;
  unsigned r;
  size_t slots;
  size_t count;
  size_t nbits;
  size_t need;
  size_t i;
  size_t o;
  unsigned long acc = 0;
  unsigned nb = 0;
  ds_status_t st;
  memset(f, 0, sizeof(*f));
  if (len < QT_HEAD || !amq_get_size(in + 2, &count)) {
    return DS_ERR_INVALID;
  }
  q = in[0];
  r = in[1];
  if (q == 0 || r == 0 || q + r > AMQ_W || q >= sizeof(size_t) * CHAR_BIT - 1) {
    return DS_ERR_INVALID;
  }
  slots = (size_t)1 << q;
  if (!ds_size_mul(slots, r, &nbits) ||
      !ds_size_add(QT_HEAD, amq_nbytes(nbits), &need) ||
      !ds_size_add(need, amq_nbytes(3 * slots), &need) || len != need) {
    return DS_ERR_INVALID;
  }
  st = ds_quotient_filter_init(f, q, r, ctx);
  if (st != DS_OK) {
    return st;
  }
  if (!amq_bits_in(in + QT_HEAD, nbits, f->rem)) {
    ds_quotient_filter_destroy(f);
    return DS_ERR_INVALID;
  }
  o = QT_HEAD + amq_nbytes(nbits);
  for (i = 0; i < slots; i++) {
    if (nb < 3) {
      acc |= (unsigned long)in[o++] << nb;
      nb += 8;
    }
    f->meta[i] = (unsigned char)(acc & 7UL);
    acc >>= 3;
    nb -= 3;
  }
  f->count = count;
  /* zero padding, then the slot structure the lookups rely on */
  if (acc != 0 || ds_quotient_filter_check(f) != DS_OK) {
    ds_quotient_filter_destroy(f);
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

/* ---- xor filter (static) ---- */

typedef struct amq_xor {
  ds_xor_filter_t f;
  unsigned fp_bits;
  ds_context_t *context;
  int built;
} amq_xor_t;

static ds_status_t xo_init(void *p, size_t capacity, unsigned fp_bits,
                           ds_context_t *ctx) {
  amq_xor_t *x = (amq_xor_t *)p;
  (void)capacity;
  memset(x, 0, sizeof(*x));
  x->fp_bits = fp_bits == 0 ? 8 : (fp_bits > 32 ? 32 : fp_bits);
  x->context = ctx;
  return DS_OK;
}

static void xo_destroy(void *p) {
  amq_xor_t *x = (amq_xor_t *)p;
  if (x->built) {
    ds_xor_filter_destroy(&x->f);
  }
  x->built = 0;
}

static ds_status_t xo_build(void *p, const unsigned long *h, size_t n) {
  amq_xor_t *x = (amq_xor_t *)p;
  ds_status_t st;
  if (x->built) {
    return DS_ERR_STATE;
  }
  st = ds_xor_filter_build(&x->f, h, n, x->fp_bits, x->context);
  if (st == DS_OK) {
    x->built = 1;
  }
  return st;
}

static bool xo_contains(const void *p, unsigned long hash) {
  const amq_xor_t *x = (const amq_xor_t *)p;
  return x->built && ds_xor_filter_contains(&x->f, hash);
}

static size_t xo_bytes(const void *p) {
  const amq_xor_t *x = (const amq_xor_t *)p;
  return sizeof(*x) + (x->built ? ds_xor_filter_bytes(&x->f) : 0);
}

#define XO_HEAD 26

static ds_status_t xo_save(const void *p, unsigned char *out, size_t cap,
                           size_t *len) {
  const amq_xor_t *x = (const amq_xor_t *)p;
  ds_status_t st = DS_OK;
  *len = 2;
  if (x->built) {
    /* allocated, so it fits */
    st =
        amq_payload_len(XO_HEAD, amq_nbytes(x->f.capacity * x->f.fp_bits), len);
  }
  if (st != DS_OK || out == NULL) {
    return st;
  }
  if (cap < *len) {
    return DS_ERR_RANGE;
  }
  out[0] = (unsigned char)x->fp_bits;
  out[1] = (unsigned char)(x->built ? 1 : 0);
  if (x->built) {
    amq_put_size(out + 2, x->f.segment);
    amq_put_size(out + 10, x->f.count);
    amq_put_ulong(out + 18, x->f.seed);
    amq_bits_out(x->f.table, x->f.capacity * x->f.fp_bits, out + XO_HEAD);
  }
  return DS_OK;
}

static ds_status_t xo_load(void *p, const unsigned char *in, size_t len,
                           ds_context_t *ctx) {
  amq_xor_t *x = (amq_xor_t *)p;
  size_t segment;
  size_t count;
  size_t capacity;
  size_t nbits;
  unsigned long seed;
  unsigned long *table;
  unsigned fp;
  memset(x, 0, sizeof(*x));
  if (len < 2) {
    return DS_ERR_INVALID;
  }
  fp = in[0];
  if (fp < 1 || fp > 32 || fp >= AMQ_W || in[1] > 1) {
    return DS_ERR_INVALID;
  }
  x->fp_bits = fp;
  x->context = ctx;
  if (in[1] == 0) {
    return len == 2 ? DS_OK : DS_ERR_INVALID;
  }
  if (len < XO_HEAD || !amq_get_size(in + 2, &segment) ||
      !amq_get_size(in + 10, &count) || !amq_get_ulong(in + 18, &seed) ||
      segment == 0 || !ds_size_mul(segment, 3, &capacity) ||
      !ds_size_mul(capacity, fp, &nbits) || count > capacity ||
      len - XO_HEAD != amq_nbytes(nbits)) {
    return DS_ERR_INVALID;
  }
  table = (unsigned long *)ds_context_calloc(ctx, nbits / AMQ_W + 2,
                                             sizeof(unsigned long));
  if (table == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!amq_bits_in(in + XO_HEAD, nbits, table)) {
    ds_context_free(ctx, table);
    return DS_ERR_INVALID;
  }
  x->f.table = table;
  x->f.capacity = capacity;
  x->f.segment = segment;
  x->f.count = count;
  x->f.fp_bits = fp;
  x->f.seed = seed;
  x->f.context = ctx;
  x->built = 1;
  return DS_OK;
}

static const ds_amq_ops_t amq_bloom_ops = {"bloom",
                                           DS_AMQ_DYNAMIC,
                                           sizeof(amq_bloom_t),
                                           bl_init,
                                           bl_destroy,
                                           bl_insert,
                                           bl_build,
                                           bl_contains,
                                           amq_unsupported,
                                           bl_bytes,
                                           1,
                                           bl_save,
                                           bl_load};
static const ds_amq_ops_t amq_cuckoo_ops = {"cuckoo filter",
                                            DS_AMQ_DYNAMIC | DS_AMQ_DELETE |
                                                DS_AMQ_EXACT_CAPACITY,
                                            sizeof(ds_cuckoo_filter_t),
                                            ck_init,
                                            ck_destroy,
                                            ck_insert,
                                            ck_build,
                                            ck_contains,
                                            ck_remove,
                                            ck_bytes,
                                            2,
                                            ck_save,
                                            ck_load};
static const ds_amq_ops_t amq_quotient_ops = {"quotient filter",
                                              DS_AMQ_DYNAMIC | DS_AMQ_DELETE |
                                                  DS_AMQ_EXACT_CAPACITY,
                                              sizeof(ds_quotient_filter_t),
                                              qt_init,
                                              qt_destroy,
                                              qt_insert,
                                              qt_build,
                                              qt_contains,
                                              qt_remove,
                                              qt_bytes,
                                              3,
                                              qt_save,
                                              qt_load};
static const ds_amq_ops_t amq_xor_ops = {
    "xor filter", 0,           sizeof(amq_xor_t),
    xo_init,      xo_destroy,  amq_unsupported,
    xo_build,     xo_contains, amq_unsupported,
    xo_bytes,     4,           xo_save,
    xo_load};

const ds_amq_ops_t *ds_amq_bloom(void) { return &amq_bloom_ops; }
const ds_amq_ops_t *ds_amq_cuckoo(void) { return &amq_cuckoo_ops; }
const ds_amq_ops_t *ds_amq_quotient(void) { return &amq_quotient_ops; }
const ds_amq_ops_t *ds_amq_xor(void) { return &amq_xor_ops; }

const ds_amq_ops_t *ds_amq_builtin(unsigned id) {
  switch (id) {
  case 1:
    return &amq_bloom_ops;
  case 2:
    return &amq_cuckoo_ops;
  case 3:
    return &amq_quotient_ops;
  case 4:
    return &amq_xor_ops;
  default:
    return NULL;
  }
}

ds_status_t ds_amq_init(ds_amq_t *f, const ds_amq_ops_t *ops, size_t capacity,
                        unsigned fp_bits, ds_context_t *context) {
  ds_status_t st;
  if (f == NULL) {
    return DS_ERR_NULL;
  }
  if (ops == NULL) {
    ops = ds_amq_bloom();
  }
  f->ops = NULL;
  f->impl = ds_context_alloc(context, ops->instance_size);
  if (f->impl == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ops->init(f->impl, capacity, fp_bits, context);
  if (st != DS_OK) {
    ds_context_free(context, f->impl);
    f->impl = NULL;
    return st;
  }
  f->ops = ops;
  f->context = context;
  return DS_OK;
}

void ds_amq_destroy(ds_amq_t *f) {
  if (f == NULL || f->ops == NULL) {
    return;
  }
  f->ops->destroy(f->impl);
  ds_context_free(f->context, f->impl);
  f->ops = NULL;
  f->impl = NULL;
}

ds_status_t ds_amq_insert(ds_amq_t *f, unsigned long hash) {
  if (f == NULL || f->ops == NULL) {
    return DS_ERR_NULL;
  }
  return f->ops->insert(f->impl, hash);
}

ds_status_t ds_amq_build(ds_amq_t *f, const unsigned long *hashes, size_t n) {
  if (f == NULL || f->ops == NULL || (hashes == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  return f->ops->build(f->impl, hashes, n);
}

bool ds_amq_contains(const ds_amq_t *f, unsigned long hash) {
  if (f == NULL || f->ops == NULL) {
    return false;
  }
  return f->ops->contains(f->impl, hash);
}

ds_status_t ds_amq_remove(ds_amq_t *f, unsigned long hash) {
  if (f == NULL || f->ops == NULL) {
    return DS_ERR_NULL;
  }
  return f->ops->remove(f->impl, hash);
}

size_t ds_amq_bytes(const ds_amq_t *f) {
  if (f == NULL || f->ops == NULL) {
    return 0;
  }
  return f->ops->bytes(f->impl);
}

/* ---- images ---- */

ds_status_t ds_amq_save(const ds_amq_t *f, void *out, size_t cap,
                        size_t *out_len) {
  unsigned char *p = (unsigned char *)out;
  size_t plen;
  size_t plen2;
  size_t total;
  ds_status_t st;
  if (f == NULL || f->ops == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  if (f->ops->save == NULL || f->ops->id == 0 || f->ops->id > 255) {
    return DS_ERR_UNSUPPORTED;
  }
  st = f->ops->save(f->impl, NULL, 0, &plen);
  if (st != DS_OK) {
    return st;
  }
  if (!ds_size_add(plen, DS_AMQ_IMAGE_OVERHEAD, &total)) {
    return DS_ERR_OVERFLOW;
  }
  *out_len = total;
  if (p == NULL) {
    return DS_OK;
  }
  if (cap < total) {
    return DS_ERR_RANGE;
  }
  amq_put32(p, AMQ_MAGIC);
  p[4] = (unsigned char)AMQ_VERSION;
  p[5] = (unsigned char)f->ops->id;
  p[6] = (unsigned char)AMQ_W;
  p[7] = 0;
  amq_put_size(p + 8, total);
  st = f->ops->save(f->impl, p + AMQ_HEADER, plen, &plen2);
  if (st != DS_OK) {
    return st;
  }
  if (plen2 != plen) {
    return DS_ERR_INTERNAL;
  }
  amq_put32(p + total - 4, amq_crc32c(p, total - 4));
  return DS_OK;
}

ds_status_t ds_amq_image_info(const void *in, size_t avail,
                              ds_amq_image_t *info) {
  const unsigned char *p = (const unsigned char *)in;
  size_t length;
  if (p == NULL || info == NULL) {
    return DS_ERR_NULL;
  }
  memset(info, 0, sizeof(*info));
  if (avail < DS_AMQ_IMAGE_OVERHEAD || amq_get32(p) != AMQ_MAGIC) {
    return DS_ERR_INVALID;
  }
  info->version = p[4];
  if (info->version != AMQ_VERSION) {
    return DS_ERR_UNSUPPORTED;
  }
  if (p[7] != 0 || !amq_get_size(p + 8, &length) ||
      length < DS_AMQ_IMAGE_OVERHEAD || length > avail ||
      amq_get32(p + length - 4) != amq_crc32c(p, length - 4)) {
    return DS_ERR_INVALID;
  }
  info->id = p[5];
  info->word_bits = p[6];
  info->length = length;
  info->payload_offset = AMQ_HEADER;
  info->payload_length = length - DS_AMQ_IMAGE_OVERHEAD;
  return DS_OK;
}

ds_status_t ds_amq_load_with(ds_amq_t *f, const ds_amq_ops_t *ops,
                             const void *in, size_t len,
                             ds_context_t *context) {
  ds_amq_image_t info;
  ds_status_t st;
  if (f == NULL || in == NULL) {
    return DS_ERR_NULL;
  }
  f->ops = NULL;
  f->impl = NULL;
  st = ds_amq_image_info(in, len, &info);
  if (st != DS_OK) {
    return st;
  }
  if (info.length != len) {
    return DS_ERR_INVALID;
  }
  if (info.word_bits != AMQ_W) {
    return DS_ERR_UNSUPPORTED;
  }
  if (ops == NULL) {
    ops = ds_amq_builtin(info.id);
    if (ops == NULL) {
      return DS_ERR_UNSUPPORTED;
    }
  } else if (ops->id != info.id) {
    return DS_ERR_INVALID;
  }
  if (ops->load == NULL) {
    return DS_ERR_UNSUPPORTED;
  }
  f->impl = ds_context_alloc(context, ops->instance_size);
  if (f->impl == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ops->load(f->impl, (const unsigned char *)in + info.payload_offset,
                 info.payload_length, context);
  if (st != DS_OK) {
    ds_context_free(context, f->impl);
    f->impl = NULL;
    return st;
  }
  f->ops = ops;
  f->context = context;
  return DS_OK;
}

ds_status_t ds_amq_load(ds_amq_t *f, const void *in, size_t len,
                        ds_context_t *context) {
  return ds_amq_load_with(f, NULL, in, len, context);
}
