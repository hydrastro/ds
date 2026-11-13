#include "hollow_heap.h"
#include "checked.h"
#include <string.h>

#define HH_CHUNK 256

/* an item's record (a handle) */
struct ds_hollow_item {
  union {
    ds_hollow_node_t *node; /* the full node holding the item */
    ds_hollow_item_t *next; /* the next free record */
  } u;
};

/* a node; the item follows at h->node_offset */
struct ds_hollow_node {
  ds_hollow_item_t *rec;   /* NULL when hollow or free */
  ds_hollow_node_t *child; /* first child */
  ds_hollow_node_t *next;  /* next sibling in the first parent's list, the
                              next node of a work list, or the next free
                              node */
  ds_hollow_node_t *ep;    /* second parent, for a hollow node with two */
  unsigned rank;
};

/* a block of HH_CHUNK nodes or records, from DS_MAX_ALIGN on */
struct ds_hollow_chunk {
  ds_hollow_chunk_t *next;
};

static unsigned char *hh_item(const ds_hollow_heap_t *h, ds_hollow_node_t *n) {
  return (unsigned char *)n + h->node_offset;
}

static const unsigned char *hh_citem(const ds_hollow_heap_t *h,
                                     const ds_hollow_node_t *n) {
  return (const unsigned char *)n + h->node_offset;
}

/* the root of two full nodes, the other made its first child */
static ds_hollow_node_t *hh_link(const ds_hollow_heap_t *h, ds_hollow_node_t *v,
                                 ds_hollow_node_t *w) {
  ds_hollow_node_t *top = w;
  ds_hollow_node_t *below = v;
  if (h->compare(hh_citem(h, v), hh_citem(h, w), h->user) < 0) {
    top = v;
    below = w;
  }
  below->next = top->child;
  top->child = below;
  return top;
}

/* ---- pools ---- */

/* a new chunk of HH_CHUNK slots of the given stride, owned by h */
static unsigned char *hh_chunk_new(ds_hollow_heap_t *h, size_t stride) {
  ds_hollow_chunk_t *c = (ds_hollow_chunk_t *)ds_context_alloc(
      h->context, DS_MAX_ALIGN + HH_CHUNK * stride);
  if (c == NULL) {
    return NULL;
  }
  c->next = NULL;
  if (h->chunks_tail == NULL) {
    h->chunks = c;
  } else {
    h->chunks_tail->next = c;
  }
  h->chunks_tail = c;
  return (unsigned char *)c + DS_MAX_ALIGN;
}

static void hh_spare_node(ds_hollow_heap_t *h, ds_hollow_node_t *n) {
  n->rec = NULL;
  n->next = h->spare;
  if (h->spare == NULL) {
    h->spare_tail = n;
  }
  h->spare = n;
}

static void hh_spare_item(ds_hollow_heap_t *h, ds_hollow_item_t *r) {
  r->u.next = h->spare_items;
  if (h->spare_items == NULL) {
    h->spare_items_tail = r;
  }
  h->spare_items = r;
}

static ds_hollow_node_t *hh_node_new(ds_hollow_heap_t *h) {
  ds_hollow_node_t *n;
  if (h->spare == NULL) {
    unsigned char *p = hh_chunk_new(h, h->node_size);
    size_t i;
    if (p == NULL) {
      return NULL;
    }
    for (i = HH_CHUNK; i-- > 0;) {
      hh_spare_node(h, (ds_hollow_node_t *)(void *)(p + i * h->node_size));
    }
  }
  n = h->spare;
  h->spare = n->next;
  if (h->spare == NULL) {
    h->spare_tail = NULL;
  }
  n->rec = NULL;
  n->child = NULL;
  n->next = NULL;
  n->ep = NULL;
  n->rank = 0;
  h->nodes++;
  return n;
}

static void hh_node_free(ds_hollow_heap_t *h, ds_hollow_node_t *n) {
  hh_spare_node(h, n);
  h->nodes--;
}

