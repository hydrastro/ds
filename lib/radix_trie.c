#include "radix_trie.h"
#include <stdlib.h>
#include <string.h>

static ds_radix_trie_node_t *
FUNC(radix_trie_new_node)(ds_radix_trie_t *trie, const unsigned char *edge,
                          size_t edge_length) {
  ds_radix_trie_node_t *node =
      (ds_radix_trie_node_t *)trie->allocator(sizeof(ds_radix_trie_node_t));
  node->edge_length = edge_length;
  if (edge_length > 0) {
    size_t i;
    node->edge = (unsigned char *)trie->allocator(edge_length);
    for (i = 0; i < edge_length; i++) {
      node->edge[i] = edge[i];
    }
  } else {
    node->edge = NULL;
  }
  node->children = NULL;
  node->num_children = 0;
  node->is_terminal = false;
  node->value = NULL;
  return node;
}

ds_radix_trie_t *FUNC(ds_radix_trie_create)(void) {
  return FUNC(ds_radix_trie_create_alloc)(malloc, free);
}

ds_radix_trie_t *FUNC(ds_radix_trie_create_alloc)(void *(*allocator)(size_t),
                                                  void (*deallocator)(void *)) {
  ds_radix_trie_t *trie = (ds_radix_trie_t *)allocator(sizeof(ds_radix_trie_t));
  trie->allocator = allocator;
  trie->deallocator = deallocator;
  trie->size = 0;
  trie->root = FUNC(radix_trie_new_node)(trie, NULL, 0);
  return trie;
}

static ds_radix_trie_node_t *
FUNC(radix_trie_find_child)(ds_radix_trie_node_t *node, unsigned char first) {
  size_t i;
  for (i = 0; i < node->num_children; i++) {
    if (node->children[i]->edge_length > 0 &&
        node->children[i]->edge[0] == first) {
      return node->children[i];
    }
  }
  return NULL;
}

static void FUNC(radix_trie_add_child)(ds_radix_trie_t *trie,
                                       ds_radix_trie_node_t *parent,
                                       ds_radix_trie_node_t *child) {
  ds_radix_trie_node_t **grown;
  size_t i;
  grown = (ds_radix_trie_node_t **)trie->allocator(
      (parent->num_children + 1) * sizeof(ds_radix_trie_node_t *));
  for (i = 0; i < parent->num_children; i++) {
    grown[i] = parent->children[i];
  }
  grown[parent->num_children] = child;
  if (parent->children != NULL) {
    trie->deallocator(parent->children);
  }
  parent->children = grown;
  parent->num_children += 1;
}

static void FUNC(radix_trie_remove_child)(ds_radix_trie_t *trie,
                                          ds_radix_trie_node_t *parent,
                                          ds_radix_trie_node_t *child) {
  size_t i;
  size_t j = 0;
  ds_radix_trie_node_t **shrunk;
  if (parent->num_children == 1) {
    trie->deallocator(parent->children);
    parent->children = NULL;
    parent->num_children = 0;
    return;
  }
  shrunk = (ds_radix_trie_node_t **)trie->allocator(
      (parent->num_children - 1) * sizeof(ds_radix_trie_node_t *));
  for (i = 0; i < parent->num_children; i++) {
    if (parent->children[i] != child) {
      shrunk[j] = parent->children[i];
      j += 1;
    }
  }
  trie->deallocator(parent->children);
  parent->children = shrunk;
  parent->num_children -= 1;
}

static size_t FUNC(radix_trie_common_prefix)(const unsigned char *a,
                                             size_t a_len,
                                             const unsigned char *b,
                                             size_t b_len) {
  size_t i = 0;
  size_t limit = a_len < b_len ? a_len : b_len;
  while (i < limit && a[i] == b[i]) {
    i += 1;
  }
  return i;
}

static void FUNC(radix_trie_set_edge)(ds_radix_trie_t *trie,
                                      ds_radix_trie_node_t *node,
                                      const unsigned char *edge,
                                      size_t edge_length) {
  unsigned char *buffer;
  size_t i;
  if (edge_length > 0) {
    buffer = (unsigned char *)trie->allocator(edge_length);
    for (i = 0; i < edge_length; i++) {
      buffer[i] = edge[i];
    }
  } else {
    buffer = NULL;
  }
  if (node->edge != NULL) {
    trie->deallocator(node->edge);
  }
  node->edge = buffer;
  node->edge_length = edge_length;
}

