#include "art.h"
#include <stdlib.h>
#include <string.h>

#define ART_NODE4 1
#define ART_NODE16 2
#define ART_NODE48 3
#define ART_NODE256 4
#define ART_LEAF 5
#define ART_INLINE_PREFIX 10

typedef struct art_node {
  unsigned char type;
  unsigned char edge;
  struct art_node *parent;
} art_node_t;

typedef struct art_inner {
  art_node_t node;
  unsigned short count; /* 0..256 byte children; terminal is separate */
  size_t prefix_len;
  unsigned char small[ART_INLINE_PREFIX];
  unsigned char *large;
  art_node_t *terminal;
} art_inner_t;

typedef struct art_node4 {
  art_inner_t inner;
  unsigned char keys[4];
  art_node_t *children[4];
} art_node4_t;
typedef struct art_node16 {
  art_inner_t inner;
  unsigned char keys[16];
  art_node_t *children[16];
} art_node16_t;
typedef struct art_node48 {
  art_inner_t inner;
  unsigned char slots[256];
  art_node_t *children[48];
} art_node48_t;
typedef struct art_node256 {
  art_inner_t inner;
  art_node_t *children[256];
} art_node256_t;
typedef struct art_leaf {
  art_node_t node;
  void *value;
  size_t len;
  unsigned char key[1];
} art_leaf_t;

struct art {
  art_node_t *root;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
};

static unsigned char *art_prefix(art_inner_t *n) {
  return n->large != NULL ? n->large : n->small;
}

static art_inner_t *art_inner_new(ds_art_t *a, unsigned char type) {
  art_inner_t *n;
  size_t bytes = type == ART_NODE4    ? sizeof(art_node4_t)
                 : type == ART_NODE16 ? sizeof(art_node16_t)
                 : type == ART_NODE48 ? sizeof(art_node48_t)
                                      : sizeof(art_node256_t);
  n = (art_inner_t *)a->allocator(bytes);
  if (n != NULL) {
    memset(n, 0, bytes);
    n->node.type = type;
  }
  return n;
}

static bool art_prefix_init(ds_art_t *a, art_inner_t *n,
                            const unsigned char *bytes, size_t len) {
  if (len > ART_INLINE_PREFIX) {
    n->large = (unsigned char *)a->allocator(len);
    if (n->large == NULL) {
      return false;
    }
  }
  if (len != 0) {
    memcpy(art_prefix(n), bytes, len);
  }
  n->prefix_len = len;
  return true;
}

static void art_inner_free(ds_art_t *a, art_inner_t *n) {
  if (n->large != NULL) {
    a->deallocator(n->large);
  }
  a->deallocator(n);
}

static void art_prefix_drop(ds_art_t *a, art_inner_t *n, size_t skip) {
  size_t left = n->prefix_len - skip;
  if (n->large != NULL && left <= ART_INLINE_PREFIX) {
    if (left != 0) {
      memcpy(n->small, n->large + skip, left);
    }
    a->deallocator(n->large);
    n->large = NULL;
  } else if (left != 0) {
    unsigned char *p = art_prefix(n);
    memmove(p, p + skip, left);
  }
  n->prefix_len = left;
}

static art_leaf_t *art_leaf_new(ds_art_t *a, const unsigned char *key,
                                size_t len, void *value) {
  art_leaf_t *n = (art_leaf_t *)a->allocator(sizeof(*n) + len);
  if (n == NULL) {
    return NULL;
  }
  memset(&n->node, 0, sizeof(n->node));
  n->node.type = ART_LEAF;
  n->len = len;
  n->value = value;
  if (len != 0) {
    memcpy(n->key, key, len);
  }
  return n;
}

static bool art_equal(const art_leaf_t *n, const unsigned char *key,
                      size_t len) {
  return n->len == len && (len == 0 || memcmp(n->key, key, len) == 0);
}

static art_node_t **art_child(art_inner_t *n, unsigned char edge) {
  unsigned i;
  switch (n->node.type) {
  case ART_NODE4: {
    art_node4_t *p = (art_node4_t *)(void *)n;
    for (i = 0; i < n->count; i++) {
      if (p->keys[i] == edge)
        return &p->children[i];
    }
    break;
  }
  case ART_NODE16: {
    art_node16_t *p = (art_node16_t *)(void *)n;
    for (i = 0; i < n->count; i++) {
      if (p->keys[i] == edge)
        return &p->children[i];
    }
    break;
  }
  case ART_NODE48: {
    art_node48_t *p = (art_node48_t *)(void *)n;
    return p->slots[edge] == 0 ? NULL : &p->children[p->slots[edge] - 1];
  }
  default: {
    art_node256_t *p = (art_node256_t *)(void *)n;
    return p->children[edge] == NULL ? NULL : &p->children[edge];
  }
  }
  return NULL;
}

