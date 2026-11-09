#include "pq_iface.h"
#include "binomial_heap.h"
#include "checked.h"
#include "meldable_heap.h"
#include "pairing_heap.h"
#include <string.h>

/* ---- shared node layout: backend header, then the item ---- */

typedef struct pq_base {
  size_t item_size;
  size_t offset; /* item offset inside a node */
  size_t node_size;
  ds_pq_compare_fn compare;
  void *user;
  ds_context_t *context;
} pq_base_t;

static ds_status_t pq_base_init(pq_base_t *b, size_t header, size_t item_size,
                                ds_pq_compare_fn compare, void *user,
                                ds_context_t *context) {
  if (compare == NULL) {
    return DS_ERR_NULL;
  }
  if (item_size == 0) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_align(header, DS_MAX_ALIGN, &b->offset) ||
      !ds_size_add(b->offset, item_size, &b->node_size)) {
    return DS_ERR_OVERFLOW;
  }
  b->item_size = item_size;
  b->compare = compare;
  b->user = user;
  b->context = context;
  return DS_OK;
}

static unsigned char *pq_item(const pq_base_t *b, void *node) {
  return (unsigned char *)node + b->offset;
}

static const unsigned char *pq_citem(const pq_base_t *b, const void *node) {
  return (const unsigned char *)node + b->offset;
}

static void *pq_node_new(const pq_base_t *b, const void *item) {
  void *n = ds_context_alloc(b->context, b->node_size);
  if (n != NULL) {
    memcpy(pq_item(b, n), item, b->item_size);
  }
  return n;
}

static void pq_take(const pq_base_t *b, void *node, void *out_item) {
  if (out_item != NULL) {
    memcpy(out_item, pq_item(b, node), b->item_size);
  }
  ds_context_free(b->context, node);
}

/* ---- implicit d-ary heap ---- */

typedef struct pqa_node {
  size_t index;
} pqa_node_t;

typedef struct pqa {
  pq_base_t b;
  pqa_node_t **a;
  size_t n;
  size_t capacity;
  size_t d;
} pqa_t;

static bool pqa_less(const pqa_t *h, const pqa_node_t *x, const pqa_node_t *y) {
  return h->b.compare(pq_citem(&h->b, x), pq_citem(&h->b, y), h->b.user) < 0;
}

static size_t pqa_up(pqa_t *h, size_t i) {
  pqa_node_t *node = h->a[i];
  while (i > 0) {
    size_t p = (i - 1) / h->d;
    if (!pqa_less(h, node, h->a[p])) {
      break;
    }
    h->a[i] = h->a[p];
    h->a[i]->index = i;
    i = p;
  }
  h->a[i] = node;
  node->index = i;
  return i;
}

static void pqa_down(pqa_t *h, size_t i) {
  pqa_node_t *node = h->a[i];
  for (;;) {
    size_t c, best, j, end;
    if (i > (h->n - 1) / h->d) {
      break;
    }
    c = h->d * i + 1;
    if (c >= h->n) {
      break;
    }
    best = c;
    end = h->n - c > h->d ? c + h->d : h->n;
    for (j = c + 1; j < end; j++) {
      if (pqa_less(h, h->a[j], h->a[best])) {
        best = j;
      }
    }
    if (!pqa_less(h, h->a[best], node)) {
      break;
    }
    h->a[i] = h->a[best];
    h->a[i]->index = i;
    i = best;
  }
  h->a[i] = node;
  node->index = i;
}

static void pqa_fix(pqa_t *h, size_t i) {
  if (pqa_up(h, i) == i) {
    pqa_down(h, i);
  }
}

static ds_status_t pqa_init(void *pq, size_t d, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  pqa_t *h = (pqa_t *)pq;
  h->a = NULL;
  h->n = 0;
  h->capacity = 0;
  h->d = d;
  return pq_base_init(&h->b, sizeof(pqa_node_t), item_size, compare, user,
                      context);
}

static ds_status_t pqa_init2(void *pq, size_t item_size,
                             ds_pq_compare_fn compare, void *user,
                             ds_context_t *context) {
  return pqa_init(pq, 2, item_size, compare, user, context);
}