bool FUNC(ds_radix_trie_insert)(ds_radix_trie_t *trie, const void *key,
                                size_t length, void *value) {
  const unsigned char *bytes = (const unsigned char *)key;
  ds_radix_trie_node_t *node;
  size_t offset = 0;
  bool inserted = false;
  node = trie->root;
  for (;;) {
    ds_radix_trie_node_t *child;
    size_t common;
    if (offset == length) {
      if (!node->is_terminal) {
        node->is_terminal = true;
        trie->size += 1;
        inserted = true;
      }
      node->value = value;
      break;
    }
    child = FUNC(radix_trie_find_child)(node, bytes[offset]);
    if (child == NULL) {
      ds_radix_trie_node_t *leaf =
          FUNC(radix_trie_new_node)(trie, bytes + offset, length - offset);
      leaf->is_terminal = true;
      leaf->value = value;
      FUNC(radix_trie_add_child)(trie, node, leaf);
      trie->size += 1;
      inserted = true;
      break;
    }
    common = FUNC(radix_trie_common_prefix)(child->edge, child->edge_length,
                                            bytes + offset, length - offset);
    if (common == child->edge_length) {
      /* whole edge consumed: descend */
      offset += common;
      node = child;
      continue;
    }
    /* split the edge at common */
    {
      ds_radix_trie_node_t *split =
          FUNC(radix_trie_new_node)(trie, child->edge, common);
      /* child keeps the suffix of its edge */
      FUNC(radix_trie_set_edge)
      (trie, child, child->edge + common, child->edge_length - common);
      FUNC(radix_trie_remove_child)(trie, node, child);
      FUNC(radix_trie_add_child)(trie, node, split);
      FUNC(radix_trie_add_child)(trie, split, child);
      offset += common;
      if (offset == length) {
        split->is_terminal = true;
        split->value = value;
        trie->size += 1;
        inserted = true;
      } else {
        ds_radix_trie_node_t *leaf =
            FUNC(radix_trie_new_node)(trie, bytes + offset, length - offset);
        leaf->is_terminal = true;
        leaf->value = value;
        FUNC(radix_trie_add_child)(trie, split, leaf);
        trie->size += 1;
        inserted = true;
      }
      break;
    }
  }
  return inserted;
}

static ds_radix_trie_node_t *
FUNC(radix_trie_find_node)(ds_radix_trie_t *trie, const unsigned char *bytes,
                           size_t length) {
  ds_radix_trie_node_t *node = trie->root;
  size_t offset = 0;
  while (offset < length) {
    ds_radix_trie_node_t *child =
        FUNC(radix_trie_find_child)(node, bytes[offset]);
    size_t common;
    if (child == NULL) {
      return NULL;
    }
    common = FUNC(radix_trie_common_prefix)(child->edge, child->edge_length,
                                            bytes + offset, length - offset);
    if (common != child->edge_length) {
      return NULL;
    }
    offset += common;
    node = child;
  }
  return node;
}

bool FUNC(ds_radix_trie_contains)(ds_radix_trie_t *trie, const void *key,
                                  size_t length) {
  ds_radix_trie_node_t *node;
  bool result;
  node = FUNC(radix_trie_find_node)(trie, (const unsigned char *)key, length);
  result = node != NULL && node->is_terminal;
  return result;
}

void *FUNC(ds_radix_trie_get)(ds_radix_trie_t *trie, const void *key,
                              size_t length) {
  ds_radix_trie_node_t *node;
  void *result = NULL;
  node = FUNC(radix_trie_find_node)(trie, (const unsigned char *)key, length);
  if (node != NULL && node->is_terminal) {
    result = node->value;
  }
  return result;
}

static void FUNC(radix_trie_merge_with_child)(ds_radix_trie_t *trie,
                                              ds_radix_trie_node_t *node) {
  /* node has exactly one child and is not terminal: merge edges */
  ds_radix_trie_node_t *child = node->children[0];
  size_t merged_length = node->edge_length + child->edge_length;
  unsigned char *merged = (unsigned char *)trie->allocator(merged_length);
  size_t i;
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
  if (child->edge != NULL) {
    trie->deallocator(child->edge);
  }
  trie->deallocator(child);
}

