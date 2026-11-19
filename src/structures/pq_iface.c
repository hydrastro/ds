#include <dsa/core/checked.h>
#include <dsa/structures/binomial_heap.h>
#include <dsa/structures/fibonacci_heap.h>
#include <dsa/structures/hollow_heap.h>
#include <dsa/structures/meldable_heap.h>
#include <dsa/structures/min_max_heap.h>
#include <dsa/structures/pairing_heap.h>
#include <dsa/structures/pq_iface.h>
#include <dsa/structures/rank_pairing_heap.h>
#include <dsa/structures/shadow_heap.h>
#include <dsa/structures/strict_fibonacci_heap.h>
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
  b->context = context != NULL ? context : ds_default_context();
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

/* ---- packed binary heap ----
 *
 * Items live inline in one array in heap order, each slot holding a
 * pointer to the item's handle cell followed by the item. Cells come from
 * chunks that never move, so a handle (the cell's address) stays valid
 * while its item is queued; the cell records the item's current slot.
 * Sifting moves slot bytes and updates the moved items' cells. */

#define PQK_CHUNK 256

typedef struct pqk_cell {
  size_t pos;            /* slot of the item */
  struct pqk_cell *next; /* free list */
} pqk_cell_t;

typedef struct pqk_chunk {
  struct pqk_chunk *next;
  pqk_cell_t cells[PQK_CHUNK];
} pqk_chunk_t;

/* handle cells, shared by the packed and weak heaps */
typedef struct pq_cells {
  pqk_chunk_t *chunks;
  pqk_cell_t *free;
  size_t spare; /* cells on the free list */
} pq_cells_t;

static ds_status_t pq_cells_add_chunk(pq_cells_t *p, ds_context_t *context) {
  pqk_chunk_t *c =
      (pqk_chunk_t *)ds_context_alloc(context, sizeof(pqk_chunk_t));
  size_t i;
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  c->next = p->chunks;
  p->chunks = c;
  for (i = PQK_CHUNK; i-- > 0;) {
    c->cells[i].next = p->free;
    p->free = &c->cells[i];
  }
  p->spare += PQK_CHUNK;
  return DS_OK;
}

static ds_status_t pq_cells_reserve(pq_cells_t *p, ds_context_t *context,
                                    size_t count) {
  ds_status_t st = DS_OK;
  while (st == DS_OK && p->spare < count) {
    st = pq_cells_add_chunk(p, context);
  }
  return st;
}

/* a cell, after pq_cells_reserve(p, context, 1) succeeded */
static pqk_cell_t *pq_cells_take(pq_cells_t *p) {
  pqk_cell_t *cell = p->free;
  p->free = cell->next;
  p->spare--;
  return cell;
}

static void pq_cells_give(pq_cells_t *p, pqk_cell_t *cell) {
  cell->next = p->free;
  p->free = cell;
  p->spare++;
}

static void pq_cells_destroy(pq_cells_t *p, ds_context_t *context) {
  while (p->chunks != NULL) {
    pqk_chunk_t *next = p->chunks->next;
    ds_context_free(context, p->chunks);
    p->chunks = next;
  }
  p->free = NULL;
  p->spare = 0;
}

typedef struct pqk {
  pq_base_t b;            /* offset: the item offset inside a slot */
  unsigned char *slots;   /* capacity slots of b.node_size bytes */
  unsigned char *scratch; /* one slot, for sifting */
  size_t n;
  size_t capacity;
  pq_cells_t cells;
} pqk_t;

static unsigned char *pqk_slot(const pqk_t *h, size_t i) {
  return h->slots + i * h->b.node_size;
}

static pqk_cell_t *pqk_cell_of(const unsigned char *slot) {
  pqk_cell_t *c;
  memcpy(&c, slot, sizeof(c));
  return c;
}

static void pqk_place(pqk_t *h, size_t i, const unsigned char *slot) {
  memcpy(pqk_slot(h, i), slot, h->b.node_size);
  pqk_cell_of(slot)->pos = i;
}

static bool pqk_less(const pqk_t *h, const unsigned char *x,
                     const unsigned char *y) {
  return h->b.compare(x + h->b.offset, y + h->b.offset, h->b.user) < 0;
}

/* the item at slot i moves up or down to its place (hole method) */
static void pqk_fix(pqk_t *h, size_t i) {
  unsigned char *x = h->scratch;
  size_t start = i;
  memcpy(x, pqk_slot(h, i), h->b.node_size);
  while (i > 0) {
    size_t p = (i - 1) / 2;
    if (!pqk_less(h, x, pqk_slot(h, p))) {
      break;
    }
    pqk_place(h, i, pqk_slot(h, p));
    i = p;
  }
  if (i == start) {
    for (;;) {
      size_t c = 2 * i + 1;
      if (c >= h->n || c < i) {
        break;
      }
      if (c + 1 < h->n && pqk_less(h, pqk_slot(h, c + 1), pqk_slot(h, c))) {
        c++;
      }
      if (!pqk_less(h, pqk_slot(h, c), x)) {
        break;
      }
      pqk_place(h, i, pqk_slot(h, c));
      i = c;
    }
  }
  pqk_place(h, i, x);
}

/* room for need slots of size bytes: *slots grows by doubling */
static ds_status_t pq_slots_grow(ds_context_t *context, unsigned char **slots,
                                 size_t *capacity, size_t size, size_t need) {
  size_t cap;
  size_t bytes;
  unsigned char *a;
  if (need <= *capacity) {
    return DS_OK;
  }
  cap = *capacity < 8 ? 8 : *capacity;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2) {
      cap = need;
      break;
    }
    cap *= 2;
  }
  if (!ds_size_mul(cap, size, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  a = (unsigned char *)ds_context_realloc(context, *slots, bytes);
  if (a == NULL) {
    return DS_ERR_ALLOC;
  }
  *slots = a;
  *capacity = cap;
  return DS_OK;
}

static ds_status_t pqk_grow(pqk_t *h, size_t need) {
  return pq_slots_grow(h->b.context, &h->slots, &h->capacity, h->b.node_size,
                       need);
}

static ds_status_t pqk_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  pqk_t *h = (pqk_t *)pq;
  ds_status_t st;
  memset(h, 0, sizeof(*h));
  st = pq_base_init(&h->b, sizeof(pqk_cell_t *), item_size, compare, user,
                    context);
  if (st != DS_OK) {
    return st;
  }
  /* slots keep every item at DS_MAX_ALIGN */
  if (!ds_size_align(h->b.node_size, DS_MAX_ALIGN, &h->b.node_size)) {
    return DS_ERR_OVERFLOW;
  }
  h->scratch = (unsigned char *)ds_context_alloc(context, h->b.node_size);
  return h->scratch == NULL ? DS_ERR_ALLOC : DS_OK;
}

