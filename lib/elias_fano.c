#include "elias_fano.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define EF_W (sizeof(unsigned long) * CHAR_BIT)

/* SWAR popcount, valid for any word of up to 255 bits */
static unsigned ef_popcount(unsigned long x) {
  x = x - ((x >> 1) & (~0UL / 3));
  x = (x & (~0UL / 5)) + ((x >> 2) & (~0UL / 5));
  x = (x + (x >> 4)) & (~0UL / 17);
  return (unsigned)((x * (~0UL / 255)) >> (EF_W - CHAR_BIT));
}

static int ef_get_bit(const unsigned long *v, size_t i) {
  return (int)((v[i / EF_W] >> (i % EF_W)) & 1UL);
}

static void ef_set_bit(unsigned long *v, size_t i) {
  v[i / EF_W] |= 1UL << (i % EF_W);
}

static unsigned long ef_read(const unsigned long *v, size_t pos,
                             unsigned width) {
  unsigned long r;
  size_t w = pos / EF_W;
  unsigned off = (unsigned)(pos % EF_W);
  if (width == 0) {
    return 0;
  }
  r = v[w] >> off;
  if (off + width > EF_W) {
    r |= v[w + 1] << (EF_W - off);
  }
  if (width < EF_W) {
    r &= (1UL << width) - 1;
  }
  return r;
}

static void ef_write(unsigned long *v, size_t pos, unsigned width,
                     unsigned long x) {
  size_t w = pos / EF_W;
  unsigned off = (unsigned)(pos % EF_W);
  if (width == 0) {
    return;
  }
  v[w] |= x << off;
  if (off + width > EF_W) {
    v[w + 1] |= x >> (EF_W - off);
  }
}

#define EF_SUBS (DS_EF_SAMPLE / DS_EF_SUB)

static int ef_is(const ds_elias_fano_t *ef, size_t i, int bit) {
  return ef_get_bit(ef->high, i) == bit;
}

/* end (exclusive bound) of block b of a directory */
static size_t ef_block_end(const ds_elias_fano_t *ef, const ds_ef_select_t *s,
                           size_t b) {
  return b + 1 < s->blocks ? s->start[b + 1] : ef->high_bits;
}

static size_t ef_block_count(const ds_ef_select_t *s, size_t b) {
  size_t rest = s->count - b * DS_EF_SAMPLE;
  return rest < DS_EF_SAMPLE ? rest : DS_EF_SAMPLE;
}

static void ef_sel_destroy(ds_context_t *context, ds_ef_select_t *s) {
  ds_context_free(context, s->start);
  ds_context_free(context, s->dir);
  ds_context_free(context, s->mask);
  ds_context_free(context, s->is_long);
  ds_context_free(context, s->sub);
  ds_context_free(context, s->lpos);
  ds_context_free(context, s->spos);
  memset(s, 0, sizeof(*s));
}

static void *ef_alloc_array(ds_context_t *context, size_t count, size_t size) {
  size_t bytes;
  if (!ds_size_mul(count == 0 ? 1 : count, size, &bytes)) {
    return NULL;
  }
  return ds_context_alloc(context, bytes);
}

/* Three-level select directory over the occurrences of `bit` in high:
 * blocks of DS_EF_SAMPLE occurrences; a block spanning DS_EF_LONG_BLOCK
 * bits or more stores all its positions; a shorter block stores 16-bit
 * offsets of every DS_EF_SUB-th occurrence, and a sub-block spanning
 * DS_EF_LONG_SUB bits or more stores its DS_EF_SUB offsets. Every other
 * sub-block is scanned: fewer than DS_EF_LONG_SUB bits. */
