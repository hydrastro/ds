#include "art.h"
#include <stdlib.h>
#include <string.h>

#define DS_ART_NODE4 1
#define DS_ART_NODE16 2
#define DS_ART_NODE48 3
#define DS_ART_NODE256 4
#define DS_ART_LEAF 5
#define DS_ART_MAX_PREFIX 10

typedef struct art_node {
  unsigned char type;
  unsigned char num_children;
  unsigned char prefix_len;
  unsigned char prefix[DS_ART_MAX_PREFIX];
} ds_art_node_t;

typedef struct art_node4 {
  ds_art_node_t base;
  unsigned char keys[4];
  ds_art_node_t *children[4];
} ds_art_node4_t;

typedef struct art_node16 {
  ds_art_node_t base;
  unsigned char keys[16];
  ds_art_node_t *children[16];
} ds_art_node16_t;

typedef struct art_node48 {
  ds_art_node_t base;
  unsigned char child_index[256]; /* 0 = empty, else 1-based slot */
  ds_art_node_t *children[48];
} ds_art_node48_t;

typedef struct art_node256 {
  ds_art_node_t base;
  ds_art_node_t *children[256];
} ds_art_node256_t;

typedef struct art_leaf {
  ds_art_node_t base; /* type = DS_ART_LEAF; other header fields unused */
  void *value;
  size_t key_length;
  unsigned char key[1]; /* flexible: allocated key_length bytes */
} ds_art_leaf_t;

struct art {
  ds_art_node_t *root;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
};

static bool FUNC(art_is_leaf)(ds_art_node_t *node) {
  return node != NULL && node->type == DS_ART_LEAF;
}

ds_art_t *FUNC(ds_art_create)(void) {
  return FUNC(ds_art_create_alloc)(malloc, free);
}

ds_art_t *FUNC(ds_art_create_alloc)(void *(*allocator)(size_t),
                                    void (*deallocator)(void *)) {
  ds_art_t *art = (ds_art_t *)allocator(sizeof(ds_art_t));
  art->root = NULL;
  art->size = 0;
  art->allocator = allocator;
  art->deallocator = deallocator;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(art);
#endif
  return art;
}

static ds_art_node_t *FUNC(art_alloc_node)(ds_art_t *art, unsigned char type) {
  ds_art_node_t *node;
  size_t sz;
  switch (type) {
  case DS_ART_NODE4:
    sz = sizeof(ds_art_node4_t);
    break;
  case DS_ART_NODE16:
    sz = sizeof(ds_art_node16_t);
    break;
  case DS_ART_NODE48:
    sz = sizeof(ds_art_node48_t);
    break;
  default:
    sz = sizeof(ds_art_node256_t);
    break;
  }
  node = (ds_art_node_t *)art->allocator(sz);
  memset(node, 0, sz);
  node->type = type;
  node->num_children = 0;
  node->prefix_len = 0;
  return node;
}

static ds_art_leaf_t *FUNC(art_make_leaf)(ds_art_t *art,
                                          const unsigned char *key,
                                          size_t key_length, void *value) {
  ds_art_leaf_t *leaf =
      (ds_art_leaf_t *)art->allocator(sizeof(ds_art_leaf_t) + key_length);
  leaf->base.type = DS_ART_LEAF;
  leaf->base.num_children = 0;
  leaf->base.prefix_len = 0;
  leaf->value = value;
  leaf->key_length = key_length;
  if (key_length > 0) {
    memcpy(leaf->key, key, key_length);
  }
  return leaf;
}

static bool FUNC(art_leaf_matches)(ds_art_leaf_t *leaf,
                                   const unsigned char *key,
                                   size_t key_length) {
  if (leaf->key_length != key_length) {
    return false;
  }
  return memcmp(leaf->key, key, key_length) == 0;
}

