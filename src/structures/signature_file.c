#include <dsa/algorithms/hash.h>
#include <dsa/structures/signature_file.h>
#include <string.h>

static size_t sf_default_hash(const unsigned char *w, size_t len, size_t seed,
                              void *user) {
  unsigned long h1, h2, lo, hi;
  (void)user;
  h1 = ds_hash_murmur3_32(w, len, 0UL);
  h2 = ds_hash_murmur3_32(w, len, 0x9747b28cUL);
  lo = ds_hash_mix32(h1 + (unsigned long)seed * 0x9e3779b9UL);
  hi = ds_hash_mix32(h2 + (unsigned long)seed * 0x85ebca6bUL);
  return (size_t)lo | (((size_t)hi << 16) << 16);
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
  f->context = context != NULL ? context : ds_default_context();
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
  unsigned long h1 = 0;
  unsigned long h2 = 0;
  size_t i;
  size_t bit;
  if (f == NULL || sig == NULL || (word == NULL && len > 0)) {
    return;
  }
  /* Each probe is mixed independently, not an arithmetic progression.
   * The built-in path reads the word twice regardless of k. */
  if (f->hash == sf_default_hash) {
    h1 = ds_hash_murmur3_32(word, len, 0UL);
    h2 = ds_hash_murmur3_32(word, len, 0x9747b28cUL);
  }
  for (i = 0; i < f->k; i++) {
    if (f->hash == sf_default_hash) {
      unsigned long lo = ds_hash_mix32(h1 + (unsigned long)i * 0x9e3779b9UL);
      unsigned long hi = ds_hash_mix32(h2 + (unsigned long)i * 0x85ebca6bUL);
      bit = ((size_t)lo | (((size_t)hi << 16) << 16)) % f->m;
    } else {
      bit = f->hash(word, len, i, f->user) % f->m;
    }
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
  size_t i;
  void *p;
  if (f == NULL || (count > 0 && (words == NULL || lens == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < count; i++) {
    if (words[i] == NULL && lens[i] != 0) {
      return DS_ERR_NULL;
    }
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
  for (i = 0; i < count; i++) {
    if (words[i] == NULL && lens[i] != 0) {
      return DS_ERR_NULL;
    }
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
