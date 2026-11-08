#include "rank_select.h"
#include "checked.h"
#include "elias_fano.h"
#include <limits.h>
#include <string.h>

#define RS_W (sizeof(unsigned long) * CHAR_BIT)
#define RS_SB 8U       /* words per rank superblock */
#define RS_SAMPLE 512U /* ones (zeros) per select hint */
#define RRR_B 15U      /* bits per RRR block */
#define RRR_SB 32U     /* blocks per RRR superblock */

static unsigned rs_popcount(unsigned long x) {
#if defined(__GNUC__)
  return (unsigned)__builtin_popcountl(x);
#else
  unsigned c = 0;
  while (x != 0) {
    x &= x - 1;
    c++;
  }
  return c;
#endif
}

static unsigned rs_ctz(unsigned long x) {
#if defined(__GNUC__)
  return (unsigned)__builtin_ctzl(x);
#else
  unsigned c = 0;
  while ((x & 1UL) == 0) {
    x >>= 1;
    c++;
  }
  return c;
#endif
}

/* position of the k-th set bit of x (k < popcount(x)) */
static unsigned rs_select_word(unsigned long x, size_t k) {
  while (k-- > 0) {
    x &= x - 1;
  }
  return rs_ctz(x);
}

static void *rs_array(ds_context_t *ctx, size_t count, size_t size, bool zero) {
  size_t bytes;
  if (count == 0) {
    count = 1;
  }
  if (!ds_size_mul(count, size, &bytes)) {
    return NULL;
  }
  return zero ? ds_context_calloc(ctx, count, size)
              : ds_context_alloc(ctx, bytes);
}

/* ---- plain: bits + rank directory + select hints ---- */

typedef struct rsp {
  unsigned long *words;
  size_t n;
  size_t nw;
  size_t ones;
  size_t *super; /* ones before word j * RS_SB, j = 0..nw / RS_SB + 1 */
  size_t nsuper;
  size_t *hint1; /* superblock holding the (t * RS_SAMPLE)-th one */
  size_t nhint1;
  size_t *hint0;
  size_t nhint0;
} rsp_t;

static void rsp_destroy(void *state, ds_context_t *ctx) {
  rsp_t *p = (rsp_t *)state;
  if (p == NULL) {
    return;
  }
  ds_context_free(ctx, p->words);
  ds_context_free(ctx, p->super);
  ds_context_free(ctx, p->hint1);
  ds_context_free(ctx, p->hint0);
  ds_context_free(ctx, p);
}

static size_t rsp_zeros_before_super(const rsp_t *p, size_t j) {
  size_t bits = j * RS_SB * RS_W;
  if (bits > p->n) {
    bits = p->n;
  }
  return bits - p->super[j];
}

static ds_status_t rsp_build(void **state, const unsigned long *words, size_t n,
                             ds_context_t *ctx) {
  rsp_t *p = (rsp_t *)ds_context_calloc(ctx, 1, sizeof(rsp_t));
  size_t j, w, zeros;
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  p->n = n;
  p->nw = n / RS_W + 1;
  p->nsuper = p->nw / RS_SB + 2;
  p->words = (unsigned long *)rs_array(ctx, p->nw, sizeof(unsigned long), 1);
  p->super = (size_t *)rs_array(ctx, p->nsuper, sizeof(size_t), 0);
  if (p->words == NULL || p->super == NULL) {
    rsp_destroy(p, ctx);
    return DS_ERR_ALLOC;
  }
  if (n > 0) {
    memcpy(p->words, words, ((n + RS_W - 1) / RS_W) * sizeof(unsigned long));
  }
  if (n % RS_W != 0) {
    p->words[n / RS_W] &= (1UL << (n % RS_W)) - 1UL;
  }
  for (j = 0; j < p->nsuper; j++) {
    p->super[j] = p->ones;
    for (w = j * RS_SB; w < (j + 1) * RS_SB && w < p->nw; w++) {
      p->ones += rs_popcount(p->words[w]);
    }
  }
  zeros = n - p->ones;
  p->nhint1 = p->ones / RS_SAMPLE + 1;
  p->nhint0 = zeros / RS_SAMPLE + 1;
  p->hint1 = (size_t *)rs_array(ctx, p->nhint1, sizeof(size_t), 0);
  p->hint0 = (size_t *)rs_array(ctx, p->nhint0, sizeof(size_t), 0);
  if (p->hint1 == NULL || p->hint0 == NULL) {
    rsp_destroy(p, ctx);
    return DS_ERR_ALLOC;
  }
  /* hint[t]: last superblock whose prefix count is <= t * RS_SAMPLE */
  j = 0;
  for (w = 0; w < p->nhint1; w++) {
    while (j + 1 < p->nsuper && p->super[j + 1] <= w * RS_SAMPLE) {
      j++;
    }
    p->hint1[w] = j;
  }
  j = 0;
  for (w = 0; w < p->nhint0; w++) {
    while (j + 1 < p->nsuper &&
           rsp_zeros_before_super(p, j + 1) <= w * RS_SAMPLE) {
      j++;
    }
    p->hint0[w] = j;
  }
  *state = p;
  return DS_OK;
}

