#include <dsa/structures/hamt.h>
#include <stdlib.h>
#include <string.h>

#define DS_HAMT_BITS 5
#define DS_HAMT_MASK 31U
#define DS_HAMT_MAX_LEVEL 6 /* 6 * 5 = 30 bits of a 32-bit hash */

struct hamt_node {
  bool is_leaf;
  /* branch fields */
  unsigned long bitmap;      /* occupancy of the 32 slots */
  ds_hamt_node_t **children; /* compact: only present slots */
  size_t num_children;
  /* leaf fields */
  unsigned char *key;
  size_t key_length;
  void *value;
  unsigned long hash;
  struct hamt_node *next; /* collision chain at terminal level */
};

static unsigned long hamt_hash(const void *key, size_t length) {
  const unsigned char *bytes = (const unsigned char *)key;
  unsigned long hash = 2166136261UL;
  size_t i;
  for (i = 0; i < length; i++) {
    hash ^= (unsigned long)bytes[i];
    hash *= 16777619UL;
    hash &= 0xFFFFFFFFUL;
  }
  return hash;
}

static size_t hamt_popcount(unsigned long x) {
  size_t count = 0;
  while (x != 0UL) {
    x &= x - 1UL;
    count += 1;
  }
  return count;
}

static size_t hamt_slot(unsigned long hash, int level) {
  return (size_t)((hash >> (level * DS_HAMT_BITS)) & DS_HAMT_MASK);
}

/* index into the compact child array for a given slot */
static size_t hamt_child_index(unsigned long bitmap, size_t slot) {
  unsigned long mask = (1UL << slot) - 1UL;
  return hamt_popcount(bitmap & mask);
}

static bool hamt_slot_occupied(unsigned long bitmap, size_t slot) {
  return (bitmap & (1UL << slot)) != 0UL;
}