static ds_hollow_item_t *hh_rec_new(ds_hollow_heap_t *h) {
  ds_hollow_item_t *r;
  if (h->spare_items == NULL) {
    unsigned char *p = hh_chunk_new(h, sizeof(ds_hollow_item_t));
    size_t i;
    if (p == NULL) {
      return NULL;
    }
    for (i = HH_CHUNK; i-- > 0;) {
      hh_spare_item(
          h, (ds_hollow_item_t *)(void *)(p + i * sizeof(ds_hollow_item_t)));
    }
  }
  r = h->spare_items;
  h->spare_items = r->u.next;
  if (h->spare_items == NULL) {
    h->spare_items_tail = NULL;
  }
  r->u.node = NULL;
  return r;
}

/* ---- the DAG ---- */

/* Detach the children of v, which is about to be freed. A child with no
 * other parent goes on *list. A child with two parents stays with the
 * other one: if v is its second parent, the child is v's last child (it
 * was v's first when v was made, and later children go in front) and its
 * next pointer belongs to the first parent's list, so the scan stops
 * there; if v is its first parent, it becomes the last child of its
 * second parent. Either way it has one parent afterwards. */
static void hh_orphans(ds_hollow_node_t *v, ds_hollow_node_t **list) {
  ds_hollow_node_t *w = v->child;
  while (w != NULL) {
    ds_hollow_node_t *u = w;
    w = w->next;
    if (u->ep == NULL) {
      u->next = *list;
      *list = u;
    } else {
      if (u->ep == v) {
        w = NULL;
      } else {
        u->next = NULL;
      }
      u->ep = NULL;
    }
  }
  v->child = NULL;
}

/* put full root u in the rank table, linking equal ranks */
static void hh_ranked(ds_hollow_heap_t *h, ds_hollow_node_t *u,
                      unsigned *max_rank, ds_hollow_node_t **spill) {
  while (u->rank < DS_HOLLOW_MAX_RANK && h->ranks[u->rank] != NULL) {
    ds_hollow_node_t *x = h->ranks[u->rank];
    h->ranks[u->rank] = NULL;
    u = hh_link(h, u, x);
    u->rank++;
  }
  if (u->rank >= DS_HOLLOW_MAX_RANK) {
    /* past any rank N nodes can reach; still correct, just unranked */
    *spill = *spill == NULL ? u : hh_link(h, *spill, u);
    return;
  }
  h->ranks[u->rank] = u;
  if (u->rank > *max_rank) {
    *max_rank = u->rank;
  }
}

/* link what the rank table holds (and any spill) into one tree */
static ds_hollow_node_t *hh_unranked(ds_hollow_heap_t *h, unsigned max_rank,
                                     ds_hollow_node_t *spill) {
  ds_hollow_node_t *root = spill;
  unsigned i;
  for (i = 0; i <= max_rank; i++) {
    if (h->ranks[i] != NULL) {
      root = root == NULL ? h->ranks[i] : hh_link(h, root, h->ranks[i]);
      h->ranks[i] = NULL;
    }
  }
  return root;
}

/* The root has gone hollow: free hollow roots one after another, put the
 * full nodes they leave without a parent in the rank table, then link
 * what the table holds into one tree. */
static void hh_consolidate(ds_hollow_heap_t *h) {
  ds_hollow_node_t *list = h->root;
  ds_hollow_node_t *spill = NULL;
  unsigned max_rank = 0;
  list->next = NULL;
  while (list != NULL) {
    ds_hollow_node_t *v = list;
    list = list->next;
    if (v->rec != NULL) {
      hh_ranked(h, v, &max_rank, &spill);
    } else {
      hh_orphans(v, &list);
      hh_node_free(h, v);
    }
  }
  h->root = hh_unranked(h, max_rank, spill);
}