static size_t rsp_rank1(const void *state, size_t i) {
  const rsp_t *p = (const rsp_t *)state;
  size_t w = i / RS_W, j, r = p->super[w / RS_SB];
  for (j = (w / RS_SB) * RS_SB; j < w; j++) {
    r += rs_popcount(p->words[j]);
  }
  if (i % RS_W != 0) {
    r += rs_popcount(p->words[w] & ((1UL << (i % RS_W)) - 1UL));
  }
  return r;
}

static size_t rsp_select(const rsp_t *p, size_t k, int bit) {
  size_t total = bit ? p->ones : p->n - p->ones;
  size_t t = k / RS_SAMPLE, lo, hi, w, r;
  const size_t *hint = bit ? p->hint1 : p->hint0;
  size_t nhint = bit ? p->nhint1 : p->nhint0;
  if (k >= total) {
    return p->n;
  }
  lo = hint[t];
  hi = t + 1 < nhint ? hint[t + 1] + 1 : p->nsuper;
  /* last superblock in [lo, hi) with count before it <= k */
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    size_t c = bit ? p->super[mid] : rsp_zeros_before_super(p, mid);
    if (c <= k) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  r = bit ? p->super[lo] : rsp_zeros_before_super(p, lo);
  for (w = lo * RS_SB;; w++) {
    unsigned long x = bit ? p->words[w] : ~p->words[w];
    size_t c = rs_popcount(x);
    if (r + c > k) {
      return w * RS_W + rs_select_word(x, k - r);
    }
    r += c;
  }
}

static size_t rsp_select1(const void *state, size_t k) {
  return rsp_select((const rsp_t *)state, k, 1);
}

static size_t rsp_select0(const void *state, size_t k) {
  return rsp_select((const rsp_t *)state, k, 0);
}

static bool rsp_access(const void *state, size_t i) {
  const rsp_t *p = (const rsp_t *)state;
  return ((p->words[i / RS_W] >> (i % RS_W)) & 1UL) != 0;
}

static size_t rsp_bytes(const void *state) {
  const rsp_t *p = (const rsp_t *)state;
  return sizeof(*p) + p->nw * sizeof(unsigned long) +
         (p->nsuper + p->nhint1 + p->nhint0) * sizeof(size_t);
}

/* ---- RRR ---- */

typedef struct rsr {
  size_t n;
  size_t ones;
  size_t nblocks;
  unsigned char *classes; /* two 4-bit classes per byte */
  unsigned long *offsets; /* concatenated variable-width offsets */
  size_t *sb_rank;        /* ones before superblock */
  size_t *sb_ptr;         /* offset bit position of superblock */
  size_t nsb;
  size_t offset_bits;
} rsr_t;

static unsigned rrr_binom[RRR_B + 1][RRR_B + 1];
static unsigned rrr_width[RRR_B + 1];
static int rrr_ready = 0;

static void rrr_tables(void) {
  unsigned i, j;
  if (rrr_ready) {
    return;
  }
  for (i = 0; i <= RRR_B; i++) {
    rrr_binom[i][0] = 1;
    for (j = 1; j <= RRR_B; j++) {
      rrr_binom[i][j] =
          i == 0 ? 0 : rrr_binom[i - 1][j - 1] + rrr_binom[i - 1][j];
    }
  }
  for (i = 0; i <= RRR_B; i++) {
    unsigned c = rrr_binom[RRR_B][i], w = 0;
    while ((1U << w) < c) {
      w++;
    }
    rrr_width[i] = w;
  }
  rrr_ready = 1;
}

static unsigned rrr_class(const rsr_t *r, size_t b) {
  return (r->classes[b / 2] >> ((b % 2) * 4)) & 15U;
}

static unsigned long rrr_read(const unsigned long *bits, size_t pos,
                              unsigned width) {
  unsigned long v;
  size_t w = pos / RS_W;
  unsigned off = (unsigned)(pos % RS_W);
  if (width == 0) {
    return 0;
  }
  v = bits[w] >> off;
  if (off + width > RS_W) {
    v |= bits[w + 1] << (RS_W - off);
  }
  return v & ((1UL << width) - 1UL);
}

