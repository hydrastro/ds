#include "hash_family.h"
#include "numeric.h"

#define HF_MASK32 0xFFFFFFFFUL
#define HF_P DS_HASH_MERSENNE31

/* A fixed pseudo-random permutation of 0..255. */
static const unsigned char hf_pearson_default[256] = {
    127, 247, 22,  221, 184, 103, 188, 213, 15,  225, 85,  189, 142, 106, 119,
    198, 104, 253, 72,  133, 122, 51,  24,  222, 79,  171, 131, 146, 26,  190,
    217, 159, 174, 44,  101, 16,  27,  140, 21,  18,  40,  9,   53,  73,  110,
    194, 206, 129, 92,  170, 231, 19,  37,  23,  2,   61,  193, 88,  93,  6,
    203, 199, 95,  241, 35,  12,  158, 14,  248, 205, 147, 236, 215, 100, 29,
    89,  5,   151, 180, 4,   116, 208, 230, 112, 212, 102, 96,  177, 36,  47,
    82,  70,  43,  201, 192, 52,  56,  11,  219, 13,  168, 65,  49,  107, 78,
    97,  163, 149, 143, 160, 123, 75,  124, 138, 91,  1,   125, 165, 243, 223,
    84,  77,  126, 114, 71,  132, 57,  120, 68,  210, 98,  115, 196, 229, 251,
    185, 244, 228, 246, 81,  39,  178, 59,  148, 118, 45,  62,  232, 153, 200,
    42,  69,  28,  233, 105, 17,  227, 164, 80,  25,  67,  226, 211, 76,  113,
    195, 8,   234, 197, 172, 10,  60,  235, 176, 99,  117, 209, 167, 175, 218,
    58,  31,  252, 48,  183, 157, 191, 64,  152, 41,  34,  139, 141, 109, 161,
    63,  150, 245, 242, 33,  144, 181, 224, 0,   87,  46,  154, 66,  74,  162,
    202, 255, 207, 166, 83,  38,  238, 121, 135, 214, 237, 111, 136, 155, 128,
    173, 30,  239, 250, 220, 130, 3,   32,  204, 90,  134, 182, 216, 20,  179,
    240, 254, 186, 169, 86,  55,  137, 108, 54,  50,  156, 249, 145, 7,   94,
    187};

static unsigned long hf_rotl32(unsigned long x, unsigned int r) {
  x &= HF_MASK32;
  r &= 31U;
  if (r == 0) {
    return x;
  }
  return ((x << r) | (x >> (32U - r))) & HF_MASK32;
}

unsigned int ds_hash_pearson(const void *data, size_t length,
                             const unsigned char *table) {
  const unsigned char *p = (const unsigned char *)data;
  const unsigned char *t = table != NULL ? table : hf_pearson_default;
  unsigned int h = 0;
  size_t i;
  for (i = 0; i < length; i++) {
    h = t[h ^ p[i]];
  }
  return h;
}

unsigned long ds_hash_pearson_wide(const void *data, size_t length,
                                   unsigned int bytes,
                                   const unsigned char *table) {
  const unsigned char *p = (const unsigned char *)data;
  const unsigned char *t = table != NULL ? table : hf_pearson_default;
  unsigned long out = 0;
  unsigned int j;
  unsigned int h;
  size_t i;
  if (bytes < 1) {
    bytes = 1;
  }
  if (bytes > 4) {
    bytes = 4;
  }
  for (j = 0; j < bytes; j++) {
    /* Each output byte starts from a different first step. */
    h = t[(length > 0 ? p[0] : 0U) ^ j];
    for (i = 1; i < length; i++) {
      h = t[h ^ p[i]];
    }
    out = (out << 8) | h;
  }
  return out;
}

void ds_hash_pearson_table(unsigned char table[256], ds_random_func_t random,
                           void *random_state) {
  unsigned int i;
  unsigned int j;
  unsigned char t;
  for (i = 0; i < 256; i++) {
    table[i] = (unsigned char)i;
  }
  for (i = 255; i > 0; i--) {
    j = (unsigned int)(random(random_state) % (i + 1));
    t = table[i];
    table[i] = table[j];
    table[j] = t;
  }
}

unsigned long ds_hash_multiplicative(unsigned long key, unsigned int bits) {
  unsigned long x = (key * 2654435769UL) & HF_MASK32;
  if (bits == 0) {
    return 0;
  }
  if (bits >= 32) {
    return x;
  }
  return x >> (32U - bits);
}

unsigned long ds_hash_order_preserving(unsigned long key, unsigned long low,
                                       unsigned long high,
                                       unsigned long buckets) {
  unsigned long span;
  unsigned long off;
  if (buckets == 0 || high <= low || key <= low) {
    return 0;
  }
  if (key >= high) {
    return buckets - 1;
  }
  span = high - low;
  off = key - low;
  /* floor(off * buckets / (span + 1)) without overflow */
  if (span == ~0UL) {
    return off / (span / buckets + 1);
  }
  off = (unsigned long)((double)off * (double)buckets / ((double)span + 1.0));
  return off < buckets ? off : buckets - 1; /* rounding near the top */
}