static ds_status_t pqa_init4(void *pq, size_t item_size,
                             ds_pq_compare_fn compare, void *user,
                             ds_context_t *context) {
  return pqa_init(pq, 4, item_size, compare, user, context);
}

static void pqa_destroy(void *pq) {
  pqa_t *h = (pqa_t *)pq;
  size_t i;
  for (i = 0; i < h->n; i++) {
    ds_context_free(h->b.context, h->a[i]);
  }
  ds_context_free(h->b.context, h->a);
  h->a = NULL;
  h->n = 0;
  h->capacity = 0;
}

static ds_status_t pqa_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  pqa_t *h = (pqa_t *)pq;
  pqa_node_t *node;
  if (h->n == h->capacity) {
    size_t cap = h->capacity < 8 ? 8 : h->capacity * 2, bytes;
    pqa_node_t **a;
    if (cap < h->capacity || !ds_size_mul(cap, sizeof(*a), &bytes)) {
      return DS_ERR_OVERFLOW;
    }
    a = (pqa_node_t **)ds_context_realloc(h->b.context, h->a, bytes);
    if (a == NULL) {
      return DS_ERR_ALLOC;
    }
    h->a = a;
    h->capacity = cap;
  }
  node = (pqa_node_t *)pq_node_new(&h->b, item);
  if (node == NULL) {
    return DS_ERR_ALLOC;
  }
  h->a[h->n] = node;
  pqa_up(h, h->n++);
  if (out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)node;
  }
  return DS_OK;
}

static const void *pqa_top(const void *pq) {
  const pqa_t *h = (const pqa_t *)pq;
  return h->n == 0 ? NULL : pq_citem(&h->b, h->a[0]);
}

static ds_status_t pqa_remove_at(pqa_t *h, size_t i, void *out_item) {
  pqa_node_t *node = h->a[i];
  h->n--;
  if (i < h->n) {
    h->a[i] = h->a[h->n];
    h->a[i]->index = i;
    pqa_fix(h, i);
  }
  pq_take(&h->b, node, out_item);
  return DS_OK;
}

static ds_status_t pqa_pop(void *pq, void *out_item) {
  pqa_t *h = (pqa_t *)pq;
  if (h->n == 0) {
    return DS_EMPTY;
  }
  return pqa_remove_at(h, 0, out_item);
}

static ds_status_t pqa_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqa_t *h = (pqa_t *)pq;
  pqa_node_t *node = (pqa_node_t *)(void *)handle;
  memmove(pq_item(&h->b, node), item, h->b.item_size);
  pqa_fix(h, node->index);
  return DS_OK;
}

static ds_status_t pqa_remove(void *pq, ds_pq_handle_t handle, void *out_item) {
  pqa_t *h = (pqa_t *)pq;
  return pqa_remove_at(h, ((pqa_node_t *)(void *)handle)->index, out_item);
}

static const void *pqa_item(const void *pq, ds_pq_handle_t handle) {
  return pq_citem(&((const pqa_t *)pq)->b, handle);
}

static size_t pqa_size(const void *pq) { return ((const pqa_t *)pq)->n; }

/* ---- binomial heap ---- */

typedef struct pqb {
  pq_base_t b;
  ds_binomial_heap_t h;
} pqb_t;

static int pqb_compare(const ds_binomial_heap_node_t *x,
                       const ds_binomial_heap_node_t *y, void *user) {
  const pqb_t *q = (const pqb_t *)user;
  return q->b.compare(pq_citem(&q->b, x), pq_citem(&q->b, y), q->b.user);
}

static ds_status_t pqb_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  pqb_t *q = (pqb_t *)pq;
  ds_status_t st = pq_base_init(&q->b, sizeof(ds_binomial_heap_node_t),
                                item_size, compare, user, context);
  if (st != DS_OK) {
    return st;
  }
  return ds_binomial_heap_init(&q->h, pqb_compare, q);
}

static void pqb_release(ds_binomial_heap_node_t *node, void *user) {
  ds_context_free((ds_context_t *)user, node);
}

static void pqb_destroy(void *pq) {
  pqb_t *q = (pqb_t *)pq;
  ds_binomial_heap_clear(&q->h, pqb_release, q->b.context);
}

