#include <dsa/core/checked.h>
#include <dsa/structures/fault_store.h>
#include <string.h>

/* a pending operation: a write keeps its bytes at data * page_size in the
 * data buffer; a truncation has data == FS_TRUNCATE and page the count */
#define FS_TRUNCATE ((size_t) - 1)

typedef struct fs_op {
  unsigned long page;
  size_t data;
} fs_op_t;

typedef struct fs_store {
  ds_page_store_t *base;
  size_t page_size;
  fs_op_t *ops;
  size_t nops;
  size_t applied; /* operations already applied by a failed sync */
  size_t cap;
  unsigned char *data;    /* the bytes of pending writes */
  unsigned char *scratch; /* one page, for torn writes */
  size_t nwrites;
  size_t data_cap; /* pages */
  unsigned long count;
  ds_fault_fn fault;
  void *user;
  ds_fault_stats_t stats;
  ds_context_t *context;
} fs_store_t;

static ds_status_t fs_check(fs_store_t *f, ds_fault_op_t op,
                            unsigned long page) {
  ds_status_t st;
  f->stats.ops[op]++;
  if (f->fault == NULL) {
    return DS_OK;
  }
  st = f->fault(op, page, f->stats.ops[op], f->user);
  if (st != DS_OK) {
    f->stats.failures++;
  }
  return st;
}

static void fs_clear(fs_store_t *f) {
  f->nops = 0;
  f->applied = 0;
  f->nwrites = 0;
  f->count = ds_page_store_count(f->base);
}

/* room for one more operation (and its page, for a write) */
static ds_status_t fs_reserve(fs_store_t *f, bool write) {
  size_t bytes;
  if (f->nops == f->cap) {
    size_t cap = f->cap < 16 ? 16 : f->cap;
    fs_op_t *ops;
    if (f->cap >= 16) {
      if (cap > (size_t)-1 / 2) {
        return DS_ERR_OVERFLOW;
      }
      cap *= 2;
    }
    if (!ds_size_mul(cap, sizeof(fs_op_t), &bytes)) {
      return DS_ERR_OVERFLOW;
    }
    ops = (fs_op_t *)ds_context_realloc(f->context, f->ops, bytes);
    if (ops == NULL) {
      return DS_ERR_ALLOC;
    }
    f->ops = ops;
    f->cap = cap;
  }
  if (write && f->nwrites == f->data_cap) {
    size_t cap = f->data_cap < 8 ? 8 : f->data_cap;
    unsigned char *data;
    if (f->data_cap >= 8) {
      if (cap > (size_t)-1 / 2) {
        return DS_ERR_OVERFLOW;
      }
      cap *= 2;
    }
    if (!ds_size_mul(cap, f->page_size, &bytes)) {
      return DS_ERR_OVERFLOW;
    }
    data = (unsigned char *)ds_context_realloc(f->context, f->data, bytes);
    if (data == NULL) {
      return DS_ERR_ALLOC;
    }
    f->data = data;
    f->data_cap = cap;
  }
  return DS_OK;
}

static ds_status_t fs_read(void *impl, unsigned long page, void *buf) {
  fs_store_t *f = (fs_store_t *)impl;
  size_t i;
  ds_status_t st = fs_check(f, DS_FAULT_READ, page);
  if (st != DS_OK) {
    return st;
  }
  if (page >= f->count) {
    memset(buf, 0, f->page_size);
    return DS_OK;
  }
  for (i = f->nops; i-- > f->applied;) {
    const fs_op_t *op = &f->ops[i];
    if (op->data == FS_TRUNCATE) {
      if (page >= op->page) {
        memset(buf, 0, f->page_size); /* cut off and not written since */
        return DS_OK;
      }
    } else if (op->page == page) {
      memcpy(buf, f->data + op->data * f->page_size, f->page_size);
      return DS_OK;
    }
  }
  return ds_page_store_read(f->base, page, buf);
}