void ds_universal_hash_init(ds_universal_hash_t *h, unsigned long m,
                            ds_random_func_t random, void *random_state) {
  h->a = 1 + random(random_state) % (HF_P - 1);
  h->b = random(random_state) % HF_P;
  h->m = m == 0 ? 1 : m;
}

unsigned long ds_universal_hash(const ds_universal_hash_t *h,
                                unsigned long key) {
  unsigned long x = ds_mulmod(h->a, key % HF_P, HF_P);
  x = (x + h->b) % HF_P;
  return x % h->m;
}

void ds_multiply_shift_init(ds_multiply_shift_t *h, unsigned int bits,
                            ds_random_func_t random, void *random_state) {
  h->a = (random(random_state) & HF_MASK32) | 1UL;
  h->bits = bits > 32 ? 32 : bits;
}

unsigned long ds_multiply_shift(const ds_multiply_shift_t *h,
                                unsigned long key) {
  unsigned long x = (h->a * (key & HF_MASK32)) & HF_MASK32;
  if (h->bits == 0) {
    return 0;
  }
  return h->bits >= 32 ? x : x >> (32U - h->bits);
}

void ds_tabulation_hash_init(ds_tabulation_hash_t *h, ds_random_func_t random,
                             void *random_state) {
  unsigned int t;
  unsigned int i;
  for (t = 0; t < 4; t++) {
    for (i = 0; i < 256; i++) {
      h->table[t][i] = random(random_state) & HF_MASK32;
    }
  }
}

unsigned long ds_tabulation_hash(const ds_tabulation_hash_t *h,
                                 unsigned long key) {
  return h->table[0][key & 0xFFUL] ^ h->table[1][(key >> 8) & 0xFFUL] ^
         h->table[2][(key >> 16) & 0xFFUL] ^ h->table[3][(key >> 24) & 0xFFUL];
}

unsigned long ds_tabulation_hash_bytes(const ds_tabulation_hash_t *h,
                                       const void *data, size_t length) {
  const unsigned char *p = (const unsigned char *)data;
  unsigned long x = h->table[0][length & 0xFFUL];
  size_t i;
  for (i = 0; i < length; i++) {
    x = hf_rotl32(x, 5) ^ h->table[i & 3U][p[i]];
  }
  return x;
}

void ds_rolling_hash_init(ds_rolling_hash_t *h, unsigned long base) {
  h->base = base == 0 ? 256 : base % HF_P;
  h->value = 0;
  h->power = 1;
  h->window = 0;
}

void ds_rolling_hash_push(ds_rolling_hash_t *h, unsigned char in) {
  h->value = (ds_mulmod(h->value, h->base, HF_P) + in) % HF_P;
  if (h->window > 0) {
    h->power = ds_mulmod(h->power, h->base, HF_P);
  }
  h->window++;
}

void ds_rolling_hash_roll(ds_rolling_hash_t *h, unsigned char out,
                          unsigned char in) {
  unsigned long drop;
  if (h->window == 0) {
    ds_rolling_hash_push(h, in);
    return;
  }
  drop = ds_mulmod(out, h->power, HF_P);
  h->value = (h->value + HF_P - drop) % HF_P;
  h->value = (ds_mulmod(h->value, h->base, HF_P) + in) % HF_P;
}

unsigned long ds_rolling_hash_of(unsigned long base, const void *data,
                                 size_t length) {
  ds_rolling_hash_t h;
  const unsigned char *p = (const unsigned char *)data;
  size_t i;
  ds_rolling_hash_init(&h, base);
  for (i = 0; i < length; i++) {
    ds_rolling_hash_push(&h, p[i]);
  }
  return h.value;
}

void ds_buzhash_init(ds_buzhash_t *h, ds_random_func_t random,
                     void *random_state) {
  unsigned int i;
  for (i = 0; i < 256; i++) {
    h->table[i] = random(random_state) & HF_MASK32;
  }
  ds_buzhash_reset(h);
}

void ds_buzhash_reset(ds_buzhash_t *h) {
  h->value = 0;
  h->window = 0;
}

void ds_buzhash_push(ds_buzhash_t *h, unsigned char in) {
  h->value = hf_rotl32(h->value, 1) ^ h->table[in];
  h->window++;
}

void ds_buzhash_roll(ds_buzhash_t *h, unsigned char out, unsigned char in) {
  if (h->window == 0) {
    ds_buzhash_push(h, in);
    return;
  }
  h->value = hf_rotl32(h->value, 1) ^
             hf_rotl32(h->table[out], (unsigned int)(h->window % 32U)) ^
             h->table[in];
}