static void pqk_destroy(void *pq) {
  pqk_t *h = (pqk_t *)pq;
  pq_cells_destroy(&h->cells, h->b.context);
  ds_context_free(h->b.context, h->slots);
  ds_context_free(h->b.context, h->scratch);
  h->slots = NULL;
  h->scratch = NULL;
  h->n = h->capacity = 0;
}

static ds_status_t pqk_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  pqk_t *h = (pqk_t *)pq;
  pqk_cell_t *cell;
  unsigned char *slot;
  ds_status_t st = pqk_grow(h, h->n + 1);
  if (st == DS_OK) {
    st = pq_cells_reserve(&h->cells, h->b.context, 1);
  }
  if (st != DS_OK) {
    return st;
  }
  cell = pq_cells_take(&h->cells);
  slot = pqk_slot(h, h->n);
  memcpy(slot, &cell, sizeof(cell));
  memcpy(slot + h->b.offset, item, h->b.item_size);
  cell->pos = h->n++;
  pqk_fix(h, h->n - 1);
  if (out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)cell;
  }
  return DS_OK;
}

static ds_status_t pqk_reserve(void *pq, size_t count) {
  pqk_t *h = (pqk_t *)pq;
  size_t need;
  ds_status_t st;
  if (!ds_size_add(h->n, count, &need)) {
    return DS_ERR_OVERFLOW;
  }
  st = pqk_grow(h, need);
  if (st == DS_OK) {
    st = pq_cells_reserve(&h->cells, h->b.context, count);
  }
  return st;
}

static const void *pqk_top(const void *pq) {
  const pqk_t *h = (const pqk_t *)pq;
  return h->n == 0 ? NULL : pqk_slot(h, 0) + h->b.offset;
}

static ds_status_t pqk_remove_at(pqk_t *h, size_t i, void *out_item) {
  pqk_cell_t *cell = pqk_cell_of(pqk_slot(h, i));
  if (out_item != NULL) {
    memcpy(out_item, pqk_slot(h, i) + h->b.offset, h->b.item_size);
  }
  pq_cells_give(&h->cells, cell);
  h->n--;
  if (i < h->n) {
    pqk_place(h, i, pqk_slot(h, h->n));
    pqk_fix(h, i);
  }
  return DS_OK;
}

static ds_status_t pqk_pop(void *pq, void *out_item) {
  pqk_t *h = (pqk_t *)pq;
  if (h->n == 0) {
    return DS_EMPTY;
  }
  return pqk_remove_at(h, 0, out_item);
}

static ds_status_t pqk_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqk_t *h = (pqk_t *)pq;
  pqk_cell_t *cell = (pqk_cell_t *)(void *)handle;
  memmove(pqk_slot(h, cell->pos) + h->b.offset, item, h->b.item_size);
  pqk_fix(h, cell->pos);
  return DS_OK;
}

static ds_status_t pqk_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
  pqk_t *h = (pqk_t *)pq;
  return pqk_remove_at(h, ((pqk_cell_t *)(void *)handle)->pos, out_item);
}

static const void *pqk_item(const void *pq, ds_pq_handle_t handle) {
  const pqk_t *h = (const pqk_t *)pq;
  return pqk_slot(h, ((const pqk_cell_t *)(const void *)handle)->pos) +
         h->b.offset;
}

static size_t pqk_size(const void *pq) { return ((const pqk_t *)pq)->n; }

/* ---- addressable weak heap ----
 *
 * Slots as in the packed heap (a handle cell, then the item) and one
 * reverse bit per slot: a node i > 0 has children 2i + r(i) (left) and
 * 2i + 1 - r(i) (right), the root only the right child 1, and each item is
 * no greater than those in its right subtree. A join swaps two slots,
 * fixing both cells, and flips a bit. Decreasing an item climbs by joins
 * to its distinguished ancestors; removing one first carries it to the
 * root as if it were the smallest (every join swaps), then takes the root
 * out as pop does; increasing does both and puts the item back in as a
 * push, in its own cell. */

typedef struct pqw {
  pq_base_t b;
  unsigned char *slots;   /* capacity slots of b.node_size bytes */
  unsigned char *scratch; /* two slots: a swap, and an item on hold */
  unsigned char *bits;    /* reverse bits, capacity of them */
  size_t n;
  size_t capacity;
  size_t bit_capacity; /* bits room, in slots */
  pq_cells_t cells;
} pqw_t;

static unsigned char *pqw_slot(const pqw_t *h, size_t i) {
  return h->slots + i * h->b.node_size;
}

static unsigned pqw_bit(const pqw_t *h, size_t i) {
  return (unsigned)(h->bits[i >> 3] >> (i & 7)) & 1u;
}

static void pqw_flip(pqw_t *h, size_t i) {
  h->bits[i >> 3] = (unsigned char)(h->bits[i >> 3] ^ (1u << (i & 7)));
}

static void pqw_clear(pqw_t *h, size_t i) {
  h->bits[i >> 3] = (unsigned char)(h->bits[i >> 3] & ~(1u << (i & 7)));
}

static void pqw_put(pqw_t *h, size_t i, const unsigned char *slot) {
  memcpy(pqw_slot(h, i), slot, h->b.node_size);
  pqk_cell_of(slot)->pos = i;
}

static void pqw_swap(pqw_t *h, size_t i, size_t j) {
  memcpy(h->scratch, pqw_slot(h, i), h->b.node_size);
  pqw_put(h, i, pqw_slot(h, j));
  pqw_put(h, j, h->scratch);
}

static bool pqw_less(const pqw_t *h, size_t i, size_t j) {
  return h->b.compare(pqw_slot(h, i) + h->b.offset,
                      pqw_slot(h, j) + h->b.offset, h->b.user) < 0;
}

/* climb while j is a left child; the parent then has j in its right
 * subtree */
static size_t pqw_dancestor(const pqw_t *h, size_t j) {
  while ((j & 1u) == pqw_bit(h, j >> 1)) {
    j >>= 1;
  }
  return j >> 1;
}