static art_node_t *art_next_child(art_inner_t *n, unsigned start) {
  unsigned i;
  if (n->node.type == ART_NODE4 || n->node.type == ART_NODE16) {
    unsigned char *keys;
    art_node_t **children;
    if (n->node.type == ART_NODE4) {
      art_node4_t *p = (art_node4_t *)(void *)n;
      keys = p->keys;
      children = p->children;
    } else {
      art_node16_t *p = (art_node16_t *)(void *)n;
      keys = p->keys;
      children = p->children;
    }
    for (i = 0; i < n->count; i++) {
      if ((unsigned)keys[i] >= start)
        return children[i];
    }
  } else {
    for (i = start; i < 256; i++) {
      art_node_t **p = art_child(n, (unsigned char)i);
      if (p != NULL)
        return *p;
    }
  }
  return NULL;
}

/* n is already linked: its parent is NULL exactly at the root, otherwise
 * the terminal slot or its edge slot still names n. */
static art_node_t **art_ref(ds_art_t *a, art_node_t *n) {
  art_inner_t *parent;
  if (n->parent == NULL)
    return &a->root;
  parent = (art_inner_t *)(void *)n->parent;
  return parent->terminal == n ? &parent->terminal : art_child(parent, n->edge);
}

/* The caller has reserved a slot. No allocation and no failure. */
static void art_store(art_inner_t *n, unsigned char edge, art_node_t *child) {
  unsigned pos = 0;
  unsigned i;
  if (n->node.type == ART_NODE4 || n->node.type == ART_NODE16) {
    unsigned char *keys;
    art_node_t **children;
    if (n->node.type == ART_NODE4) {
      art_node4_t *p = (art_node4_t *)(void *)n;
      keys = p->keys;
      children = p->children;
    } else {
      art_node16_t *p = (art_node16_t *)(void *)n;
      keys = p->keys;
      children = p->children;
    }
    while (pos < n->count && keys[pos] < edge)
      pos++;
    for (i = n->count; i > pos; i--) {
      keys[i] = keys[i - 1];
      children[i] = children[i - 1];
    }
    keys[pos] = edge;
    children[pos] = child;
  } else if (n->node.type == ART_NODE48) {
    art_node48_t *p = (art_node48_t *)(void *)n;
    while (p->children[pos] != NULL)
      pos++;
    p->slots[edge] = (unsigned char)(pos + 1);
    p->children[pos] = child;
  } else {
    ((art_node256_t *)(void *)n)->children[edge] = child;
  }
  n->count++;
  child->parent = &n->node;
  child->edge = edge;
}

static void art_terminal(art_inner_t *n, art_node_t *leaf) {
  n->terminal = leaf;
  leaf->parent = &n->node;
}

/* Transfer the prefix and children only after the replacement exists. */
static art_inner_t *art_resize(ds_art_t *a, art_inner_t *n,
                               unsigned char type) {
  art_inner_t *p = art_inner_new(a, type);
  art_node_t **ref;
  art_node_t *child;
  unsigned start = 0;
  if (p == NULL)
    return NULL;
  ref = art_ref(a, &n->node);
  *p = *n;
  p->node.type = type;
  p->count = 0;
  while ((child = art_next_child(n, start)) != NULL) {
    start = (unsigned)child->edge + 1;
    art_store(p, child->edge, child);
  }
  if (p->terminal != NULL)
    p->terminal->parent = &p->node;
  *ref = &p->node;
  a->deallocator(n); /* prefix ownership moved to p */
  return p;
}

static ds_status_t art_add(ds_art_t *a, art_inner_t *n, unsigned char edge,
                           art_node_t *leaf) {
  unsigned capacity = n->node.type == ART_NODE4    ? 4U
                      : n->node.type == ART_NODE16 ? 16U
                      : n->node.type == ART_NODE48 ? 48U
                                                   : 256U;
  if (n->count == capacity) {
    n = art_resize(a, n, (unsigned char)(n->node.type + 1));
    if (n == NULL)
      return DS_ERR_ALLOC;
  }
  art_store(n, edge, leaf);
  return DS_OK;
}