static ds_status_t ef_sel_build(ds_elias_fano_t *ef, ds_ef_select_t *s, int bit,
                                size_t count) {
  ds_context_t *ctx = ef->context;
  size_t i;
  size_t k;
  size_t b;
  size_t nlong = 0;
  size_t nsub = 0;
  memset(s, 0, sizeof(*s));
  s->count = count;
  s->blocks = count / DS_EF_SAMPLE + (count % DS_EF_SAMPLE != 0);
  if (s->blocks == 0) {
    return DS_OK;
  }
  s->start = (size_t *)ef_alloc_array(ctx, s->blocks, sizeof(size_t));
  s->dir = (size_t *)ef_alloc_array(ctx, s->blocks, sizeof(size_t));
  s->mask =
      (unsigned short *)ef_alloc_array(ctx, s->blocks, sizeof(unsigned short));
  s->is_long = (unsigned char *)ef_alloc_array(ctx, s->blocks, 1);
  s->sub = (unsigned short *)ef_alloc_array(ctx, s->blocks,
                                            EF_SUBS * sizeof(unsigned short));
  if (s->start == NULL || s->dir == NULL || s->mask == NULL ||
      s->is_long == NULL || s->sub == NULL) {
    return DS_ERR_ALLOC;
  }
  k = 0;
  for (i = 0; i < ef->high_bits; i++) {
    if (ef_is(ef, i, bit)) {
      if (k % DS_EF_SAMPLE == 0) {
        s->start[k / DS_EF_SAMPLE] = i;
      }
      k++;
    }
  }
  for (b = 0; b < s->blocks; b++) {
    s->mask[b] = 0;
    s->is_long[b] = (unsigned char)(ef_block_end(ef, s, b) - s->start[b] >=
                                    DS_EF_LONG_BLOCK);
    if (s->is_long[b]) {
      s->dir[b] = nlong * DS_EF_SAMPLE;
      nlong++;
    }
  }
  if (nlong > 0) {
    s->lpos =
        (size_t *)ef_alloc_array(ctx, nlong * DS_EF_SAMPLE, sizeof(size_t));
    if (s->lpos == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  k = 0;
  for (i = 0; i < ef->high_bits; i++) {
    if (ef_is(ef, i, bit)) {
      size_t r = k % DS_EF_SAMPLE;
      b = k / DS_EF_SAMPLE;
      if (s->is_long[b]) {
        s->lpos[s->dir[b] + r] = i;
      } else if (r % DS_EF_SUB == 0) {
        s->sub[b * EF_SUBS + r / DS_EF_SUB] = (unsigned short)(i - s->start[b]);
      }
      k++;
    }
  }
  for (b = 0; b < s->blocks; b++) {
    size_t occ = ef_block_count(s, b);
    size_t j;
    unsigned m = 0;
    if (s->is_long[b]) {
      continue;
    }
    s->dir[b] = nsub * DS_EF_SUB;
    for (j = 0; j * DS_EF_SUB < occ; j++) {
      size_t from = s->start[b] + s->sub[b * EF_SUBS + j];
      size_t to = (j + 1) * DS_EF_SUB < occ
                      ? s->start[b] + s->sub[b * EF_SUBS + j + 1]
                      : ef_block_end(ef, s, b);
      if (to - from >= DS_EF_LONG_SUB) {
        m |= 1u << j;
        nsub++;
      }
    }
    s->mask[b] = (unsigned short)m;
  }
  if (nsub > 0) {
    s->spos = (unsigned short *)ef_alloc_array(ctx, nsub * DS_EF_SUB,
                                               sizeof(unsigned short));
    if (s->spos == NULL) {
      return DS_ERR_ALLOC;
    }
    k = 0;
    for (i = 0; i < ef->high_bits; i++) {
      if (ef_is(ef, i, bit)) {
        size_t r = k % DS_EF_SAMPLE;
        size_t j = r / DS_EF_SUB;
        unsigned m;
        b = k / DS_EF_SAMPLE;
        m = s->mask[b];
        if (!s->is_long[b] && ((m >> j) & 1u) != 0) {
          size_t slot =
              s->dir[b] +
              ef_popcount((unsigned long)(m & ((1u << j) - 1u))) * DS_EF_SUB +
              r % DS_EF_SUB;
          s->spos[slot] = (unsigned short)(i - s->start[b]);
        }
        k++;
      }
    }
  }
  return DS_OK;
}

/* position of the k-th (0-based) bit equal to `bit` in high: O(1), at most
 * DS_EF_LONG_SUB / word bits + 1 words scanned */
static size_t ef_select(const ds_elias_fano_t *ef, size_t k, int bit) {
  const ds_ef_select_t *s = bit ? &ef->sel1 : &ef->sel0;
  size_t b = k / DS_EF_SAMPLE;
  size_t r = k % DS_EF_SAMPLE;
  size_t j = r / DS_EF_SUB;
  size_t left = r % DS_EF_SUB;
  unsigned m;
  size_t pos;
  size_t w;
  unsigned long word;
  if (s->is_long[b]) {
    return s->lpos[s->dir[b] + r];
  }
  m = s->mask[b];
  if (((m >> j) & 1u) != 0) {
    return s->start[b] +
           s->spos[s->dir[b] +
                   ef_popcount((unsigned long)(m & ((1u << j) - 1u))) *
                       DS_EF_SUB +
                   left];
  }
  /* pos is the position of occurrence (k / SUB) * SUB itself */
  pos = s->start[b] + s->sub[b * EF_SUBS + j];
  w = pos / EF_W;
  word = bit ? ef->high[w] : ~ef->high[w];
  word &= ~0UL << (pos % EF_W); /* from pos on */
  for (;;) {
    unsigned c = ef_popcount(word);
    if (left < c) {
      while (left > 0) {
        word &= word - 1;
        left--;
      }
      /* lowest set bit */
      {
        unsigned bb = 0;
        while (((word >> bb) & 1UL) == 0) {
          bb++;
        }
        return w * EF_W + bb;
      }
    }
    left -= c;
    w++;
    word = bit ? ef->high[w] : ~ef->high[w];
  }
}

ds_status_t ds_elias_fano_build(ds_elias_fano_t *ef,
                                const unsigned long *values, size_t n,
                                unsigned long universe, ds_context_t *context) {
  size_t i;
  size_t words;
  unsigned l = 0;
  if (ef == NULL || (n > 0 && values == NULL)) {
    return DS_ERR_NULL;
  }
  memset(ef, 0, sizeof(*ef));
  if (universe == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    if (values[i] >= universe || (i > 0 && values[i] < values[i - 1])) {
      return DS_ERR_INVALID;
    }
  }
  if (n > (size_t)-1 / EF_W / 4) {
    return DS_ERR_OVERFLOW;
  }
  if (n == 0) {
    /* nothing to encode: the widest low part keeps the high part at one
     * or two bits instead of one per universe value */
    l = (unsigned)EF_W - 1;
  }
  while (n > 0 && l + 1 < EF_W && (universe >> (l + 1)) >= n) {
    l++;
  }
  ef->n = n;
  ef->universe = universe;
  ef->low_bits = l;
  ef->context = context;
  ef->high_bits = n + (size_t)(universe >> l) + 1;
  words = ef->high_bits / EF_W + 2;
  ef->high =
      (unsigned long *)ds_context_calloc(context, words, sizeof(unsigned long));
  ef->low = (unsigned long *)ds_context_calloc(context, (n * l) / EF_W + 2,
                                               sizeof(unsigned long));
  if (ef->high == NULL || ef->low == NULL) {
    ds_elias_fano_destroy(ef);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    ef_set_bit(ef->high, (size_t)(values[i] >> l) + i);
    ef_write(ef->low, i * l, l,
             l == 0 ? 0 : values[i] & ((l < EF_W ? (1UL << l) : 0UL) - 1));
  }
  ef->zeros = ef->high_bits - n;
  if (ef_sel_build(ef, &ef->sel1, 1, n) != DS_OK ||
      ef_sel_build(ef, &ef->sel0, 0, ef->zeros) != DS_OK) {
    ds_elias_fano_destroy(ef);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_elias_fano_destroy(ds_elias_fano_t *ef) {
  if (ef == NULL) {
    return;
  }
  ds_context_free(ef->context, ef->high);
  ds_context_free(ef->context, ef->low);
  ef_sel_destroy(ef->context, &ef->sel1);
  ef_sel_destroy(ef->context, &ef->sel0);
  memset(ef, 0, sizeof(*ef));
}

size_t ds_elias_fano_size(const ds_elias_fano_t *ef) {
  return ef == NULL ? 0 : ef->n;
}

size_t ds_elias_fano_bits(const ds_elias_fano_t *ef) {
  return ef == NULL ? 0 : ef->n * ef->low_bits + ef->high_bits;
}

static size_t ef_sel_bytes(const ds_ef_select_t *s) {
  size_t i;
  size_t subs = 0;
  size_t longs = 0;
  for (i = 0; i < s->blocks; i++) {
    if (s->is_long[i]) {
      longs++;
    } else {
      subs += ef_popcount((unsigned long)s->mask[i]);
    }
  }
  return s->blocks * (2 * sizeof(size_t) + sizeof(unsigned short) + 1 +
                      EF_SUBS * sizeof(unsigned short)) +
         longs * DS_EF_SAMPLE * sizeof(size_t) +
         subs * DS_EF_SUB * sizeof(unsigned short);
}

size_t ds_elias_fano_index_bytes(const ds_elias_fano_t *ef) {
  return ef == NULL ? 0 : ef_sel_bytes(&ef->sel1) + ef_sel_bytes(&ef->sel0);
}

unsigned long ds_elias_fano_get(const ds_elias_fano_t *ef, size_t i) {
  size_t pos;
  if (ef == NULL || i >= ef->n) {
    return 0;
  }
  pos = ef_select(ef, i, 1);
  return ((unsigned long)(pos - i) << ef->low_bits) |
         ef_read(ef->low, i * ef->low_bits, ef->low_bits);
}

ds_status_t ds_elias_fano_next_geq(const ds_elias_fano_t *ef, unsigned long x,
                                   size_t *out_index, unsigned long *out) {
  unsigned long hx;
  unsigned long lx;
  size_t lo;
  size_t hi;
  if (ef == NULL || out_index == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (ef->n == 0 || x >= ef->universe) {
    return DS_NOT_FOUND;
  }
  hx = x >> ef->low_bits;
  lx = ef->low_bits < EF_W ? x & ((1UL << ef->low_bits) - 1) : x;
  /* bucket hx (the elements whose high part is hx) lies between zero
   * number hx - 1 and zero number hx of the high part; hx < zeros because
   * x < universe */
  lo = hx == 0 ? 0 : ef_select(ef, (size_t)hx - 1, 0) + 1 - (size_t)hx;
  hi = ef_select(ef, (size_t)hx, 0) - (size_t)hx;
  /* low parts are sorted within a bucket: binary search the first >= lx */
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (ef_read(ef->low, mid * ef->low_bits, ef->low_bits) < lx) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo >= ef->n) {
    return DS_NOT_FOUND;
  }
  /* either inside the bucket or the first element of a later bucket */
  *out_index = lo;
  *out = ds_elias_fano_get(ef, lo);
  return DS_OK;
}

ds_status_t ds_elias_fano_prev_leq(const ds_elias_fano_t *ef, unsigned long x,
                                   size_t *out_index, unsigned long *out) {
  size_t idx;
  unsigned long v;
  ds_status_t st;
  if (ef == NULL || out_index == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (ef->n == 0) {
    return DS_NOT_FOUND;
  }
  if (x >= ef->universe - 1) {
    idx = ef->n - 1;
  } else {
    st = ds_elias_fano_next_geq(ef, x + 1, &idx, &v);
    if (st == DS_NOT_FOUND) {
      idx = ef->n;
    } else if (st != DS_OK) {
      return st;
    }
    if (idx == 0) {
      return DS_NOT_FOUND;
    }
    idx--;
  }
  *out_index = idx;
  *out = ds_elias_fano_get(ef, idx);
  return DS_OK;
}