static void pqw_climb(pqw_t *h, size_t j) {
  while (j != 0) {
    size_t i = pqw_dancestor(h, j);
    if (!pqw_less(h, j, i)) {
      return;
    }
    pqw_swap(h, i, j);
    pqw_flip(h, j);
    j = i;
  }
}

/* as pqw_climb for an item smaller than every other */
static void pqw_to_root(pqw_t *h, size_t j) {
  while (j != 0) {
    size_t i = pqw_dancestor(h, j);
    pqw_swap(h, i, j);
    pqw_flip(h, j);
    j = i;
  }
}

/* the root was replaced: join it with the leftmost path below 1 */
static void pqw_sift_down(pqw_t *h) {
  size_t x = 1;
  if (h->n < 2) {
    return;
  }
  while (2 * x + pqw_bit(h, x) < h->n) {
    x = 2 * x + pqw_bit(h, x);
  }
  for (; x > 0; x >>= 1) {
    if (pqw_less(h, x, 0)) {
      pqw_swap(h, 0, x);
      pqw_flip(h, x);
    }
  }
}

/* drop the root's slot (its cell stays with the caller) */
static void pqw_drop_root(pqw_t *h) {
  h->n--;
  if (h->n > 0) {
    pqw_put(h, 0, pqw_slot(h, h->n));
    pqw_sift_down(h);
  }
}

/* a slot appended at n, as a push does */
static void pqw_append(pqw_t *h, const unsigned char *slot) {
  size_t i = h->n++;
  pqw_put(h, i, slot);
  pqw_clear(h, i);
  if (i > 0 && (i & 1u) == 0) {
    pqw_clear(h, i >> 1); /* a new only child goes left */
  }
  pqw_climb(h, i);
}

static ds_status_t pqw_grow(pqw_t *h, size_t need) {
  ds_status_t st = pq_slots_grow(h->b.context, &h->slots, &h->capacity,
                                 h->b.node_size, need);
  if (st == DS_OK && need > h->bit_capacity) {
    size_t cap = h->capacity; /* bits follow the slots */
    unsigned char *a = (unsigned char *)ds_context_realloc(
        h->b.context, h->bits, cap / 8 + 1);
    if (a == NULL) {
      return DS_ERR_ALLOC;
    }
    h->bits = a;
    h->bit_capacity = cap;
  }
  return st;
}

static ds_status_t pqw_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  pqw_t *h = (pqw_t *)pq;
  size_t two;
  ds_status_t st;
  memset(h, 0, sizeof(*h));
  st = pq_base_init(&h->b, sizeof(pqk_cell_t *), item_size, compare, user,
                    context);
  if (st != DS_OK) {
    return st;
  }
  if (!ds_size_align(h->b.node_size, DS_MAX_ALIGN, &h->b.node_size) ||
      !ds_size_mul(h->b.node_size, 2, &two)) {
    return DS_ERR_OVERFLOW;
  }
  h->scratch = (unsigned char *)ds_context_alloc(context, two);
  return h->scratch == NULL ? DS_ERR_ALLOC : DS_OK;
}

static void pqw_destroy(void *pq) {
  pqw_t *h = (pqw_t *)pq;
  pq_cells_destroy(&h->cells, h->b.context);
  ds_context_free(h->b.context, h->slots);
  ds_context_free(h->b.context, h->bits);
  ds_context_free(h->b.context, h->scratch);
  h->slots = NULL;
  h->bits = NULL;
  h->scratch = NULL;
  h->n = h->capacity = h->bit_capacity = 0;
}

static ds_status_t pqw_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  pqw_t *h = (pqw_t *)pq;
  unsigned char *slot = h->scratch + h->b.node_size;
  pqk_cell_t *cell;
  ds_status_t st = pqw_grow(h, h->n + 1);
  if (st == DS_OK) {
    st = pq_cells_reserve(&h->cells, h->b.context, 1);
  }
  if (st != DS_OK) {
    return st;
  }
  cell = pq_cells_take(&h->cells);
  memcpy(slot, &cell, sizeof(cell));
  memcpy(slot + h->b.offset, item, h->b.item_size);
  pqw_append(h, slot);
  if (out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)cell;
  }
  return DS_OK;
}

static ds_status_t pqw_reserve(void *pq, size_t count) {
  pqw_t *h = (pqw_t *)pq;
  size_t need;
  ds_status_t st;
  if (!ds_size_add(h->n, count, &need)) {
    return DS_ERR_OVERFLOW;
  }
  st = pqw_grow(h, need);
  if (st == DS_OK) {
    st = pq_cells_reserve(&h->cells, h->b.context, count);
  }
  return st;
}

static const void *pqw_top(const void *pq) {
  const pqw_t *h = (const pqw_t *)pq;
  return h->n == 0 ? NULL : pqw_slot(h, 0) + h->b.offset;
}

static ds_status_t pqw_pop(void *pq, void *out_item) {
  pqw_t *h = (pqw_t *)pq;
  if (h->n == 0) {
    return DS_EMPTY;
  }
  if (out_item != NULL) {
    memcpy(out_item, pqw_slot(h, 0) + h->b.offset, h->b.item_size);
  }
  pq_cells_give(&h->cells, pqk_cell_of(pqw_slot(h, 0)));
  pqw_drop_root(h);
  return DS_OK;
}

static ds_status_t pqw_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqw_t *h = (pqw_t *)pq;
  pqk_cell_t *cell = (pqk_cell_t *)(void *)handle;
  unsigned char *slot = pqw_slot(h, cell->pos);
  int c = h->b.compare(item, slot + h->b.offset, h->b.user);
  memmove(slot + h->b.offset, item, h->b.item_size);
  if (c < 0) {
    pqw_climb(h, cell->pos);
  } else if (c > 0) {
    unsigned char *held = h->scratch + h->b.node_size;
    pqw_to_root(h, cell->pos);
    memcpy(held, pqw_slot(h, 0), h->b.node_size);
    pqw_drop_root(h);
    pqw_append(h, held);
  }
  return DS_OK;
}

static ds_status_t pqw_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
  pqw_t *h = (pqw_t *)pq;
  pqk_cell_t *cell = (pqk_cell_t *)(void *)handle;
  if (out_item != NULL) {
    memcpy(out_item, pqw_slot(h, cell->pos) + h->b.offset, h->b.item_size);
  }
  pqw_to_root(h, cell->pos);
  pq_cells_give(&h->cells, cell);
  pqw_drop_root(h);
  return DS_OK;
}

