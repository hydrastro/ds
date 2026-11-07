#include "signature_file.h"
#include <string.h>

static size_t sf_default_hash(const unsigned char *w, size_t len, size_t seed,
                              void *user) {
  /* FNV-1a with the seed folded in, then a final mix */
  size_t h = (size_t)2166136261UL ^ (seed * (size_t)0x9E3779B1UL);
  size_t i;
  (void)user;
  for (i = 0; i < len; i++) {
    h ^= w[i];
    h *= (size_t)16777619UL;
  }
  h ^= h >> 15;
  h *= (size_t)0x2c1b3c6dUL;
  h ^= h >> 12;
  return h;
}

ds_status_t ds_signature_file_init(ds_signature_file_t *f, size_t m, size_t k,
                                   ds_signature_hash_func_t hash, void *user,
                                   ds_context_t *context) {
  if (f == NULL) {
    return DS_ERR_NULL;
  }
  if (m < 8 || k == 0 || k > m || m > ((size_t)-1) / 2) {
    return DS_ERR_INVALID;
  }
  memset(f, 0, sizeof(*f));
  f->m = m;
  f->k = k;
  f->row_bytes = (m + 7) / 8;
  f->hash = hash != NULL ? hash : sf_default_hash;
  f->user = user;
  f->context = context;
  return DS_OK;
}

void ds_signature_file_destroy(ds_signature_file_t *f) {
  if (f == NULL) {
    return;
  }
  ds_context_free(f->context, f->rows);
  f->rows = NULL;
  f->records = f->capacity = 0;
}

size_t ds_signature_file_records(const ds_signature_file_t *f) {
  return f == NULL ? 0 : f->records;
}

void ds_signature_superimpose(const ds_signature_file_t *f,
                              const unsigned char *word, size_t len,
                              unsigned char *sig) {
  size_t h1;
  size_t h2;
  size_t i;
  size_t bit;
  if (f == NULL || sig == NULL || (word == NULL && len > 0)) {
    return;
  }
  /* k positions by double hashing */
  h1 = f->hash(word, len, 0, f->user);
  h2 = f->hash(word, len, 1, f->user) | 1u;
  for (i = 0; i < f->k; i++) {
    bit = (h1 + i * h2) % f->m;
    sig[bit / 8] = (unsigned char)(sig[bit / 8] | (1u << (bit % 8)));
  }
}

static void sf_build(const ds_signature_file_t *f,
                     const unsigned char *const *words, const size_t *lens,
                     size_t count, unsigned char *sig) {
  size_t i;
  memset(sig, 0, f->row_bytes);
  for (i = 0; i < count; i++) {
    ds_signature_superimpose(f, words[i], lens[i], sig);
  }
}

ds_status_t ds_signature_file_add(ds_signature_file_t *f,
                                  const unsigned char *const *words,
                                  const size_t *lens, size_t count,
                                  size_t *out_record) {
  size_t cap;
  void *p;
  if (f == NULL || (count > 0 && (words == NULL || lens == NULL))) {
    return DS_ERR_NULL;
  }
  if (f->records == f->capacity) {
    cap = f->capacity < 16 ? 16 : f->capacity * 2;
    if (cap <= f->capacity || cap > ((size_t)-1) / f->row_bytes) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(f->context, f->rows, cap * f->row_bytes);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    f->rows = (unsigned char *)p;
    f->capacity = cap;
  }
  sf_build(f, words, lens, count, f->rows + f->records * f->row_bytes);
  if (out_record != NULL) {
    *out_record = f->records;
  }
  f->records++;
  return DS_OK;
}

ds_status_t ds_signature_file_query(const ds_signature_file_t *f,
                                    const unsigned char *const *words,
                                    const size_t *lens, size_t count,
                                    bool (*visit)(size_t record, void *user),
                                    void *user) {
  unsigned char *q;
  const unsigned char *row;
  size_t r;
  size_t i;
  if (f == NULL || visit == NULL ||
      (count > 0 && (words == NULL || lens == NULL))) {
    return DS_ERR_NULL;
  }
  q = (unsigned char *)ds_context_alloc(f->context, f->row_bytes);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  sf_build(f, words, lens, count, q);
  for (r = 0; r < f->records; r++) {
    row = f->rows + r * f->row_bytes;
    for (i = 0; i < f->row_bytes && (row[i] & q[i]) == q[i]; i++) {
    }
    if (i == f->row_bytes && !visit(r, user)) {
      ds_context_free(f->context, q);
      return DS_STOP;
    }
  }
  ds_context_free(f->context, q);
  return DS_OK;
}

double ds_signature_file_density(const ds_signature_file_t *f, size_t record) {
  const unsigned char *row;
  size_t i;
  size_t bits = 0;
  unsigned c;
  if (f == NULL || record >= f->records) {
    return 0.0;
  }
  row = f->rows + record * f->row_bytes;
  for (i = 0; i < f->row_bytes; i++) {
    for (c = row[i]; c != 0; c &= c - 1) {
      bits++;
    }
  }
  return (double)bits / (double)f->m;
}
