#include "fm_index.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define FM_W (sizeof(unsigned long) * CHAR_BIT)
#define FM_SB 65536UL /* occurrence-table superblock */
#define FM_B 64UL     /* occurrence-table block */

static unsigned fm_popcount(unsigned long x) {
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

static void *fm_alloc_array(ds_context_t *ctx, size_t count, size_t size) {
  size_t bytes;
  if (!ds_size_mul(count == 0 ? 1 : count, size, &bytes)) {
    return NULL;
  }
  return ds_context_alloc(ctx, bytes);
}

static void *fm_calloc_array(ds_context_t *ctx, size_t count, size_t size) {
  return ds_context_calloc(ctx, count == 0 ? 1 : count, size);
}

/* ---- rank bit vector ---- */

static ds_status_t fm_bits_init(ds_fm_bits_t *b, size_t n, ds_context_t *ctx) {
  size_t words = n / FM_W + 1;
  b->n = n;
  b->words =
      (unsigned long *)fm_calloc_array(ctx, words, sizeof(unsigned long));
  b->blocks = (size_t *)fm_alloc_array(ctx, words / 8 + 1, sizeof(size_t));
  if (b->words == NULL || b->blocks == NULL) {
    ds_context_free(ctx, b->words);
    ds_context_free(ctx, b->blocks);
    b->words = NULL;
    b->blocks = NULL;
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

static void fm_bits_set(ds_fm_bits_t *b, size_t i) {
  b->words[i / FM_W] |= 1UL << (i % FM_W);
}

static int fm_bits_get(const ds_fm_bits_t *b, size_t i) {
  return (int)((b->words[i / FM_W] >> (i % FM_W)) & 1UL);
}

static void fm_bits_finish(ds_fm_bits_t *b) {
  size_t words = b->n / FM_W + 1, w, acc = 0;
  for (w = 0; w < words; w++) {
    if (w % 8 == 0) {
      b->blocks[w / 8] = acc;
    }
    acc += fm_popcount(b->words[w]);
  }
  if (words % 8 == 0) {
    b->blocks[words / 8] = acc;
  }
}

/* ones in [0, i) */
static size_t fm_bits_rank1(const ds_fm_bits_t *b, size_t i) {
  size_t w = i / FM_W, j, r = b->blocks[w / 8];
  for (j = (w / 8) * 8; j < w; j++) {
    r += fm_popcount(b->words[j]);
  }
  if (i % FM_W != 0) {
    r += fm_popcount(b->words[w] & ((1UL << (i % FM_W)) - 1UL));
  }
  return r;
}

static void fm_bits_free(ds_fm_bits_t *b, ds_context_t *ctx) {
  ds_context_free(ctx, b->words);
  ds_context_free(ctx, b->blocks);
  b->words = NULL;
  b->blocks = NULL;
  b->n = 0;
}

static size_t fm_bits_bytes(const ds_fm_bits_t *b) {
  size_t words = b->n / FM_W + 1;
  return words * sizeof(unsigned long) + (words / 8 + 1) * sizeof(size_t);
}

/* ---- occurrence table backend ---- */

typedef struct fm_occt {
  unsigned short *bwt;
  size_t n;
  unsigned sigma;
  size_t *super;
  unsigned short *block;
} fm_occt_t;

static void occt_destroy(void *state, ds_context_t *ctx) {
  fm_occt_t *t = (fm_occt_t *)state;
  if (t == NULL) {
    return;
  }
  ds_context_free(ctx, t->bwt);
  ds_context_free(ctx, t->super);
  ds_context_free(ctx, t->block);
  ds_context_free(ctx, t);
}

static ds_status_t occt_build(void **state, const unsigned short *bwt, size_t n,
                              unsigned sigma, ds_context_t *ctx) {
  fm_occt_t *t = (fm_occt_t *)ds_context_alloc(ctx, sizeof(fm_occt_t));
  size_t nsuper = n / FM_SB + 1, nblock = n / FM_B + 1, i, ns, nb;
  size_t *run, *blk;
  unsigned c;
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  run = (size_t *)fm_calloc_array(ctx, 2 * (size_t)sigma, sizeof(size_t));
  if (run == NULL) {
    ds_context_free(ctx, t);
    return DS_ERR_ALLOC;
  }
  blk = run + sigma;
  t->n = n;
  t->sigma = sigma;
  t->bwt = (unsigned short *)fm_alloc_array(ctx, n, sizeof(unsigned short));
  t->super = NULL;
  t->block = NULL;
  if (ds_size_mul(nsuper, sigma, &ns) && ds_size_mul(nblock, sigma, &nb)) {
    t->super = (size_t *)fm_alloc_array(ctx, ns, sizeof(size_t));
    t->block =
        (unsigned short *)fm_alloc_array(ctx, nb, sizeof(unsigned short));
  }
  if (t->bwt == NULL || t->super == NULL || t->block == NULL) {
    ds_context_free(ctx, run);
    occt_destroy(t, ctx);
    return DS_ERR_ALLOC;
  }
  if (n > 0) {
    memcpy(t->bwt, bwt, n * sizeof(unsigned short));
  }
  for (i = 0; i <= n; i++) {
    if (i % FM_SB == 0) {
      for (c = 0; c < sigma; c++) {
        run[c] += blk[c];
        blk[c] = 0;
        t->super[(i / FM_SB) * sigma + c] = run[c];
      }
    }
    if (i % FM_B == 0) {
      for (c = 0; c < sigma; c++) {
        t->block[(i / FM_B) * sigma + c] = (unsigned short)blk[c];
      }
    }
    if (i < n) {
      blk[bwt[i]]++;
    }
  }
  ds_context_free(ctx, run);
  *state = t;
  return DS_OK;
}

static size_t occt_rank(const void *state, unsigned c, size_t i) {
  const fm_occt_t *t = (const fm_occt_t *)state;
  size_t r = t->super[(i / FM_SB) * t->sigma + c] +
             (size_t)t->block[(i / FM_B) * t->sigma + c];
  size_t j;
  for (j = (i / FM_B) * FM_B; j < i; j++) {
    r += t->bwt[j] == c ? 1U : 0U;
  }
  return r;
}

static unsigned occt_access(const void *state, size_t i) {
  return ((const fm_occt_t *)state)->bwt[i];
}

static size_t occt_bytes(const void *state) {
  const fm_occt_t *t = (const fm_occt_t *)state;
  return sizeof(*t) + t->n * sizeof(unsigned short) +
         (t->n / FM_SB + 1) * t->sigma * sizeof(size_t) +
         (t->n / FM_B + 1) * t->sigma * sizeof(unsigned short);
}

/* ---- wavelet matrix backend ---- */

typedef struct fm_wm {
  unsigned levels;
  size_t n;
  ds_fm_bits_t bits[16];
  size_t zeros[16];
} fm_wm_t;

static void wm_destroy(void *state, ds_context_t *ctx) {
  fm_wm_t *w = (fm_wm_t *)state;
  unsigned l;
  if (w == NULL) {
    return;
  }
  for (l = 0; l < w->levels; l++) {
    fm_bits_free(&w->bits[l], ctx);
  }
  ds_context_free(ctx, w);
}

static ds_status_t wm_build(void **state, const unsigned short *bwt, size_t n,
                            unsigned sigma, ds_context_t *ctx) {
  fm_wm_t *w = (fm_wm_t *)ds_context_calloc(ctx, 1, sizeof(fm_wm_t));
  unsigned short *cur, *next, *sw;
  unsigned l, levels = 1;
  size_t i;
  if (w == NULL) {
    return DS_ERR_ALLOC;
  }
  while ((1U << levels) < sigma) {
    levels++;
  }
  w->n = n;
  if (sigma > 65536U) {
    ds_context_free(ctx, w);
    return DS_ERR_RANGE;
  }
  cur = (unsigned short *)fm_alloc_array(ctx, n, sizeof(unsigned short));
  next = (unsigned short *)fm_alloc_array(ctx, n, sizeof(unsigned short));
  if (cur == NULL || next == NULL) {
    ds_context_free(ctx, cur);
    ds_context_free(ctx, next);
    ds_context_free(ctx, w);
    return DS_ERR_ALLOC;
  }
  if (n > 0) {
    memcpy(cur, bwt, n * sizeof(unsigned short));
  }
  for (l = 0; l < levels; l++) {
    unsigned shift = levels - 1 - l;
    size_t z = 0, o;
    if (fm_bits_init(&w->bits[l], n, ctx) != DS_OK) {
      ds_context_free(ctx, cur);
      ds_context_free(ctx, next);
      wm_destroy(w, ctx);
      return DS_ERR_ALLOC;
    }
    w->levels = l + 1;
    for (i = 0; i < n; i++) {
      if ((cur[i] >> shift) & 1U) {
        fm_bits_set(&w->bits[l], i);
      } else {
        z++;
      }
    }
    fm_bits_finish(&w->bits[l]);
    w->zeros[l] = z;
    o = z;
    z = 0;
    for (i = 0; i < n; i++) {
      if ((cur[i] >> shift) & 1U) {
        next[o++] = cur[i];
      } else {
        next[z++] = cur[i];
      }
    }
    sw = cur;
    cur = next;
    next = sw;
  }
  ds_context_free(ctx, cur);
  ds_context_free(ctx, next);
  *state = w;
  return DS_OK;
}

static size_t wm_rank(const void *state, unsigned c, size_t i) {
  const fm_wm_t *w = (const fm_wm_t *)state;
  size_t s = 0, e = i;
  unsigned l;
  for (l = 0; l < w->levels; l++) {
    if ((c >> (w->levels - 1 - l)) & 1U) {
      s = w->zeros[l] + fm_bits_rank1(&w->bits[l], s);
      e = w->zeros[l] + fm_bits_rank1(&w->bits[l], e);
    } else {
      s = s - fm_bits_rank1(&w->bits[l], s);
      e = e - fm_bits_rank1(&w->bits[l], e);
    }
  }
  return e - s;
}

static unsigned wm_access(const void *state, size_t i) {
  const fm_wm_t *w = (const fm_wm_t *)state;
  unsigned l, c = 0;
  for (l = 0; l < w->levels; l++) {
    if (fm_bits_get(&w->bits[l], i)) {
      c = (c << 1) | 1U;
      i = w->zeros[l] + fm_bits_rank1(&w->bits[l], i);
    } else {
      c <<= 1;
      i = i - fm_bits_rank1(&w->bits[l], i);
    }
  }
  return c;
}

static size_t wm_bytes(const void *state) {
  const fm_wm_t *w = (const fm_wm_t *)state;
  size_t b = sizeof(*w);
  unsigned l;
  for (l = 0; l < w->levels; l++) {
    b += fm_bits_bytes(&w->bits[l]);
  }
  return b;
}

static const ds_fm_occ_ops_t fm_occ_table_ops = {
    "occurrence table", occt_build, occt_rank,
    occt_access,        occt_bytes, occt_destroy};
static const ds_fm_occ_ops_t fm_occ_wavelet_ops = {
    "wavelet matrix", wm_build, wm_rank, wm_access, wm_bytes, wm_destroy};

const ds_fm_occ_ops_t *ds_fm_occ_table(void) { return &fm_occ_table_ops; }
const ds_fm_occ_ops_t *ds_fm_occ_wavelet(void) { return &fm_occ_wavelet_ops; }

/* ---- default suffix array: prefix doubling with radix sort ---- */

static ds_status_t fm_sa_doubling(const unsigned char *s, size_t n, size_t *sa,
                                  ds_context_t *ctx) {
  size_t *rank, *tmp, *sa2, *cnt, i, k, m, p;
  if (n == 0) {
    return DS_OK;
  }
  m = n > 256 ? n + 1 : 257;
  rank = (size_t *)fm_alloc_array(ctx, n, sizeof(size_t));
  tmp = (size_t *)fm_alloc_array(ctx, n, sizeof(size_t));
  sa2 = (size_t *)fm_alloc_array(ctx, n, sizeof(size_t));
  cnt = (size_t *)fm_alloc_array(ctx, m, sizeof(size_t));
  if (rank == NULL || tmp == NULL || sa2 == NULL || cnt == NULL) {
    ds_context_free(ctx, rank);
    ds_context_free(ctx, tmp);
    ds_context_free(ctx, sa2);
    ds_context_free(ctx, cnt);
    return DS_ERR_ALLOC;
  }
  /* ranks start at 1 so that "past the end" (0) sorts first */
  memset(cnt, 0, m * sizeof(size_t));
  for (i = 0; i < n; i++) {
    rank[i] = (size_t)s[i] + 1;
    cnt[rank[i]]++;
  }
  for (i = 1; i < m; i++) {
    cnt[i] += cnt[i - 1];
  }
  for (i = n; i-- > 0;) {
    sa[--cnt[rank[i]]] = i;
  }
  for (k = 1;; k *= 2) {
    size_t top;
    p = 0;
    for (i = n - (k < n ? k : n); i < n; i++) {
      sa2[p++] = i;
    }
    for (i = 0; i < n; i++) {
      if (sa[i] >= k) {
        sa2[p++] = sa[i] - k;
      }
    }
    memset(cnt, 0, m * sizeof(size_t));
    for (i = 0; i < n; i++) {
      cnt[rank[i]]++;
    }
    for (i = 1; i < m; i++) {
      cnt[i] += cnt[i - 1];
    }
    for (i = n; i-- > 0;) {
      sa[--cnt[rank[sa2[i]]]] = sa2[i];
    }
    tmp[sa[0]] = 1;
    top = 1;
    for (i = 1; i < n; i++) {
      size_t a = sa[i - 1], b = sa[i];
      size_t ra = a + k < n ? rank[a + k] : 0;
      size_t rb = b + k < n ? rank[b + k] : 0;
      if (rank[a] != rank[b] || ra != rb) {
        top++;
      }
      tmp[b] = top;
    }
    memcpy(rank, tmp, n * sizeof(size_t));
    if (top == n || k >= n) {
      break;
    }
  }
  ds_context_free(ctx, rank);
  ds_context_free(ctx, tmp);
  ds_context_free(ctx, sa2);
  ds_context_free(ctx, cnt);
  return DS_OK;
}

/* ---- index ---- */

void ds_fm_config_init(ds_fm_config_t *config) {
  if (config != NULL) {
    memset(config, 0, sizeof(*config));
  }
}

static void fm_zero(ds_fm_index_t *fm) { memset(fm, 0, sizeof(*fm)); }

void ds_fm_index_destroy(ds_fm_index_t *fm) {
  if (fm == NULL) {
    return;
  }
  if (fm->occ != NULL && fm->occ_state != NULL) {
    fm->occ->destroy(fm->occ_state, fm->context);
  }
  fm_bits_free(&fm->sampled, fm->context);
  ds_context_free(fm->context, fm->sa_samples);
  ds_context_free(fm->context, fm->isa_samples);
  fm_zero(fm);
}

static size_t fm_lf(const ds_fm_index_t *fm, size_t row) {
  unsigned c = fm->occ->access(fm->occ_state, row);
  return fm->C[c] + fm->occ->rank(fm->occ_state, c, row);
}

ds_status_t ds_fm_index_build(ds_fm_index_t *fm, const unsigned char *text,
                              size_t n, const ds_fm_config_t *config) {
  ds_fm_config_t def;
  ds_context_t *ctx;
  ds_fm_sa_builder_t builder;
  size_t *sa = NULL, rows, i, nsamp = 0, k;
  unsigned short *bwt = NULL;
  size_t counts[257];
  unsigned c, sigma = 1;
  ds_status_t st;
  if (fm == NULL || (text == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  if (config == NULL) {
    ds_fm_config_init(&def);
    config = &def;
  }
  fm_zero(fm);
  ctx = config->context;
  fm->context = ctx;
  fm->occ = config->occ != NULL ? config->occ : ds_fm_occ_table();
  fm->sa_sample = config->sa_sample != 0 ? config->sa_sample : 32;
  fm->isa_sample = config->isa_sample != 0 ? config->isa_sample : fm->sa_sample;
  builder = config->sa_builder != NULL ? config->sa_builder : fm_sa_doubling;
  if (!ds_size_add(n, 1, &rows)) {
    return DS_ERR_OVERFLOW;
  }
  fm->n = n;
  /* alphabet: used bytes get symbols 1.., the marker 0 */
  memset(counts, 0, sizeof(counts));
  for (i = 0; i < n; i++) {
    counts[text[i]] = 1;
  }
  for (c = 0; c < 256; c++) {
    if (counts[c]) {
      fm->map[c] = (unsigned short)sigma;
      fm->unmap[sigma] = (unsigned char)c;
      sigma++;
    }
  }
  fm->sigma = sigma;
  sa = (size_t *)fm_alloc_array(ctx, rows, sizeof(size_t));
  bwt = (unsigned short *)fm_alloc_array(ctx, rows, sizeof(unsigned short));
  if (sa == NULL || bwt == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  sa[0] = n;
  st = builder(text, n, sa + 1, ctx);
  if (st != DS_OK) {
    goto fail;
  }
  memset(counts, 0, sizeof(counts));
  for (i = 0; i < rows; i++) {
    if (sa[i] == 0) {
      bwt[i] = 0;
      fm->primary = i;
    } else {
      bwt[i] = fm->map[text[sa[i] - 1]];
    }
    counts[bwt[i]]++;
  }
  fm->C[0] = 0;
  for (c = 0; c < sigma; c++) {
    fm->C[c + 1] = fm->C[c] + counts[c];
  }
  st = fm->occ->build(&fm->occ_state, bwt, rows, sigma, ctx);
  if (st != DS_OK) {
    fm->occ_state = NULL;
    goto fail;
  }
  ds_context_free(ctx, bwt);
  bwt = NULL;
  st = fm_bits_init(&fm->sampled, rows, ctx);
  if (st != DS_OK) {
    goto fail;
  }
  for (i = 0; i < rows; i++) {
    if (sa[i] % fm->sa_sample == 0) {
      fm_bits_set(&fm->sampled, i);
      nsamp++;
    }
  }
  fm_bits_finish(&fm->sampled);
  fm->sa_samples = (size_t *)fm_alloc_array(ctx, nsamp, sizeof(size_t));
  fm->isa_samples =
      (size_t *)fm_alloc_array(ctx, n / fm->isa_sample + 1, sizeof(size_t));
  if (fm->sa_samples == NULL || fm->isa_samples == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  k = 0;
  for (i = 0; i < rows; i++) {
    if (sa[i] % fm->sa_sample == 0) {
      fm->sa_samples[k++] = sa[i];
    }
    if (sa[i] % fm->isa_sample == 0) {
      fm->isa_samples[sa[i] / fm->isa_sample] = i;
    }
  }
  ds_context_free(ctx, sa);
  return DS_OK;
fail:
  ds_context_free(ctx, sa);
  ds_context_free(ctx, bwt);
  ds_fm_index_destroy(fm);
  return st;
}

bool ds_fm_index_backward_step(const ds_fm_index_t *fm, unsigned char c,
                               size_t *sp, size_t *ep) {
  unsigned s = fm->map[c];
  if (s == 0) {
    *ep = *sp;
    return false;
  }
  *sp = fm->C[s] + fm->occ->rank(fm->occ_state, s, *sp);
  *ep = fm->C[s] + fm->occ->rank(fm->occ_state, s, *ep);
  return *sp < *ep;
}

size_t ds_fm_index_count(const ds_fm_index_t *fm, const unsigned char *p,
                         size_t m, size_t *out_sp, size_t *out_ep) {
  size_t sp = 0, ep, j = m;
  if (fm == NULL || fm->occ_state == NULL || (p == NULL && m > 0)) {
    return 0;
  }
  ep = fm->n + 1;
  while (j-- > 0 && sp < ep) {
    ds_fm_index_backward_step(fm, p[j], &sp, &ep);
  }
  if (sp > ep) {
    ep = sp;
  }
  if (out_sp != NULL) {
    *out_sp = sp;
  }
  if (out_ep != NULL) {
    *out_ep = ep;
  }
  return ep - sp;
}

size_t ds_fm_index_locate_row(const ds_fm_index_t *fm, size_t row) {
  size_t steps = 0;
  while (!fm_bits_get(&fm->sampled, row)) {
    row = fm_lf(fm, row);
    steps++;
  }
  return fm->sa_samples[fm_bits_rank1(&fm->sampled, row)] + steps;
}

ds_status_t ds_fm_index_locate(const ds_fm_index_t *fm, const unsigned char *p,
                               size_t m, size_t *out, size_t capacity,
                               size_t *out_total) {
  size_t sp, ep, i, total;
  if (fm == NULL || (out == NULL && capacity > 0)) {
    return DS_ERR_NULL;
  }
  total = ds_fm_index_count(fm, p, m, &sp, &ep);
  for (i = 0; i < total && i < capacity; i++) {
    out[i] = ds_fm_index_locate_row(fm, sp + i);
  }
  if (out_total != NULL) {
    *out_total = total;
  }
  return DS_OK;
}

ds_status_t ds_fm_index_extract(const ds_fm_index_t *fm, size_t start,
                                size_t len, unsigned char *out) {
  size_t end, q, row;
  if (fm == NULL || (out == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (start > fm->n || len > fm->n - start) {
    return DS_ERR_RANGE;
  }
  end = start + len;
  q = (end + fm->isa_sample - 1) / fm->isa_sample * fm->isa_sample;
  if (q >= fm->n) {
    q = fm->n;
    row = 0; /* the marker suffix */
  } else {
    row = fm->isa_samples[q / fm->isa_sample];
  }
  while (q > start) {
    unsigned c = fm->occ->access(fm->occ_state, row);
    q--;
    if (q < end) {
      out[q - start] = fm->unmap[c];
    }
    row = fm->C[c] + fm->occ->rank(fm->occ_state, c, row);
  }
  return DS_OK;
}

ds_status_t ds_fm_index_bwt(const ds_fm_index_t *fm, unsigned char *out,
                            size_t *out_primary) {
  size_t i;
  if (fm == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i <= fm->n; i++) {
    out[i] = fm->unmap[fm->occ->access(fm->occ_state, i)];
  }
  if (out_primary != NULL) {
    *out_primary = fm->primary;
  }
  return DS_OK;
}

size_t ds_fm_index_bytes(const ds_fm_index_t *fm) {
  if (fm == NULL || fm->occ_state == NULL) {
    return 0;
  }
  return sizeof(*fm) + fm->occ->bytes(fm->occ_state) +
         fm_bits_bytes(&fm->sampled) +
         (fm_bits_rank1(&fm->sampled, fm->n + 1) + fm->n / fm->isa_sample + 1) *
             sizeof(size_t);
}