static const void *pqw_item(const void *pq, ds_pq_handle_t handle) {
  const pqw_t *h = (const pqw_t *)pq;
  return pqw_slot(h, ((const pqk_cell_t *)(const void *)handle)->pos) +
         h->b.offset;
}

static size_t pqw_size(const void *pq) { return ((const pqw_t *)pq)->n; }

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

static bool pqa_less(const pqa_t *h, const pqa_node_t *x,
                     const pqa_node_t *y) {
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

static ds_status_t pqa_reserve(void *pq, size_t count) {
  pqa_t *h = (pqa_t *)pq;
  size_t need, bytes;
  pqa_node_t **a;
  if (!ds_size_add(h->n, count, &need)) {
    return DS_ERR_OVERFLOW;
  }
  if (need <= h->capacity) {
    return DS_OK;
  }
  if (!ds_size_mul(need, sizeof(*a), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  a = (pqa_node_t **)ds_context_realloc(h->b.context, h->a, bytes);
  if (a == NULL) {
    return DS_ERR_ALLOC;
  }
  h->a = a;
  h->capacity = need;
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

static ds_status_t pqa_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
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

static ds_status_t pqb_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
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

static ds_status_t pqm_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
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
  q->box->context = context != NULL ? context : ds_default_context();
  ds_pairing_heap_node_init(&q->nil);
  memset(&q->h, 0, sizeof(q->h));
  q->h.root = NULL;
  q->h.nil = &q->nil;
  q->h.size = 0;
  q->h.compare = pqp_compare;

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

static ds_status_t pqp_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
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

/* ---- hollow heap: dsa/structures/hollow_heap.h behind the ops table ---- */

static ds_status_t pqh_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  return ds_hollow_heap_init((ds_hollow_heap_t *)pq, item_size, compare, user,
                             context);
}

static void pqh_destroy(void *pq) {
  ds_hollow_heap_destroy((ds_hollow_heap_t *)pq);
}

static ds_status_t pqh_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  ds_hollow_item_t *it = NULL;
  ds_status_t st = ds_hollow_heap_push((ds_hollow_heap_t *)pq, item, &it);
  if (st == DS_OK && out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)it;
  }
  return st;
}

static const void *pqh_top(const void *pq) {
  return ds_hollow_heap_top((const ds_hollow_heap_t *)pq);
}

static ds_status_t pqh_pop(void *pq, void *out_item) {
  return ds_hollow_heap_pop((ds_hollow_heap_t *)pq, out_item);
}

static ds_status_t pqh_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  return ds_hollow_heap_update((ds_hollow_heap_t *)pq,
                               (ds_hollow_item_t *)(void *)handle, item);
}

static ds_status_t pqh_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
  return ds_hollow_heap_remove((ds_hollow_heap_t *)pq,
                               (ds_hollow_item_t *)(void *)handle, out_item);
}

static const void *pqh_item(const void *pq, ds_pq_handle_t handle) {
  return ds_hollow_heap_item((const ds_hollow_heap_t *)pq,
                             (const ds_hollow_item_t *)(void *)handle);
}

static size_t pqh_size(const void *pq) {
  return ds_hollow_heap_size((const ds_hollow_heap_t *)pq);
}

static void pqh_meld(void *pq, void *other) {
  ds_hollow_heap_meld((ds_hollow_heap_t *)pq, (ds_hollow_heap_t *)other);
}

/* ---- rank-pairing heaps: dsa/structures/rank_pairing_heap.h behind the ops
 * table ---- */

static ds_status_t pqr_init1(void *pq, size_t item_size,
                             ds_pq_compare_fn compare, void *user,
                             ds_context_t *context) {
  return ds_rp_heap_init((ds_rp_heap_t *)pq, item_size, compare, user, 1,
                         context);
}

static ds_status_t pqr_init2(void *pq, size_t item_size,
                             ds_pq_compare_fn compare, void *user,
                             ds_context_t *context) {
  return ds_rp_heap_init((ds_rp_heap_t *)pq, item_size, compare, user, 2,
                         context);
}

static void pqr_destroy(void *pq) { ds_rp_heap_destroy((ds_rp_heap_t *)pq); }

static ds_status_t pqr_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  ds_rp_node_t *n = NULL;
  ds_status_t st = ds_rp_heap_push((ds_rp_heap_t *)pq, item, &n);
  if (st == DS_OK && out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)n;
  }
  return st;
}

static const void *pqr_top(const void *pq) {
  return ds_rp_heap_top((const ds_rp_heap_t *)pq);
}

static ds_status_t pqr_pop(void *pq, void *out_item) {
  return ds_rp_heap_pop((ds_rp_heap_t *)pq, out_item);
}

static ds_status_t pqr_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  return ds_rp_heap_update((ds_rp_heap_t *)pq, (ds_rp_node_t *)(void *)handle,
                           item);
}

static ds_status_t pqr_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
  return ds_rp_heap_remove((ds_rp_heap_t *)pq, (ds_rp_node_t *)(void *)handle,
                           out_item);
}

static const void *pqr_item(const void *pq, ds_pq_handle_t handle) {
  return ds_rp_heap_item((const ds_rp_heap_t *)pq,
                         (const ds_rp_node_t *)(void *)handle);
}

static size_t pqr_size(const void *pq) {
  return ds_rp_heap_size((const ds_rp_heap_t *)pq);
}

static void pqr_meld(void *pq, void *other) {
  ds_rp_heap_meld((ds_rp_heap_t *)pq, (ds_rp_heap_t *)other);
}

/* ---- Fibonacci heap: dsa/structures/fibonacci_heap.h's item heap behind the
 * ops table
 * ---- */

static ds_status_t pqf_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  return ds_fib_heap_init((ds_fib_heap_t *)pq, item_size, compare, user,
                          context);
}

static void pqf_destroy(void *pq) { ds_fib_heap_destroy((ds_fib_heap_t *)pq); }

static ds_status_t pqf_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  ds_fib_item_t *x = NULL;
  ds_status_t st = ds_fib_heap_push((ds_fib_heap_t *)pq, item, &x);
  if (st == DS_OK && out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)x;
  }
  return st;
}

static const void *pqf_top(const void *pq) {
  return ds_fib_heap_top((const ds_fib_heap_t *)pq);
}

static ds_status_t pqf_pop(void *pq, void *out_item) {
  return ds_fib_heap_pop((ds_fib_heap_t *)pq, out_item);
}