/* find pointer to the child slot for byte c, or NULL */
static ds_art_node_t **FUNC(art_find_child)(ds_art_node_t *node,
                                            unsigned char c) {
  int i;
  switch (node->type) {
  case DS_ART_NODE4: {
    ds_art_node4_t *n = (ds_art_node4_t *)node;
    for (i = 0; i < node->num_children; i++) {
      if (n->keys[i] == c) {
        return &n->children[i];
      }
    }
    break;
  }
  case DS_ART_NODE16: {
    ds_art_node16_t *n = (ds_art_node16_t *)node;
    for (i = 0; i < node->num_children; i++) {
      if (n->keys[i] == c) {
        return &n->children[i];
      }
    }
    break;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    unsigned char idx = n->child_index[c];
    if (idx != 0) {
      return &n->children[idx - 1];
    }
    break;
  }
  case DS_ART_NODE256: {
    ds_art_node256_t *n = (ds_art_node256_t *)node;
    if (n->children[c] != NULL) {
      return &n->children[c];
    }
    break;
  }
  default:
    break;
  }
  return NULL;
}

static void FUNC(art_add_child)(ds_art_t *art, ds_art_node_t *node,
                                ds_art_node_t **ref, unsigned char c,
                                ds_art_node_t *child);

/* grow node to the next larger type, returning the new node */
static ds_art_node_t *FUNC(art_grow)(ds_art_t *art, ds_art_node_t *node) {
  switch (node->type) {
  case DS_ART_NODE4: {
    ds_art_node4_t *n = (ds_art_node4_t *)node;
    ds_art_node16_t *m =
        (ds_art_node16_t *)FUNC(art_alloc_node)(art, DS_ART_NODE16);
    int i;
    m->base.num_children = node->num_children;
    m->base.prefix_len = node->prefix_len;
    memcpy(m->base.prefix, node->prefix, DS_ART_MAX_PREFIX);
    for (i = 0; i < node->num_children; i++) {
      m->keys[i] = n->keys[i];
      m->children[i] = n->children[i];
    }
    art->deallocator(node);
    return (ds_art_node_t *)m;
  }
  case DS_ART_NODE16: {
    ds_art_node16_t *n = (ds_art_node16_t *)node;
    ds_art_node48_t *m =
        (ds_art_node48_t *)FUNC(art_alloc_node)(art, DS_ART_NODE48);
    int i;
    m->base.num_children = node->num_children;
    m->base.prefix_len = node->prefix_len;
    memcpy(m->base.prefix, node->prefix, DS_ART_MAX_PREFIX);
    for (i = 0; i < node->num_children; i++) {
      m->children[i] = n->children[i];
      m->child_index[n->keys[i]] = (unsigned char)(i + 1);
    }
    art->deallocator(node);
    return (ds_art_node_t *)m;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    ds_art_node256_t *m =
        (ds_art_node256_t *)FUNC(art_alloc_node)(art, DS_ART_NODE256);
    int i;
    m->base.num_children = node->num_children;
    m->base.prefix_len = node->prefix_len;
    memcpy(m->base.prefix, node->prefix, DS_ART_MAX_PREFIX);
    for (i = 0; i < 256; i++) {
      if (n->child_index[i] != 0) {
        m->children[i] = n->children[n->child_index[i] - 1];
      }
    }
    art->deallocator(node);
    return (ds_art_node_t *)m;
  }
  default:
    return node;
  }
}

static void FUNC(art_add_child)(ds_art_t *art, ds_art_node_t *node,
                                ds_art_node_t **ref, unsigned char c,
                                ds_art_node_t *child) {
  switch (node->type) {
  case DS_ART_NODE4: {
    ds_art_node4_t *n = (ds_art_node4_t *)node;
    if (node->num_children < 4) {
      int i;
      int pos = 0;
      while (pos < node->num_children && n->keys[pos] < c) {
        pos++;
      }
      for (i = node->num_children; i > pos; i--) {
        n->keys[i] = n->keys[i - 1];
        n->children[i] = n->children[i - 1];
      }
      n->keys[pos] = c;
      n->children[pos] = child;
      node->num_children++;
    } else {
      ds_art_node_t *grown = FUNC(art_grow)(art, node);
      *ref = grown;
      FUNC(art_add_child)(art, grown, ref, c, child);
    }
    break;
  }
  case DS_ART_NODE16: {
    ds_art_node16_t *n = (ds_art_node16_t *)node;
    if (node->num_children < 16) {
      int i;
      int pos = 0;
      while (pos < node->num_children && n->keys[pos] < c) {
        pos++;
      }
      for (i = node->num_children; i > pos; i--) {
        n->keys[i] = n->keys[i - 1];
        n->children[i] = n->children[i - 1];
      }
      n->keys[pos] = c;
      n->children[pos] = child;
      node->num_children++;
    } else {
      ds_art_node_t *grown = FUNC(art_grow)(art, node);
      *ref = grown;
      FUNC(art_add_child)(art, grown, ref, c, child);
    }
    break;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    if (node->num_children < 48) {
      int pos = 0;
      while (pos < 48 && n->children[pos] != NULL) {
        pos++;
      }
      if (pos < 48) {
        n->children[pos] = child;
        n->child_index[c] = (unsigned char)(pos + 1);
        node->num_children++;
      }
    } else {
      ds_art_node_t *grown = FUNC(art_grow)(art, node);
      *ref = grown;
      FUNC(art_add_child)(art, grown, ref, c, child);
    }
    break;
  }
  default: {
    ds_art_node256_t *n = (ds_art_node256_t *)node;
    n->children[c] = child;
    node->num_children++;
    break;
  }
  }
}