static ds_status_t pqb_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  pqb_t *q = (pqb_t *)pq;
  ds_binomial_heap_node_t *n =
      (ds_binomial_heap_node_t *)pq_node_new(&q->b, item);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  ds_binomial_heap_node_init(n);
  ds_binomial_heap_push(&q->h, n);
  if (out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)n;
  }
  return DS_OK;
}

static const void *pqb_top(const void *pq) {
  const pqb_t *q = (const pqb_t *)pq;
  const ds_binomial_heap_node_t *n = ds_binomial_heap_peek(&q->h);
  return n == NULL ? NULL : pq_citem(&q->b, n);
}

static ds_status_t pqb_pop(void *pq, void *out_item) {
  pqb_t *q = (pqb_t *)pq;
  ds_binomial_heap_node_t *n;
  if (ds_binomial_heap_is_empty(&q->h)) {
    return DS_EMPTY;
  }
  n = ds_binomial_heap_pop(&q->h);
  pq_take(&q->b, n, out_item);
  return DS_OK;
}

static ds_status_t pqb_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqb_t *q = (pqb_t *)pq;
  ds_binomial_heap_node_t *n = (ds_binomial_heap_node_t *)(void *)handle;
  memmove(pq_item(&q->b, n), item, q->b.item_size);
  ds_binomial_heap_update(&q->h, n);
  return DS_OK;
}

static ds_status_t pqb_remove(void *pq, ds_pq_handle_t handle, void *out_item) {
  pqb_t *q = (pqb_t *)pq;
  ds_binomial_heap_node_t *n = (ds_binomial_heap_node_t *)(void *)handle;
  ds_binomial_heap_remove(&q->h, n);
  pq_take(&q->b, n, out_item);
  return DS_OK;
}

static const void *pqb_item(const void *pq, ds_pq_handle_t handle) {
  return pq_citem(&((const pqb_t *)pq)->b, handle);
}

static size_t pqb_size(const void *pq) {
  return ds_binomial_heap_size(&((const pqb_t *)pq)->h);
}

static void pqb_meld(void *pq, void *other) {
  ds_binomial_heap_meld(&((pqb_t *)pq)->h, &((pqb_t *)other)->h);
}

/* ---- meldable heap family ---- */

typedef struct pqm {
  pq_base_t b;
  ds_meldable_heap_t h;
} pqm_t;

static int pqm_compare(const ds_meldable_heap_node_t *x,
                       const ds_meldable_heap_node_t *y, void *user) {
  const pqm_t *q = (const pqm_t *)user;
  return q->b.compare(pq_citem(&q->b, x), pq_citem(&q->b, y), q->b.user);
}

static ds_status_t pqm_init(void *pq, const ds_meldable_heap_policy_t *policy,
                            size_t item_size, ds_pq_compare_fn compare,
                            void *user, ds_context_t *context) {
  pqm_t *q = (pqm_t *)pq;
  ds_status_t st = pq_base_init(&q->b, sizeof(ds_meldable_heap_node_t),
                                item_size, compare, user, context);
  if (st != DS_OK) {
    return st;
  }
  return ds_meldable_heap_init(&q->h, pqm_compare, q, policy, NULL);
}

static ds_status_t pqm_init_leftist(void *pq, size_t item_size,
                                    ds_pq_compare_fn compare, void *user,
                                    ds_context_t *context) {
  return pqm_init(pq, ds_meldable_heap_leftist_policy(), item_size, compare,
                  user, context);
}

static ds_status_t pqm_init_weight(void *pq, size_t item_size,
                                   ds_pq_compare_fn compare, void *user,
                                   ds_context_t *context) {
  return pqm_init(pq, ds_meldable_heap_weight_policy(), item_size, compare,
                  user, context);
}

static ds_status_t pqm_init_skew(void *pq, size_t item_size,
                                 ds_pq_compare_fn compare, void *user,
                                 ds_context_t *context) {
  return pqm_init(pq, ds_meldable_heap_skew_policy(), item_size, compare, user,
                  context);
}