void ds_hollow_heap_rebuild(ds_hollow_heap_t *h) {
  ds_hollow_node_t *list;
  ds_hollow_node_t *full = NULL;
  ds_hollow_node_t *spill = NULL;
  unsigned max_rank = 0;
  if (h == NULL || h->root == NULL) {
    return;
  }
  list = h->root;
  list->next = NULL;
  while (list != NULL) {
    ds_hollow_node_t *v = list;
    list = list->next;
    hh_orphans(v, &list);
    if (v->rec != NULL) {
      v->next = full;
      full = v;
    } else {
      hh_node_free(h, v);
    }
  }
  /* the items, every one a single node, linked by rank as a pop would */
  while (full != NULL) {
    ds_hollow_node_t *x = full;
    full = full->next;
    x->rank = 0;
    hh_ranked(h, x, &max_rank, &spill);
  }
  h->root = hh_unranked(h, max_rank, spill);
  h->rebuilds++;
}

/* keep hollow nodes at most about as many as items */
static void hh_bound(ds_hollow_heap_t *h) {
  if (h->nodes - h->size > h->size + 64) {
    ds_hollow_heap_rebuild(h);
  }
}

/* ---- public ---- */

ds_status_t ds_hollow_heap_init(ds_hollow_heap_t *h, size_t item_size,
                                ds_hollow_compare_fn compare, void *user,
                                ds_context_t *context) {
  size_t chunk;
  if (h == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  memset(h, 0, sizeof(*h));
  if (item_size == 0) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_align(sizeof(ds_hollow_node_t), DS_MAX_ALIGN, &h->node_offset) ||
      !ds_size_add(h->node_offset, item_size, &h->node_size) ||
      !ds_size_align(h->node_size, DS_MAX_ALIGN, &h->node_size) ||
      !ds_size_mul(h->node_size, HH_CHUNK, &chunk) ||
      !ds_size_add(chunk, DS_MAX_ALIGN, &chunk)) {
    return DS_ERR_OVERFLOW;
  }
  h->item_size = item_size;
  h->compare = compare;
  h->user = user;
  h->context = context;
  return DS_OK;
}

void ds_hollow_heap_destroy(ds_hollow_heap_t *h) {
  ds_hollow_chunk_t *c;
  if (h == NULL) {
    return;
  }
  c = h->chunks;
  while (c != NULL) {
    ds_hollow_chunk_t *next = c->next;
    ds_context_free(h->context, c);
    c = next;
  }
  h->chunks = NULL;
  h->chunks_tail = NULL;
  h->spare = NULL;
  h->spare_tail = NULL;
  h->spare_items = NULL;
  h->spare_items_tail = NULL;
  h->root = NULL;
  h->size = 0;
  h->nodes = 0;
}