static size_t FUNC(art_min_size)(size_t la, size_t lb) {
  size_t m = la < lb ? la : lb;
  return m;
}

bool FUNC(ds_art_insert)(ds_art_t *art, const unsigned char *key,
                         size_t key_length, void *value) {
  ds_art_node_t **ref;
  size_t depth = 0;
  bool added = true;
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  ref = &art->root;
  for (;;) {
    ds_art_node_t *node = *ref;
    if (node == NULL) {
      *ref = (ds_art_node_t *)FUNC(art_make_leaf)(art, key, key_length, value);
      art->size += 1;
      break;
    }
    if (FUNC(art_is_leaf)(node)) {
      ds_art_leaf_t *leaf = (ds_art_leaf_t *)node;
      ds_art_node_t *new_node;
      ds_art_leaf_t *new_leaf;
      size_t prefix;
      size_t i;
      if (FUNC(art_leaf_matches)(leaf, key, key_length)) {
        leaf->value = value;
        added = false;
        break;
      }
      /* split: build a Node4 holding the two divergent leaves */
      new_node = FUNC(art_alloc_node)(art, DS_ART_NODE4);
      prefix = 0;
      {
        size_t maxp = FUNC(art_min_size)(leaf->key_length, key_length);
        for (i = depth; i < maxp; i++) {
          if (leaf->key[i] != key[i]) {
            break;
          }
          prefix++;
        }
      }
      new_node->prefix_len =
          (unsigned char)(prefix > DS_ART_MAX_PREFIX ? DS_ART_MAX_PREFIX
                                                     : prefix);
      for (i = 0; i < new_node->prefix_len; i++) {
        new_node->prefix[i] = key[depth + i];
      }
      new_leaf = FUNC(art_make_leaf)(art, key, key_length, value);
      {
        unsigned char leaf_byte =
            depth + prefix < leaf->key_length ? leaf->key[depth + prefix] : 0;
        unsigned char key_byte =
            depth + prefix < key_length ? key[depth + prefix] : 0;
        FUNC(art_add_child)
        (art, new_node, ref, leaf_byte, (ds_art_node_t *)leaf);
        *ref = new_node;
        FUNC(art_add_child)
        (art, new_node, ref, key_byte, (ds_art_node_t *)new_leaf);
      }
      art->size += 1;
      break;
    }
    /* internal node: match the compressed prefix */
    if (node->prefix_len > 0) {
      size_t max = node->prefix_len < (key_length - depth)
                       ? node->prefix_len
                       : (key_length - depth);
      size_t shared = 0;
      while (shared < max && node->prefix[shared] == key[depth + shared]) {
        shared++;
      }
      if (shared < node->prefix_len) {
        /* prefix diverges: split this node */
        ds_art_node_t *new_node = FUNC(art_alloc_node)(art, DS_ART_NODE4);
        ds_art_leaf_t *new_leaf =
            FUNC(art_make_leaf)(art, key, key_length, value);
        size_t i;
        new_node->prefix_len = (unsigned char)shared;
        for (i = 0; i < shared; i++) {
          new_node->prefix[i] = node->prefix[i];
        }
        {
          unsigned char old_byte = node->prefix[shared];
          unsigned char new_byte =
              depth + shared < key_length ? key[depth + shared] : 0;
          unsigned char remaining =
              (unsigned char)(node->prefix_len - shared - 1);
          /* shift the old node's prefix past the split point */
          memmove(node->prefix, node->prefix + shared + 1, remaining);
          node->prefix_len = remaining;
          *ref = new_node;
          FUNC(art_add_child)(art, new_node, ref, old_byte, node);
          FUNC(art_add_child)
          (art, new_node, ref, new_byte, (ds_art_node_t *)new_leaf);
        }
        art->size += 1;
        break;
      }
      depth += node->prefix_len;
    }
    {
      unsigned char c = depth < key_length ? key[depth] : 0;
      ds_art_node_t **child = FUNC(art_find_child)(node, c);
      if (child != NULL) {
        ref = child;
        depth += 1;
      } else {
        ds_art_leaf_t *new_leaf =
            FUNC(art_make_leaf)(art, key, key_length, value);
        FUNC(art_add_child)(art, node, ref, c, (ds_art_node_t *)new_leaf);
        art->size += 1;
        break;
      }
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
  return added;
}

static ds_art_leaf_t *FUNC(art_search)(ds_art_t *art, const unsigned char *key,
                                       size_t key_length) {
  ds_art_node_t *node = art->root;
  size_t depth = 0;
  while (node != NULL) {
    if (FUNC(art_is_leaf)(node)) {
      ds_art_leaf_t *leaf = (ds_art_leaf_t *)node;
      if (FUNC(art_leaf_matches)(leaf, key, key_length)) {
        return leaf;
      }
      return NULL;
    }
    if (node->prefix_len > 0) {
      size_t max = node->prefix_len < (key_length - depth)
                       ? node->prefix_len
                       : (key_length - depth);
      size_t shared = 0;
      while (shared < max && node->prefix[shared] == key[depth + shared]) {
        shared++;
      }
      if (shared < node->prefix_len) {
        return NULL;
      }
      depth += node->prefix_len;
    }
    {
      unsigned char c = depth < key_length ? key[depth] : 0;
      ds_art_node_t **child = FUNC(art_find_child)(node, c);
      if (child == NULL) {
        return NULL;
      }
      node = *child;
      depth += 1;
    }
  }
  return NULL;
}

bool FUNC(ds_art_contains)(ds_art_t *art, const unsigned char *key,
                           size_t key_length) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  result = FUNC(art_search)(art, key, key_length) != NULL;
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
  return result;
}

void *FUNC(ds_art_get)(ds_art_t *art, const unsigned char *key,
                       size_t key_length) {
  ds_art_leaf_t *leaf;
  void *result = NULL;
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  leaf = FUNC(art_search)(art, key, key_length);
  if (leaf != NULL) {
    result = leaf->value;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
  return result;
}

/* remove the child for byte c from a node; shrink if warranted */
static void FUNC(art_remove_child)(ds_art_t *art, ds_art_node_t *node,
                                   ds_art_node_t **ref, unsigned char c);

static ds_art_node_t *FUNC(art_shrink)(ds_art_t *art, ds_art_node_t *node) {
  switch (node->type) {
  case DS_ART_NODE16: {
    ds_art_node16_t *n = (ds_art_node16_t *)node;
    ds_art_node4_t *m =
        (ds_art_node4_t *)FUNC(art_alloc_node)(art, DS_ART_NODE4);
    int i;
    m->base.num_children = node->num_children;
    m->base.prefix_len = node->prefix_len;
    memcpy(m->base.prefix, node->prefix, DS_ART_MAX_PREFIX);
    for (i = 0; i < node->num_children; i++) {
      m->keys[i] = n->keys[i];
      m->children[i] = n->children[i];
    }
    art->deallocator(node);
    return (ds_art_node_t *)m;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    ds_art_node16_t *m =
        (ds_art_node16_t *)FUNC(art_alloc_node)(art, DS_ART_NODE16);
    int i;
    int pos = 0;
    m->base.prefix_len = node->prefix_len;
    memcpy(m->base.prefix, node->prefix, DS_ART_MAX_PREFIX);
    for (i = 0; i < 256; i++) {
      if (n->child_index[i] != 0) {
        m->keys[pos] = (unsigned char)i;
        m->children[pos] = n->children[n->child_index[i] - 1];
        pos++;
      }
    }
    m->base.num_children = (unsigned char)pos;
    art->deallocator(node);
    return (ds_art_node_t *)m;
  }
  case DS_ART_NODE256: {
    ds_art_node256_t *n = (ds_art_node256_t *)node;
    ds_art_node48_t *m =
        (ds_art_node48_t *)FUNC(art_alloc_node)(art, DS_ART_NODE48);
    int i;
    int pos = 0;
    m->base.prefix_len = node->prefix_len;
    memcpy(m->base.prefix, node->prefix, DS_ART_MAX_PREFIX);
    for (i = 0; i < 256; i++) {
      if (n->children[i] != NULL) {
        m->children[pos] = n->children[i];
        m->child_index[i] = (unsigned char)(pos + 1);
        pos++;
      }
    }
    m->base.num_children = (unsigned char)pos;
    art->deallocator(node);
    return (ds_art_node_t *)m;
  }
  default:
    return node;
  }
}

static void FUNC(art_remove_child)(ds_art_t *art, ds_art_node_t *node,
                                   ds_art_node_t **ref, unsigned char c) {
  switch (node->type) {
  case DS_ART_NODE4:
  case DS_ART_NODE16: {
    /* both share the keys[]/children[] parallel-array layout */
    unsigned char *keys;
    ds_art_node_t **children;
    int i;
    int pos = -1;
    if (node->type == DS_ART_NODE4) {
      ds_art_node4_t *n = (ds_art_node4_t *)node;
      keys = n->keys;
      children = n->children;
    } else {
      ds_art_node16_t *n = (ds_art_node16_t *)node;
      keys = n->keys;
      children = n->children;
    }
    for (i = 0; i < node->num_children; i++) {
      if (keys[i] == c) {
        pos = i;
        break;
      }
    }
    if (pos < 0) {
      return;
    }
    for (i = pos; i + 1 < node->num_children; i++) {
      keys[i] = keys[i + 1];
      children[i] = children[i + 1];
    }
    node->num_children--;
    break;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    unsigned char idx = n->child_index[c];
    if (idx == 0) {
      return;
    }
    n->children[idx - 1] = NULL;
    n->child_index[c] = 0;
    node->num_children--;
    break;
  }
  default: {
    ds_art_node256_t *n = (ds_art_node256_t *)node;
    if (n->children[c] == NULL) {
      return;
    }
    n->children[c] = NULL;
    node->num_children--;
    break;
  }
  }
  /* shrink to a smaller layout when sparse enough */
  if (node->type == DS_ART_NODE16 && node->num_children <= 3) {
    *ref = FUNC(art_shrink)(art, node);
  } else if (node->type == DS_ART_NODE48 && node->num_children <= 12) {
    *ref = FUNC(art_shrink)(art, node);
  } else if (node->type == DS_ART_NODE256 && node->num_children <= 37) {
    *ref = FUNC(art_shrink)(art, node);
  }
}

bool FUNC(ds_art_remove)(ds_art_t *art, const unsigned char *key,
                         size_t key_length, void **out_value) {
  ds_art_node_t **ref;
  ds_art_node_t **parent_ref = NULL;
  ds_art_node_t *parent = NULL;
  unsigned char parent_byte = 0;
  size_t depth = 0;
  bool removed = false;
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  ref = &art->root;
  while (*ref != NULL) {
    ds_art_node_t *node = *ref;
    if (FUNC(art_is_leaf)(node)) {
      ds_art_leaf_t *leaf = (ds_art_leaf_t *)node;
      if (FUNC(art_leaf_matches)(leaf, key, key_length)) {
        if (out_value != NULL) {
          *out_value = leaf->value;
        }
        art->deallocator(leaf);
        *ref = NULL;
        if (parent != NULL) {
          FUNC(art_remove_child)(art, parent, parent_ref, parent_byte);
        }
        art->size -= 1;
        removed = true;
      }
      break;
    }
    if (node->prefix_len > 0) {
      size_t max = node->prefix_len < (key_length - depth)
                       ? node->prefix_len
                       : (key_length - depth);
      size_t shared = 0;
      while (shared < max && node->prefix[shared] == key[depth + shared]) {
        shared++;
      }
      if (shared < node->prefix_len) {
        break;
      }
      depth += node->prefix_len;
    }
    {
      unsigned char c = depth < key_length ? key[depth] : 0;
      ds_art_node_t **child = FUNC(art_find_child)(node, c);
      if (child == NULL) {
        break;
      }
      parent = node;
      parent_ref = ref;
      parent_byte = c;
      ref = child;
      depth += 1;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
  return removed;
}

size_t FUNC(ds_art_size)(ds_art_t *art) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  result = art->size;
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
  return result;
}

bool FUNC(ds_art_is_empty)(ds_art_t *art) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  result = art->size == 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
  return result;
}

static void FUNC(art_walk_node)(ds_art_node_t *node,
                                void (*callback)(const unsigned char *, size_t,
                                                 void *, void *),
                                void *user) {
  int i;
  if (node == NULL) {
    return;
  }
  if (FUNC(art_is_leaf)(node)) {
    ds_art_leaf_t *leaf = (ds_art_leaf_t *)node;
    callback(leaf->key, leaf->key_length, leaf->value, user);
    return;
  }
  switch (node->type) {
  case DS_ART_NODE4: {
    ds_art_node4_t *n = (ds_art_node4_t *)node;
    for (i = 0; i < node->num_children; i++) {
      FUNC(art_walk_node)(n->children[i], callback, user);
    }
    break;
  }
  case DS_ART_NODE16: {
    ds_art_node16_t *n = (ds_art_node16_t *)node;
    for (i = 0; i < node->num_children; i++) {
      FUNC(art_walk_node)(n->children[i], callback, user);
    }
    break;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    for (i = 0; i < 256; i++) {
      if (n->child_index[i] != 0) {
        FUNC(art_walk_node)(n->children[n->child_index[i] - 1], callback, user);
      }
    }
    break;
  }
  default: {
    ds_art_node256_t *n = (ds_art_node256_t *)node;
    for (i = 0; i < 256; i++) {
      if (n->children[i] != NULL) {
        FUNC(art_walk_node)(n->children[i], callback, user);
      }
    }
    break;
  }
  }
}

void FUNC(ds_art_walk)(ds_art_t *art,
                       void (*callback)(const unsigned char *, size_t, void *,
                                        void *),
                       void *user) {
#ifdef DS_THREAD_SAFE
  LOCK(art);
#endif
  FUNC(art_walk_node)(art->root, callback, user);
#ifdef DS_THREAD_SAFE
  UNLOCK(art);
#endif
}

static void FUNC(art_destroy_node)(ds_art_t *art, ds_art_node_t *node,
                                   void (*destroy_value)(void *)) {
  int i;
  if (node == NULL) {
    return;
  }
  if (FUNC(art_is_leaf)(node)) {
    ds_art_leaf_t *leaf = (ds_art_leaf_t *)node;
    if (destroy_value != NULL) {
      destroy_value(leaf->value);
    }
    art->deallocator(leaf);
    return;
  }
  switch (node->type) {
  case DS_ART_NODE4: {
    ds_art_node4_t *n = (ds_art_node4_t *)node;
    for (i = 0; i < node->num_children; i++) {
      FUNC(art_destroy_node)(art, n->children[i], destroy_value);
    }
    break;
  }
  case DS_ART_NODE16: {
    ds_art_node16_t *n = (ds_art_node16_t *)node;
    for (i = 0; i < node->num_children; i++) {
      FUNC(art_destroy_node)(art, n->children[i], destroy_value);
    }
    break;
  }
  case DS_ART_NODE48: {
    ds_art_node48_t *n = (ds_art_node48_t *)node;
    for (i = 0; i < 256; i++) {
      if (n->child_index[i] != 0) {
        FUNC(art_destroy_node)
        (art, n->children[n->child_index[i] - 1], destroy_value);
      }
    }
    break;
  }
  default: {
    ds_art_node256_t *n = (ds_art_node256_t *)node;
    for (i = 0; i < 256; i++) {
      if (n->children[i] != NULL) {
        FUNC(art_destroy_node)(art, n->children[i], destroy_value);
      }
    }
    break;
  }
  }
  art->deallocator(node);
}

void FUNC(ds_art_destroy)(ds_art_t *art, void (*destroy_value)(void *)) {
  FUNC(art_destroy_node)(art, art->root, destroy_value);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(art);
#endif
  art->deallocator(art);
}