bool FUNC(ds_radix_trie_remove)(ds_radix_trie_t *trie, const void *key,
                                size_t length, void **out_value) {
  const unsigned char *bytes = (const unsigned char *)key;
  ds_radix_trie_node_t *node;
  ds_radix_trie_node_t *parent;
  ds_radix_trie_node_t *stack[256];
  size_t depth = 0;
  size_t offset = 0;
  bool removed = false;
  node = trie->root;
  stack[depth] = node;
  depth += 1;
  while (offset < length) {
    ds_radix_trie_node_t *child =
        FUNC(radix_trie_find_child)(node, bytes[offset]);
    size_t common;
    if (child == NULL || depth >= 256) {
      node = NULL;
      break;
    }
    common = FUNC(radix_trie_common_prefix)(child->edge, child->edge_length,
                                            bytes + offset, length - offset);
    if (common != child->edge_length) {
      node = NULL;
      break;
    }
    offset += common;
    node = child;
    stack[depth] = node;
    depth += 1;
  }
  if (node != NULL && node->is_terminal) {
    if (out_value != NULL) {
      *out_value = node->value;
    }
    node->is_terminal = false;
    node->value = NULL;
    trie->size -= 1;
    removed = true;
    /* cleanup: prune empty leaves and merge single-child chains */
    while (depth >= 2) {
      ds_radix_trie_node_t *current = stack[depth - 1];
      parent = stack[depth - 2];
      if (!current->is_terminal && current->num_children == 0) {
        FUNC(radix_trie_remove_child)(trie, parent, current);
        if (current->edge != NULL) {
          trie->deallocator(current->edge);
        }
        trie->deallocator(current);
        depth -= 1;
      } else if (!current->is_terminal && current->num_children == 1 &&
                 current != trie->root) {
        FUNC(radix_trie_merge_with_child)(trie, current);
        break;
      } else {
        break;
      }
    }
  }
  return removed;
}

size_t FUNC(ds_radix_trie_size)(ds_radix_trie_t *trie) {
  size_t result;
  result = trie->size;
  return result;
}

bool FUNC(ds_radix_trie_is_empty)(ds_radix_trie_t *trie) {
  bool result;
  result = trie->size == 0;
  return result;
}

static void FUNC(radix_trie_walk_recursive)(
    ds_radix_trie_node_t *node, unsigned char **buffer, size_t *buffer_cap,
    size_t prefix_length, void *(*allocator)(size_t),
    void (*deallocator)(void *),
    void (*callback)(const unsigned char *, size_t, void *)) {
  size_t needed = prefix_length + node->edge_length;
  size_t i;
  if (needed > *buffer_cap) {
    size_t new_cap = *buffer_cap == 0 ? 16 : *buffer_cap;
    unsigned char *grown;
    size_t j;
    while (new_cap < needed) {
      new_cap *= 2;
    }
    grown = (unsigned char *)allocator(new_cap);
    for (j = 0; j < prefix_length; j++) {
      grown[j] = (*buffer)[j];
    }
    if (*buffer != NULL) {
      deallocator(*buffer);
    }
    *buffer = grown;
    *buffer_cap = new_cap;
  }
  for (i = 0; i < node->edge_length; i++) {
    (*buffer)[prefix_length + i] = node->edge[i];
  }
  if (node->is_terminal) {
    callback(*buffer, needed, node->value);
  }
  for (i = 0; i < node->num_children; i++) {
    FUNC(radix_trie_walk_recursive)
    (node->children[i], buffer, buffer_cap, needed, allocator, deallocator,
     callback);
  }
}

void FUNC(ds_radix_trie_walk)(ds_radix_trie_t *trie,
                              void (*callback)(const unsigned char *, size_t,
                                               void *)) {
  unsigned char *buffer = NULL;
  size_t buffer_cap = 0;
  FUNC(radix_trie_walk_recursive)
  (trie->root, &buffer, &buffer_cap, 0, trie->allocator, trie->deallocator,
   callback);
  if (buffer != NULL) {
    trie->deallocator(buffer);
  }
}

static void FUNC(radix_trie_destroy_recursive)(ds_radix_trie_t *trie,
                                               ds_radix_trie_node_t *node,
                                               void (*destroy_value)(void *)) {
  size_t i;
  for (i = 0; i < node->num_children; i++) {
    FUNC(radix_trie_destroy_recursive)(trie, node->children[i], destroy_value);
  }
  if (node->is_terminal && destroy_value != NULL) {
    destroy_value(node->value);
  }
  if (node->children != NULL) {
    trie->deallocator(node->children);
  }
  if (node->edge != NULL) {
    trie->deallocator(node->edge);
  }
  trie->deallocator(node);
}

void FUNC(ds_radix_trie_destroy)(ds_radix_trie_t *trie,
                                 void (*destroy_value)(void *)) {
  FUNC(radix_trie_destroy_recursive)(trie, trie->root, destroy_value);
  trie->deallocator(trie);
}