static ds_status_t pqf_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  return ds_fib_heap_update((ds_fib_heap_t *)pq,
                            (ds_fib_item_t *)(void *)handle, item);
}

static ds_status_t pqf_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
  return ds_fib_heap_remove((ds_fib_heap_t *)pq,
                            (ds_fib_item_t *)(void *)handle, out_item);
}

static const void *pqf_item(const void *pq, ds_pq_handle_t handle) {
  return ds_fib_heap_item((const ds_fib_heap_t *)pq,
                          (const ds_fib_item_t *)(void *)handle);
}

static size_t pqf_size(const void *pq) {
  return ds_fib_heap_size((const ds_fib_heap_t *)pq);
}

static void pqf_meld(void *pq, void *other) {
  ds_fib_heap_meld((ds_fib_heap_t *)pq, (ds_fib_heap_t *)other);
}

/* ---- strict Fibonacci heap: dsa/structures/strict_fibonacci_heap.h behind
 * the ops table
 * ---- */

static ds_status_t pqs_init(void *pq, size_t item_size,
                            ds_pq_compare_fn compare, void *user,
                            ds_context_t *context) {
  return ds_sf_heap_init((ds_sf_heap_t *)pq, item_size, compare, user,
                         context);
}

static void pqs_destroy(void *pq) { ds_sf_heap_destroy((ds_sf_heap_t *)pq); }

static ds_status_t pqs_push(void *pq, const void *item,
                            ds_pq_handle_t *out_handle) {
  ds_sf_cell_t *c = NULL;
  ds_status_t st = ds_sf_heap_push((ds_sf_heap_t *)pq, item, &c);
  if (st == DS_OK && out_handle != NULL) {
    *out_handle = (ds_pq_handle_t)(void *)c;
  }
  return st;
}

static const void *pqs_top(const void *pq) {
  return ds_sf_heap_top((const ds_sf_heap_t *)pq);
}

static ds_status_t pqs_pop(void *pq, void *out_item) {
  return ds_sf_heap_pop((ds_sf_heap_t *)pq, out_item);
}

static ds_status_t pqs_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  return ds_sf_heap_update((ds_sf_heap_t *)pq, (ds_sf_cell_t *)(void *)handle,
                           item);
}

static ds_status_t pqs_remove(void *pq, ds_pq_handle_t handle,
                              void *out_item) {
  return ds_sf_heap_remove((ds_sf_heap_t *)pq, (ds_sf_cell_t *)(void *)handle,
                           out_item);
}

static const void *pqs_item(const void *pq, ds_pq_handle_t handle) {
  return ds_sf_heap_item((const ds_sf_heap_t *)pq,
                         (const ds_sf_cell_t *)(void *)handle);
}

static size_t pqs_size(const void *pq) {
  return ds_sf_heap_size((const ds_sf_heap_t *)pq);
}

static void pqs_meld(void *pq, void *other) {
  ds_sf_heap_meld((ds_sf_heap_t *)pq, (ds_sf_heap_t *)other);
}

/* ---- addressable adapters over the min-max and shadow heaps ----
 * Heap slots hold pointers to stable payload nodes. Their move callbacks
 * maintain the inverse index, so update/remove never search the array. */
typedef struct pqt_node {
  size_t index;
} pqt_node_t;
typedef struct pqt {
  pq_base_t b;
  bool shadow;
  union {
    ds_min_max_heap_t minmax;
    ds_shadow_heap_t shadow;
  } heap;
} pqt_t;

static pqt_node_t *pqt_node(const void *slot) {
  pqt_node_t *node = NULL;
  if (slot != NULL) {
    memcpy(&node, slot, sizeof(node));
  }
  return node;
}

static int pqt_compare(const void *a, const void *b, void *user) {
  const pqt_t *h = (const pqt_t *)user;
  return h->b.compare(pq_citem(&h->b, pqt_node(a)),
                      pq_citem(&h->b, pqt_node(b)), h->b.user);
}

static void pqt_moved(const void *elem, size_t index, void *user) {
  pqt_node_t *node;
  (void)user;
  /* The core reports an occupied slot, never the nullable result of at(). */
  memcpy(&node, elem, sizeof(node));
  node->index = index;
}

static size_t pqt_size(const void *pq) {
  const pqt_t *h = (const pqt_t *)pq;
  return h->shadow ? ds_shadow_heap_size(&h->heap.shadow)
                   : ds_min_max_heap_size(&h->heap.minmax);
}

static pqt_node_t *pqt_at(const pqt_t *h, size_t index) {
  return pqt_node(h->shadow ? ds_shadow_heap_at(&h->heap.shadow, index)
                            : ds_min_max_heap_at(&h->heap.minmax, index));
}

static ds_status_t pqt_init(void *pq, size_t size, ds_pq_compare_fn compare,
                            void *user, ds_context_t *context, bool shadow) {
  pqt_t *h = (pqt_t *)pq;
  ds_status_t st;
  memset(h, 0, sizeof(*h));
  st = pq_base_init(&h->b, sizeof(pqt_node_t), size, compare, user, context);
  if (st != DS_OK) {
    return st;
  }
  h->shadow = shadow;
  if (shadow) {
    st = ds_shadow_heap_init(&h->heap.shadow, sizeof(pqt_node_t *),
                             pqt_compare, h, 0, context);
    if (st == DS_OK) {
      ds_shadow_heap_set_move_callback(&h->heap.shadow, pqt_moved, h);
    }
  } else {
    st = ds_min_max_heap_init(&h->heap.minmax, sizeof(pqt_node_t *),
                              pqt_compare, h, context);
    if (st == DS_OK) {
      ds_min_max_heap_set_move_callback(&h->heap.minmax, pqt_moved, h);
    }
  }
  return st;
}

static ds_status_t pqt_init_minmax(void *pq, size_t size,
                                   ds_pq_compare_fn compare, void *user,
                                   ds_context_t *context) {
  return pqt_init(pq, size, compare, user, context, false);
}

static ds_status_t pqt_init_shadow(void *pq, size_t size,
                                   ds_pq_compare_fn compare, void *user,
                                   ds_context_t *context) {
  return pqt_init(pq, size, compare, user, context, true);
}

