#include "elias_fano.h"
#include <limits.h>
#include <string.h>

#define EF_W (sizeof(unsigned long) * CHAR_BIT)

static unsigned ef_popcount(unsigned long x) {
  unsigned c = 0;
  while (x != 0) {
    x &= x - 1;
    c++;
  }
  return c;
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

/* position of the k-th (0-based) bit equal to `bit` in high */
static size_t ef_select(const ds_elias_fano_t *ef, size_t k, int bit) {
  const size_t *sample = bit ? ef->sample1 : ef->sample0;
  size_t pos = sample[k / DS_EF_SAMPLE];
  size_t left = k % DS_EF_SAMPLE;
  size_t w;
  unsigned long word;
  /* the sample is the position of bit number (k / S) * S itself */
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
        unsigned b = 0;
        while (((word >> b) & 1UL) == 0) {
          b++;
        }
        return w * EF_W + b;
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
  size_t ones = 0;
  size_t zeros = 0;
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
  ef->sample1 = (size_t *)ds_context_alloc(context, (n / DS_EF_SAMPLE + 2) *
                                                        sizeof(size_t));
  ef->sample0 = (size_t *)ds_context_alloc(
      context, (ef->high_bits / DS_EF_SAMPLE + 2) * sizeof(size_t));
  if (ef->high == NULL || ef->low == NULL || ef->sample1 == NULL ||
      ef->sample0 == NULL) {
    ds_elias_fano_destroy(ef);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    ef_set_bit(ef->high, (size_t)(values[i] >> l) + i);
    ef_write(ef->low, i * l, l,
             l == 0 ? 0 : values[i] & ((l < EF_W ? (1UL << l) : 0UL) - 1));
  }
  for (i = 0; i < ef->high_bits; i++) {
    if (ef_get_bit(ef->high, i)) {
      if (ones % DS_EF_SAMPLE == 0) {
        ef->sample1[ones / DS_EF_SAMPLE] = i;
      }
      ones++;
    } else {
      if (zeros % DS_EF_SAMPLE == 0) {
        ef->sample0[zeros / DS_EF_SAMPLE] = i;
      }
      zeros++;
    }
  }
  ef->zeros = zeros;
  return DS_OK;
}

void ds_elias_fano_destroy(ds_elias_fano_t *ef) {
  if (ef == NULL) {
    return;
  }
  ds_context_free(ef->context, ef->high);
  ds_context_free(ef->context, ef->low);
  ds_context_free(ef->context, ef->sample1);
  ds_context_free(ef->context, ef->sample0);
  memset(ef, 0, sizeof(*ef));
}

size_t ds_elias_fano_size(const ds_elias_fano_t *ef) {
  return ef == NULL ? 0 : ef->n;
}

size_t ds_elias_fano_bits(const ds_elias_fano_t *ef) {
  return ef == NULL ? 0 : ef->n * ef->low_bits + ef->high_bits;
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
  size_t pos;
  size_t i;
  if (ef == NULL || out_index == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (ef->n == 0 || x >= ef->universe) {
    return DS_NOT_FOUND;
  }
  hx = x >> ef->low_bits;
  /* the elements with high part >= hx start after the hx-th zero */
  if (hx == 0) {
    pos = 0;
    i = 0;
  } else {
    pos = ef_select(ef, (size_t)hx - 1, 0) + 1;
    i = pos - (size_t)hx;
  }
  for (; i < ef->n; i++) {
    unsigned long v;
    while (!ef_get_bit(ef->high, pos)) {
      pos++;
    }
    v = ((unsigned long)(pos - i) << ef->low_bits) |
        ef_read(ef->low, i * ef->low_bits, ef->low_bits);
    if (v >= x) {
      *out_index = i;
      *out = v;
      return DS_OK;
    }
    pos++;
  }
  return DS_NOT_FOUND;
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