static ds_status_t fs_write(void *impl, unsigned long page, const void *buf) {
  fs_store_t *f = (fs_store_t *)impl;
  ds_status_t st = fs_check(f, DS_FAULT_WRITE, page);
  if (st == DS_OK) {
    st = fs_reserve(f, true);
  }
  if (st != DS_OK) {
    return st;
  }
  memcpy(f->data + f->nwrites * f->page_size, buf, f->page_size);
  f->ops[f->nops].page = page;
  f->ops[f->nops].data = f->nwrites++;
  f->nops++;
  if (page >= f->count) {
    f->count = page + 1;
  }
  return DS_OK;
}

static ds_status_t fs_truncate(void *impl, unsigned long pages) {
  fs_store_t *f = (fs_store_t *)impl;
  ds_status_t st = fs_check(f, DS_FAULT_TRUNCATE, pages);
  if (st != DS_OK || pages >= f->count) {
    return st;
  }
  st = fs_reserve(f, false);
  if (st != DS_OK) {
    return st;
  }
  f->ops[f->nops].page = pages;
  f->ops[f->nops].data = FS_TRUNCATE;
  f->nops++;
  f->count = pages;
  return DS_OK;
}

static ds_status_t fs_apply(fs_store_t *f, const fs_op_t *op) {
  if (op->data == FS_TRUNCATE) {
    return ds_page_store_truncate(f->base, op->page);
  }
  return ds_page_store_write(f->base, op->page,
                             f->data + op->data * f->page_size);
}

static ds_status_t fs_sync(void *impl) {
  fs_store_t *f = (fs_store_t *)impl;
  ds_status_t st = fs_check(f, DS_FAULT_SYNC, 0);
  if (st != DS_OK) {
    return st;
  }
  while (f->applied < f->nops) {
    st = fs_apply(f, &f->ops[f->applied]);
    if (st != DS_OK) {
      return st; /* the rest stays pending for the next sync */
    }
    f->applied++;
  }
  fs_clear(f);
  return ds_page_store_sync(f->base);
}

static unsigned long fs_count(const void *impl) {
  return ((const fs_store_t *)impl)->count;
}

static void fs_close(void *impl) {
  fs_store_t *f = (fs_store_t *)impl;
  ds_context_free(f->context, f->ops);
  ds_context_free(f->context, f->data);
  ds_context_free(f->context, f->scratch);
  ds_context_free(f->context, f);
}

/* the generator of the crash modes: xorshift32 */
static unsigned long fs_rand(unsigned long *s) {
  unsigned long x = *s;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  *s = x;
  return x;
}

static const ds_page_store_ops_t fs_ops_durable = {
    "fault",     DS_PAGE_STORE_DURABLE_SYNC,
    fs_read,     fs_write,
    fs_sync,     fs_count,
    fs_truncate, fs_close};
static const ds_page_store_ops_t fs_ops_truncate = {
    "fault",     DS_PAGE_STORE_TRUNCATE_PERSISTS,
    fs_read,     fs_write,
    fs_sync,     fs_count,
    fs_truncate, fs_close};
static const ds_page_store_ops_t fs_ops_both = {
    "fault",     DS_PAGE_STORE_DURABLE_SYNC | DS_PAGE_STORE_TRUNCATE_PERSISTS,
    fs_read,     fs_write,
    fs_sync,     fs_count,
    fs_truncate, fs_close};
static const ds_page_store_ops_t fs_ops_none = {
    "fault", 0U, fs_read, fs_write, fs_sync, fs_count, fs_truncate, fs_close};

static fs_store_t *fs_of(const ds_page_store_t *store) {
  if (store == NULL ||
      (store->ops != &fs_ops_durable && store->ops != &fs_ops_truncate &&
       store->ops != &fs_ops_both && store->ops != &fs_ops_none)) {
    return NULL;
  }
  return (fs_store_t *)store->impl;
}