static void rrr_write(unsigned long *bits, size_t pos, unsigned width,
                      unsigned long v) {
  size_t w = pos / RS_W;
  unsigned off = (unsigned)(pos % RS_W);
  if (width == 0) {
    return;
  }
  bits[w] |= v << off;
  if (off + width > RS_W) {
    bits[w + 1] |= v >> (RS_W - off);
  }
}

static unsigned long rrr_block_bits(const unsigned long *words, size_t n,
                                    size_t b) {
  size_t pos = b * RRR_B;
  return rrr_read(words, pos, RRR_B > n - pos ? (unsigned)(n - pos) : RRR_B);
}

static unsigned long rrr_encode(unsigned long x, unsigned c) {
  unsigned long off = 0;
  unsigned pos = RRR_B;
  while (pos-- > 0 && c > 0) {
    if ((x >> pos) & 1UL) {
      off += rrr_binom[pos][c];
      c--;
    }
  }
  return off;
}

static unsigned long rrr_decode(unsigned long off, unsigned c) {
  unsigned long x = 0;
  unsigned pos = RRR_B;
  while (pos-- > 0 && c > 0) {
    if (off >= rrr_binom[pos][c]) {
      x |= 1UL << pos;
      off -= rrr_binom[pos][c];
      c--;
    }
  }
  return x;
}

static void rsr_destroy(void *state, ds_context_t *ctx) {
  rsr_t *r = (rsr_t *)state;
  if (r == NULL) {
    return;
  }
  ds_context_free(ctx, r->classes);
  ds_context_free(ctx, r->offsets);
  ds_context_free(ctx, r->sb_rank);
  ds_context_free(ctx, r->sb_ptr);
  ds_context_free(ctx, r);
}

static ds_status_t rsr_build(void **state, const unsigned long *words, size_t n,
                             ds_context_t *ctx) {
  rsr_t *r = (rsr_t *)ds_context_calloc(ctx, 1, sizeof(rsr_t));
  size_t b, pos = 0;
  if (r == NULL) {
    return DS_ERR_ALLOC;
  }
  rrr_tables();
  r->n = n;
  r->nblocks = (n + RRR_B - 1) / RRR_B;
  r->nsb = r->nblocks / RRR_SB + 1;
  r->classes = (unsigned char *)rs_array(ctx, r->nblocks / 2 + 1, 1, 1);
  r->sb_rank = (size_t *)rs_array(ctx, r->nsb, sizeof(size_t), 0);
  r->sb_ptr = (size_t *)rs_array(ctx, r->nsb, sizeof(size_t), 0);
  if (r->classes == NULL || r->sb_rank == NULL || r->sb_ptr == NULL) {
    rsr_destroy(r, ctx);
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < r->nblocks; b++) {
    unsigned c = rs_popcount(rrr_block_bits(words, n, b));
    r->classes[b / 2] |= (unsigned char)(c << ((b % 2) * 4));
    r->offset_bits += rrr_width[c];
  }
  r->offsets = (unsigned long *)rs_array(ctx, r->offset_bits / RS_W + 2,
                                         sizeof(unsigned long), 1);
  if (r->offsets == NULL) {
    rsr_destroy(r, ctx);
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < r->nblocks; b++) {
    unsigned long x = rrr_block_bits(words, n, b);
    unsigned c = rrr_class(r, b);
    if (b % RRR_SB == 0) {
      r->sb_rank[b / RRR_SB] = r->ones;
      r->sb_ptr[b / RRR_SB] = pos;
    }
    rrr_write(r->offsets, pos, rrr_width[c], rrr_encode(x, c));
    pos += rrr_width[c];
    r->ones += c;
  }
  if (r->nblocks % RRR_SB == 0) {
    r->sb_rank[r->nblocks / RRR_SB] = r->ones;
    r->sb_ptr[r->nblocks / RRR_SB] = pos;
  }
  *state = r;
  return DS_OK;
}

/* walk to block b: ones before it and its offset position */
static void rsr_seek(const rsr_t *r, size_t b, size_t *rank, size_t *ptr) {
  size_t sb = b / RRR_SB, j;
  *rank = r->sb_rank[sb];
  *ptr = r->sb_ptr[sb];
  for (j = sb * RRR_SB; j < b; j++) {
    unsigned c = rrr_class(r, j);
    *rank += c;
    *ptr += rrr_width[c];
  }
}

