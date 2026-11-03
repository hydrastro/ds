#include "iblt.h"
#include <string.h>

typedef union ib_max {
  long l;
  double d;
  void *p;
  long double ld;
} ib_max_t;
struct ib_align_probe {
  char c;
  ib_max_t u;
};
#define IB_ALIGN (offsetof(struct ib_align_probe, u))
#define IB_CHECK_SEED 0x5BD1E995UL

static size_t ib_round(size_t n) {
  return (n + IB_ALIGN - 1) / IB_ALIGN * IB_ALIGN;
}

/* MurmurHash3 x86_32. */
static unsigned long ib_murmur(const void *key, size_t length,
                               unsigned long seed, void *user) {
  const unsigned char *p = (const unsigned char *)key;
  unsigned long h = seed & 0xFFFFFFFFUL;
  unsigned long k;
  size_t i;
  size_t blocks = length / 4;
  (void)user;
  for (i = 0; i < blocks; i++) {
    k = (unsigned long)p[4 * i] | ((unsigned long)p[4 * i + 1] << 8) |
        ((unsigned long)p[4 * i + 2] << 16) |
        ((unsigned long)p[4 * i + 3] << 24);
    k = (k * 0xCC9E2D51UL) & 0xFFFFFFFFUL;
    k = ((k << 15) | (k >> 17)) & 0xFFFFFFFFUL;
    k = (k * 0x1B873593UL) & 0xFFFFFFFFUL;
    h ^= k;
    h = ((h << 13) | (h >> 19)) & 0xFFFFFFFFUL;
    h = (h * 5 + 0xE6546B64UL) & 0xFFFFFFFFUL;
  }
  k = 0;
  if ((length & 3U) >= 3) {
    k ^= (unsigned long)p[4 * blocks + 2] << 16;
  }
  if ((length & 3U) >= 2) {
    k ^= (unsigned long)p[4 * blocks + 1] << 8;
  }
  if ((length & 3U) >= 1) {
    k ^= (unsigned long)p[4 * blocks];
    k = (k * 0xCC9E2D51UL) & 0xFFFFFFFFUL;
    k = ((k << 15) | (k >> 17)) & 0xFFFFFFFFUL;
    k = (k * 0x1B873593UL) & 0xFFFFFFFFUL;
    h ^= k;
  }
  h ^= (unsigned long)length & 0xFFFFFFFFUL;
  h ^= h >> 16;
  h = (h * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
  h ^= h >> 13;
  h = (h * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

static unsigned long ib_hash(const ds_iblt_t *t, const void *key,
                             unsigned long seed) {
  return t->config.hash(key, t->config.key_size, seed, t->config.user);
}

static size_t ib_cell(const ds_iblt_t *t, const void *key, unsigned int j) {
  size_t per = t->cells / t->config.hashes;
  unsigned long h =
      ib_hash(t, key, t->config.seed + 0x9E3779B9UL * (unsigned long)(j + 1));
  return (size_t)j * per + (size_t)(h % (unsigned long)per);
}

static unsigned long ib_check(const ds_iblt_t *t, const void *key) {
  return ib_hash(t, key, t->config.seed ^ IB_CHECK_SEED);
}

static unsigned char *ib_ksum(const ds_iblt_t *t, size_t c) {
  return t->key_sums + c * t->config.key_size;
}

static unsigned char *ib_vsum(const ds_iblt_t *t, size_t c) {
  return t->value_sums + c * t->config.value_size;
}

static void ib_xor(unsigned char *dst, const unsigned char *src, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    dst[i] = (unsigned char)(dst[i] ^ src[i]);
  }
}

void ds_iblt_config_init(ds_iblt_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->cells = 0;
  config->hashes = 3;
  config->seed = 0x2545F491UL;
  config->hash = NULL;
  config->user = NULL;
  config->context = NULL;
}

static size_t ib_cells_of(const ds_iblt_config_t *config) {
  size_t k = config->hashes;
  return (config->cells + k - 1) / k * k;
}

size_t ds_iblt_buffer_size(const ds_iblt_config_t *config) {
  size_t n = ib_cells_of(config);
  return IB_ALIGN - 1 + ib_round(n * sizeof(long)) +
         ib_round(n * sizeof(unsigned long)) + ib_round(n * config->key_size) +
         n * config->value_size;
}

static ds_status_t ib_setup(ds_iblt_t *t, const ds_iblt_config_t *config,
                            void *block) {
  unsigned char *p = (unsigned char *)block;
  size_t misalign = (size_t)p % IB_ALIGN;
  size_t n;
  if (misalign != 0) {
    p += IB_ALIGN - misalign;
  }
  t->config = *config;
  if (t->config.hash == NULL) {
    t->config.hash = ib_murmur;
  }
  n = ib_cells_of(config);
  t->cells = n;
  t->counts = (long *)(void *)p;
  p += ib_round(n * sizeof(long));
  t->hash_sums = (unsigned long *)(void *)p;
  p += ib_round(n * sizeof(unsigned long));
  t->key_sums = p;
  p += ib_round(n * config->key_size);
  t->value_sums = p;
  t->block = block;
  ds_iblt_clear(t);
  return DS_OK;
}

static ds_status_t ib_check_config(const ds_iblt_config_t *config) {
  if (config == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || config->cells == 0 || config->hashes < 2 ||
      config->hashes > 8) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

ds_status_t ds_iblt_init(ds_iblt_t *iblt, const ds_iblt_config_t *config) {
  void *block;
  ds_status_t status;
  if (iblt == NULL) {
    return DS_ERR_NULL;
  }
  status = ib_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  block = ds_context_alloc(config->context, ds_iblt_buffer_size(config));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  iblt->fixed = false;
  return ib_setup(iblt, config, block);
}

ds_status_t ds_iblt_init_buffer(ds_iblt_t *iblt, const ds_iblt_config_t *config,
                                void *buffer) {
  ds_status_t status;
  if (iblt == NULL || buffer == NULL) {
    return DS_ERR_NULL;
  }
  status = ib_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  iblt->fixed = true;
  return ib_setup(iblt, config, buffer);
}

void ds_iblt_destroy(ds_iblt_t *iblt) {
  if (iblt != NULL && !iblt->fixed) {
    ds_context_free(iblt->config.context, iblt->block);
  }
  if (iblt != NULL) {
    iblt->block = NULL;
    iblt->cells = 0;
  }
}

void ds_iblt_clear(ds_iblt_t *iblt) {
  size_t n = iblt->cells;
  size_t i;
  for (i = 0; i < n; i++) {
    iblt->counts[i] = 0;
    iblt->hash_sums[i] = 0;
  }
  memset(iblt->key_sums, 0, n * iblt->config.key_size);
  if (iblt->config.value_size > 0) {
    memset(iblt->value_sums, 0, n * iblt->config.value_size);
  }
}

static void ib_apply(ds_iblt_t *t, const void *key, const void *value,
                     long delta) {
  unsigned long check = ib_check(t, key);
  unsigned int j;
  size_t c;
  for (j = 0; j < t->config.hashes; j++) {
    c = ib_cell(t, key, j);
    t->counts[c] += delta;
    t->hash_sums[c] ^= check;
    ib_xor(ib_ksum(t, c), (const unsigned char *)key, t->config.key_size);
    if (t->config.value_size > 0) {
      ib_xor(ib_vsum(t, c), (const unsigned char *)value, t->config.value_size);
    }
  }
}

void ds_iblt_insert(ds_iblt_t *iblt, const void *key, const void *value) {
  ib_apply(iblt, key, value, 1);
}

void ds_iblt_erase(ds_iblt_t *iblt, const void *key, const void *value) {
  ib_apply(iblt, key, value, -1);
}

static bool ib_pure(const ds_iblt_t *t, size_t c) {
  return (t->counts[c] == 1 || t->counts[c] == -1) &&
         t->hash_sums[c] == ib_check(t, ib_ksum(t, c));
}

static bool ib_cell_empty(const ds_iblt_t *t, size_t c) {
  size_t i;
  if (t->counts[c] != 0 || t->hash_sums[c] != 0) {
    return false;
  }
  for (i = 0; i < t->config.key_size; i++) {
    if (ib_ksum(t, c)[i] != 0) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_iblt_get(const ds_iblt_t *iblt, const void *key,
                        void *out_value) {
  unsigned int j;
  size_t c;
  for (j = 0; j < iblt->config.hashes; j++) {
    c = ib_cell(iblt, key, j);
    if (ib_cell_empty(iblt, c)) {
      return DS_NOT_FOUND;
    }
    if (iblt->counts[c] == 1 && iblt->hash_sums[c] == ib_check(iblt, key) &&
        memcmp(ib_ksum(iblt, c), key, iblt->config.key_size) == 0) {
      if (out_value != NULL && iblt->config.value_size > 0) {
        memcpy(out_value, ib_vsum(iblt, c), iblt->config.value_size);
      }
      return DS_OK;
    }
    if (ib_pure(iblt, c)) {
      return DS_NOT_FOUND; /* the cell holds exactly one other key */
    }
  }
  return DS_ERR_STATE;
}

ds_status_t ds_iblt_subtract(ds_iblt_t *iblt, const ds_iblt_t *other) {
  size_t i;
  if (iblt->cells != other->cells ||
      iblt->config.hashes != other->config.hashes ||
      iblt->config.key_size != other->config.key_size ||
      iblt->config.value_size != other->config.value_size ||
      iblt->config.seed != other->config.seed) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < iblt->cells; i++) {
    iblt->counts[i] -= other->counts[i];
    iblt->hash_sums[i] ^= other->hash_sums[i];
  }
  ib_xor(iblt->key_sums, other->key_sums, iblt->cells * iblt->config.key_size);
  if (iblt->config.value_size > 0) {
    ib_xor(iblt->value_sums, other->value_sums,
           iblt->cells * iblt->config.value_size);
  }
  return DS_OK;
}

bool ds_iblt_is_empty(const ds_iblt_t *iblt) {
  size_t i;
  for (i = 0; i < iblt->cells; i++) {
    if (!ib_cell_empty(iblt, i)) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_iblt_peel(ds_iblt_t *iblt, ds_iblt_visit_func_t visit,
                         void *user) {
  size_t i;
  bool progress = true;
  int sign;
  ds_status_t status;
  unsigned char kbuf[64];
  unsigned char vbuf[64];
  unsigned char *key = kbuf;
  unsigned char *value = vbuf;
  ds_context_t *ctx = iblt->config.context;
  if (iblt->config.key_size > sizeof kbuf) {
    key = (unsigned char *)ds_context_alloc(ctx, iblt->config.key_size);
  }
  if (iblt->config.value_size > sizeof vbuf) {
    value = (unsigned char *)ds_context_alloc(ctx, iblt->config.value_size);
  }
  if (key == NULL || value == NULL) {
    status = DS_ERR_ALLOC;
    goto out;
  }
  status = DS_OK;
  while (progress) {
    progress = false;
    for (i = 0; i < iblt->cells; i++) {
      if (!ib_pure(iblt, i)) {
        continue;
      }
      sign = iblt->counts[i] > 0 ? 1 : -1;
      memcpy(key, ib_ksum(iblt, i), iblt->config.key_size);
      if (iblt->config.value_size > 0) {
        memcpy(value, ib_vsum(iblt, i), iblt->config.value_size);
      }
      ib_apply(iblt, key, value, -sign);
      progress = true;
      if (visit != NULL) {
        status = visit(key, value, sign, user);
        if (status != DS_OK) {
          goto out;
        }
      }
    }
  }
  status = ds_iblt_is_empty(iblt) ? DS_OK : DS_ERR_STATE;
out:
  if (key != kbuf && key != NULL) {
    ds_context_free(ctx, key);
  }
  if (value != vbuf && value != NULL) {
    ds_context_free(ctx, value);
  }
  return status;
}

ds_status_t ds_iblt_list(const ds_iblt_t *iblt, ds_iblt_visit_func_t visit,
                         void *user) {
  ds_iblt_t copy;
  ds_status_t status = ds_iblt_init(&copy, &iblt->config);
  if (status != DS_OK) {
    return status;
  }
  memcpy(copy.counts, iblt->counts, iblt->cells * sizeof(long));
  memcpy(copy.hash_sums, iblt->hash_sums, iblt->cells * sizeof(unsigned long));
  memcpy(copy.key_sums, iblt->key_sums, iblt->cells * iblt->config.key_size);
  if (iblt->config.value_size > 0) {
    memcpy(copy.value_sums, iblt->value_sums,
           iblt->cells * iblt->config.value_size);
  }
  status = ds_iblt_peel(&copy, visit, user);
  ds_iblt_destroy(&copy);
  return status;
}