ds_status_t ds_page_store_fault(ds_page_store_t *store, ds_page_store_t *base,
                                ds_fault_fn fault, void *user,
                                ds_context_t *context) {
  fs_store_t *f;
  unsigned traits;
  if (store == NULL || base == NULL || base->ops == NULL) {
    return DS_ERR_NULL;
  }
  if (base->page_size == 0) {
    return DS_ERR_INVALID;
  }
  f = (fs_store_t *)ds_context_calloc(context, 1, sizeof(fs_store_t));
  if (f == NULL) {
    return DS_ERR_ALLOC;
  }
  f->scratch = (unsigned char *)ds_context_alloc(context, base->page_size);
  if (f->scratch == NULL) {
    ds_context_free(context, f);
    return DS_ERR_ALLOC;
  }
  f->base = base;
  f->page_size = base->page_size;
  f->fault = fault;
  f->user = user;
  f->context = context != NULL ? context : ds_default_context();
  fs_clear(f);
  traits = base->ops->traits &
           (DS_PAGE_STORE_DURABLE_SYNC | DS_PAGE_STORE_TRUNCATE_PERSISTS);
  store->ops =
      traits == (DS_PAGE_STORE_DURABLE_SYNC | DS_PAGE_STORE_TRUNCATE_PERSISTS)
          ? &fs_ops_both
      : traits == DS_PAGE_STORE_DURABLE_SYNC      ? &fs_ops_durable
      : traits == DS_PAGE_STORE_TRUNCATE_PERSISTS ? &fs_ops_truncate
                                                  : &fs_ops_none;
  store->impl = f;
  store->page_size = base->page_size;
  return DS_OK;
}

ds_status_t ds_fault_store_set(ds_page_store_t *store, ds_fault_fn fault,
                               void *user) {
  fs_store_t *f = fs_of(store);
  if (f == NULL) {
    return store == NULL ? DS_ERR_NULL : DS_ERR_INVALID;
  }
  f->fault = fault;
  f->user = user;
  return DS_OK;
}

ds_status_t ds_fault_store_crash(ds_page_store_t *store, ds_fault_crash_t mode,
                                 unsigned long seed) {
  fs_store_t *f = fs_of(store);
  unsigned long s;
  size_t i;
  size_t writes = 0;
  size_t torn = (size_t)-1;
  size_t keep = 0;
  ds_status_t first = DS_OK;
  if (f == NULL) {
    return store == NULL ? DS_ERR_NULL : DS_ERR_INVALID;
  }
  if ((int)mode < (int)DS_FAULT_CRASH_LOSE ||
      (int)mode > (int)DS_FAULT_CRASH_TORN) {
    return DS_ERR_INVALID;
  }
  /* the subset is drawn twice from the same seed: once to pick the torn
   * write among the writes that land, once to apply */
  s = (seed & 0xFFFFFFFFUL) == 0 ? 0x9E3779B9UL : (seed & 0xFFFFFFFFUL);
  if (mode == DS_FAULT_CRASH_TORN) {
    unsigned long t = s;
    for (i = f->applied; i < f->nops; i++) {
      if ((fs_rand(&t) & 1UL) != 0 && f->ops[i].data != FS_TRUNCATE) {
        writes++;
      }
    }
    if (writes > 0) {
      torn = (size_t)(fs_rand(&t) % (unsigned long)writes);
      keep = (size_t)(fs_rand(&t) % (unsigned long)f->page_size);
    }
  }
  writes = 0;
  for (i = f->applied; i < f->nops && mode != DS_FAULT_CRASH_LOSE; i++) {
    const fs_op_t *op = &f->ops[i];
    ds_status_t st = DS_OK;
    bool lands = mode == DS_FAULT_CRASH_ALL || (fs_rand(&s) & 1UL) != 0;
    if (!lands) {
      continue;
    }
    if (op->data != FS_TRUNCATE && mode == DS_FAULT_CRASH_TORN &&
        writes++ == torn) {
      /* the new prefix over the old contents */
      st = ds_page_store_read(f->base, op->page, f->scratch);
      if (st == DS_OK) {
        memcpy(f->scratch, f->data + op->data * f->page_size, keep);
        st = ds_page_store_write(f->base, op->page, f->scratch);
      }
    } else {
      st = fs_apply(f, op);
    }
    if (st != DS_OK && first == DS_OK) {
      first = st;
    }
  }
  f->stats.crashes++;
  fs_clear(f);
  return first;
}

ds_status_t ds_fault_store_stats(const ds_page_store_t *store,
                                 ds_fault_stats_t *out) {
  const fs_store_t *f = fs_of(store);
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (f == NULL) {
    return store == NULL ? DS_ERR_NULL : DS_ERR_INVALID;
  }
  *out = f->stats;
  out->pending = f->nops - f->applied;
  return DS_OK;
}