ds_status_t ds_hollow_heap_push(ds_hollow_heap_t *h, const void *item,
                                ds_hollow_item_t **out_handle) {
  ds_hollow_item_t *rec;
  ds_hollow_node_t *n;
  if (h == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  rec = hh_rec_new(h);
  if (rec == NULL) {
    return DS_ERR_ALLOC;
  }
  n = hh_node_new(h);
  if (n == NULL) {
    hh_spare_item(h, rec);
    return DS_ERR_ALLOC;
  }
  memcpy(hh_item(h, n), item, h->item_size);
  n->rec = rec;
  rec->u.node = n;
  h->root = h->root == NULL ? n : hh_link(h, n, h->root);
  h->size++;
  if (out_handle != NULL) {
    *out_handle = rec;
  }
  return DS_OK;
}

const void *ds_hollow_heap_top(const ds_hollow_heap_t *h) {
  if (h == NULL || h->root == NULL) {
    return NULL;
  }
  return hh_citem(h, h->root);
}

ds_status_t ds_hollow_heap_pop(ds_hollow_heap_t *h, void *out_item) {
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (h->root == NULL) {
    return DS_EMPTY;
  }
  if (out_item != NULL) {
    memcpy(out_item, hh_citem(h, h->root), h->item_size);
  }
  hh_spare_item(h, h->root->rec);
  h->root->rec = NULL;
  h->size--;
  hh_consolidate(h);
  hh_bound(h);
  return DS_OK;
}

ds_status_t ds_hollow_heap_update(ds_hollow_heap_t *h, ds_hollow_item_t *it,
                                  const void *item) {
  ds_hollow_node_t *u;
  ds_hollow_node_t *v;
  int c;
  if (h == NULL || it == NULL || item == NULL) {
    return DS_ERR_NULL;
  }
  u = it->u.node;
  c = h->compare(item, hh_citem(h, u), h->user);
  if (c == 0 || (c < 0 && u == h->root)) {
    /* the order around the node still holds */
    memmove(hh_item(h, u), item, h->item_size);
    return DS_OK;
  }
  v = hh_node_new(h); /* before any change: failing changes nothing */
  if (v == NULL) {
    return DS_ERR_ALLOC;
  }
  memmove(hh_item(h, v), item, h->item_size);
  v->rec = it;
  it->u.node = v;
  u->rec = NULL;
  if (c < 0) {
    /* decrease: the hollow node becomes the new node's only child and
     * keeps its own children, so nothing is cut */
    v->rank = u->rank > 2 ? u->rank - 2 : 0;
    v->child = u;
    u->ep = v;
    h->root = hh_link(h, v, h->root);
  } else {
    /* increase: a removal and a push */
    if (u == h->root) {
      hh_consolidate(h);
    }
    h->root = h->root == NULL ? v : hh_link(h, v, h->root);
  }
  hh_bound(h);
  return DS_OK;
}

ds_status_t ds_hollow_heap_remove(ds_hollow_heap_t *h, ds_hollow_item_t *it,
                                  void *out_item) {
  ds_hollow_node_t *u;
  if (h == NULL || it == NULL) {
    return DS_ERR_NULL;
  }
  u = it->u.node;
  if (out_item != NULL) {
    memcpy(out_item, hh_citem(h, u), h->item_size);
  }
  u->rec = NULL;
  hh_spare_item(h, it);
  h->size--;
  if (u == h->root) {
    hh_consolidate(h);
  }
  hh_bound(h);
  return DS_OK;
}

const void *ds_hollow_heap_item(const ds_hollow_heap_t *h,
                                const ds_hollow_item_t *it) {
  if (h == NULL || it == NULL) {
    return NULL;
  }
  return hh_citem(h, it->u.node);
}

size_t ds_hollow_heap_size(const ds_hollow_heap_t *h) {
  return h == NULL ? 0 : h->size;
}

size_t ds_hollow_heap_nodes(const ds_hollow_heap_t *h) {
  return h == NULL ? 0 : h->nodes;
}

void ds_hollow_heap_meld(ds_hollow_heap_t *h, ds_hollow_heap_t *other) {
  if (h == NULL || other == NULL || h == other) {
    return;
  }
  if (other->root != NULL) {
    h->root = h->root == NULL ? other->root : hh_link(h, h->root, other->root);
  }
  h->size += other->size;
  h->nodes += other->nodes;
  /* the nodes and records live in other's chunks: take the chunks and
   * the free lists */
  if (other->chunks != NULL) {
    if (h->chunks_tail == NULL) {
      h->chunks = other->chunks;
    } else {
      h->chunks_tail->next = other->chunks;
    }
    h->chunks_tail = other->chunks_tail;
  }
  if (other->spare != NULL) {
    other->spare_tail->next = h->spare;
    if (h->spare == NULL) {
      h->spare_tail = other->spare_tail;
    }
    h->spare = other->spare;
  }
  if (other->spare_items != NULL) {
    other->spare_items_tail->u.next = h->spare_items;
    if (h->spare_items == NULL) {
      h->spare_items_tail = other->spare_items_tail;
    }
    h->spare_items = other->spare_items;
  }
  other->root = NULL;
  other->size = 0;
  other->nodes = 0;
  other->chunks = NULL;
  other->chunks_tail = NULL;
  other->spare = NULL;
  other->spare_tail = NULL;
  other->spare_items = NULL;
  other->spare_items_tail = NULL;
}