static ds_status_t pqm_init_random(void *pq, size_t item_size,
                                   ds_pq_compare_fn compare, void *user,
                                   ds_context_t *context) {
  return pqm_init(pq, ds_meldable_heap_random_policy(), item_size, compare,
                  user, context);
}

static void pqm_release(ds_meldable_heap_node_t *node, void *user) {
  ds_context_free((ds_context_t *)user, node);
}

static void pqm_destroy(void *pq) {
  pqm_t *q = (pqm_t *)pq;
  ds_meldable_heap_clear(&q->h, pqm_release, q->b.context);
}

static ds_status_t pqm_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  pqm_t *q = (pqm_t *)pq;
  ds_meldable_heap_node_t *n =
      (ds_meldable_heap_node_t *)pq_node_new(&q->b, item);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  ds_meldable_heap_push(&q->h, n);
  if (out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)n;
  }
  return DS_OK;
}

static const void *pqm_top(const void *pq) {
  const pqm_t *q = (const pqm_t *)pq;
  const ds_meldable_heap_node_t *n = ds_meldable_heap_peek(&q->h);
  return n == NULL ? NULL : pq_citem(&q->b, n);
}

static ds_status_t pqm_pop(void *pq, void *out_item) {
  pqm_t *q = (pqm_t *)pq;
  ds_meldable_heap_node_t *n;
  if (ds_meldable_heap_is_empty(&q->h)) {
    return DS_EMPTY;
  }
  n = ds_meldable_heap_pop(&q->h);
  pq_take(&q->b, n, out_item);
  return DS_OK;
}

static ds_status_t pqm_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqm_t *q = (pqm_t *)pq;
  ds_meldable_heap_node_t *n = (ds_meldable_heap_node_t *)(void *)handle;
  memmove(pq_item(&q->b, n), item, q->b.item_size);
  ds_meldable_heap_update(&q->h, n);
  return DS_OK;
}

static ds_status_t pqm_remove(void *pq, ds_pq_handle_t handle, void *out_item) {
  pqm_t *q = (pqm_t *)pq;
  ds_meldable_heap_node_t *n = (ds_meldable_heap_node_t *)(void *)handle;
  ds_meldable_heap_remove(&q->h, n);
  pq_take(&q->b, n, out_item);
  return DS_OK;
}

static const void *pqm_item(const void *pq, ds_pq_handle_t handle) {
  return pq_citem(&((const pqm_t *)pq)->b, handle);
}

static size_t pqm_size(const void *pq) {
  return ds_meldable_heap_size(&((const pqm_t *)pq)->h);
}

static void pqm_meld(void *pq, void *other) {
  ds_meldable_heap_meld(&((pqm_t *)pq)->h, &((pqm_t *)other)->h);
}

/* ---- pairing heap ----
 * The pairing heap's comparator gets no user pointer, so each node points
 * at a reference-counted comparator box; melded nodes keep their box alive
 * after their original queue is gone. Removal and key increases detach the
 * node by forcing it to the top (decrease-key) and extracting it. */

typedef struct pqp_box {
  ds_pq_compare_fn compare;
  void *user;
  size_t offset;
  size_t refs;
  ds_context_t *context;
} pqp_box_t;

typedef struct pqp_node {
  ds_pairing_heap_node_t hn;
  pqp_box_t *box;
  int forced;
} pqp_node_t;

typedef struct pqp {
  pq_base_t b;
  ds_pairing_heap_t h;
  ds_pairing_heap_node_t nil;
  pqp_box_t *box;
} pqp_t;

static int pqp_compare(ds_pairing_heap_node_t *x, ds_pairing_heap_node_t *y) {
  const pqp_node_t *a = (const pqp_node_t *)(void *)x;
  const pqp_node_t *b = (const pqp_node_t *)(void *)y;
  if (a->forced) {
    return -1;
  }
  if (b->forced) {
    return 1;
  }
  return a->box->compare((const unsigned char *)a + a->box->offset,
                         (const unsigned char *)b + a->box->offset,
                         a->box->user);
}

static void pqp_box_release(pqp_box_t *box) {
  if (--box->refs == 0) {
    ds_context_free(box->context, box);
  }
}

