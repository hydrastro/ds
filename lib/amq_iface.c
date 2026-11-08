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

static const ds_amq_ops_t amq_bloom_ops = {
    "bloom",   DS_AMQ_DYNAMIC, sizeof(amq_bloom_t), bl_init,         bl_destroy,
    bl_insert, bl_build,       bl_contains,         amq_unsupported, bl_bytes};
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
                                            ck_bytes};
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
                                              qt_bytes};
static const ds_amq_ops_t amq_xor_ops = {
    "xor filter",    0,        sizeof(amq_xor_t), xo_init,         xo_destroy,
    amq_unsupported, xo_build, xo_contains,       amq_unsupported, xo_bytes};

const ds_amq_ops_t *ds_amq_bloom(void) { return &amq_bloom_ops; }
const ds_amq_ops_t *ds_amq_cuckoo(void) { return &amq_cuckoo_ops; }
const ds_amq_ops_t *ds_amq_quotient(void) { return &amq_quotient_ops; }
const ds_amq_ops_t *ds_amq_xor(void) { return &amq_xor_ops; }

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