static ds_hamt_node_t *hamt_new_branch(ds_hamt_t *hamt) {
  ds_hamt_node_t *node = (ds_hamt_node_t *)ds_context_alloc(
      hamt->context, sizeof(ds_hamt_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->is_leaf = false;
  node->bitmap = 0UL;
  node->children = NULL;
  node->num_children = 0;
  node->key = NULL;
  node->key_length = 0;
  node->value = NULL;
  node->hash = 0UL;
  node->next = NULL;
  return node;
}

static ds_hamt_node_t *hamt_new_leaf(ds_hamt_t *hamt, const void *key,
                                     size_t key_length, void *value,
                                     unsigned long hash) {
  ds_hamt_node_t *node = (ds_hamt_node_t *)ds_context_alloc(
      hamt->context, sizeof(ds_hamt_node_t));
  size_t i;
  if (node == NULL) {
    return NULL;
  }
  node->is_leaf = true;
  node->bitmap = 0UL;
  node->children = NULL;
  node->num_children = 0;
  node->key_length = key_length;
  node->key = key_length > 0 ? (unsigned char *)ds_context_alloc(hamt->context,
                                                                 key_length)
                             : NULL;
  if (key_length > 0 && node->key == NULL) {
    ds_context_free(hamt->context, node);
    return NULL;
  }
  for (i = 0; i < key_length; i++) {
    node->key[i] = ((const unsigned char *)key)[i];
  }
  node->value = value;
  node->hash = hash;
  node->next = NULL;
  return node;
}

static ds_hamt_t *hamt_create_internal(ds_context_t *context) {
  ds_hamt_t *hamt;

  hamt = (ds_hamt_t *)ds_context_alloc(context, sizeof(ds_hamt_t));
  if (hamt == NULL) {
    return NULL;
  }
  hamt->context = context != NULL ? context : ds_default_context();

  hamt->size = 0;
  hamt->root = hamt_new_branch(hamt);
  if (hamt->root == NULL) {
    ds_context_free(context, hamt);
    return NULL;
  }
  return hamt;
}

ds_hamt_t *ds_hamt_create(ds_context_t *context) {
  return hamt_create_internal(context);
}
ds_status_t ds_hamt_create_context(ds_context_t *context, ds_hamt_t **out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out =
      hamt_create_internal(context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

static bool hamt_key_equal(ds_hamt_node_t *leaf, const void *key,
                           size_t key_length) {
  if (leaf->key_length != key_length) {
    return false;
  }
  /* the empty key has no buffer: memcmp needs valid pointers even for 0 */
  return key_length == 0 || memcmp(leaf->key, key, key_length) == 0;
}

/* insert child pointer at slot, growing the compact array */
static bool hamt_branch_add(ds_hamt_t *hamt, ds_hamt_node_t *branch,
                            size_t slot, ds_hamt_node_t *child) {
  size_t idx = hamt_child_index(branch->bitmap, slot);
  ds_hamt_node_t **grown = (ds_hamt_node_t **)ds_context_alloc(
      hamt->context, (branch->num_children + 1) * sizeof(ds_hamt_node_t *));
  size_t i;
  if (grown == NULL) {
    return false;
  }
  for (i = 0; i < idx; i++) {
    grown[i] = branch->children[i];
  }
  grown[idx] = child;
  for (i = idx; i < branch->num_children; i++) {
    grown[i + 1] = branch->children[i];
  }
  if (branch->children != NULL) {
    ds_context_free(hamt->context, branch->children);
  }
  branch->children = grown;
  branch->num_children += 1;
  branch->bitmap |= (1UL << slot);
  return true;
}

static void hamt_branch_remove(ds_hamt_t *hamt, ds_hamt_node_t *branch,
                               size_t slot) {
  size_t idx = hamt_child_index(branch->bitmap, slot);
  size_t i;
  if (branch->num_children == 1) {
    ds_context_free(hamt->context, branch->children);
    branch->children = NULL;
    branch->num_children = 0;
    branch->bitmap = 0UL;
    return;
  }
  for (i = idx; i + 1 < branch->num_children; i++) {
    branch->children[i] = branch->children[i + 1];
  }
  branch->num_children -= 1;
  branch->bitmap &= ~(1UL << slot);
}

/* convert a leaf into a branch so two leaves with colliding prefixes split */
static ds_hamt_node_t *hamt_split_leaf(ds_hamt_t *hamt,
                                       ds_hamt_node_t *existing,
                                       ds_hamt_node_t *fresh, int level) {
  ds_hamt_node_t *branch = hamt_new_branch(hamt);
  size_t es = hamt_slot(existing->hash, level);
  size_t fs = hamt_slot(fresh->hash, level);
  if (branch == NULL) {
    return NULL;
  }
  branch->num_children = es == fs ? 1U : 2U;
  branch->children = (ds_hamt_node_t **)ds_context_alloc(
      hamt->context, branch->num_children * sizeof(*branch->children));
  if (branch->children == NULL) {
    ds_context_free(hamt->context, branch);
    return NULL;
  }
  branch->bitmap = (1UL << es) | (1UL << fs);
  if (es == fs) {
    /* Unequal 32-bit hashes diverge by level 6, including bits 30..31.
     * Allocate the whole path before changing the existing leaf/chain. */
    branch->children[0] = hamt_split_leaf(hamt, existing, fresh, level + 1);
    if (branch->children[0] == NULL) {
      ds_context_free(hamt->context, branch->children);
      ds_context_free(hamt->context, branch);
      return NULL;
    }
  } else {
    branch->children[es < fs ? 0 : 1] = existing;
    branch->children[es < fs ? 1 : 0] = fresh;
  }
  return branch;
}

ds_status_t ds_hamt_insert_ex(ds_hamt_t *hamt, const void *key,
                              size_t key_length, void *value) {
  unsigned long hash;
  ds_hamt_node_t *node;
  int level = 0;
  if (hamt == NULL || (key == NULL && key_length != 0)) {
    return DS_ERR_NULL;
  }
  hash = hamt_hash(key, key_length);
  node = hamt->root;
  for (;;) {
    size_t slot = hamt_slot(hash, level);
    if (!hamt_slot_occupied(node->bitmap, slot)) {
      ds_hamt_node_t *leaf;
      if (hamt->size == (size_t)-1) {
        return DS_ERR_OVERFLOW;
      }
      leaf = hamt_new_leaf(hamt, key, key_length, value, hash);
      if (leaf == NULL) {
        return DS_ERR_ALLOC;
      }
      if (!hamt_branch_add(hamt, node, slot, leaf)) {
        if (leaf->key != NULL) {
          ds_context_free(hamt->context, leaf->key);
        }
        ds_context_free(hamt->context, leaf);
        return DS_ERR_ALLOC;
      }
      hamt->size += 1;
      return DS_OK;
    } else {
      size_t idx = hamt_child_index(node->bitmap, slot);
      ds_hamt_node_t *child = node->children[idx];
      if (child->is_leaf) {
        /* search the collision chain for an exact key match */
        ds_hamt_node_t *cur = child;
        ds_hamt_node_t *tail = child;
        while (cur != NULL) {
          if (hamt_key_equal(cur, key, key_length)) {
            cur->value = value;
            return DS_EXISTS;
          }
          tail = cur;
          cur = cur->next;
        }
        if (hamt->size == (size_t)-1) {
          return DS_ERR_OVERFLOW;
        }
        {
          ds_hamt_node_t *fresh =
              hamt_new_leaf(hamt, key, key_length, value, hash);
          if (fresh == NULL) {
            return DS_ERR_ALLOC;
          }
          if (child->hash == hash) {
            tail->next = fresh;
          } else {
            ds_hamt_node_t *split =
                hamt_split_leaf(hamt, child, fresh, level + 1);
            if (split == NULL) {
              if (fresh->key != NULL) {
                ds_context_free(hamt->context, fresh->key);
              }
              ds_context_free(hamt->context, fresh);
              return DS_ERR_ALLOC;
            }
            node->children[idx] = split;
          }
          hamt->size += 1;
          return DS_OK;
        }
      } else {
        node = child;
        level += 1;
      }
    }
  }
}

bool ds_hamt_insert(ds_hamt_t *hamt, const void *key, size_t key_length,
                    void *value) {
  return ds_hamt_insert_ex(hamt, key, key_length, value) == DS_OK;
}

static ds_hamt_node_t *hamt_find_leaf(ds_hamt_t *hamt, const void *key,
                                      size_t key_length, unsigned long hash) {
  ds_hamt_node_t *node = hamt->root;
  int level = 0;
  for (;;) {
    size_t slot = hamt_slot(hash, level);
    size_t idx;
    ds_hamt_node_t *child;
    if (!hamt_slot_occupied(node->bitmap, slot)) {
      return NULL;
    }
    idx = hamt_child_index(node->bitmap, slot);
    child = node->children[idx];
    if (child->is_leaf) {
      while (child != NULL) {
        if (hamt_key_equal(child, key, key_length)) {
          return child;
        }
        child = child->next;
      }
      return NULL;
    }
    node = child;
    level += 1;
  }
}

bool ds_hamt_contains(ds_hamt_t *hamt, const void *key, size_t key_length) {
  bool result;
  result = hamt_find_leaf(hamt, key, key_length, hamt_hash(key, key_length)) !=
           NULL;
  return result;
}

void *ds_hamt_get(ds_hamt_t *hamt, const void *key, size_t key_length) {
  ds_hamt_node_t *leaf;
  void *result = NULL;
  leaf = hamt_find_leaf(hamt, key, key_length, hamt_hash(key, key_length));
  if (leaf != NULL) {
    result = leaf->value;
  }
  return result;
}

bool ds_hamt_remove(ds_hamt_t *hamt, const void *key, size_t key_length,
                    void **out_value) {
  unsigned long hash;
  ds_hamt_node_t *node;
  ds_hamt_node_t *parents[DS_HAMT_MAX_LEVEL + 2];
  size_t parent_slots[DS_HAMT_MAX_LEVEL + 2];
  int depth = 0;
  int level = 0;
  bool removed = false;
  hash = hamt_hash(key, key_length);
  node = hamt->root;
  for (;;) {
    size_t slot = hamt_slot(hash, level);
    size_t idx;
    ds_hamt_node_t *child;
    if (!hamt_slot_occupied(node->bitmap, slot)) {
      break;
    }
    idx = hamt_child_index(node->bitmap, slot);
    child = node->children[idx];
    if (child->is_leaf) {
      ds_hamt_node_t *cur = child;
      ds_hamt_node_t *prev = NULL;
      while (cur != NULL) {
        if (hamt_key_equal(cur, key, key_length)) {
          if (out_value != NULL) {
            *out_value = cur->value;
          }
          if (prev != NULL) {
            prev->next = cur->next;
          } else if (cur->next != NULL) {
            /* promote the chain successor into the slot */
            node->children[idx] = cur->next;
          } else {
            hamt_branch_remove(hamt, node, slot);
            /* prune now-empty branches up the recorded path */
            while (depth > 0 && node->num_children == 0 &&
                   node != hamt->root) {
              ds_hamt_node_t *parent = parents[depth - 1];
              size_t pslot = parent_slots[depth - 1];
              ds_context_free(hamt->context, node);
              hamt_branch_remove(hamt, parent, pslot);
              node = parent;
              depth -= 1;
            }
          }
          if (cur->key != NULL) {
            ds_context_free(hamt->context, cur->key);
          }
          ds_context_free(hamt->context, cur);
          hamt->size -= 1;
          removed = true;
          break;
        }
        prev = cur;
        cur = cur->next;
      }
      break;
    }
    parents[depth] = node;
    parent_slots[depth] = slot;
    depth += 1;
    node = child;
    level += 1;
  }
  return removed;
}

size_t ds_hamt_size(ds_hamt_t *hamt) {
  size_t result;
  result = hamt->size;
  return result;
}

bool ds_hamt_is_empty(ds_hamt_t *hamt) {
  bool result;
  result = hamt->size == 0;
  return result;
}

static ds_status_t hamt_walk_node(ds_hamt_node_t *node,
                                  ds_hamt_visit_fn callback, void *user) {
  size_t i;
  if (node->is_leaf) {
    ds_hamt_node_t *cur = node;
    while (cur != NULL) {
      ds_status_t st = callback(cur->key, cur->key_length, cur->value, user);
      if (st != DS_OK) {
        return st;
      }
      cur = cur->next;
    }
    return DS_OK;
  }
  for (i = 0; i < node->num_children; i++) {
    ds_status_t st = hamt_walk_node(node->children[i], callback, user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

ds_status_t ds_hamt_walk_with(ds_hamt_t *hamt, ds_hamt_visit_fn callback,
                              void *user) {
  if (hamt == NULL || callback == NULL) {
    return DS_ERR_NULL;
  }
  return hamt_walk_node(hamt->root, callback, user);
}
typedef struct hamt_direct_visit {
  void (*callback)(const void *, size_t, void *, void *);
  void *user;
} hamt_direct_visit_t;
static ds_status_t hamt_visit_direct(const void *key, size_t length,
                                     void *value, void *user) {
  hamt_direct_visit_t *p = (hamt_direct_visit_t *)user;
  p->callback(key, length, value, p->user);
  return DS_OK;
}
void ds_hamt_walk(ds_hamt_t *hamt,
                  void (*callback)(const void *, size_t, void *, void *),
                  void *user) {
  hamt_direct_visit_t policy;
  if (callback == NULL) {
    return;
  }
  policy.callback = callback;
  policy.user = user;
  (void)ds_hamt_walk_with(hamt, hamt_visit_direct, &policy);
}

static void hamt_destroy_node(ds_hamt_t *hamt, ds_hamt_node_t *node,
                              void (*destroy_value)(void *, void *),
                              void *user) {
  if (node->is_leaf) {
    ds_hamt_node_t *cur = node;
    while (cur != NULL) {
      ds_hamt_node_t *next = cur->next;
      if (destroy_value != NULL) {
        destroy_value(cur->value, user);
      }
      if (cur->key != NULL) {
        ds_context_free(hamt->context, cur->key);
      }
      ds_context_free(hamt->context, cur);
      cur = next;
    }
    return;
  }
  {
    size_t i;
    for (i = 0; i < node->num_children; i++) {
      hamt_destroy_node(hamt, node->children[i], destroy_value, user);
    }
    if (node->children != NULL) {
      ds_context_free(hamt->context, node->children);
    }
    ds_context_free(hamt->context, node);
  }
}

void ds_hamt_destroy_with(ds_hamt_t *hamt,
                          void (*destroy_value)(void *, void *), void *user) {
  if (hamt == NULL) {
    return;
  }
  hamt_destroy_node(hamt, hamt->root, destroy_value, user);
  ds_context_free(hamt->context, hamt);
}
static void hamt_destroy_direct(void *value, void *user) {
  void (**destroy)(void *) = (void (**)(void *))user;
  (*destroy)(value);
}
void ds_hamt_destroy(ds_hamt_t *tree, void (*destroy_value)(void *)) {
  ds_hamt_destroy_with(tree,
                       destroy_value != NULL ? hamt_destroy_direct : NULL,
                       &destroy_value);
}

ds_status_t ds_hamt_find_ex(ds_hamt_t *tree, const void *key, size_t length,
                            void **out) {
  ds_hamt_node_t *leaf;
  if (tree == NULL || out == NULL || (length > 0 && key == NULL)) {
    return DS_ERR_NULL;
  }
  leaf = hamt_find_leaf(tree, key, length, hamt_hash(key, length));
  if (leaf == NULL) {
    return DS_NOT_FOUND;
  }
  *out = leaf->value;
  return DS_OK;
}