static void pqp_free(pqp_node_t *n, void *out_item, size_t item_size) {
  pqp_box_t *box = n->box;
  if (out_item != NULL) {
    memcpy(out_item, (unsigned char *)n + box->offset, item_size);
  }
  ds_context_free(box->context, n);
  pqp_box_release(box);
}

static ds_status_t pqp_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  pqp_t *q = (pqp_t *)pq;
  ds_status_t st = pq_base_init(&q->b, sizeof(pqp_node_t), item_size, compare,
                                user, context);
  if (st != DS_OK) {
    return st;
  }
  q->box = (pqp_box_t *)ds_context_alloc(context, sizeof(pqp_box_t));
  if (q->box == NULL) {
    return DS_ERR_ALLOC;
  }
  q->box->compare = compare;
  q->box->user = user;
  q->box->offset = q->b.offset;
  q->box->refs = 1;
  q->box->context = context;
  ds_pairing_heap_node_init(&q->nil);
  q->h.root = NULL;
  q->h.nil = &q->nil;
  q->h.size = 0;
  q->h.compare = pqp_compare;
  q->h.allocator = NULL;
  q->h.deallocator = NULL;
  return DS_OK;
}

static void pqp_destroy(void *pq) {
  pqp_t *q = (pqp_t *)pq;
  ds_pairing_heap_node_t *stack = q->h.root;
  /* iterative: splice each node's child list onto the work list */
  while (stack != NULL) {
    ds_pairing_heap_node_t *n = stack;
    stack = n->sibling;
    if (n->child != NULL) {
      ds_pairing_heap_node_t *last = n->child;
      while (last->sibling != NULL) {
        last = last->sibling;
      }
      last->sibling = stack;
      stack = n->child;
    }
    pqp_free((pqp_node_t *)(void *)n, NULL, 0);
  }
  q->h.root = NULL;
  q->h.size = 0;
  pqp_box_release(q->box);
  q->box = NULL;
}

static ds_status_t pqp_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  pqp_t *q = (pqp_t *)pq;
  pqp_node_t *n = (pqp_node_t *)pq_node_new(&q->b, item);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  ds_pairing_heap_node_init(&n->hn);
  n->box = q->box;
  n->forced = 0;
  q->box->refs++;
  ds_pairing_heap_insert(&q->h, &n->hn);
  if (out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)n;
  }
  return DS_OK;
}

static const void *pqp_top(const void *pq) {
  const pqp_t *q = (const pqp_t *)pq;
  return q->h.root == NULL ? NULL : pq_citem(&q->b, q->h.root);
}

static ds_status_t pqp_pop(void *pq, void *out_item) {
  pqp_t *q = (pqp_t *)pq;
  if (q->h.root == NULL) {
    return DS_EMPTY;
  }
  pqp_free((pqp_node_t *)(void *)ds_pairing_heap_extract(&q->h), out_item,
           q->b.item_size);
  return DS_OK;
}

static void pqp_detach(pqp_t *q, pqp_node_t *n) {
  n->forced = 1;
  ds_pairing_heap_decrease_key(&q->h, &n->hn);
  (void)ds_pairing_heap_extract(&q->h);
  n->forced = 0;
}

static ds_status_t pqp_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqp_t *q = (pqp_t *)pq;
  pqp_node_t *n = (pqp_node_t *)(void *)handle;
  if (q->b.compare(item, pq_citem(&q->b, n), q->b.user) < 0) {
    memmove(pq_item(&q->b, n), item, q->b.item_size);
    ds_pairing_heap_decrease_key(&q->h, &n->hn);
    return DS_OK;
  }
  pqp_detach(q, n);
  memmove(pq_item(&q->b, n), item, q->b.item_size);
  ds_pairing_heap_insert(&q->h, &n->hn);
  return DS_OK;
}

static ds_status_t pqp_remove(void *pq, ds_pq_handle_t handle, void *out_item) {
  pqp_t *q = (pqp_t *)pq;
  pqp_node_t *n = (pqp_node_t *)(void *)handle;
  pqp_detach(q, n);
  pqp_free(n, out_item, q->b.item_size);
  return DS_OK;
}

static const void *pqp_item(const void *pq, ds_pq_handle_t handle) {
  return pq_citem(&((const pqp_t *)pq)->b, handle);
}

