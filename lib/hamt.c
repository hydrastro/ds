#include "hamt.h"
#include <stdlib.h>
#include <string.h>

#define DS_HAMT_BITS 5
#define DS_HAMT_FANOUT 32
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

ds_hamt_t *ds_hamt_create(void) { return ds_hamt_create_alloc(malloc, free); }

static ds_hamt_node_t *hamt_new_branch(ds_hamt_t *hamt) {
  ds_hamt_node_t *node =
      (ds_hamt_node_t *)hamt->allocator(sizeof(ds_hamt_node_t));
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
  ds_hamt_node_t *node =
      (ds_hamt_node_t *)hamt->allocator(sizeof(ds_hamt_node_t));
  size_t i;
  node->is_leaf = true;
  node->bitmap = 0UL;
  node->children = NULL;
  node->num_children = 0;
  node->key_length = key_length;
  node->key =
      key_length > 0 ? (unsigned char *)hamt->allocator(key_length) : NULL;
  for (i = 0; i < key_length; i++) {
    node->key[i] = ((const unsigned char *)key)[i];
  }
  node->value = value;
  node->hash = hash;
  node->next = NULL;
  return node;
}

ds_hamt_t *ds_hamt_create_alloc(void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  ds_hamt_t *hamt = (ds_hamt_t *)allocator(sizeof(ds_hamt_t));
  hamt->allocator = allocator;
  hamt->deallocator = deallocator;
  hamt->size = 0;
  hamt->root = hamt_new_branch(hamt);
  return hamt;
}

static bool hamt_key_equal(ds_hamt_node_t *leaf, const void *key,
                           size_t key_length) {
  if (leaf->key_length != key_length) {
    return false;
  }
  return memcmp(leaf->key, key, key_length) == 0;
}

/* insert child pointer at slot, growing the compact array */
static void hamt_branch_add(ds_hamt_t *hamt, ds_hamt_node_t *branch,
                            size_t slot, ds_hamt_node_t *child) {
  size_t idx = hamt_child_index(branch->bitmap, slot);
  ds_hamt_node_t **grown = (ds_hamt_node_t **)hamt->allocator(
      (branch->num_children + 1) * sizeof(ds_hamt_node_t *));
  size_t i;
  for (i = 0; i < idx; i++) {
    grown[i] = branch->children[i];
  }
  grown[idx] = child;
  for (i = idx; i < branch->num_children; i++) {
    grown[i + 1] = branch->children[i];
  }
  if (branch->children != NULL) {
    hamt->deallocator(branch->children);
  }
  branch->children = grown;
  branch->num_children += 1;
  branch->bitmap |= (1UL << slot);
}

static void hamt_branch_remove(ds_hamt_t *hamt, ds_hamt_node_t *branch,
                               size_t slot) {
  size_t idx = hamt_child_index(branch->bitmap, slot);
  size_t i;
  if (branch->num_children == 1) {
    hamt->deallocator(branch->children);
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
  if (level >= DS_HAMT_MAX_LEVEL) {
    /* exhausted hash bits: chain as collision list under the branch */
    existing->next = fresh;
    /* park the chain at slot 0 */
    hamt_branch_add(hamt, branch, 0, existing);
    return branch;
  }
  {
    size_t es = hamt_slot(existing->hash, level);
    size_t fs = hamt_slot(fresh->hash, level);
    if (es == fs) {
      ds_hamt_node_t *child = hamt_split_leaf(hamt, existing, fresh, level + 1);
      hamt_branch_add(hamt, branch, es, child);
    } else {
      hamt_branch_add(hamt, branch, es, existing);
      hamt_branch_add(hamt, branch, fs, fresh);
    }
  }
  return branch;
}

bool ds_hamt_insert(ds_hamt_t *hamt, const void *key, size_t key_length,
                    void *value) {
  unsigned long hash;
  ds_hamt_node_t *node;
  int level = 0;
  bool added = false;
  hash = hamt_hash(key, key_length);
  node = hamt->root;
  for (;;) {
    size_t slot = hamt_slot(hash, level);
    if (!hamt_slot_occupied(node->bitmap, slot)) {
      ds_hamt_node_t *leaf = hamt_new_leaf(hamt, key, key_length, value, hash);
      hamt_branch_add(hamt, node, slot, leaf);
      hamt->size += 1;
      added = true;
      break;
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
            added = false;
            goto done;
          }
          tail = cur;
          cur = cur->next;
        }
        if (level + 1 > DS_HAMT_MAX_LEVEL) {
          /* append to collision chain */
          tail->next = hamt_new_leaf(hamt, key, key_length, value, hash);
          hamt->size += 1;
          added = true;
        } else {
          ds_hamt_node_t *fresh =
              hamt_new_leaf(hamt, key, key_length, value, hash);
          ds_hamt_node_t *split =
              hamt_split_leaf(hamt, child, fresh, level + 1);
          node->children[idx] = split;
          hamt->size += 1;
          added = true;
        }
        break;
      } else {
        node = child;
        level += 1;
      }
    }
  }
done:
  return added;
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
  result =
      hamt_find_leaf(hamt, key, key_length, hamt_hash(key, key_length)) != NULL;
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
            while (depth > 0 && node->num_children == 0 && node != hamt->root) {
              ds_hamt_node_t *parent = parents[depth - 1];
              size_t pslot = parent_slots[depth - 1];
              hamt->deallocator(node);
              hamt_branch_remove(hamt, parent, pslot);
              node = parent;
              depth -= 1;
            }
          }
          if (cur->key != NULL) {
            hamt->deallocator(cur->key);
          }
          hamt->deallocator(cur);
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

static void hamt_walk_node(ds_hamt_node_t *node,
                           void (*callback)(const void *, size_t, void *,
                                            void *),
                           void *user) {
  size_t i;
  if (node->is_leaf) {
    ds_hamt_node_t *cur = node;
    while (cur != NULL) {
      callback(cur->key, cur->key_length, cur->value, user);
      cur = cur->next;
    }
    return;
  }
  for (i = 0; i < node->num_children; i++) {
    hamt_walk_node(node->children[i], callback, user);
  }
}

void ds_hamt_walk(ds_hamt_t *hamt,
                  void (*callback)(const void *, size_t, void *, void *),
                  void *user) {
  hamt_walk_node(hamt->root, callback, user);
}

static void hamt_destroy_node(ds_hamt_t *hamt, ds_hamt_node_t *node,
                              void (*destroy_value)(void *)) {
  if (node->is_leaf) {
    ds_hamt_node_t *cur = node;
    while (cur != NULL) {
      ds_hamt_node_t *next = cur->next;
      if (destroy_value != NULL) {
        destroy_value(cur->value);
      }
      if (cur->key != NULL) {
        hamt->deallocator(cur->key);
      }
      hamt->deallocator(cur);
      cur = next;
    }
    return;
  }
  {
    size_t i;
    for (i = 0; i < node->num_children; i++) {
      hamt_destroy_node(hamt, node->children[i], destroy_value);
    }
    if (node->children != NULL) {
      hamt->deallocator(node->children);
    }
    hamt->deallocator(node);
  }
}

void ds_hamt_destroy(ds_hamt_t *hamt, void (*destroy_value)(void *)) {
  hamt_destroy_node(hamt, hamt->root, destroy_value);
  hamt->deallocator(hamt);
}
