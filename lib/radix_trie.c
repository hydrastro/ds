#include "radix_trie.h"
#include <stdlib.h>
#include <string.h>

static ds_radix_trie_node_t *radix_trie_new_node(ds_radix_trie_t *trie,
                                                 const unsigned char *edge,
                                                 size_t edge_length) {
  ds_radix_trie_node_t *node =
      (ds_radix_trie_node_t *)trie->allocator(sizeof(ds_radix_trie_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->edge_length = edge_length;
  if (edge_length > 0) {
    size_t i;
    node->edge = (unsigned char *)trie->allocator(edge_length);
    if (node->edge == NULL) {
      trie->deallocator(node);
      return NULL;
    }
    for (i = 0; i < edge_length; i++) {
      node->edge[i] = edge[i];
    }
  } else {
    node->edge = NULL;
  }
  node->parent = NULL;
  node->children = NULL;
  node->num_children = 0;
  node->is_terminal = false;
  node->value = NULL;
  return node;
}

ds_radix_trie_t *ds_radix_trie_create(void) {
  return ds_radix_trie_create_alloc(malloc, free);
}

ds_radix_trie_t *ds_radix_trie_create_alloc(void *(*allocator)(size_t),
                                            void (*deallocator)(void *)) {
  ds_radix_trie_t *trie;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  trie = (ds_radix_trie_t *)allocator(sizeof(ds_radix_trie_t));
  if (trie == NULL) {
    return NULL;
  }
  trie->allocator = allocator;
  trie->deallocator = deallocator;
  trie->size = 0;
  trie->root = radix_trie_new_node(trie, NULL, 0);
  if (trie->root == NULL) {
    deallocator(trie);
    return NULL;
  }
  return trie;
}

static ds_radix_trie_node_t *radix_trie_find_child(ds_radix_trie_node_t *node,
                                                   unsigned char first) {
  size_t i;
  for (i = 0; i < node->num_children; i++) {
    if (node->children[i]->edge_length > 0 &&
        node->children[i]->edge[0] == first) {
      return node->children[i];
    }
  }
  return NULL;
}

static bool radix_trie_add_child(ds_radix_trie_t *trie,
                                 ds_radix_trie_node_t *parent,
                                 ds_radix_trie_node_t *child) {
  ds_radix_trie_node_t **grown;
  size_t i;
  grown = (ds_radix_trie_node_t **)trie->allocator(
      (parent->num_children + 1) * sizeof(ds_radix_trie_node_t *));
  if (grown == NULL)
    return false;
  for (i = 0; i < parent->num_children; i++) {
    grown[i] = parent->children[i];
  }
  grown[parent->num_children] = child;
  if (parent->children != NULL) {
    trie->deallocator(parent->children);
  }
  parent->children = grown;
  parent->num_children += 1;
  child->parent = parent;
  return true;
}

static void radix_trie_remove_child(ds_radix_trie_t *trie,
                                    ds_radix_trie_node_t *parent,
                                    ds_radix_trie_node_t *child) {
  size_t i, n = 0;
  for (i = 0; i < parent->num_children; i++)
    if (parent->children[i] != child)
      parent->children[n++] = parent->children[i];
  parent->num_children = n;
  if (n == 0) {
    trie->deallocator(parent->children);
    parent->children = NULL;
  }
}

static size_t radix_trie_common_prefix(const unsigned char *a, size_t a_len,
                                       const unsigned char *b, size_t b_len) {
  size_t i = 0;
  size_t limit = a_len < b_len ? a_len : b_len;
  while (i < limit && a[i] == b[i]) {
    i += 1;
  }
  return i;
}

static void radix_trie_free_new(ds_radix_trie_t *trie,
                                ds_radix_trie_node_t *node) {
  if (node == NULL)
    return;
  if (node->edge != NULL)
    trie->deallocator(node->edge);
  if (node->children != NULL)
    trie->deallocator(node->children);
  trie->deallocator(node);
}

ds_status_t ds_radix_trie_insert_ex(ds_radix_trie_t *trie, const void *key,
                                    size_t length, void *value) {
  const unsigned char *bytes = (const unsigned char *)key;
  ds_radix_trie_node_t *node;
  size_t offset = 0;
  if (trie == NULL || (key == NULL && length != 0))
    return DS_ERR_NULL;
  node = trie->root;
  for (;;) {
    ds_radix_trie_node_t *child;
    size_t common;
    if (offset == length) {
      if (node->is_terminal) {
        node->value = value;
        return DS_EXISTS;
      }
      if (trie->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      node->is_terminal = true;
      node->value = value;
      trie->size++;
      return DS_OK;
    }
    child = radix_trie_find_child(node, bytes[offset]);
    if (child == NULL) {
      ds_radix_trie_node_t *leaf;
      if (trie->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      leaf = radix_trie_new_node(trie, bytes + offset, length - offset);
      if (leaf == NULL)
        return DS_ERR_ALLOC;
      leaf->is_terminal = true;
      leaf->value = value;
      if (!radix_trie_add_child(trie, node, leaf)) {
        radix_trie_free_new(trie, leaf);
        return DS_ERR_ALLOC;
      }
      trie->size++;
      return DS_OK;
    }
    common = radix_trie_common_prefix(child->edge, child->edge_length,
                                      bytes + offset, length - offset);
    if (common == child->edge_length) {
      offset += common;
      node = child;
      continue;
    }
    {
      ds_radix_trie_node_t *split, *leaf = NULL;
      size_t i, slots = offset + common == length ? 1U : 2U;
      if (trie->size == (size_t)-1)
        return DS_ERR_OVERFLOW;
      split = radix_trie_new_node(trie, child->edge, common);
      if (split == NULL)
        return DS_ERR_ALLOC;
      if (slots == 2) {
        leaf = radix_trie_new_node(trie, bytes + offset + common,
                                   length - offset - common);
        if (leaf == NULL) {
          radix_trie_free_new(trie, split);
          return DS_ERR_ALLOC;
        }
        leaf->is_terminal = true;
        leaf->value = value;
      }
      split->children = (ds_radix_trie_node_t **)trie->allocator(
          slots * sizeof(*split->children));
      if (split->children == NULL) {
        radix_trie_free_new(trie, leaf);
        radix_trie_free_new(trie, split);
        return DS_ERR_ALLOC;
      }
      /* Commit: all new storage and key bytes now exist. The existing edge
       * buffer can be shortened in place, without another allocation. */
      memmove(child->edge, child->edge + common, child->edge_length - common);
      child->edge_length -= common;
      split->children[0] = child;
      child->parent = split;
      split->num_children = slots;
      split->parent = node;
      if (leaf != NULL) {
        split->children[1] = leaf;
        leaf->parent = split;
      } else {
        split->is_terminal = true;
        split->value = value;
      }
      for (i = 0; i < node->num_children; i++)
        if (node->children[i] == child) {
          node->children[i] = split;
          break;
        }
      trie->size++;
      return DS_OK;
    }
  }
}

bool ds_radix_trie_insert(ds_radix_trie_t *trie, const void *key, size_t length,
                          void *value) {
  return ds_radix_trie_insert_ex(trie, key, length, value) == DS_OK;
}

static ds_radix_trie_node_t *radix_trie_find_node(ds_radix_trie_t *trie,
                                                  const unsigned char *bytes,
                                                  size_t length) {
  ds_radix_trie_node_t *node;
  size_t offset = 0;
  if (trie == NULL || (bytes == NULL && length != 0))
    return NULL;
  node = trie->root;
  while (offset < length) {
    ds_radix_trie_node_t *child = radix_trie_find_child(node, bytes[offset]);
    size_t common;
    if (child == NULL) {
      return NULL;
    }
    common = radix_trie_common_prefix(child->edge, child->edge_length,
                                      bytes + offset, length - offset);
    if (common != child->edge_length) {
      return NULL;
    }
    offset += common;
    node = child;
  }
  return node;
}

bool ds_radix_trie_contains(ds_radix_trie_t *trie, const void *key,
                            size_t length) {
  ds_radix_trie_node_t *node;
  bool result;
  node = radix_trie_find_node(trie, (const unsigned char *)key, length);
  result = node != NULL && node->is_terminal;
  return result;
}

void *ds_radix_trie_get(ds_radix_trie_t *trie, const void *key, size_t length) {
  ds_radix_trie_node_t *node;
  void *result = NULL;
  node = radix_trie_find_node(trie, (const unsigned char *)key, length);
  if (node != NULL && node->is_terminal) {
    result = node->value;
  }
  return result;
}

static bool radix_trie_merge_with_child(ds_radix_trie_t *trie,
                                        ds_radix_trie_node_t *node) {
  /* node has exactly one child and is not terminal: merge edges */
  ds_radix_trie_node_t *child = node->children[0];
  size_t merged_length;
  unsigned char *merged;
  size_t i;
  if (child->edge_length > (size_t)-1 - node->edge_length)
    return false;
  merged_length = node->edge_length + child->edge_length;
  merged = (unsigned char *)trie->allocator(merged_length);
  if (merged == NULL)
    return false;
  for (i = 0; i < node->edge_length; i++) {
    merged[i] = node->edge[i];
  }
  for (i = 0; i < child->edge_length; i++) {
    merged[node->edge_length + i] = child->edge[i];
  }
  if (node->edge != NULL) {
    trie->deallocator(node->edge);
  }
  node->edge = merged;
  node->edge_length = merged_length;
  node->is_terminal = child->is_terminal;
  node->value = child->value;
  trie->deallocator(node->children);
  node->children = child->children;
  node->num_children = child->num_children;
  for (i = 0; i < node->num_children; i++)
    node->children[i]->parent = node;
  if (child->edge != NULL) {
    trie->deallocator(child->edge);
  }
  trie->deallocator(child);
  return true;
}

bool ds_radix_trie_remove(ds_radix_trie_t *trie, const void *key, size_t length,
                          void **out_value) {
  ds_radix_trie_node_t *node;
  if (trie == NULL || (key == NULL && length != 0))
    return false;
  node = radix_trie_find_node(trie, (const unsigned char *)key, length);
  if (node == NULL || !node->is_terminal)
    return false;
  if (out_value != NULL)
    *out_value = node->value;
  node->is_terminal = false;
  node->value = NULL;
  trie->size--;
  while (node != trie->root && !node->is_terminal) {
    if (node->num_children == 0) {
      ds_radix_trie_node_t *parent = node->parent;
      radix_trie_remove_child(trie, parent, node);
      radix_trie_free_new(trie, node);
      node = parent;
    } else if (node->num_children == 1 &&
               radix_trie_merge_with_child(trie, node)) {
      /* Another unary node may remain if earlier optional compression failed.
       */
    } else
      break;
  }
  return true;
}

size_t ds_radix_trie_size(ds_radix_trie_t *trie) {
  size_t result;
  result = trie->size;
  return result;
}

bool ds_radix_trie_is_empty(ds_radix_trie_t *trie) {
  bool result;
  result = trie->size == 0;
  return result;
}

/* The walk keeps the path from the root on an explicit stack (a radix
 * trie is as deep as its longest chain of nested keys), next to the key
 * buffer. Both grow through the trie's allocator; if one cannot grow, the
 * walk ends there. */
typedef struct rt_frame {
  ds_radix_trie_node_t *node;
  size_t next;   /* the next child to enter */
  size_t prefix; /* key bytes up to and including this node's edge */
} rt_frame_t;

/* appends node's edge to the key at prefix and reports the key if node
 * ends one; false when the buffer cannot grow */
static bool rt_enter(ds_radix_trie_t *trie, ds_radix_trie_node_t *node,
                     size_t prefix, unsigned char **buffer, size_t *cap,
                     void (*callback)(const unsigned char *, size_t, void *)) {
  size_t needed;
  size_t i;
  if (node->edge_length > (size_t)-1 - prefix)
    return false;
  needed = prefix + node->edge_length;
  if (needed > *cap) {
    size_t new_cap = *cap == 0 ? 16 : *cap;
    unsigned char *grown;
    while (new_cap < needed) {
      if (new_cap > (size_t)-1 / 2) {
        return false;
      }
      new_cap *= 2;
    }
    grown = (unsigned char *)trie->allocator(new_cap);
    if (grown == NULL) {
      return false;
    }
    for (i = 0; i < prefix; i++) {
      grown[i] = (*buffer)[i];
    }
    if (*buffer != NULL) {
      trie->deallocator(*buffer);
    }
    *buffer = grown;
    *cap = new_cap;
  }
  for (i = 0; i < node->edge_length; i++) {
    (*buffer)[prefix + i] = node->edge[i];
  }
  if (node->is_terminal) {
    callback(*buffer, needed, node->value);
  }
  return true;
}

static bool rt_push_frame(ds_radix_trie_t *trie, rt_frame_t **stack,
                          size_t *depth, size_t *cap,
                          ds_radix_trie_node_t *node, size_t prefix) {
  if (*depth == *cap) {
    size_t new_cap = *cap == 0 ? 16 : *cap * 2;
    rt_frame_t *grown;
    size_t i;
    if (*cap > (size_t)-1 / 2 / sizeof(rt_frame_t)) {
      return false;
    }
    grown = (rt_frame_t *)trie->allocator(new_cap * sizeof(rt_frame_t));
    if (grown == NULL) {
      return false;
    }
    for (i = 0; i < *depth; i++) {
      grown[i] = (*stack)[i];
    }
    if (*stack != NULL) {
      trie->deallocator(*stack);
    }
    *stack = grown;
    *cap = new_cap;
  }
  (*stack)[*depth].node = node;
  (*stack)[*depth].next = 0;
  (*stack)[*depth].prefix = prefix + node->edge_length;
  (*depth)++;
  return true;
}

void ds_radix_trie_walk(ds_radix_trie_t *trie,
                        void (*callback)(const unsigned char *, size_t,
                                         void *)) {
  unsigned char *buffer = NULL;
  size_t buffer_cap = 0;
  rt_frame_t *stack = NULL;
  size_t depth = 0;
  size_t stack_cap = 0;
  if (trie->root != NULL &&
      rt_enter(trie, trie->root, 0, &buffer, &buffer_cap, callback) &&
      rt_push_frame(trie, &stack, &depth, &stack_cap, trie->root, 0)) {
    while (depth > 0) {
      rt_frame_t *f = &stack[depth - 1];
      ds_radix_trie_node_t *child;
      size_t prefix;
      if (f->next == f->node->num_children) {
        depth--;
        continue;
      }
      child = f->node->children[f->next++];
      prefix = f->prefix;
      if (!rt_enter(trie, child, prefix, &buffer, &buffer_cap, callback) ||
          !rt_push_frame(trie, &stack, &depth, &stack_cap, child, prefix)) {
        break;
      }
    }
  }
  if (stack != NULL) {
    trie->deallocator(stack);
  }
  if (buffer != NULL) {
    trie->deallocator(buffer);
  }
}

/* Destruction needs no memory: nodes waiting to be freed form a list
 * linked through their edge pointers, whose bytes are freed first. */
static void rt_doom(ds_radix_trie_t *trie, ds_radix_trie_node_t **list,
                    ds_radix_trie_node_t *node) {
  if (node->edge != NULL) {
    trie->deallocator(node->edge);
  }
  node->edge = (unsigned char *)(void *)*list;
  *list = node;
}

void ds_radix_trie_destroy(ds_radix_trie_t *trie,
                           void (*destroy_value)(void *)) {
  ds_radix_trie_node_t *list = NULL;
  if (trie->root != NULL) {
    rt_doom(trie, &list, trie->root);
  }
  while (list != NULL) {
    ds_radix_trie_node_t *x = list;
    size_t i;
    list = (ds_radix_trie_node_t *)(void *)x->edge;
    for (i = 0; i < x->num_children; i++) {
      rt_doom(trie, &list, x->children[i]);
    }
    if (x->is_terminal && destroy_value != NULL) {
      destroy_value(x->value);
    }
    if (x->children != NULL) {
      trie->deallocator(x->children);
    }
    trie->deallocator(x);
  }
  trie->deallocator(trie);
}