static size_t pqp_size(const void *pq) { return ((const pqp_t *)pq)->h.size; }

static void pqp_meld(void *pq, void *other) {
  ds_pairing_heap_merge(&((pqp_t *)pq)->h, &((pqp_t *)other)->h);
}

/* ---- ops tables ---- */

#define PQ_WL DS_PQ_WORST_LOG
#define PQ_ML DS_PQ_MELDABLE

static const ds_pq_ops_t pq_binary_ops = {"binary",      PQ_WL | DS_PQ_IMPLICIT,
                                          sizeof(pqa_t), pqa_init2,
                                          pqa_destroy,   pqa_push,
                                          pqa_top,       pqa_pop,
                                          pqa_update,    pqa_remove,
                                          pqa_item,      pqa_size,
                                          NULL};
static const ds_pq_ops_t pq_quaternary_ops = {
    "quaternary",  PQ_WL | DS_PQ_IMPLICIT,
    sizeof(pqa_t), pqa_init4,
    pqa_destroy,   pqa_push,
    pqa_top,       pqa_pop,
    pqa_update,    pqa_remove,
    pqa_item,      pqa_size,
    NULL};
static const ds_pq_ops_t pq_binomial_ops = {
    "binomial", PQ_WL | PQ_ML, sizeof(pqb_t), pqb_init,   pqb_destroy,
    pqb_push,   pqb_top,       pqb_pop,       pqb_update, pqb_remove,
    pqb_item,   pqb_size,      pqb_meld};
static const ds_pq_ops_t pq_leftist_ops = {
    "leftist", PQ_WL | PQ_ML, sizeof(pqm_t), pqm_init_leftist, pqm_destroy,
    pqm_push,  pqm_top,       pqm_pop,       pqm_update,       pqm_remove,
    pqm_item,  pqm_size,      pqm_meld};
static const ds_pq_ops_t pq_weight_ops = {"weight-biased leftist",
                                          PQ_WL | PQ_ML,
                                          sizeof(pqm_t),
                                          pqm_init_weight,
                                          pqm_destroy,
                                          pqm_push,
                                          pqm_top,
                                          pqm_pop,
                                          pqm_update,
                                          pqm_remove,
                                          pqm_item,
                                          pqm_size,
                                          pqm_meld};
static const ds_pq_ops_t pq_skew_ops = {"skew",        DS_PQ_AMORTIZED | PQ_ML,
                                        sizeof(pqm_t), pqm_init_skew,
                                        pqm_destroy,   pqm_push,
                                        pqm_top,       pqm_pop,
                                        pqm_update,    pqm_remove,
                                        pqm_item,      pqm_size,
                                        pqm_meld};
static const ds_pq_ops_t pq_random_ops = {"randomized meldable",
                                          DS_PQ_RANDOMIZED | PQ_ML,
                                          sizeof(pqm_t),
                                          pqm_init_random,
                                          pqm_destroy,
                                          pqm_push,
                                          pqm_top,
                                          pqm_pop,
                                          pqm_update,
                                          pqm_remove,
                                          pqm_item,
                                          pqm_size,
                                          pqm_meld};
static const ds_pq_ops_t pq_pairing_ops = {
    "pairing",
    DS_PQ_AMORTIZED | PQ_ML | DS_PQ_FAST_DECREASE | DS_PQ_MELD_ANY_CONTEXT,
    sizeof(pqp_t),
    pqp_init,
    pqp_destroy,
    pqp_push,
    pqp_top,
    pqp_pop,
    pqp_update,
    pqp_remove,
    pqp_item,
    pqp_size,
    pqp_meld};

const ds_pq_ops_t *ds_pq_binary_ops(void) { return &pq_binary_ops; }
const ds_pq_ops_t *ds_pq_quaternary_ops(void) { return &pq_quaternary_ops; }
const ds_pq_ops_t *ds_pq_binomial_ops(void) { return &pq_binomial_ops; }
const ds_pq_ops_t *ds_pq_leftist_ops(void) { return &pq_leftist_ops; }
const ds_pq_ops_t *ds_pq_weight_ops(void) { return &pq_weight_ops; }
const ds_pq_ops_t *ds_pq_skew_ops(void) { return &pq_skew_ops; }
const ds_pq_ops_t *ds_pq_random_ops(void) { return &pq_random_ops; }
const ds_pq_ops_t *ds_pq_pairing_ops(void) { return &pq_pairing_ops; }

