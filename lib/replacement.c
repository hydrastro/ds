#include "replacement.h"
#include <string.h>

#define RP_NONE DS_REPLACEMENT_NONE

/* shared per-frame state and an intrusive doubly linked list */
typedef struct rp_frame {
  size_t prev;
  size_t next;
  unsigned char resident;
  unsigned char evictable;
  unsigned char bit; /* reference / visited bit */
  unsigned char linked;
} rp_frame_t;

typedef struct rp_base {
  rp_frame_t *f;
  size_t n;
  size_t head; /* list head */
  size_t tail;
  size_t hand;
  ds_context_t *context;
} rp_base_t;

static ds_status_t rp_init(void *p, size_t frames, ds_context_t *ctx) {
  rp_base_t *b = (rp_base_t *)p;
  size_t i;
  memset(b, 0, sizeof(*b));
  b->f = (rp_frame_t *)ds_context_calloc(ctx, frames == 0 ? 1 : frames,
                                         sizeof(rp_frame_t));
  if (b->f == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < frames; i++) {
    b->f[i].prev = b->f[i].next = RP_NONE;
  }
  b->n = frames;
  b->head = b->tail = b->hand = RP_NONE;
  b->context = ctx;
  return DS_OK;
}

static void rp_destroy(void *p) {
  rp_base_t *b = (rp_base_t *)p;
  ds_context_free(b->context, b->f);
  b->f = NULL;
}

static void rp_unlink(rp_base_t *b, size_t i) {
  rp_frame_t *x = &b->f[i];
  if (!x->linked) {
    return;
  }
  if (x->prev != RP_NONE) {
    b->f[x->prev].next = x->next;
  } else {
    b->head = x->next;
  }
  if (x->next != RP_NONE) {
    b->f[x->next].prev = x->prev;
  } else {
    b->tail = x->prev;
  }
  x->prev = x->next = RP_NONE;
  x->linked = 0;
}

static void rp_push_tail(rp_base_t *b, size_t i) {
  rp_frame_t *x = &b->f[i];
  x->next = RP_NONE;
  x->prev = b->tail;
  if (b->tail != RP_NONE) {
    b->f[b->tail].next = i;
  } else {
    b->head = i;
  }
  b->tail = i;
  x->linked = 1;
}

static void rp_push_head(rp_base_t *b, size_t i) {
  rp_frame_t *x = &b->f[i];
  x->prev = RP_NONE;
  x->next = b->head;
  if (b->head != RP_NONE) {
    b->f[b->head].prev = i;
  } else {
    b->tail = i;
  }
  b->head = i;
  x->linked = 1;
}

/* ---- LRU: the list holds evictable frames, least recent first ---- */

static void lru_admit(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  b->f[i].resident = 1;
  b->f[i].evictable = 0;
}

static void lru_touch(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  if (b->f[i].linked) {
    rp_unlink(b, i);
    rp_push_tail(b, i);
  }
}

static void lru_set_evictable(void *p, size_t i, bool e) {
  rp_base_t *b = (rp_base_t *)p;
  b->f[i].evictable = e ? 1U : 0U;
  rp_unlink(b, i);
  if (e && b->f[i].resident) {
    rp_push_tail(b, i);
  }
}

static size_t lru_victim(void *p) { return ((rp_base_t *)p)->head; }

static void lru_evict(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  rp_unlink(b, i);
  b->f[i].resident = 0;
  b->f[i].evictable = 0;
}

/* ---- FIFO: the list holds residents in admission order ---- */

static void fifo_admit(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  b->f[i].resident = 1;
  b->f[i].evictable = 0;
  rp_unlink(b, i);
  rp_push_tail(b, i);
}

static void fifo_touch(void *p, size_t i) {
  (void)p;
  (void)i;
}

static void fifo_set_evictable(void *p, size_t i, bool e) {
  ((rp_base_t *)p)->f[i].evictable = e ? 1U : 0U;
}

static size_t fifo_victim(void *p) {
  rp_base_t *b = (rp_base_t *)p;
  size_t i = b->head;
  while (i != RP_NONE && !b->f[i].evictable) {
    i = b->f[i].next;
  }
  return i;
}

/* ---- CLOCK ---- */

static void clock_admit(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  b->f[i].resident = 1;
  b->f[i].evictable = 0;
  b->f[i].bit = 1;
}

static void clock_touch(void *p, size_t i) { ((rp_base_t *)p)->f[i].bit = 1; }

