#include "cuckoo_filter.h"
#include <string.h>

#define CF_MAX_KICKS 500
#define CF_SIZE_MAX (~(size_t)0)

static unsigned long cf_mix(unsigned long h) {
  h &= 0xFFFFFFFFUL;
  h ^= h >> 16;
  h = (h * 0x7FEB352DUL) & 0xFFFFFFFFUL;
  h ^= h >> 15;
  h = (h * 0x846CA68BUL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

static unsigned short cf_fingerprint(unsigned long hash) {
  unsigned long f = cf_mix(hash ^ 0x5BD1E995UL) & 0xFFFFUL;
  return (unsigned short)(f == 0 ? 1 : f);
}

static size_t cf_index1(const ds_cuckoo_filter_t *f, unsigned long hash) {
  unsigned long folded = cf_mix(hash) ^ ((hash >> 16 >> 16) * 0x9E3779B9UL);
  return (size_t)folded & (f->buckets - 1);
}

static size_t cf_alt(const ds_cuckoo_filter_t *f, size_t index,
                     unsigned short fp) {
  return (index ^ (size_t)cf_mix(fp)) & (f->buckets - 1);
}

static unsigned short *cf_bucket(const ds_cuckoo_filter_t *f, size_t index) {
  return f->table + index * DS_CUCKOO_SLOTS;
}

static bool cf_put(ds_cuckoo_filter_t *f, size_t index, unsigned short fp) {
  unsigned short *b = cf_bucket(f, index);
  unsigned int i;
  for (i = 0; i < DS_CUCKOO_SLOTS; i++) {
    if (b[i] == 0) {
      b[i] = fp;
      return true;
    }
  }
  return false;
}

static bool cf_has(const ds_cuckoo_filter_t *f, size_t index,
                   unsigned short fp) {
  const unsigned short *b = cf_bucket(f, index);
  unsigned int i;
  for (i = 0; i < DS_CUCKOO_SLOTS; i++) {
    if (b[i] == fp) {
      return true;
    }
  }
  return false;
}

static bool cf_del(ds_cuckoo_filter_t *f, size_t index, unsigned short fp) {
  unsigned short *b = cf_bucket(f, index);
  unsigned int i;
  for (i = 0; i < DS_CUCKOO_SLOTS; i++) {
    if (b[i] == fp) {
      b[i] = 0;
      return true;
    }
  }
  return false;
}

static unsigned long cf_rand(ds_cuckoo_filter_t *f) {
  f->rng = (f->rng * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
  return f->rng >> 8;
}

/* Insert with kicks; on failure the last displaced fingerprint becomes the
 * victim. */
static ds_status_t cf_insert_fp(ds_cuckoo_filter_t *f, size_t i1,
                                unsigned short fp) {
  size_t i2 = cf_alt(f, i1, fp);
  size_t index;
  unsigned short tmp;
  unsigned int slot;
  unsigned int kick;
  if (cf_put(f, i1, fp) || cf_put(f, i2, fp)) {
    return DS_OK;
  }
  index = (cf_rand(f) & 1UL) != 0 ? i1 : i2;
  for (kick = 0; kick < CF_MAX_KICKS; kick++) {
    slot = (unsigned int)(cf_rand(f) % DS_CUCKOO_SLOTS);
    tmp = cf_bucket(f, index)[slot];
    cf_bucket(f, index)[slot] = fp;
    fp = tmp;
    index = cf_alt(f, index, fp);
    if (cf_put(f, index, fp)) {
      return DS_OK;
    }
  }
  f->victim = fp;
  f->victim_bucket = index;
  return DS_OK; /* stored, but as the victim: the filter is now full */
}

size_t ds_cuckoo_filter_buckets_for(size_t items) {
  size_t needed = items / DS_CUCKOO_SLOTS + 1;
  size_t buckets = 1;
  needed += needed / 16; /* aim for ~94% load */
  while (buckets < needed) {
    if (buckets > CF_SIZE_MAX / 2) {
      return 0;
    }
    buckets <<= 1;
  }
  return buckets;
}

static ds_status_t cf_setup(ds_cuckoo_filter_t *f, size_t buckets) {
  if (f == NULL) {
    return DS_ERR_NULL;
  }
  if (buckets == 0 || (buckets & (buckets - 1)) != 0) {
    return DS_ERR_INVALID;
  }
  if (buckets > CF_SIZE_MAX / (DS_CUCKOO_SLOTS * sizeof(unsigned short))) {
    return DS_ERR_OVERFLOW;
  }
  f->buckets = buckets;
  f->count = 0;
  f->rng = 0x2545F491UL;
  f->victim = 0;
  f->victim_bucket = 0;
  return DS_OK;
}

ds_status_t ds_cuckoo_filter_init(ds_cuckoo_filter_t *filter, size_t buckets,
                                  ds_context_t *context) {
  ds_status_t status = cf_setup(filter, buckets);
  if (status != DS_OK) {
    return status;
  }
  filter->context = context != NULL ? context : ds_default_context();
  filter->fixed = false;
  filter->table = (unsigned short *)ds_context_calloc(
      filter->context, buckets * DS_CUCKOO_SLOTS, sizeof(unsigned short));
  return filter->table == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_status_t ds_cuckoo_filter_init_buffer(ds_cuckoo_filter_t *filter,
                                         size_t buckets,
                                         unsigned short *table) {
  ds_status_t status;
  if (table == NULL) {
    return DS_ERR_NULL;
  }
  status = cf_setup(filter, buckets);
  if (status != DS_OK) {
    return status;
  }
  filter->table = table;
  filter->context = NULL;
  filter->fixed = true;
  ds_cuckoo_filter_clear(filter);
  return DS_OK;
}

void ds_cuckoo_filter_destroy(ds_cuckoo_filter_t *filter) {
  if (filter == NULL) {
    return;
  }
  if (!filter->fixed) {
    ds_context_free(filter->context, filter->table);
  }
  filter->table = NULL;
  filter->count = 0;
}

ds_status_t ds_cuckoo_filter_insert(ds_cuckoo_filter_t *filter,
                                    unsigned long hash) {
  if (filter->victim != 0) {
    return DS_ERR_OVERFLOW;
  }
  cf_insert_fp(filter, cf_index1(filter, hash), cf_fingerprint(hash));
  filter->count++;
  return DS_OK;
}

bool ds_cuckoo_filter_contains(const ds_cuckoo_filter_t *filter,
                               unsigned long hash) {
  unsigned short fp = cf_fingerprint(hash);
  size_t i1 = cf_index1(filter, hash);
  size_t i2 = cf_alt(filter, i1, fp);
  if (cf_has(filter, i1, fp) || cf_has(filter, i2, fp)) {
    return true;
  }
  return filter->victim == fp &&
         (filter->victim_bucket == i1 || filter->victim_bucket == i2);
}

ds_status_t ds_cuckoo_filter_remove(ds_cuckoo_filter_t *filter,
                                    unsigned long hash) {
  unsigned short fp = cf_fingerprint(hash);
  size_t i1 = cf_index1(filter, hash);
  size_t i2 = cf_alt(filter, i1, fp);
  unsigned short victim;
  if (cf_del(filter, i1, fp) || cf_del(filter, i2, fp)) {
    filter->count--;
  } else if (filter->victim == fp &&
             (filter->victim_bucket == i1 || filter->victim_bucket == i2)) {
    filter->victim = 0;
    filter->count--;
    return DS_OK;
  } else {
    return DS_NOT_FOUND;
  }
  if (filter->victim != 0) {
    /* Space was freed: try to place the victim again. */
    victim = filter->victim;
    filter->victim = 0;
    cf_insert_fp(filter, filter->victim_bucket, victim);
  }
  return DS_OK;
}

size_t ds_cuckoo_filter_count(const ds_cuckoo_filter_t *filter) {
  return filter->count;
}

void ds_cuckoo_filter_clear(ds_cuckoo_filter_t *filter) {
  memset(filter->table, 0,
         filter->buckets * DS_CUCKOO_SLOTS * sizeof(unsigned short));
  filter->count = 0;
  filter->victim = 0;
}
