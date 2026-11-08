#include "xor_filter.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define XF_W (sizeof(unsigned long) * CHAR_BIT)
#define XF_TRIES 64

static unsigned long xf_mix(unsigned long h) {
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

static unsigned long xf_get(const ds_xor_filter_t *f, size_t i) {
  size_t pos = i * f->fp_bits, w = pos / XF_W;
  unsigned off = (unsigned)(pos % XF_W);
  unsigned long v = f->table[w] >> off;
  if (off + f->fp_bits > XF_W) {
    v |= f->table[w + 1] << (XF_W - off);
  }
  return v & ((1UL << f->fp_bits) - 1UL);
}

static void xf_set(ds_xor_filter_t *f, size_t i, unsigned long v) {
  size_t pos = i * f->fp_bits, w = pos / XF_W;
  unsigned off = (unsigned)(pos % XF_W);
  unsigned long mask = (1UL << f->fp_bits) - 1UL;
  f->table[w] = (f->table[w] & ~(mask << off)) | ((v & mask) << off);
  if (off + f->fp_bits > XF_W) {
    unsigned hi = off + f->fp_bits - (unsigned)XF_W;
    unsigned long hmask = (1UL << hi) - 1UL;
    f->table[w + 1] = (f->table[w + 1] & ~hmask) | ((v & mask) >> (XF_W - off));
  }
}

/* the three slots and the fingerprint of a key */
static void xf_slots(const ds_xor_filter_t *f, unsigned long key, size_t s[3],
                     unsigned long *fp) {
  unsigned long x = xf_mix(key + f->seed);
  unsigned long y = xf_mix(x ^ 0x9E3779B9UL);
  unsigned long z = xf_mix(y + 0x632BE5ABUL);
  s[0] = (size_t)(x % (unsigned long)f->segment);
  s[1] = (size_t)(y % (unsigned long)f->segment) + f->segment;
  s[2] = (size_t)(z % (unsigned long)f->segment) + 2 * f->segment;
  *fp = (x ^ (y >> 7)) & ((1UL << f->fp_bits) - 1UL);
}

static int xf_cmp(unsigned long a, unsigned long b) {
  return (a > b) - (a < b);
}

/* heap sort: no recursion, no allocation */
static void xf_sort(unsigned long *a, size_t n) {
  size_t start, end;
  if (n < 2) {
    return;
  }
  for (start = n / 2; start-- > 0;) {
    size_t root = start;
    while (2 * root + 1 < n) {
      size_t c = 2 * root + 1;
      unsigned long t;
      if (c + 1 < n && xf_cmp(a[c], a[c + 1]) < 0) {
        c++;
      }
      if (xf_cmp(a[root], a[c]) >= 0) {
        break;
      }
      t = a[root];
      a[root] = a[c];
      a[c] = t;
      root = c;
    }
  }
  for (end = n - 1; end > 0; end--) {
    unsigned long t = a[0];
    size_t root = 0;
    a[0] = a[end];
    a[end] = t;
    while (2 * root + 1 < end) {
      size_t c = 2 * root + 1;
      if (c + 1 < end && xf_cmp(a[c], a[c + 1]) < 0) {
        c++;
      }
      if (xf_cmp(a[root], a[c]) >= 0) {
        break;
      }
      t = a[root];
      a[root] = a[c];
      a[c] = t;
      root = c;
    }
  }
}

ds_status_t ds_xor_filter_build(ds_xor_filter_t *f, const unsigned long *hashes,
                                size_t n, unsigned fp_bits,
                                ds_context_t *context) {
  unsigned long *keys = NULL, *xors = NULL, *stack_key = NULL;
  size_t *counts = NULL, *queue = NULL, *stack_slot = NULL;
  size_t i, m = 0, words, try_;
  ds_status_t st = DS_ERR_INTERNAL;
  if (f == NULL || (hashes == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  memset(f, 0, sizeof(*f));
  if (fp_bits == 0 || fp_bits > 32 || fp_bits >= XF_W) {
    return DS_ERR_INVALID;
  }
  f->fp_bits = fp_bits;
  f->context = context;
  keys = (unsigned long *)ds_context_alloc(context, (n == 0 ? 1 : n) *
                                                        sizeof(unsigned long));
  if (keys == NULL) {
    return DS_ERR_ALLOC;
  }
  if (n > 0) {
    memcpy(keys, hashes, n * sizeof(unsigned long));
  }
  xf_sort(keys, n);
  for (i = 0; i < n; i++) {
    if (m == 0 || keys[m - 1] != keys[i]) {
      keys[m++] = keys[i];
    }
  }
  f->count = m;
  f->segment = (m + m / 4 + 32) / 3 + 1; /* capacity ~ 1.25 n + 32 */
  f->capacity = 3 * f->segment;
  words = f->capacity * fp_bits / XF_W + 2;
  f->table =
      (unsigned long *)ds_context_alloc(context, words * sizeof(unsigned long));
  counts = (size_t *)ds_context_alloc(context, f->capacity * sizeof(size_t));
  xors = (unsigned long *)ds_context_alloc(context,
                                           f->capacity * sizeof(unsigned long));
  queue = (size_t *)ds_context_alloc(context, f->capacity * sizeof(size_t));
  stack_key = (unsigned long *)ds_context_alloc(
      context, (m == 0 ? 1 : m) * sizeof(unsigned long));
  stack_slot =
      (size_t *)ds_context_alloc(context, (m == 0 ? 1 : m) * sizeof(size_t));
  if (f->table == NULL || counts == NULL || xors == NULL || queue == NULL ||
      stack_key == NULL || stack_slot == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (try_ = 0; try_ < XF_TRIES; try_++) {
    size_t qh = 0, qt = 0, sp = 0, s[3];
    unsigned long fp;
    f->seed = xf_mix(0x51ED270BUL + (unsigned long)try_ * 0x9E3779B9UL);
    memset(counts, 0, f->capacity * sizeof(size_t));
    memset(xors, 0, f->capacity * sizeof(unsigned long));
    for (i = 0; i < m; i++) {
      int k;
      xf_slots(f, keys[i], s, &fp);
      for (k = 0; k < 3; k++) {
        counts[s[k]]++;
        xors[s[k]] ^= keys[i];
      }
    }
    for (i = 0; i < f->capacity; i++) {
      if (counts[i] == 1) {
        queue[qt++] = i;
      }
    }
    /* peel: a slot hit by one key determines that key */
    while (qh < qt) {
      size_t slot = queue[qh++];
      unsigned long key;
      int k;
      if (counts[slot] != 1) {
        continue;
      }
      key = xors[slot];
      stack_key[sp] = key;
      stack_slot[sp] = slot;
      sp++;
      xf_slots(f, key, s, &fp);
      for (k = 0; k < 3; k++) {
        counts[s[k]]--;
        xors[s[k]] ^= key;
        if (counts[s[k]] == 1) {
          queue[qt++] = s[k];
        }
      }
    }
    if (sp == m) {
      memset(f->table, 0, words * sizeof(unsigned long));
      while (sp-- > 0) {
        unsigned long v;
        int k;
        xf_slots(f, stack_key[sp], s, &fp);
        v = fp;
        for (k = 0; k < 3; k++) {
          if (s[k] != stack_slot[sp]) {
            v ^= xf_get(f, s[k]);
          }
        }
        xf_set(f, stack_slot[sp], v);
      }
      st = DS_OK;
      break;
    }
  }
done:
  ds_context_free(context, keys);
  ds_context_free(context, counts);
  ds_context_free(context, xors);
  ds_context_free(context, queue);
  ds_context_free(context, stack_key);
  ds_context_free(context, stack_slot);
  if (st != DS_OK) {
    ds_context_free(context, f->table);
    f->table = NULL;
  }
  return st;
}

void ds_xor_filter_destroy(ds_xor_filter_t *f) {
  if (f == NULL) {
    return;
  }
  ds_context_free(f->context, f->table);
  memset(f, 0, sizeof(*f));
}

bool ds_xor_filter_contains(const ds_xor_filter_t *f, unsigned long hash) {
  size_t s[3];
  unsigned long fp;
  if (f == NULL || f->table == NULL) {
    return false;
  }
  xf_slots(f, hash, s, &fp);
  return (fp ^ xf_get(f, s[0]) ^ xf_get(f, s[1]) ^ xf_get(f, s[2])) == 0;
}

size_t ds_xor_filter_count(const ds_xor_filter_t *f) {
  return f == NULL ? 0 : f->count;
}

size_t ds_xor_filter_bytes(const ds_xor_filter_t *f) {
  if (f == NULL) {
    return 0;
  }
  return sizeof(*f) +
         (f->capacity * f->fp_bits / XF_W + 2) * sizeof(unsigned long);
}
