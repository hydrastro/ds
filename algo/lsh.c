#include "lsh.h"
#include "hash.h"
#include "numeric.h"
#include <math.h>

#define LSH_P 0x7FFFFFFFUL

ds_status_t ds_minhash_init(ds_minhash_t *mh, size_t k, ds_random_func_t random,
                            void *random_state, ds_context_t *context) {
  size_t i;
  if (mh == NULL || random == NULL) {
    return DS_ERR_NULL;
  }
  if (k == 0 || k > ((size_t)-1) / sizeof(unsigned long)) {
    return DS_ERR_INVALID;
  }
  mh->k = k;
  mh->context = context;
  mh->a = (unsigned long *)ds_context_alloc(context, k * sizeof(unsigned long));
  mh->b = (unsigned long *)ds_context_alloc(context, k * sizeof(unsigned long));
  if (mh->a == NULL || mh->b == NULL) {
    ds_minhash_destroy(mh);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < k; i++) {
    mh->a[i] = 1 + random(random_state) % (LSH_P - 1);
    mh->b[i] = random(random_state) % LSH_P;
  }
  return DS_OK;
}

void ds_minhash_destroy(ds_minhash_t *mh) {
  if (mh != NULL) {
    ds_context_free(mh->context, mh->a);
    ds_context_free(mh->context, mh->b);
    mh->a = NULL;
    mh->b = NULL;
    mh->k = 0;
  }
}

void ds_minhash_signature(const ds_minhash_t *mh,
                          const unsigned long *element_hashes, size_t count,
                          unsigned long *out) {
  size_t i;
  size_t j;
  unsigned long x;
  unsigned long v;
  for (i = 0; i < mh->k; i++) {
    out[i] = LSH_P;
  }
  for (j = 0; j < count; j++) {
    x = (element_hashes[j] & 0xFFFFFFFFUL) % LSH_P;
    for (i = 0; i < mh->k; i++) {
      v = (ds_mulmod(mh->a[i], x, LSH_P) + mh->b[i]) % LSH_P;
      if (v < out[i]) {
        out[i] = v;
      }
    }
  }
}

double ds_minhash_similarity(const unsigned long *a, const unsigned long *b,
                             size_t k) {
  size_t i;
  size_t same = 0;
  if (k == 0) {
    return 0;
  }
  for (i = 0; i < k; i++) {
    same += a[i] == b[i];
  }
  return (double)same / (double)k;
}

double ds_jaccard_sorted(const unsigned long *a, size_t na,
                         const unsigned long *b, size_t nb) {
  size_t i = 0;
  size_t j = 0;
  size_t inter = 0;
  size_t uni = 0;
  unsigned long x;
  while (i < na || j < nb) {
    if (j >= nb || (i < na && a[i] < b[j])) {
      x = a[i];
      uni++;
    } else if (i >= na || b[j] < a[i]) {
      x = b[j];
      uni++;
    } else {
      x = a[i];
      inter++;
      uni++;
    }
    while (i < na && a[i] == x) {
      i++;
    }
    while (j < nb && b[j] == x) {
      j++;
    }
  }
  return uni == 0 ? 1.0 : (double)inter / (double)uni;
}

ds_status_t ds_lsh_bands(const unsigned long *signature, size_t k, size_t bands,
                         unsigned long *out_bands) {
  size_t rows;
  size_t band;
  size_t r;
  unsigned long h;
  unsigned char bytes[4];
  unsigned long v;
  if (bands == 0 || k % bands != 0) {
    return DS_ERR_INVALID;
  }
  rows = k / bands;
  for (band = 0; band < bands; band++) {
    h = 0x9E3779B9UL ^ (unsigned long)band;
    for (r = 0; r < rows; r++) {
      v = signature[band * rows + r];
      bytes[0] = (unsigned char)(v & 0xFFU);
      bytes[1] = (unsigned char)((v >> 8) & 0xFFU);
      bytes[2] = (unsigned char)((v >> 16) & 0xFFU);
      bytes[3] = (unsigned char)((v >> 24) & 0xFFU);
      h = ds_hash_murmur3_32(bytes, 4, h);
    }
    out_bands[band] = h;
  }
  return DS_OK;
}

double ds_lsh_candidate_probability(double s, size_t rows, size_t bands) {
  return 1.0 - pow(1.0 - pow(s, (double)rows), (double)bands);
}

unsigned long ds_simhash(const unsigned long *feature_hashes,
                         const double *weights, size_t count,
                         unsigned int bits) {
  double votes[32];
  unsigned int bit;
  size_t i;
  unsigned long h;
  unsigned long out = 0;
  double w;
  if (bits == 0) {
    return 0;
  }
  if (bits > 32) {
    bits = 32;
  }
  for (bit = 0; bit < bits; bit++) {
    votes[bit] = 0;
  }
  for (i = 0; i < count; i++) {
    h = ds_hash_mix32(feature_hashes[i] & 0xFFFFFFFFUL);
    w = weights != NULL ? weights[i] : 1.0;
    for (bit = 0; bit < bits; bit++) {
      votes[bit] += ((h >> bit) & 1UL) ? w : -w;
    }
  }
  for (bit = 0; bit < bits; bit++) {
    if (votes[bit] > 0) {
      out |= 1UL << bit;
    }
  }
  return out;
}

unsigned int ds_simhash_distance(unsigned long a, unsigned long b) {
  unsigned long x = (a ^ b) & 0xFFFFFFFFUL;
  unsigned int n = 0;
  while (x != 0) {
    x &= x - 1;
    n++;
  }
  return n;
}