static void pqt_destroy(void *pq) {
  pqt_t *h = (pqt_t *)pq;
  size_t i, n = pqt_size(h);
  if (h->shadow) {
    ds_shadow_heap_set_move_callback(&h->heap.shadow, NULL, NULL);
  } else {
    ds_min_max_heap_set_move_callback(&h->heap.minmax, NULL, NULL);
  }
  for (i = 0; i < n; i++) {
    ds_context_free(h->b.context, pqt_at(h, i));
  }
  if (h->shadow) {
    ds_shadow_heap_destroy(&h->heap.shadow);
  } else {
    ds_min_max_heap_destroy(&h->heap.minmax);
  }
}

static ds_status_t pqt_reserve(void *pq, size_t extra) {
  pqt_t *h = (pqt_t *)pq;
  size_t need;
  if (!ds_size_add(pqt_size(h), extra, &need)) {
    return DS_ERR_OVERFLOW;
  }
  return h->shadow ? ds_shadow_heap_reserve(&h->heap.shadow, need)
                   : ds_vec_reserve(&h->heap.minmax.items, need);
}

static ds_status_t pqt_push(void *pq, const void *item,
                            ds_pq_handle_t *handle) {
  pqt_t *h = (pqt_t *)pq;
  pqt_node_t *node = (pqt_node_t *)pq_node_new(&h->b, item);
  ds_status_t st;
  if (node == NULL) {
    return DS_ERR_ALLOC;
  }
  st = h->shadow ? ds_shadow_heap_push(&h->heap.shadow, &node)
                 : ds_min_max_heap_push(&h->heap.minmax, &node);
  if (st != DS_OK) {
    ds_context_free(h->b.context, node);
    return st;
  }
  if (handle != NULL) {
    *handle = (ds_pq_handle_t)(void *)node;
  }
  return DS_OK;
}

static const void *pqt_top(const void *pq) {
  const pqt_t *h = (const pqt_t *)pq;
  pqt_node_t *node =
      pqt_node(h->shadow ? ds_shadow_heap_top(&h->heap.shadow)
                         : ds_min_max_heap_peek_min(&h->heap.minmax));
  return node == NULL ? NULL : pq_citem(&h->b, node);
}

static ds_status_t pqt_pop(void *pq, void *out) {
  pqt_t *h = (pqt_t *)pq;
  pqt_node_t *node = NULL;
  ds_status_t st = h->shadow ? ds_shadow_heap_pop(&h->heap.shadow, &node)
                             : ds_min_max_heap_pop_min(&h->heap.minmax, &node);
  if (st == DS_OK) {
    pq_take(&h->b, node, out);
  }
  return st;
}

static bool pqt_valid(const pqt_t *h, const pqt_node_t *node) {
  return node != NULL && node->index < pqt_size(h) &&
         pqt_at(h, node->index) == node;
}

static ds_status_t pqt_update(void *pq, ds_pq_handle_t handle,
                              const void *item) {
  pqt_t *h = (pqt_t *)pq;
  pqt_node_t *node = (pqt_node_t *)(void *)handle;
  if (!pqt_valid(h, node)) {
    return DS_ERR_INVALID;
  }
  memmove(pq_item(&h->b, node), item, h->b.item_size);
  return h->shadow ? ds_shadow_heap_repair_at(&h->heap.shadow, node->index)
                   : ds_min_max_heap_repair_at(&h->heap.minmax, node->index);
}

static ds_status_t pqt_remove(void *pq, ds_pq_handle_t handle, void *out) {
  pqt_t *h = (pqt_t *)pq;
  pqt_node_t *node = (pqt_node_t *)(void *)handle;
  ds_status_t st;
  if (!pqt_valid(h, node)) {
    return DS_ERR_INVALID;
  }
  st = h->shadow
           ? ds_shadow_heap_remove_at(&h->heap.shadow, node->index, NULL)
           : ds_min_max_heap_remove_at(&h->heap.minmax, node->index, NULL);
  if (st == DS_OK) {
    pq_take(&h->b, node, out);
  }
  return st;
}

static const void *pqt_item(const void *pq, ds_pq_handle_t handle) {
  const pqt_t *h = (const pqt_t *)pq;
  const pqt_node_t *node = (const pqt_node_t *)(const void *)handle;
  return pqt_valid(h, node) ? pq_citem(&h->b, node) : NULL;
}

/* ---- ops tables ---- */

#define PQ_WL DS_PQ_WORST_LOG
#define PQ_ML DS_PQ_MELDABLE
#define C_1 DS_PQ_COST_CONST
#define C_SUB DS_PQ_COST_SUBLOG
#define C_LOG DS_PQ_COST_LOG
#define C_LOG2 DS_PQ_COST_LOG2
#define C_N DS_PQ_COST_LINEAR
#define C_NONE DS_PQ_COST_UNSTATED
/* push, pop, decrease, increase, remove, meld as {worst, amortized} */
#define PQ_COSTS_ARRAY                                                        \
  {                                                                           \
    {C_N, C_LOG}, {C_LOG, C_LOG}, {C_LOG, C_LOG}, {C_LOG, C_LOG},             \
        {C_LOG, C_LOG}, {                                                     \
      C_NONE, C_NONE                                                          \
    }                                                                         \
  }
#define PQ_COSTS_BINOMIAL                                                     \
  {                                                                           \
    {C_LOG, C_1}, {C_LOG, C_LOG}, {C_LOG2, C_LOG2}, {C_LOG2, C_LOG2},         \
        {C_LOG2, C_LOG2}, {                                                   \
      C_LOG, C_LOG                                                            \
    }                                                                         \
  }
#define PQ_COSTS_LEFTIST                                                      \
  {                                                                           \
    {C_LOG, C_LOG}, {C_LOG, C_LOG}, {C_LOG, C_LOG}, {C_LOG, C_LOG},           \
        {C_LOG, C_LOG}, {                                                     \
      C_LOG, C_LOG                                                            \
    }                                                                         \
  }
#define PQ_COSTS_WEIGHT                                                       \
  {                                                                           \
    {C_LOG, C_LOG}, {C_LOG, C_LOG}, {C_N, C_N}, {C_N, C_N}, {C_N, C_N}, {     \
      C_LOG, C_LOG                                                            \
    }                                                                         \
  }
#define PQ_COSTS_SKEW                                                         \
  {                                                                           \
    {C_N, C_LOG}, {C_N, C_LOG}, {C_N, C_NONE}, {C_N, C_NONE}, {C_N, C_NONE},  \
    {                                                                         \
      C_N, C_LOG                                                              \
    }                                                                         \
  }