static size_t art_shared(art_inner_t *n, const unsigned char *key, size_t len,
                         size_t depth) {
  size_t shared = 0;
  size_t limit = len - depth;
  const unsigned char *p = art_prefix(n);
  if (limit > n->prefix_len)
    limit = n->prefix_len;
  while (shared < limit && p[shared] == key[depth + shared])
    shared++;
  return shared;
}

ds_art_t *ds_art_create(void) { return ds_art_create_alloc(malloc, free); }

ds_art_t *ds_art_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *)) {
  ds_art_t *a;
  if (allocator == NULL || deallocator == NULL)
    return NULL;
  a = (ds_art_t *)allocator(sizeof(*a));
  if (a != NULL) {
    a->root = NULL;
    a->size = 0;
    a->allocator = allocator;
    a->deallocator = deallocator;
  }
  return a;
}

ds_status_t ds_art_insert_ex(ds_art_t *a, const unsigned char *key, size_t len,
                             void *value) {
  art_node_t **ref;
  size_t depth = 0;
  if (a == NULL || (key == NULL && len != 0))
    return DS_ERR_NULL;
  if (len > (size_t)-1 - sizeof(art_leaf_t))
    return DS_ERR_OVERFLOW;
  ref = &a->root;
  for (;;) {
    art_node_t *node = *ref;
    art_leaf_t *fresh;
    art_inner_t *n;
    size_t shared;
    if (node == NULL) {
      if (a->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      fresh = art_leaf_new(a, key, len, value);
      if (fresh == NULL)
        return DS_ERR_ALLOC;
      *ref = &fresh->node;
      a->size++;
      return DS_OK;
    }
    if (node->type == ART_LEAF) {
      art_leaf_t *old = (art_leaf_t *)(void *)node;
      size_t limit = old->len < len ? old->len : len;
      if (art_equal(old, key, len)) {
        old->value = value;
        return DS_EXISTS;
      }
      if (a->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      shared = 0;
      while (depth + shared < limit &&
             old->key[depth + shared] == key[depth + shared])
        shared++;
      n = art_inner_new(a, ART_NODE4);
      if (n == NULL)
        return DS_ERR_ALLOC;
      if (!art_prefix_init(a, n, old->key + depth, shared)) {
        art_inner_free(a, n);
        return DS_ERR_ALLOC;
      }
      fresh = art_leaf_new(a, key, len, value);
      if (fresh == NULL) {
        art_inner_free(a, n);
        return DS_ERR_ALLOC;
      }
      n->node.parent = node->parent;
      n->node.edge = node->edge;
      if (old->len == depth + shared)
        art_terminal(n, node);
      else
        art_store(n, old->key[depth + shared], node);
      if (len == depth + shared)
        art_terminal(n, &fresh->node);
      else
        art_store(n, key[depth + shared], &fresh->node);
      *ref = &n->node;
      a->size++;
      return DS_OK;
    }
    n = (art_inner_t *)(void *)node;
    shared = art_shared(n, key, len, depth);
    if (shared != n->prefix_len) {
      art_inner_t *split;
      unsigned char old_edge = art_prefix(n)[shared];
      if (a->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      split = art_inner_new(a, ART_NODE4);
      if (split == NULL)
        return DS_ERR_ALLOC;
      if (!art_prefix_init(a, split, art_prefix(n), shared)) {
        art_inner_free(a, split);
        return DS_ERR_ALLOC;
      }
      fresh = art_leaf_new(a, key, len, value);
      if (fresh == NULL) {
        art_inner_free(a, split);
        return DS_ERR_ALLOC;
      }
      split->node.parent = node->parent;
      split->node.edge = node->edge;
      art_prefix_drop(a, n, shared + 1);
      art_store(split, old_edge, node);
      if (len == depth + shared)
        art_terminal(split, &fresh->node);
      else
        art_store(split, key[depth + shared], &fresh->node);
      *ref = &split->node;
      a->size++;
      return DS_OK;
    }
    depth += shared;
    if (depth == len) {
      if (n->terminal != NULL) {
        ((art_leaf_t *)(void *)n->terminal)->value = value;
        return DS_EXISTS;
      }
      if (a->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      fresh = art_leaf_new(a, key, len, value);
      if (fresh == NULL)
        return DS_ERR_ALLOC;
      art_terminal(n, &fresh->node);
      a->size++;
      return DS_OK;
    }
    ref = art_child(n, key[depth]);
    if (ref == NULL) {
      ds_status_t st;
      if (a->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      fresh = art_leaf_new(a, key, len, value);
      if (fresh == NULL)
        return DS_ERR_ALLOC;
      st = art_add(a, n, key[depth], &fresh->node);
      if (st != DS_OK) {
        a->deallocator(fresh);
        return st;
      }
      a->size++;
      return DS_OK;
    }
    depth++;
  }
}

bool ds_art_insert(ds_art_t *a, const unsigned char *key, size_t len,
                   void *value) {
  return ds_art_insert_ex(a, key, len, value) == DS_OK;
}

static art_leaf_t *art_search(ds_art_t *a, const unsigned char *key,
                              size_t len) {
  art_node_t *node;
  size_t depth = 0;
  if (a == NULL || (key == NULL && len != 0))
    return NULL;
  node = a->root;
  while (node != NULL) {
    art_inner_t *n;
    art_node_t **child;
    if (node->type == ART_LEAF) {
      art_leaf_t *leaf = (art_leaf_t *)(void *)node;
      return art_equal(leaf, key, len) ? leaf : NULL;
    }
    n = (art_inner_t *)(void *)node;
    if (art_shared(n, key, len, depth) != n->prefix_len)
      return NULL;
    depth += n->prefix_len;
    if (depth == len)
      return (art_leaf_t *)(void *)n->terminal;
    child = art_child(n, key[depth++]);
    node = child == NULL ? NULL : *child;
  }
  return NULL;
}

bool ds_art_contains(ds_art_t *a, const unsigned char *key, size_t len) {
  return art_search(a, key, len) != NULL;
}

void *ds_art_get(ds_art_t *a, const unsigned char *key, size_t len) {
  art_leaf_t *leaf = art_search(a, key, len);
  return leaf == NULL ? NULL : leaf->value;
}

static void art_erase_child(art_inner_t *n, unsigned char edge) {
  unsigned i;
  if (n->node.type == ART_NODE4 || n->node.type == ART_NODE16) {
    unsigned char *keys;
    art_node_t **children;
    if (n->node.type == ART_NODE4) {
      art_node4_t *p = (art_node4_t *)(void *)n;
      keys = p->keys;
      children = p->children;
    } else {
      art_node16_t *p = (art_node16_t *)(void *)n;
      keys = p->keys;
      children = p->children;
    }
    for (i = 0; i < n->count && keys[i] != edge; i++) {
    }
    for (; i + 1 < n->count; i++) {
      keys[i] = keys[i + 1];
      children[i] = children[i + 1];
    }
  } else if (n->node.type == ART_NODE48) {
    art_node48_t *p = (art_node48_t *)(void *)n;
    p->children[p->slots[edge] - 1] = NULL;
    p->slots[edge] = 0;
  } else {
    ((art_node256_t *)(void *)n)->children[edge] = NULL;
  }
  n->count--;
}

static void art_replace(ds_art_t *a, art_node_t *old, art_node_t *replacement) {
  art_node_t **ref = art_ref(a, old);
  if (replacement != NULL) {
    replacement->parent = old->parent;
    replacement->edge = old->edge;
  }
  *ref = replacement;
}

/* Optional compression after a deletion. If the joined prefix needs
 * memory and allocation fails, retain the valid unary node. */
static bool art_collapse(ds_art_t *a, art_inner_t *n, art_node_t *child) {
  if (child->type != ART_LEAF) {
    art_inner_t *c = (art_inner_t *)(void *)child;
    unsigned char local[ART_INLINE_PREFIX];
    unsigned char *joined = local;
    size_t len;
    if (n->prefix_len == (size_t)-1 ||
        c->prefix_len > (size_t)-1 - n->prefix_len - 1)
      return false;
    len = n->prefix_len + 1 + c->prefix_len;
    if (len > ART_INLINE_PREFIX) {
      joined = (unsigned char *)a->allocator(len);
      if (joined == NULL)
        return false;
    }
    if (n->prefix_len != 0)
      memcpy(joined, art_prefix(n), n->prefix_len);
    joined[n->prefix_len] = child->edge;
    if (c->prefix_len != 0)
      memcpy(joined + n->prefix_len + 1, art_prefix(c), c->prefix_len);
    if (c->large != NULL)
      a->deallocator(c->large);
    c->large = joined == local ? NULL : joined;
    if (joined == local)
      memcpy(c->small, local, len);
    c->prefix_len = len;
  }
  art_replace(a, &n->node, child);
  art_inner_free(a, n);
  return true;
}

bool ds_art_remove(ds_art_t *a, const unsigned char *key, size_t len,
                   void **out_value) {
  art_leaf_t *leaf = art_search(a, key, len);
  art_node_t *parent;
  if (leaf == NULL)
    return false;
  if (out_value != NULL)
    *out_value = leaf->value;
  parent = leaf->node.parent;
  if (parent == NULL)
    a->root = NULL;
  else {
    art_inner_t *n = (art_inner_t *)(void *)parent;
    if (n->terminal == &leaf->node)
      n->terminal = NULL;
    else
      art_erase_child(n, leaf->node.edge);
  }
  a->deallocator(leaf);
  a->size--;
  while (parent != NULL) {
    art_inner_t *n = (art_inner_t *)(void *)parent;
    art_node_t *above = parent->parent;
    if (n->count == 0) {
      if (n->terminal != NULL)
        art_replace(a, parent, n->terminal);
      else if (above == NULL)
        a->root = NULL;
      else
        art_erase_child((art_inner_t *)(void *)above, parent->edge);
      art_inner_free(a, n);
    } else if (n->count == 1 && n->terminal == NULL &&
               art_collapse(a, n, art_next_child(n, 0))) {
      /* collapsed; above still names the parent to normalize next */
    } else {
      unsigned char type = n->node.type;
      if (type == ART_NODE256 && n->count <= 37)
        type = ART_NODE48;
      else if (type == ART_NODE48 && n->count <= 12)
        type = ART_NODE16;
      else if (type == ART_NODE16 && n->count <= 3)
        type = ART_NODE4;
      if (type != n->node.type)
        (void)art_resize(a, n, type);
    }
    parent = above;
  }
  return true;
}

size_t ds_art_size(ds_art_t *a) { return a == NULL ? 0 : a->size; }
bool ds_art_is_empty(ds_art_t *a) { return ds_art_size(a) == 0; }

static art_node_t *art_first(art_inner_t *n) {
  return n->terminal != NULL ? n->terminal : art_next_child(n, 0);
}

static art_node_t *art_successor(art_node_t *node) {
  while (node->parent != NULL) {
    art_inner_t *p = (art_inner_t *)(void *)node->parent;
    unsigned start = p->terminal == node ? 0U : (unsigned)node->edge + 1;
    art_node_t *next = art_next_child(p, start);
    if (next != NULL)
      return next;
    node = &p->node;
  }
  return NULL;
}

void ds_art_walk(ds_art_t *a,
                 void (*callback)(const unsigned char *, size_t, void *,
                                  void *),
                 void *user) {
  art_node_t *node;
  if (a == NULL || callback == NULL)
    return;
  node = a->root;
  while (node != NULL) {
    if (node->type == ART_LEAF) {
      art_leaf_t *leaf = (art_leaf_t *)(void *)node;
      callback(leaf->key, leaf->len, leaf->value, user);
      node = art_successor(node);
    } else {
      art_node_t *first = art_first((art_inner_t *)(void *)node);
      node = first != NULL ? first : art_successor(node);
    }
  }
}

void ds_art_destroy(ds_art_t *a, void (*destroy_value)(void *)) {
  art_node_t *node;
  if (a == NULL)
    return;
  node = a->root;
  while (node != NULL) {
    art_node_t *parent;
    if (node->type != ART_LEAF) {
      art_node_t *first = art_first((art_inner_t *)(void *)node);
      if (first != NULL) {
        node = first;
        continue;
      }
    }
    parent = node->parent;
    if (parent != NULL) {
      art_inner_t *p = (art_inner_t *)(void *)parent;
      if (p->terminal == node)
        p->terminal = NULL;
      else
        art_erase_child(p, node->edge);
    }
    if (node->type == ART_LEAF) {
      art_leaf_t *leaf = (art_leaf_t *)(void *)node;
      if (destroy_value != NULL)
        destroy_value(leaf->value);
      a->deallocator(node);
    } else
      art_inner_free(a, (art_inner_t *)(void *)node);
    node = parent;
  }
  a->deallocator(a);
}