/* ---- generic wrapper ---- */

ds_status_t ds_pq_init(ds_pq_t *pq, const ds_pq_ops_t *ops, size_t item_size,
                       ds_pq_compare_fn compare, void *user,
                       ds_context_t *context) {
  ds_status_t st;
  if (pq == NULL) {
    return DS_ERR_NULL;
  }
  pq->ops = NULL;
  pq->impl = NULL;
  if (ops == NULL) {
    ops = ds_pq_binary_ops();
  }
  pq->impl = ds_context_alloc(context, ops->instance_size);
  if (pq->impl == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ops->init(pq->impl, item_size, compare, user, context);
  if (st != DS_OK) {
    ds_context_free(context, pq->impl);
    pq->impl = NULL;
    return st;
  }
  pq->ops = ops;
  pq->item_size = item_size;
  pq->context = context;
  return DS_OK;
}

void ds_pq_destroy(ds_pq_t *pq) {
  if (pq == NULL || pq->ops == NULL) {
    return;
  }
  pq->ops->destroy(pq->impl);
  ds_context_free(pq->context, pq->impl);
  pq->impl = NULL;
  pq->ops = NULL;
}

ds_status_t ds_pq_push(ds_pq_t *pq, const void *item,
                       ds_pq_handle_t *out_handle) {
  if (pq == NULL || pq->ops == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  return pq->ops->push(pq->impl, item, out_handle);
}

const void *ds_pq_top(const ds_pq_t *pq) {
  if (pq == NULL || pq->ops == NULL) {
    return NULL;
  }
  return pq->ops->top(pq->impl);
}

ds_status_t ds_pq_pop(ds_pq_t *pq, void *out_item) {
  if (pq == NULL || pq->ops == NULL) {
    return DS_ERR_NULL;
  }
  return pq->ops->pop(pq->impl, out_item);
}

ds_status_t ds_pq_update(ds_pq_t *pq, ds_pq_handle_t handle, const void *item) {
  if (pq == NULL || pq->ops == NULL || handle == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  return pq->ops->update(pq->impl, handle, item);
}

ds_status_t ds_pq_remove(ds_pq_t *pq, ds_pq_handle_t handle, void *out_item) {
  if (pq == NULL || pq->ops == NULL || handle == NULL) {
    return DS_ERR_NULL;
  }
  return pq->ops->remove(pq->impl, handle, out_item);
}

const void *ds_pq_item(const ds_pq_t *pq, ds_pq_handle_t handle) {
  if (pq == NULL || pq->ops == NULL || handle == NULL) {
    return NULL;
  }
  return pq->ops->item(pq->impl, handle);
}

size_t ds_pq_size(const ds_pq_t *pq) {
  if (pq == NULL || pq->ops == NULL) {
    return 0;
  }
  return pq->ops->size(pq->impl);
}

bool ds_pq_is_empty(const ds_pq_t *pq) { return ds_pq_size(pq) == 0; }

ds_status_t ds_pq_meld(ds_pq_t *pq, ds_pq_t *other) {
  if (pq == NULL || other == NULL || pq->ops == NULL || other->ops == NULL) {
    return DS_ERR_NULL;
  }
  if (pq == other || pq->ops != other->ops ||
      pq->item_size != other->item_size) {
    return DS_ERR_INVALID;
  }
  if (pq->ops->meld != NULL &&
      (pq->context == other->context ||
       (pq->ops->traits & DS_PQ_MELD_ANY_CONTEXT) != 0)) {
    pq->ops->meld(pq->impl, other->impl);
    return DS_OK;
  }
  while (other->ops->size(other->impl) > 0) {
    /* push a copy first so a failure leaves both queues consistent */
    ds_status_t st =
        pq->ops->push(pq->impl, other->ops->top(other->impl), NULL);
    if (st != DS_OK) {
      return st;
    }
    (void)other->ops->pop(other->impl, NULL);
  }
  return DS_OK;
}
