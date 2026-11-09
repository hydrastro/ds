#include "pager.h"
#include "checked.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* stdio failures: DS_ERR_STATE, as in algo/external_sort */
#define PG_ERR_IO DS_ERR_STATE

/* ---- memory store ---- */

typedef struct pg_mem {
  unsigned char **pages; /* NULL: never written (reads as zeros) */
  unsigned long count;
  size_t capacity;
  size_t page_size;
  ds_context_t *context;
} pg_mem_t;

static ds_status_t mem_read(void *impl, unsigned long page, void *buf) {
  pg_mem_t *m = (pg_mem_t *)impl;
  if (page >= m->count || m->pages[page] == NULL) {
    memset(buf, 0, m->page_size);
  } else {
    memcpy(buf, m->pages[page], m->page_size);
  }
  return DS_OK;
}

static ds_status_t mem_write(void *impl, unsigned long page, const void *buf) {
  pg_mem_t *m = (pg_mem_t *)impl;
  if ((size_t)page >= m->capacity) {
    size_t cap = m->capacity < 8 ? 8 : m->capacity, bytes, i;
    unsigned char **p;
    while (cap <= (size_t)page) {
      if (cap > (size_t)-1 / 2) {
        return DS_ERR_OVERFLOW;
      }
      cap *= 2;
    }
    if (!ds_size_mul(cap, sizeof(*p), &bytes)) {
      return DS_ERR_OVERFLOW;
    }
    p = (unsigned char **)ds_context_realloc(m->context, m->pages, bytes);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = m->capacity; i < cap; i++) {
      p[i] = NULL;
    }
    m->pages = p;
    m->capacity = cap;
  }
  if (m->pages[page] == NULL) {
    m->pages[page] =
        (unsigned char *)ds_context_alloc(m->context, m->page_size);
    if (m->pages[page] == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  memcpy(m->pages[page], buf, m->page_size);
  if (page >= m->count) {
    m->count = page + 1;
  }
  return DS_OK;
}

static ds_status_t mem_sync(void *impl) {
  (void)impl;
  return DS_OK;
}

static unsigned long mem_count(const void *impl) {
  return ((const pg_mem_t *)impl)->count;
}

static ds_status_t mem_truncate(void *impl, unsigned long pages) {
  pg_mem_t *m = (pg_mem_t *)impl;
  unsigned long i;
  for (i = pages; i < m->count; i++) {
    ds_context_free(m->context, m->pages[i]);
    m->pages[i] = NULL;
  }
  if (pages < m->count) {
    m->count = pages;
  }
  return DS_OK;
}

static void mem_close(void *impl) {
  pg_mem_t *m = (pg_mem_t *)impl;
  unsigned long i;
  for (i = 0; i < m->count; i++) {
    ds_context_free(m->context, m->pages[i]);
  }
  ds_context_free(m->context, m->pages);
  ds_context_free(m->context, m);
}

static const ds_page_store_ops_t pg_mem_ops = {
    "memory",     DS_PAGE_STORE_TRUNCATE_PERSISTS,
    mem_read,     mem_write,
    mem_sync,     mem_count,
    mem_truncate, mem_close};

ds_status_t ds_page_store_memory(ds_page_store_t *store, size_t page_size,
                                 ds_context_t *context) {
  pg_mem_t *m;
  if (store == NULL) {
    return DS_ERR_NULL;
  }
  if (page_size == 0) {
    return DS_ERR_INVALID;
  }
  m = (pg_mem_t *)ds_context_calloc(context, 1, sizeof(pg_mem_t));
  if (m == NULL) {
    return DS_ERR_ALLOC;
  }
  m->page_size = page_size;
  m->context = context;
  store->ops = &pg_mem_ops;
  store->impl = m;
  store->page_size = page_size;
  return DS_OK;
}

/* ---- stdio file store ---- */

typedef struct pg_file {
  FILE *f;
  unsigned long count;
  size_t page_size;
  unsigned char *zero;
  ds_context_t *context;
} pg_file_t;

static ds_status_t file_seek(pg_file_t *p, unsigned long page) {
  if (page > (unsigned long)LONG_MAX / (unsigned long)p->page_size) {
    return DS_ERR_RANGE;
  }
  return fseek(p->f, (long)(page * (unsigned long)p->page_size), SEEK_SET) == 0
             ? DS_OK
             : PG_ERR_IO;
}

static ds_status_t file_read(void *impl, unsigned long page, void *buf) {
  pg_file_t *p = (pg_file_t *)impl;
  size_t got;
  ds_status_t st;
  if (page >= p->count) {
    memset(buf, 0, p->page_size);
    return DS_OK;
  }
  st = file_seek(p, page);
  if (st != DS_OK) {
    return st;
  }
  got = fread(buf, 1, p->page_size, p->f);
  if (got < p->page_size) {
    if (ferror(p->f)) {
      clearerr(p->f);
      return PG_ERR_IO;
    }
    clearerr(p->f);
    memset((unsigned char *)buf + got, 0, p->page_size - got);
  }
  return DS_OK;
}

static ds_status_t file_write(void *impl, unsigned long page, const void *buf) {
  pg_file_t *p = (pg_file_t *)impl;
  ds_status_t st;
  /* fill a gap with zero pages instead of seeking past the end */
  while (p->count < page) {
    st = file_seek(p, p->count);
    if (st != DS_OK) {
      return st;
    }
    if (fwrite(p->zero, 1, p->page_size, p->f) != p->page_size) {
      return PG_ERR_IO;
    }
    p->count++;
  }
  st = file_seek(p, page);
  if (st != DS_OK) {
    return st;
  }
  if (fwrite(buf, 1, p->page_size, p->f) != p->page_size) {
    return PG_ERR_IO;
  }
  if (page >= p->count) {
    p->count = page + 1;
  }
  return DS_OK;
}

static ds_status_t file_sync(void *impl) {
  return fflush(((pg_file_t *)impl)->f) == 0 ? DS_OK : PG_ERR_IO;
}

static unsigned long file_count(const void *impl) {
  return ((const pg_file_t *)impl)->count;
}

/* C89 cannot shrink a file: truncation is logical, and pages past the
 * new end read as zeros until rewritten */
static ds_status_t file_truncate(void *impl, unsigned long pages) {
  pg_file_t *p = (pg_file_t *)impl;
  if (pages < p->count) {
    p->count = pages;
  }
  return DS_OK;
}

static void file_close(void *impl) {
  pg_file_t *p = (pg_file_t *)impl;
  (void)fclose(p->f);
  ds_context_free(p->context, p->zero);
  ds_context_free(p->context, p);
}

static const ds_page_store_ops_t pg_file_ops = {
    "stdio file", 0U,         file_read,     file_write,
    file_sync,    file_count, file_truncate, file_close};

ds_status_t ds_page_store_file(ds_page_store_t *store, const char *path,
                               size_t page_size, bool truncate,
                               ds_context_t *context) {
  pg_file_t *p;
  long size;
  if (store == NULL || path == NULL) {
    return DS_ERR_NULL;
  }
  if (page_size == 0 || page_size > (size_t)LONG_MAX) {
    return DS_ERR_INVALID;
  }
  p = (pg_file_t *)ds_context_calloc(context, 1, sizeof(pg_file_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  p->zero = (unsigned char *)ds_context_calloc(context, 1, page_size);
  if (p->zero == NULL) {
    ds_context_free(context, p);
    return DS_ERR_ALLOC;
  }
  p->f = truncate ? NULL : fopen(path, "r+b");
  if (p->f == NULL) {
    p->f = fopen(path, "w+b");
  }
  if (p->f == NULL || fseek(p->f, 0L, SEEK_END) != 0 ||
      (size = ftell(p->f)) < 0) {
    if (p->f != NULL) {
      (void)fclose(p->f);
    }
    ds_context_free(context, p->zero);
    ds_context_free(context, p);
    return PG_ERR_IO;
  }
  p->page_size = page_size;
  p->count = ((unsigned long)size + (unsigned long)page_size - 1) /
             (unsigned long)page_size;
  p->context = context;
  store->ops = &pg_file_ops;
  store->impl = p;
  store->page_size = page_size;
  return DS_OK;
}

void ds_page_store_close(ds_page_store_t *store) {
  if (store == NULL || store->ops == NULL) {
    return;
  }
  store->ops->close(store->impl);
  store->ops = NULL;
  store->impl = NULL;
}

ds_status_t ds_page_store_read(ds_page_store_t *store, unsigned long page,
                               void *buf) {
  if (store == NULL || store->ops == NULL || buf == NULL) {
    return DS_ERR_NULL;
  }
  return store->ops->read(store->impl, page, buf);
}

ds_status_t ds_page_store_write(ds_page_store_t *store, unsigned long page,
                                const void *buf) {
  if (store == NULL || store->ops == NULL || buf == NULL) {
    return DS_ERR_NULL;
  }
  return store->ops->write(store->impl, page, buf);
}

ds_status_t ds_page_store_sync(ds_page_store_t *store) {
  if (store == NULL || store->ops == NULL) {
    return DS_ERR_NULL;
  }
  return store->ops->sync(store->impl);
}

unsigned long ds_page_store_count(const ds_page_store_t *store) {
  if (store == NULL || store->ops == NULL) {
    return 0;
  }
  return store->ops->count(store->impl);
}

ds_status_t ds_page_store_truncate(ds_page_store_t *store,
                                   unsigned long pages) {
  if (store == NULL || store->ops == NULL) {
    return DS_ERR_NULL;
  }
  return store->ops->truncate(store->impl, pages);
}

/* ---- pager ---- */

static unsigned long pg_hash(const void *key, void *user) {
  unsigned long x = *(const unsigned long *)key;
  (void)user;
  x ^= x >> 16;
  x *= 0x45D9F3BUL;
  x ^= x >> 16;
  return x;
}

static bool pg_equal(const void *a, const void *b, void *user) {
  (void)user;
  return *(const unsigned long *)a == *(const unsigned long *)b;
}

static unsigned char *pg_buf(const ds_pager_t *p, size_t f) {
  return p->data + f * p->store->page_size;
}

static void pg_free_all(ds_pager_t *p) {
  ds_context_free(p->context, p->data);
  ds_context_free(p->context, p->frame);
  ds_context_free(p->context, p->free_frames);
}

ds_status_t ds_pager_init_policy(ds_pager_t *pager, ds_page_store_t *store,
                                 size_t frames, const ds_map_ops_t *map_ops,
                                 const ds_replacement_ops_t *policy,
                                 ds_context_t *context) {
  size_t bytes, i;
  ds_status_t st;
  if (pager == NULL || store == NULL || store->ops == NULL) {
    return DS_ERR_NULL;
  }
  memset(pager, 0, sizeof(*pager));
  if (frames == 0) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_mul(frames, store->page_size, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  pager->context = context;
  pager->data = (unsigned char *)ds_context_alloc(context, bytes);
  pager->frame = (ds_pager_frame_t *)ds_context_calloc(
      context, frames, sizeof(ds_pager_frame_t));
  pager->free_frames =
      (size_t *)ds_context_calloc(context, frames, sizeof(size_t));
  if (pager->data == NULL || pager->frame == NULL ||
      pager->free_frames == NULL) {
    pg_free_all(pager);
    return DS_ERR_ALLOC;
  }
  st = ds_replacement_init(&pager->policy, policy, frames, context);
  if (st != DS_OK) {
    pg_free_all(pager);
    return st;
  }
  st = ds_map_init(&pager->table, map_ops, sizeof(unsigned long),
                   sizeof(size_t), pg_hash, pg_equal, NULL, context);
  if (st != DS_OK) {
    ds_replacement_destroy(&pager->policy);
    pg_free_all(pager);
    return st;
  }
  pager->store = store;
  pager->frames = frames;
  for (i = 0; i < frames; i++) {
    pager->free_frames[pager->nfree++] = frames - 1 - i;
  }
  pager->pages = ds_page_store_count(store);
  return DS_OK;
}

ds_status_t ds_pager_init(ds_pager_t *pager, ds_page_store_t *store,
                          size_t frames, const ds_map_ops_t *map_ops,
                          ds_context_t *context) {
  return ds_pager_init_policy(pager, store, frames, map_ops, NULL, context);
}

static ds_status_t pg_write_back(ds_pager_t *p, size_t f) {
  ds_pager_frame_t *fr = &p->frame[f];
  ds_status_t st;
  if (!fr->used || !fr->dirty) {
    return DS_OK;
  }
  st = ds_page_store_write(p->store, fr->page, pg_buf(p, f));
  if (st == DS_OK) {
    fr->dirty = 0;
    p->writes++;
  }
  return st;
}

/* a free frame, or the policy's victim (written back and evicted), now
 * mapped to page and pinned; on failure nothing changes beyond a possible
 * write-back */
static ds_status_t pg_claim(ds_pager_t *p, unsigned long page, size_t *out) {
  size_t f;
  ds_pager_frame_t *fr;
  ds_status_t st;
  bool from_free = p->nfree > 0;
  if (from_free) {
    f = p->free_frames[p->nfree - 1];
  } else {
    f = ds_replacement_victim(&p->policy);
    if (f == DS_REPLACEMENT_NONE) {
      return DS_ERR_STATE;
    }
  }
  fr = &p->frame[f];
  if (fr->used) {
    st = pg_write_back(p, f);
    if (st != DS_OK) {
      return st;
    }
  }
  st = ds_map_put(&p->table, &page, &f);
  if (DS_FAILED(st)) {
    return st;
  }
  if (from_free) {
    p->nfree--;
  } else {
    (void)ds_map_remove(&p->table, &fr->page, NULL);
    ds_replacement_evict(&p->policy, f);
  }
  fr->used = 1;
  fr->page = page;
  fr->dirty = 0;
  fr->pins = 1;
  ds_replacement_admit(&p->policy, f);
  *out = f;
  return DS_OK;
}

/* undo a claim whose page could not be loaded */
static void pg_release_frame(ds_pager_t *p, size_t f) {
  ds_pager_frame_t *fr = &p->frame[f];
  (void)ds_map_remove(&p->table, &fr->page, NULL);
  ds_replacement_evict(&p->policy, f);
  fr->used = 0;
  fr->pins = 0;
  fr->dirty = 0;
  p->free_frames[p->nfree++] = f;
}

ds_status_t ds_pager_get(ds_pager_t *pager, unsigned long page, void **data) {
  void *hit;
  size_t f;
  ds_status_t st;
  if (pager == NULL || data == NULL) {
    return DS_ERR_NULL;
  }
  if (page >= pager->pages) {
    return DS_ERR_RANGE;
  }
  hit = ds_map_get(&pager->table, &page);
  if (hit != NULL) {
    memcpy(&f, hit, sizeof(f));
    if (pager->frame[f].pins++ == 0) {
      ds_replacement_set_evictable(&pager->policy, f, false);
    }
    ds_replacement_touch(&pager->policy, f);
    pager->hits++;
    *data = pg_buf(pager, f);
    return DS_OK;
  }
  st = pg_claim(pager, page, &f);
  if (st != DS_OK) {
    return st;
  }
  st = ds_page_store_read(pager->store, page, pg_buf(pager, f));
  if (st != DS_OK) {
    pg_release_frame(pager, f);
    return st;
  }
  pager->misses++;
  *data = pg_buf(pager, f);
  return DS_OK;
}

ds_status_t ds_pager_new(ds_pager_t *pager, unsigned long *page, void **data) {
  size_t f;
  ds_status_t st;
  if (pager == NULL || page == NULL || data == NULL) {
    return DS_ERR_NULL;
  }
  st = pg_claim(pager, pager->pages, &f);
  if (st != DS_OK) {
    return st;
  }
  memset(pg_buf(pager, f), 0, pager->store->page_size);
  pager->frame[f].dirty = 1;
  *page = pager->pages++;
  *data = pg_buf(pager, f);
  return DS_OK;
}

static ds_pager_frame_t *pg_frame_of(ds_pager_t *p, unsigned long page,
                                     size_t *index) {
  void *hit = ds_map_get(&p->table, &page);
  size_t f;
  if (hit == NULL) {
    return NULL;
  }
  memcpy(&f, hit, sizeof(f));
  if (index != NULL) {
    *index = f;
  }
  return &p->frame[f];
}

ds_status_t ds_pager_mark_dirty(ds_pager_t *pager, unsigned long page) {
  ds_pager_frame_t *fr;
  if (pager == NULL) {
    return DS_ERR_NULL;
  }
  fr = pg_frame_of(pager, page, NULL);
  if (fr == NULL || fr->pins == 0) {
    return DS_ERR_STATE;
  }
  fr->dirty = 1;
  return DS_OK;
}

ds_status_t ds_pager_unpin(ds_pager_t *pager, unsigned long page) {
  ds_pager_frame_t *fr;
  size_t f;
  if (pager == NULL) {
    return DS_ERR_NULL;
  }
  fr = pg_frame_of(pager, page, &f);
  if (fr == NULL || fr->pins == 0) {
    return DS_ERR_STATE;
  }
  if (--fr->pins == 0) {
    ds_replacement_set_evictable(&pager->policy, f, true);
  }
  return DS_OK;
}

ds_status_t ds_pager_flush(ds_pager_t *pager) {
  size_t f;
  ds_status_t st;
  if (pager == NULL) {
    return DS_ERR_NULL;
  }
  for (f = 0; f < pager->frames; f++) {
    st = pg_write_back(pager, f);
    if (st != DS_OK) {
      return st;
    }
  }
  return ds_page_store_sync(pager->store);
}

void ds_pager_discard(ds_pager_t *pager) {
  if (pager == NULL || pager->store == NULL) {
    return;
  }
  ds_map_destroy(&pager->table);
  ds_replacement_destroy(&pager->policy);
  pg_free_all(pager);
  memset(pager, 0, sizeof(*pager));
}

ds_status_t ds_pager_close(ds_pager_t *pager) {
  ds_status_t st;
  if (pager == NULL || pager->store == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_pager_flush(pager);
  if (st == DS_OK) {
    ds_pager_discard(pager);
  }
  return st;
}

ds_status_t ds_pager_destroy(ds_pager_t *pager) {
  ds_status_t st;
  if (pager == NULL || pager->store == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_pager_flush(pager);
  ds_pager_discard(pager);
  return st;
}

unsigned long ds_pager_page_count(const ds_pager_t *pager) {
  return pager == NULL ? 0 : pager->pages;
}