static size_t clock_victim(void *p) {
  rp_base_t *b = (rp_base_t *)p;
  size_t steps;
  if (b->n == 0) {
    return RP_NONE;
  }
  if (b->hand == RP_NONE || b->hand >= b->n) {
    b->hand = 0;
  }
  /* two sweeps clear every reference bit at least once */
  for (steps = 0; steps < 2 * b->n + 1; steps++) {
    size_t i = b->hand;
    rp_frame_t *x = &b->f[i];
    b->hand = (b->hand + 1) % b->n;
    if (!x->resident || !x->evictable) {
      continue;
    }
    if (x->bit) {
      x->bit = 0;
    } else {
      return i;
    }
  }
  return RP_NONE;
}

static void clock_evict(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  b->f[i].resident = 0;
  b->f[i].evictable = 0;
  b->f[i].bit = 0;
}

/* ---- SIEVE: residents newest at the head; the hand walks toward the
 * head from the tail, clearing visited bits ---- */

static void sieve_admit(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  b->f[i].resident = 1;
  b->f[i].evictable = 0;
  b->f[i].bit = 0;
  rp_unlink(b, i);
  rp_push_head(b, i);
}

static size_t sieve_victim(void *p) {
  rp_base_t *b = (rp_base_t *)p;
  size_t o = b->hand != RP_NONE ? b->hand : b->tail, steps;
  for (steps = 0; o != RP_NONE && steps < 2 * b->n + 2; steps++) {
    rp_frame_t *x = &b->f[o];
    if (x->evictable && !x->bit) {
      b->hand = o;
      return o;
    }
    if (x->evictable) {
      x->bit = 0;
    }
    o = x->prev != RP_NONE ? x->prev : b->tail;
  }
  return RP_NONE;
}

static void sieve_evict(void *p, size_t i) {
  rp_base_t *b = (rp_base_t *)p;
  if (b->hand == i) {
    b->hand = b->f[i].prev;
  }
  rp_unlink(b, i);
  b->f[i].resident = 0;
  b->f[i].evictable = 0;
  b->f[i].bit = 0;
}

static void lru_destroy(void *p) { rp_destroy(p); }

static const ds_replacement_ops_t rp_lru = {
    "lru",     sizeof(rp_base_t), rp_init,    lru_destroy, lru_admit,
    lru_touch, lru_set_evictable, lru_victim, lru_evict};
static const ds_replacement_ops_t rp_fifo = {
    "fifo",     sizeof(rp_base_t),  rp_init,     rp_destroy, fifo_admit,
    fifo_touch, fifo_set_evictable, fifo_victim, lru_evict};
static const ds_replacement_ops_t rp_clock = {
    "clock",     sizeof(rp_base_t),  rp_init,      rp_destroy, clock_admit,
    clock_touch, fifo_set_evictable, clock_victim, clock_evict};
static const ds_replacement_ops_t rp_sieve = {
    "sieve",     sizeof(rp_base_t),  rp_init,      rp_destroy, sieve_admit,
    clock_touch, fifo_set_evictable, sieve_victim, sieve_evict};

const ds_replacement_ops_t *ds_replacement_lru(void) { return &rp_lru; }
const ds_replacement_ops_t *ds_replacement_fifo(void) { return &rp_fifo; }
const ds_replacement_ops_t *ds_replacement_clock(void) { return &rp_clock; }
const ds_replacement_ops_t *ds_replacement_sieve(void) { return &rp_sieve; }

ds_status_t ds_replacement_init(ds_replacement_t *r,
                                const ds_replacement_ops_t *ops, size_t frames,
                                ds_context_t *context) {
  ds_status_t st;
  if (r == NULL) {
    return DS_ERR_NULL;
  }
  if (ops == NULL) {
    ops = ds_replacement_lru();
  }
  r->ops = NULL;
  r->impl = ds_context_alloc(context, ops->instance_size);
  if (r->impl == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ops->init(r->impl, frames, context);
  if (st != DS_OK) {
    ds_context_free(context, r->impl);
    r->impl = NULL;
    return st;
  }
  r->ops = ops;
  r->context = context;
  return DS_OK;
}

void ds_replacement_destroy(ds_replacement_t *r) {
  if (r == NULL || r->ops == NULL) {
    return;
  }
  r->ops->destroy(r->impl);
  ds_context_free(r->context, r->impl);
  r->ops = NULL;
  r->impl = NULL;
}

void ds_replacement_admit(ds_replacement_t *r, size_t frame) {
  r->ops->admit(r->impl, frame);
}

void ds_replacement_touch(ds_replacement_t *r, size_t frame) {
  r->ops->touch(r->impl, frame);
}

void ds_replacement_set_evictable(ds_replacement_t *r, size_t frame,
                                  bool evictable) {
  r->ops->set_evictable(r->impl, frame, evictable);
}

size_t ds_replacement_victim(ds_replacement_t *r) {
  return r->ops->victim(r->impl);
}

void ds_replacement_evict(ds_replacement_t *r, size_t frame) {
  r->ops->evict(r->impl, frame);
}