static unsigned long rsr_block(const rsr_t *r, size_t b, size_t ptr) {
  unsigned c = rrr_class(r, b);
  return rrr_decode(rrr_read(r->offsets, ptr, rrr_width[c]), c);
}

static size_t rsr_rank1(const void *state, size_t i) {
  const rsr_t *r = (const rsr_t *)state;
  size_t b = i / RRR_B, rank, ptr;
  rsr_seek(r, b, &rank, &ptr);
  if (i % RRR_B != 0) {
    rank += rs_popcount(rsr_block(r, b, ptr) & ((1UL << (i % RRR_B)) - 1UL));
  }
  return rank;
}

static bool rsr_access(const void *state, size_t i) {
  const rsr_t *r = (const rsr_t *)state;
  size_t rank, ptr;
  rsr_seek(r, i / RRR_B, &rank, &ptr);
  return ((rsr_block(r, i / RRR_B, ptr) >> (i % RRR_B)) & 1UL) != 0;
}

static size_t rsr_select(const rsr_t *r, size_t k, int bit) {
  size_t lo = 0, hi = r->nsb, b, rank, ptr;
  size_t total = bit ? r->ones : r->n - r->ones;
  if (k >= total) {
    return r->n;
  }
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    size_t c = bit ? r->sb_rank[mid] : mid * RRR_SB * RRR_B - r->sb_rank[mid];
    if (mid * RRR_SB > r->nblocks || c > k) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  rank = bit ? r->sb_rank[lo] : lo * RRR_SB * RRR_B - r->sb_rank[lo];
  ptr = r->sb_ptr[lo];
  for (b = lo * RRR_SB;; b++) {
    unsigned c = rrr_class(r, b);
    size_t cnt = bit ? c : RRR_B - c;
    if (rank + cnt > k) {
      unsigned long x = rsr_block(r, b, ptr);
      if (!bit) {
        x = ~x & ((1UL << RRR_B) - 1UL);
      }
      return b * RRR_B + rs_select_word(x, k - rank);
    }
    rank += cnt;
    ptr += rrr_width[c];
  }
}

static size_t rsr_select1(const void *state, size_t k) {
  return rsr_select((const rsr_t *)state, k, 1);
}

static size_t rsr_select0(const void *state, size_t k) {
  return rsr_select((const rsr_t *)state, k, 0);
}

static size_t rsr_bytes(const void *state) {
  const rsr_t *r = (const rsr_t *)state;
  return sizeof(*r) + r->nblocks / 2 + 1 +
         (r->offset_bits / RS_W + 2) * sizeof(unsigned long) +
         2 * r->nsb * sizeof(size_t);
}

/* ---- Elias-Fano over the positions of the ones ---- */

typedef struct rse {
  ds_elias_fano_t ef;
  size_t n;
} rse_t;

static void rse_destroy(void *state, ds_context_t *ctx) {
  rse_t *e = (rse_t *)state;
  if (e == NULL) {
    return;
  }
  ds_elias_fano_destroy(&e->ef);
  ds_context_free(ctx, e);
}

static ds_status_t rse_build(void **state, const unsigned long *words, size_t n,
                             ds_context_t *ctx) {
  rse_t *e = (rse_t *)ds_context_calloc(ctx, 1, sizeof(rse_t));
  unsigned long *pos;
  size_t i, m = 0;
  ds_status_t st;
  if (e == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    m += (words[i / RS_W] >> (i % RS_W)) & 1UL;
  }
  pos = (unsigned long *)rs_array(ctx, m, sizeof(unsigned long), 0);
  if (pos == NULL) {
    ds_context_free(ctx, e);
    return DS_ERR_ALLOC;
  }
  m = 0;
  for (i = 0; i < n; i++) {
    if ((words[i / RS_W] >> (i % RS_W)) & 1UL) {
      pos[m++] = (unsigned long)i;
    }
  }
  e->n = n;
  st =
      ds_elias_fano_build(&e->ef, pos, m, (unsigned long)(n == 0 ? 1 : n), ctx);
  ds_context_free(ctx, pos);
  if (st != DS_OK) {
    ds_context_free(ctx, e);
    return st;
  }
  *state = e;
  return DS_OK;
}

static size_t rse_rank1(const void *state, size_t i) {
  const rse_t *e = (const rse_t *)state;
  size_t idx;
  unsigned long v;
  if (ds_elias_fano_next_geq(&e->ef, (unsigned long)i, &idx, &v) != DS_OK) {
    return ds_elias_fano_size(&e->ef);
  }
  return idx;
}

static size_t rse_select1(const void *state, size_t k) {
  const rse_t *e = (const rse_t *)state;
  if (k >= ds_elias_fano_size(&e->ef)) {
    return e->n;
  }
  return (size_t)ds_elias_fano_get(&e->ef, k);
}