#define PQ_COSTS_RANDOM                                                       \
  {                                                                           \
    {C_N, C_LOG}, {C_N, C_LOG}, {C_N, C_LOG}, {C_N, C_LOG}, {C_N, C_LOG}, {   \
      C_N, C_LOG                                                              \
    }                                                                         \
  }
#define PQ_COSTS_PAIRING                                                      \
  {                                                                           \
    {C_1, C_1}, {C_N, C_LOG}, {C_1, C_SUB}, {C_N, C_LOG}, {C_N, C_LOG}, {     \
      C_1, C_1                                                                \
    }                                                                         \
  }

#define PQ_COSTS_HOLLOW                                                       \
  {                                                                           \
    {C_1, C_1}, {C_N, C_LOG}, {C_N, C_1}, {C_N, C_LOG}, {C_N, C_LOG}, {       \
      C_1, C_1                                                                \
    }                                                                         \
  }

#define PQ_COSTS_RANK_PAIRING                                                 \
  {                                                                           \
    {C_1, C_1}, {C_N, C_LOG}, {C_N, C_1}, {C_N, C_LOG}, {C_N, C_LOG}, {       \
      C_1, C_1                                                                \
    }                                                                         \
  }

#define PQ_COSTS_STRICT_FIBONACCI                                             \
  {                                                                           \
    {C_1, C_1}, {C_LOG, C_LOG}, {C_1, C_1}, {C_LOG, C_LOG}, {C_LOG, C_LOG}, { \
      C_1, C_1                                                                \
    }                                                                         \
  }

static const ds_pq_ops_t pq_binary_ops = {
    "binary",      PQ_WL | DS_PQ_IMPLICIT,
    sizeof(pqa_t), pqa_init2,
    pqa_destroy,   pqa_push,
    pqa_top,       pqa_pop,
    pqa_update,    pqa_remove,
    pqa_item,      pqa_size,
    NULL,          pqa_reserve,
    PQ_COSTS_ARRAY};
static const ds_pq_ops_t pq_quaternary_ops = {
    "quaternary",  PQ_WL | DS_PQ_IMPLICIT,
    sizeof(pqa_t), pqa_init4,
    pqa_destroy,   pqa_push,
    pqa_top,       pqa_pop,
    pqa_update,    pqa_remove,
    pqa_item,      pqa_size,
    NULL,          pqa_reserve,
    PQ_COSTS_ARRAY};

static const ds_pq_ops_t pq_packed_ops = {"packed binary heap",
                                          PQ_WL | DS_PQ_IMPLICIT |
                                              DS_PQ_MOVING_ITEMS,
                                          sizeof(pqk_t),
                                          pqk_init,
                                          pqk_destroy,
                                          pqk_push,
                                          pqk_top,
                                          pqk_pop,
                                          pqk_update,
                                          pqk_remove,
                                          pqk_item,
                                          pqk_size,
                                          NULL,
                                          pqk_reserve,
                                          PQ_COSTS_ARRAY};
static const ds_pq_ops_t pq_weak_ops = {
    "weak heap",   PQ_WL | DS_PQ_IMPLICIT | DS_PQ_MOVING_ITEMS,
    sizeof(pqw_t), pqw_init,
    pqw_destroy,   pqw_push,
    pqw_top,       pqw_pop,
    pqw_update,    pqw_remove,
    pqw_item,      pqw_size,
    NULL,          pqw_reserve,
    PQ_COSTS_ARRAY};
static const ds_pq_ops_t pq_minmax_ops = {
    "min-max heap", PQ_WL | DS_PQ_IMPLICIT,
    sizeof(pqt_t),  pqt_init_minmax,
    pqt_destroy,    pqt_push,
    pqt_top,        pqt_pop,
    pqt_update,     pqt_remove,
    pqt_item,       pqt_size,
    NULL,           pqt_reserve,
    PQ_COSTS_ARRAY};
/* Conservative bounds for the automatic logarithmic shadow threshold.
 * Folding a shadow visits O(log n) affected nodes per level. */
static const ds_pq_ops_t pq_shadow_ops = {"shadow heap",
                                          DS_PQ_IMPLICIT | DS_PQ_AMORTIZED |
                                              DS_PQ_TOP_LOG,
                                          sizeof(pqt_t),
                                          pqt_init_shadow,
                                          pqt_destroy,
                                          pqt_push,
                                          pqt_top,
                                          pqt_pop,
                                          pqt_update,
                                          pqt_remove,
                                          pqt_item,
                                          pqt_size,
                                          NULL,
                                          pqt_reserve,
                                          {{C_N, C_LOG},
                                           {C_LOG2, C_LOG2},
                                           {C_LOG, C_LOG},
                                           {C_LOG, C_LOG},
                                           {C_LOG2, C_LOG2},
                                           {C_NONE, C_NONE}}};
static const ds_pq_ops_t pq_binomial_ops = {
    "binomial", PQ_ML,    sizeof(pqb_t), pqb_init,   pqb_destroy,
    pqb_push,   pqb_top,  pqb_pop,       pqb_update, pqb_remove,
    pqb_item,   pqb_size, pqb_meld,      NULL,       PQ_COSTS_BINOMIAL};
static const ds_pq_ops_t pq_leftist_ops = {
    "leftist",   PQ_WL | PQ_ML, sizeof(pqm_t),   pqm_init_leftist,
    pqm_destroy, pqm_push,      pqm_top,         pqm_pop,
    pqm_update,  pqm_remove,    pqm_item,        pqm_size,
    pqm_meld,    NULL,          PQ_COSTS_LEFTIST};
static const ds_pq_ops_t pq_weight_ops = {"weight-biased leftist",
                                          PQ_ML,
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
                                          pqm_meld,
                                          NULL,
                                          PQ_COSTS_WEIGHT};
static const ds_pq_ops_t pq_skew_ops = {"skew",        DS_PQ_AMORTIZED | PQ_ML,
                                        sizeof(pqm_t), pqm_init_skew,
                                        pqm_destroy,   pqm_push,
                                        pqm_top,       pqm_pop,
                                        pqm_update,    pqm_remove,
                                        pqm_item,      pqm_size,
                                        pqm_meld,      NULL,
                                        PQ_COSTS_SKEW};
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
                                          pqm_meld,
                                          NULL,
                                          PQ_COSTS_RANDOM};
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
    pqp_meld,
    NULL,
    PQ_COSTS_PAIRING};

