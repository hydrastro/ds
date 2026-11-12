#include "buddy_allocator.h"
#include <string.h>

#define BD_NONE (~(size_t)0)
#define BD_INSIDE 0
#define BD_FREE 1
#define BD_USED 2

static void bd_push(ds_buddy_t *b, size_t i, size_t k) {
  b->order[i] = (unsigned char)k;
  b->state[i] = BD_FREE;
  b->prev[i] = BD_NONE;
  b->next[i] = b->head[k];
  if (b->head[k] != BD_NONE) {
    b->prev[b->head[k]] = i;
  }
  b->head[k] = i;
}

static void bd_unlink(ds_buddy_t *b, size_t i) {
  size_t k = b->order[i];
  if (b->prev[i] == BD_NONE) {
    b->head[k] = b->next[i];
  } else {
    b->next[b->prev[i]] = b->next[i];
  }
  if (b->next[i] != BD_NONE) {
    b->prev[b->next[i]] = b->prev[i];
  }
  b->state[i] = BD_INSIDE;
}

ds_status_t ds_buddy_init(ds_buddy_t *b, void *buffer, size_t min_block,
                          size_t max_order, ds_context_t *context) {
  size_t k;
  if (b == NULL) {
    return DS_ERR_NULL;
  }
  if (min_block == 0 || (min_block & (min_block - 1)) != 0 || max_order > 40 ||
      max_order >= sizeof(size_t) * 8 - 1) {
    return DS_ERR_INVALID;
  }
  memset(b, 0, sizeof(*b));
  b->min_block = min_block;
  b->max_order = max_order;
  b->blocks = (size_t)1 << max_order;
  b->context = context;
  if (b->blocks > ((size_t)-1) / min_block ||
      b->blocks > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  b->next = (size_t *)ds_context_alloc(context, b->blocks * sizeof(size_t));
  b->prev = (size_t *)ds_context_alloc(context, b->blocks * sizeof(size_t));
  b->order = (unsigned char *)ds_context_alloc(context, b->blocks);
  b->state = (unsigned char *)ds_context_calloc(context, b->blocks, 1);
  b->head =
      (size_t *)ds_context_alloc(context, (max_order + 1) * sizeof(size_t));
  if (buffer == NULL) {
    b->arena =
        (unsigned char *)ds_context_alloc(context, b->blocks * min_block);
    b->owns_arena = true;
  } else {
    b->arena = (unsigned char *)buffer;
  }
  if (b->next == NULL || b->prev == NULL || b->order == NULL ||
      b->state == NULL || b->head == NULL || b->arena == NULL) {
    ds_buddy_destroy(b);
    return DS_ERR_ALLOC;
  }
  for (k = 0; k <= max_order; k++) {
    b->head[k] = BD_NONE;
  }
  bd_push(b, 0, max_order);
  b->free_bytes = b->blocks * min_block;
  return DS_OK;
}

void ds_buddy_destroy(ds_buddy_t *b) {
  if (b == NULL) {
    return;
  }
  ds_context_free(b->context, b->next);
  ds_context_free(b->context, b->prev);
  ds_context_free(b->context, b->order);
  ds_context_free(b->context, b->state);
  ds_context_free(b->context, b->head);
  if (b->owns_arena) {
    ds_context_free(b->context, b->arena);
  }
  memset(b, 0, sizeof(*b));
}

static size_t bd_order_for(const ds_buddy_t *b, size_t size) {
  size_t blocks = (size - 1) / b->min_block + 1;
  size_t k = 0;
  while (((size_t)1 << k) < blocks) {
    k++;
  }
  return k;
}

void *ds_buddy_alloc(ds_buddy_t *b, size_t size) {
  size_t k;
  size_t j;
  size_t i;
  if (b == NULL || b->arena == NULL || size == 0 ||
      size > b->blocks * b->min_block) {
    return NULL;
  }
  k = bd_order_for(b, size);
  for (j = k; j <= b->max_order && b->head[j] == BD_NONE; j++) {
  }
  if (j > b->max_order) {
    return NULL;
  }
  i = b->head[j];
  bd_unlink(b, i);
  while (j > k) {
    j--;
    bd_push(b, i + ((size_t)1 << j), j); /* the upper half stays free */
  }
  b->order[i] = (unsigned char)k;
  b->state[i] = BD_USED;
  b->free_bytes -= b->min_block << k;
  return b->arena + i * b->min_block;
}

static bool bd_index(const ds_buddy_t *b, const void *ptr, size_t *out) {
  const unsigned char *p = (const unsigned char *)ptr;
  size_t off;
  if (b == NULL || p == NULL || b->arena == NULL || p < b->arena ||
      p >= b->arena + b->blocks * b->min_block) {
    return false;
  }
  off = (size_t)(p - b->arena);
  if (off % b->min_block != 0) {
    return false;
  }
  *out = off / b->min_block;
  return b->state[*out] == BD_USED;
}

ds_status_t ds_buddy_free(ds_buddy_t *b, void *ptr) {
  size_t i;
  size_t k;
  size_t buddy;
  if (b == NULL) {
    return DS_ERR_NULL;
  }
  if (ptr == NULL) {
    return DS_OK;
  }
  if (!bd_index(b, ptr, &i)) {
    return DS_ERR_INVALID;
  }
  k = b->order[i];
  b->state[i] = BD_INSIDE;
  b->free_bytes += b->min_block << k;
  while (k < b->max_order) {
    buddy = i ^ ((size_t)1 << k);
    if (b->state[buddy] != BD_FREE || b->order[buddy] != k) {
      break;
    }
    bd_unlink(b, buddy);
    if (buddy < i) {
      i = buddy;
    }
    k++;
  }
  bd_push(b, i, k);
  return DS_OK;
}

void *ds_buddy_realloc(ds_buddy_t *b, void *ptr, size_t size) {
  size_t i;
  size_t k;
  size_t want;
  size_t buddy;
  size_t kk;
  void *n;
  if (b == NULL) {
    return NULL;
  }
  if (ptr == NULL) {
    return ds_buddy_alloc(b, size);
  }
  if (size == 0) {
    (void)ds_buddy_free(b, ptr);
    return NULL;
  }
  if (!bd_index(b, ptr, &i) || size > b->blocks * b->min_block) {
    return NULL;
  }
  k = b->order[i];
  want = bd_order_for(b, size);
  if (want <= k) {
    /* shrink in place: give back the upper halves */
    while (k > want) {
      k--;
      bd_push(b, i + ((size_t)1 << k), k);
      b->free_bytes += b->min_block << k;
    }
    b->order[i] = (unsigned char)k;
    return ptr;
  }
  /* grow in place while the right buddies are free and large enough */
  for (kk = k; kk < want; kk++) {
    buddy = i ^ ((size_t)1 << kk);
    if (buddy < i || b->state[buddy] != BD_FREE || b->order[buddy] != kk) {
      break;
    }
  }
  if (kk == want) {
    for (kk = k; kk < want; kk++) {
      buddy = i + ((size_t)1 << kk);
      bd_unlink(b, buddy);
      b->free_bytes -= b->min_block << kk;
    }
    b->order[i] = (unsigned char)want;
    return ptr;
  }
  n = ds_buddy_alloc(b, size);
  if (n == NULL) {
    return NULL;
  }
  memcpy(n, ptr, b->min_block << k);
  (void)ds_buddy_free(b, ptr);
  return n;
}

size_t ds_buddy_block_size(const ds_buddy_t *b, const void *ptr) {
  size_t i;
  if (!bd_index(b, ptr, &i)) {
    return 0;
  }
  return b->min_block << b->order[i];
}

size_t ds_buddy_free_bytes(const ds_buddy_t *b) {
  return b == NULL ? 0 : b->free_bytes;
}

size_t ds_buddy_largest_free(const ds_buddy_t *b) {
  size_t k;
  if (b == NULL || b->head == NULL) {
    return 0;
  }
  for (k = b->max_order + 1; k-- > 0;) {
    if (b->head[k] != BD_NONE) {
      return b->min_block << k;
    }
  }
  return 0;
}

static void *bd_ctx_alloc(size_t size, void *user) {
  return ds_buddy_alloc((ds_buddy_t *)user, size);
}

static void *bd_ctx_calloc(size_t count, size_t size, void *user) {
  void *p;
  if (size != 0 && count > ((size_t)-1) / size) {
    return NULL;
  }
  p = ds_buddy_alloc((ds_buddy_t *)user, count * size);
  if (p != NULL) {
    memset(p, 0, count * size);
  }
  return p;
}

static void *bd_ctx_realloc(void *ptr, size_t size, void *user) {
  return ds_buddy_realloc((ds_buddy_t *)user, ptr, size);
}

static void bd_ctx_free(void *ptr, void *user) {
  (void)ds_buddy_free((ds_buddy_t *)user, ptr);
}

void ds_context_use_buddy(ds_context_t *context, ds_buddy_t *b) {
  if (context == NULL) {
    return;
  }
  context->alloc = bd_ctx_alloc;
  context->calloc = bd_ctx_calloc;
  context->realloc = bd_ctx_realloc;
  context->free = bd_ctx_free;
  context->user = b;
  context->resource = DS_RESOURCE_HEAP | DS_RESOURCE_BOUNDED;
}