static size_t rse_select0(const void *state, size_t k) {
  const rse_t *e = (const rse_t *)state;
  size_t m = ds_elias_fano_size(&e->ef), lo = 0, hi = m;
  if (k >= e->n - m) {
    return e->n;
  }
  /* ones j with pos_j - j <= k (non-decreasing in j) all precede it */
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if ((size_t)ds_elias_fano_get(&e->ef, mid) - mid <= k) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return k + lo;
}

static bool rse_access(const void *state, size_t i) {
  const rse_t *e = (const rse_t *)state;
  size_t idx;
  unsigned long v;
  return ds_elias_fano_next_geq(&e->ef, (unsigned long)i, &idx, &v) == DS_OK &&
         v == (unsigned long)i;
}

static size_t rse_bytes(const void *state) {
  const rse_t *e = (const rse_t *)state;
  return sizeof(*e) + ds_elias_fano_bits(&e->ef) / CHAR_BIT;
}

static const ds_rs_ops_t rs_plain_ops = {"plain",     rsp_build,   rsp_rank1,
                                         rsp_select1, rsp_select0, rsp_access,
                                         rsp_bytes,   rsp_destroy};
static const ds_rs_ops_t rs_rrr_ops = {"rrr",       rsr_build,   rsr_rank1,
                                       rsr_select1, rsr_select0, rsr_access,
                                       rsr_bytes,   rsr_destroy};
static const ds_rs_ops_t rs_ef_ops = {"elias-fano", rse_build,   rse_rank1,
                                      rse_select1,  rse_select0, rse_access,
                                      rse_bytes,    rse_destroy};

const ds_rs_ops_t *ds_rs_plain(void) { return &rs_plain_ops; }
const ds_rs_ops_t *ds_rs_rrr(void) { return &rs_rrr_ops; }
const ds_rs_ops_t *ds_rs_elias_fano(void) { return &rs_ef_ops; }

/* ---- wrapper ---- */

ds_status_t ds_rs_build(ds_rs_t *rs, const ds_rs_ops_t *ops,
                        const unsigned long *words, size_t n,
                        ds_context_t *context) {
  ds_status_t st;
  if (rs == NULL || (words == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  memset(rs, 0, sizeof(*rs));
  if (ops == NULL) {
    ops = ds_rs_plain();
  }
  st = ops->build(&rs->state, words, n, context);
  if (st != DS_OK) {
    rs->state = NULL;
    return st;
  }
  rs->ops = ops;
  rs->n = n;
  rs->context = context;
  rs->ones = n == 0 ? 0 : ops->rank1(rs->state, n);
  return DS_OK;
}

void ds_rs_destroy(ds_rs_t *rs) {
  if (rs == NULL || rs->ops == NULL) {
    return;
  }
  rs->ops->destroy(rs->state, rs->context);
  memset(rs, 0, sizeof(*rs));
}

size_t ds_rs_size(const ds_rs_t *rs) { return rs == NULL ? 0 : rs->n; }

size_t ds_rs_ones(const ds_rs_t *rs) { return rs == NULL ? 0 : rs->ones; }

size_t ds_rs_rank1(const ds_rs_t *rs, size_t i) {
  if (rs == NULL || rs->ops == NULL) {
    return 0;
  }
  if (i > rs->n) {
    i = rs->n;
  }
  return rs->ops->rank1(rs->state, i);
}

size_t ds_rs_rank0(const ds_rs_t *rs, size_t i) {
  if (rs == NULL || rs->ops == NULL) {
    return 0;
  }
  if (i > rs->n) {
    i = rs->n;
  }
  return i - rs->ops->rank1(rs->state, i);
}

size_t ds_rs_select1(const ds_rs_t *rs, size_t k) {
  if (rs == NULL || rs->ops == NULL) {
    return 0;
  }
  return rs->ops->select1(rs->state, k);
}

size_t ds_rs_select0(const ds_rs_t *rs, size_t k) {
  if (rs == NULL || rs->ops == NULL) {
    return 0;
  }
  return rs->ops->select0(rs->state, k);
}

bool ds_rs_access(const ds_rs_t *rs, size_t i) {
  if (rs == NULL || rs->ops == NULL || i >= rs->n) {
    return false;
  }
  return rs->ops->access(rs->state, i);
}

size_t ds_rs_bytes(const ds_rs_t *rs) {
  if (rs == NULL || rs->ops == NULL) {
    return 0;
  }
  return rs->ops->bytes(rs->state);
}