const ds_pq_ops_t *ds_pq_binary_ops(void) { return &pq_binary_ops; }
const ds_pq_ops_t *ds_pq_quaternary_ops(void) { return &pq_quaternary_ops; }

const ds_pq_ops_t *ds_pq_packed_ops(void) { return &pq_packed_ops; }
const ds_pq_ops_t *ds_pq_weak_ops(void) { return &pq_weak_ops; }
const ds_pq_ops_t *ds_pq_min_max_ops(void) { return &pq_minmax_ops; }
const ds_pq_ops_t *ds_pq_shadow_ops(void) { return &pq_shadow_ops; }
const ds_pq_ops_t *ds_pq_binomial_ops(void) { return &pq_binomial_ops; }
const ds_pq_ops_t *ds_pq_leftist_ops(void) { return &pq_leftist_ops; }
const ds_pq_ops_t *ds_pq_weight_ops(void) { return &pq_weight_ops; }
const ds_pq_ops_t *ds_pq_skew_ops(void) { return &pq_skew_ops; }
const ds_pq_ops_t *ds_pq_random_ops(void) { return &pq_random_ops; }
static const ds_pq_ops_t pq_hollow_ops = {
    "hollow",
    DS_PQ_AMORTIZED | PQ_ML | DS_PQ_FAST_DECREASE | DS_PQ_MOVING_ITEMS,
    sizeof(ds_hollow_heap_t),
    pqh_init,
    pqh_destroy,
    pqh_push,
    pqh_top,
    pqh_pop,
    pqh_update,
    pqh_remove,
    pqh_item,
    pqh_size,
    pqh_meld,
    NULL,
    PQ_COSTS_HOLLOW};

static const ds_pq_ops_t pq_rank_pairing_ops = {"rank-pairing",
                                                DS_PQ_AMORTIZED | PQ_ML |
                                                    DS_PQ_FAST_DECREASE,
                                                sizeof(ds_rp_heap_t),
                                                pqr_init1,
                                                pqr_destroy,
                                                pqr_push,
                                                pqr_top,
                                                pqr_pop,
                                                pqr_update,
                                                pqr_remove,
                                                pqr_item,
                                                pqr_size,
                                                pqr_meld,
                                                NULL,
                                                PQ_COSTS_RANK_PAIRING};

static const ds_pq_ops_t pq_rank_pairing2_ops = {"rank-pairing type 2",
                                                 DS_PQ_AMORTIZED | PQ_ML |
                                                     DS_PQ_FAST_DECREASE,
                                                 sizeof(ds_rp_heap_t),
                                                 pqr_init2,
                                                 pqr_destroy,
                                                 pqr_push,
                                                 pqr_top,
                                                 pqr_pop,
                                                 pqr_update,
                                                 pqr_remove,
                                                 pqr_item,
                                                 pqr_size,
                                                 pqr_meld,
                                                 NULL,
                                                 PQ_COSTS_RANK_PAIRING};

static const ds_pq_ops_t pq_fibonacci_ops = {"fibonacci",
                                             DS_PQ_AMORTIZED | PQ_ML |
                                                 DS_PQ_FAST_DECREASE,
                                             sizeof(ds_fib_heap_t),
                                             pqf_init,
                                             pqf_destroy,
                                             pqf_push,
                                             pqf_top,
                                             pqf_pop,
                                             pqf_update,
                                             pqf_remove,
                                             pqf_item,
                                             pqf_size,
                                             pqf_meld,
                                             NULL,
                                             PQ_COSTS_HOLLOW};

static const ds_pq_ops_t pq_strict_fibonacci_ops = {"strict-fibonacci",
                                                    PQ_WL | PQ_ML |
                                                        DS_PQ_FAST_DECREASE,
                                                    sizeof(ds_sf_heap_t),
                                                    pqs_init,
                                                    pqs_destroy,
                                                    pqs_push,
                                                    pqs_top,
                                                    pqs_pop,
                                                    pqs_update,
                                                    pqs_remove,
                                                    pqs_item,
                                                    pqs_size,
                                                    pqs_meld,
                                                    NULL,
                                                    PQ_COSTS_STRICT_FIBONACCI};

const ds_pq_ops_t *ds_pq_pairing_ops(void) { return &pq_pairing_ops; }
const ds_pq_ops_t *ds_pq_hollow_ops(void) { return &pq_hollow_ops; }
const ds_pq_ops_t *ds_pq_rank_pairing_ops(void) {
  return &pq_rank_pairing_ops;
}
const ds_pq_ops_t *ds_pq_rank_pairing2_ops(void) {
  return &pq_rank_pairing2_ops;
}
const ds_pq_ops_t *ds_pq_fibonacci_ops(void) { return &pq_fibonacci_ops; }
const ds_pq_ops_t *ds_pq_strict_fibonacci_ops(void) {
  return &pq_strict_fibonacci_ops;
}

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
  pq->context = context != NULL ? context : ds_default_context();
  pq->compare = compare;
  pq->user = user;
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

ds_status_t ds_pq_update(ds_pq_t *pq, ds_pq_handle_t handle,
                         const void *item) {
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

ds_status_t ds_pq_reserve(ds_pq_t *pq, size_t count) {
  if (pq == NULL || pq->ops == NULL) {
    return DS_ERR_NULL;
  }
  return pq->ops->reserve == NULL ? DS_OK : pq->ops->reserve(pq->impl, count);
}

ds_status_t ds_pq_meld(ds_pq_t *pq, ds_pq_t *other) {
  if (pq == NULL || other == NULL || pq->ops == NULL || other->ops == NULL) {
    return DS_ERR_NULL;
  }
  if (pq == other || pq->ops != other->ops ||
      pq->item_size != other->item_size) {
    return DS_ERR_INVALID;
  }
  /* a native meld moves nodes: it needs the same order and memory that
   * either context can release; otherwise items are copied over */
  if (pq->ops->meld != NULL && pq->compare == other->compare &&
      pq->user == other->user &&
      (ds_context_compatible(pq->context, other->context) ||
       (pq->ops->traits & DS_PQ_MELD_ANY_CONTEXT) != 0)) {
    pq->ops->meld(pq->impl, other->impl);
    return DS_OK;
  }
  if (pq->ops->reserve != NULL) {
    ds_status_t st = pq->ops->reserve(pq->impl, other->ops->size(other->impl));
    if (st != DS_OK) {
      return st;
    }
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
