#include "persistent_trie.h"
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct ds_ptrie_edge {
  unsigned char token;
  struct ds_ptrie_node *child;
  struct ds_ptrie_edge *next;
} ds_ptrie_edge_t;

typedef struct ds_ptrie_node {
  unsigned long refs;
  unsigned char *prefix; /* bytes after this node's incoming edge token */
  size_t prefix_len;
  bool terminal;
  void *value;
  ds_ptrie_edge_t *children;
  size_t count; /* terminal nodes in the subtree, this one included */
} ds_ptrie_node_t;

struct ds_ptrie_version {
  unsigned long refs;
  ds_ptrie_node_t *root;
  ds_context_t *context; /* the trie's, for the scratch memory of visits */
  ds_ptrie_t *owner;
};

struct ds_ptrie {
  ds_ptrie_config_t config;
  ds_ptrie_version_t *root;
  ds_status_t error;
};

static ds_ptrie_node_t *ptrie_node_create(ds_ptrie_t *trie);
static ds_status_t ptrie_value_clone(ds_ptrie_t *trie, void *value, void **out);
static void ptrie_value_destroy(ds_ptrie_t *trie, void *value);
static bool ptrie_node_retain(ds_ptrie_node_t *node);
static void ptrie_node_release(ds_ptrie_t *trie, ds_ptrie_node_t *node);
static ds_ptrie_edge_t *ptrie_edge_find(ds_ptrie_edge_t *edge,
                                        unsigned char token);
static ds_ptrie_edge_t *ptrie_edge_clone_list(ds_ptrie_t *trie,
                                              ds_ptrie_edge_t *edge);
static bool ptrie_node_empty(ds_ptrie_node_t *node);
static ds_ptrie_node_t *ptrie_node_clone_shallow(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node);
static ds_ptrie_version_t *ptrie_version_create(ds_ptrie_t *trie,
                                                ds_ptrie_node_t *root);
static ds_ptrie_node_t *ptrie_find_node(ds_ptrie_node_t *node,
                                        const unsigned char *key, size_t len);
static int ptrie_key_compare(const unsigned char *a, size_t alen,
                             const unsigned char *b, size_t blen);
static bool ptrie_key_in_range(const unsigned char *key, size_t key_len,
                               const unsigned char *lower, size_t lower_len,
                               const unsigned char *upper, size_t upper_len);

/* a visit in key order: the key of the current path, the visitor and the
 * bounds of a range (NULL when open) */
typedef struct ptrie_walk {
  ds_context_t *context;
  unsigned char *key;
  size_t key_capacity;
  ds_ptrie_visit_func_t visit;
  void *user;
  const unsigned char *lower;
  size_t lower_len;
  const unsigned char *upper;
  size_t upper_len;
} ptrie_walk_t;

static ds_status_t ptrie_walk(ptrie_walk_t *walk, ds_ptrie_node_t *node,
                              size_t depth);

void ds_ptrie_config_init(ds_ptrie_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->clone_value = NULL;
  config->clone_value_ex = NULL;
  config->destroy_value = NULL;
  config->user = NULL;
  config->context = NULL;
  config->storage = DS_PTRIE_BYTES;
}

static ds_status_t ptrie_value_clone(ds_ptrie_t *trie, void *value,
                                     void **out) {
  ds_status_t st = DS_OK;
  *out = NULL;
  if (trie->config.clone_value_ex != NULL) {
    st = trie->config.clone_value_ex(value, trie->config.user, out);
    if (st > DS_OK)
      st = DS_ERR_CALLBACK;
  } else if (trie->config.clone_value != NULL) {
    *out = trie->config.clone_value(value, trie->config.user);
    if (*out == NULL && value != NULL)
      st = DS_ERR_CALLBACK;
  } else
    *out = value;
  if (st != DS_OK)
    trie->error = st;
  return st;
}

static void ptrie_value_destroy(ds_ptrie_t *trie, void *value) {
  if (trie->config.destroy_value != NULL) {
    trie->config.destroy_value(value, trie->config.user);
  }
}

static ds_ptrie_node_t *ptrie_node_create(ds_ptrie_t *trie) {
  ds_ptrie_node_t *node;
  node =
      (ds_ptrie_node_t *)ds_context_alloc(trie->config.context, sizeof(*node));
  if (node == NULL) {
    return NULL;
  }
  node->refs = 1UL;
  node->prefix = NULL;
  node->prefix_len = 0;
  node->terminal = false;
  node->value = NULL;
  node->children = NULL;
  node->count = 0U;
  return node;
}

static bool ptrie_node_retain(ds_ptrie_node_t *node) {
  if (node != NULL) {
    if (node->refs == ULONG_MAX)
      return false;
    node->refs++;
  }
  return true;
}

/* Replace a private node's label, preparing the new bytes first. */
static bool ptrie_label(ds_ptrie_t *trie, ds_ptrie_node_t *node,
                        const unsigned char *bytes, size_t len) {
  unsigned char *copy = NULL;
  if (len != 0) {
    copy = (unsigned char *)ds_context_alloc(trie->config.context, len);
    if (copy == NULL)
      return false;
    memcpy(copy, bytes, len);
  }
  ds_context_free(trie->config.context, node->prefix);
  node->prefix = copy;
  node->prefix_len = len;
  return true;
}

/* A node whose last reference goes releases its children in turn. The
 * nodes to free form a list linked through their value pointers (each
 * value is destroyed first), so a chain as deep as the longest key needs
 * neither recursion nor memory. */
static void ptrie_doom(ds_ptrie_t *trie, ds_ptrie_node_t **list,
                       ds_ptrie_node_t *node) {
  if (node->terminal) {
    ptrie_value_destroy(trie, node->value);
  }
  node->value = *list;
  *list = node;
}

static void ptrie_node_release(ds_ptrie_t *trie, ds_ptrie_node_t *node) {
  ds_ptrie_node_t *list = NULL;
  if (node == NULL) {
    return;
  }
  node->refs--;
  if (node->refs != 0UL) {
    return;
  }
  ptrie_doom(trie, &list, node);
  while (list != NULL) {
    ds_ptrie_node_t *x = list;
    ds_ptrie_edge_t *edge = x->children;
    list = (ds_ptrie_node_t *)x->value;
    while (edge != NULL) {
      ds_ptrie_edge_t *next = edge->next;
      ds_ptrie_node_t *child = edge->child;
      if (child != NULL) {
        child->refs--;
        if (child->refs == 0UL) {
          ptrie_doom(trie, &list, child);
        }
      }
      ds_context_free(trie->config.context, edge);
      edge = next;
    }
    ds_context_free(trie->config.context, x->prefix);
    ds_context_free(trie->config.context, x);
  }
}

/* a node's edges are kept in token order, so visits come out in key order
 * and a search stops at the first larger token */
static ds_ptrie_edge_t *ptrie_edge_find(ds_ptrie_edge_t *edge,
                                        unsigned char token) {
  while (edge != NULL && edge->token < token) {
    edge = edge->next;
  }
  return edge != NULL && edge->token == token ? edge : NULL;
}

static ds_ptrie_edge_t *ptrie_edge_clone_list(ds_ptrie_t *trie,
                                              ds_ptrie_edge_t *edge) {
  ds_ptrie_edge_t *head;
  ds_ptrie_edge_t *tail;
  ds_ptrie_edge_t *copy;
  head = NULL;
  tail = NULL;
  while (edge != NULL) {
    copy = (ds_ptrie_edge_t *)ds_context_alloc(trie->config.context,
                                               sizeof(*copy));
    if (copy != NULL && !ptrie_node_retain(edge->child)) {
      ds_context_free(trie->config.context, copy);
      copy = NULL;
      trie->error = DS_ERR_OVERFLOW;
    }
    if (copy == NULL) {
      while (head != NULL) {
        copy = head->next;
        ptrie_node_release(trie, head->child);
        ds_context_free(trie->config.context, head);
        head = copy;
      }
      return NULL;
    }
    copy->token = edge->token;
    copy->child = edge->child;
    copy->next = NULL;
    if (tail == NULL) {
      head = copy;
    } else {
      tail->next = copy;
    }
    tail = copy;
    edge = edge->next;
  }
  return head;
}

static bool ptrie_node_empty(ds_ptrie_node_t *node) {
  return node != NULL && !node->terminal && node->children == NULL;
}

static ds_ptrie_node_t *ptrie_node_clone_shallow(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node) {
  ds_ptrie_node_t *copy;
  copy = ptrie_node_create(trie);
  if (copy == NULL) {
    return NULL;
  }
  if (!ptrie_label(trie, copy, node->prefix, node->prefix_len)) {
    ptrie_node_release(trie, copy);
    return NULL;
  }
  copy->terminal = node->terminal;
  copy->value = NULL;
  copy->count = node->count;
  if (node->terminal) {
    if (ptrie_value_clone(trie, node->value, &copy->value) != DS_OK) {
      copy->terminal = false;
      ptrie_node_release(trie, copy);
      return NULL;
    }
  }
  copy->children = ptrie_edge_clone_list(trie, node->children);
  if (node->children != NULL && copy->children == NULL) {
    if (copy->terminal) {
      ptrie_value_destroy(trie, copy->value);
    }
    ds_context_free(trie->config.context, copy->prefix);
    ds_context_free(trie->config.context, copy);
    return NULL;
  }
  return copy;
}

/* Updates copy the path from the root to the key, bottom up: path[d] is
 * the node at depth d in the base version (NULL past its end), and each
 * copy takes the copy below it as the child on the key's edge. Keys can be
 * long, so the path lives in an array, not in call frames. */
static ds_ptrie_node_t **ptrie_path(ds_ptrie_t *trie, ds_ptrie_node_t *root,
                                    const unsigned char *key, size_t len) {
  ds_ptrie_node_t **path;
  size_t d;
  if (len >= (size_t)-1 / sizeof(*path)) {
    return NULL;
  }
  path = (ds_ptrie_node_t **)ds_context_alloc(trie->config.context,
                                              (len + 1U) * sizeof(*path));
  if (path == NULL) {
    return NULL;
  }
  path[0] = root;
  for (d = 0U; d < len; d++) {
    ds_ptrie_edge_t *edge =
        path[d] == NULL ? NULL : ptrie_edge_find(path[d]->children, key[d]);
    path[d + 1U] = edge == NULL ? NULL : edge->child;
  }
  return path;
}

/* puts child on copy's edge for token (creating the edge, or dropping it
 * when child is NULL); false when an edge cannot be allocated */
static bool ptrie_set_child(ds_ptrie_t *trie, ds_ptrie_node_t *copy,
                            unsigned char token, ds_ptrie_node_t *child) {
  ds_ptrie_edge_t *prev = NULL;
  ds_ptrie_edge_t *edge = copy->children;
  ds_ptrie_edge_t *added;
  while (edge != NULL && edge->token < token) {
    prev = edge;
    edge = edge->next;
  }
  if (edge != NULL && edge->token == token) {
    ds_ptrie_node_t *old = edge->child;
    if (child == NULL) {
      if (prev == NULL) {
        copy->children = edge->next;
      } else {
        prev->next = edge->next;
      }
      ds_context_free(trie->config.context, edge);
    } else {
      edge->child = child;
    }
    ptrie_node_release(trie, old); /* the reference the copy took */
    return true;
  }
  if (child == NULL) {
    return true;
  }
  added =
      (ds_ptrie_edge_t *)ds_context_alloc(trie->config.context, sizeof(*added));
  if (added == NULL) {
    return false;
  }
  added->token = token;
  added->child = child;
  added->next = edge; /* in token order */
  if (prev == NULL) {
    copy->children = added;
  } else {
    prev->next = added;
  }
  return true;
}

/* the new root with key set to value (which the new version owns), or
 * NULL when memory runs out (value then still belongs to the caller) */
static ds_ptrie_node_t *ptrie_insert_path(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *root,
                                          const unsigned char *key, size_t len,
                                          void *value) {
  ds_ptrie_node_t **path = ptrie_path(trie, root, key, len);
  ds_ptrie_node_t *child = NULL;
  ds_ptrie_node_t *leaf = NULL;
  bool added;
  size_t d;
  if (path == NULL) {
    return NULL;
  }
  added = path[len] == NULL || !path[len]->terminal;
  for (d = len + 1U; d-- > 0U;) {
    ds_ptrie_node_t *copy = path[d] != NULL
                                ? ptrie_node_clone_shallow(trie, path[d])
                                : ptrie_node_create(trie);
    if (copy == NULL) {
      break;
    }
    if (d == len) {
      if (copy->terminal) {
        ptrie_value_destroy(trie, copy->value);
      }
      copy->terminal = true;
      copy->value = value;
      leaf = copy;
    } else if (!ptrie_set_child(trie, copy, key[d], child)) {
      ptrie_node_release(trie, copy);
      break;
    }
    copy->count = (path[d] != NULL ? path[d]->count : 0U) + (added ? 1U : 0U);
    child = copy; /* the copy above takes it over */
    if (d == 0U) {
      ds_context_free(trie->config.context, path);
      return child;
    }
  }
  if (leaf != NULL) {
    leaf->terminal = false; /* the caller still owns value */
    leaf->value = NULL;
  }
  ptrie_node_release(trie, child);
  ds_context_free(trie->config.context, path);
  return NULL;
}

/* removes key (from_prefix false) or every key under it (true); the new
 * root, or NULL when nothing was removed or memory ran out (*removed says
 * how many keys went) */
static ds_ptrie_node_t *ptrie_remove_path(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *root,
                                          const unsigned char *key, size_t len,
                                          bool from_prefix, size_t *removed) {
  ds_ptrie_node_t **path;
  ds_ptrie_node_t *child = NULL;
  size_t d;
  size_t gone;
  *removed = 0U;
  path = ptrie_path(trie, root, key, len);
  if (path == NULL) {
    return NULL;
  }
  if (path[len] == NULL ||
      (from_prefix ? path[len]->count == 0U : !path[len]->terminal)) {
    ds_context_free(trie->config.context, path);
    return NULL;
  }
  gone = from_prefix ? path[len]->count : 1U;
  for (d = len + 1U; d-- > 0U;) {
    ds_ptrie_node_t *copy;
    if (d == len && from_prefix) {
      copy = ptrie_node_create(trie); /* the whole subtree goes */
    } else {
      copy = ptrie_node_clone_shallow(trie, path[d]);
    }
    if (copy == NULL) {
      break;
    }
    if (d == len) {
      if (copy->terminal) {
        ptrie_value_destroy(trie, copy->value);
        copy->terminal = false;
        copy->value = NULL;
      }
    } else if (!ptrie_set_child(trie, copy, key[d], child)) {
      ptrie_node_release(trie, copy);
      break;
    }
    copy->count = path[d]->count - gone;
    if (d != 0U && ptrie_node_empty(copy)) {
      ptrie_node_release(trie, copy);
      copy = NULL;
    }
    child = copy;
    if (d == 0U) {
      ds_context_free(trie->config.context, path);
      *removed = gone;
      return child;
    }
  }
  ptrie_node_release(trie, child);
  ds_context_free(trie->config.context, path);
  return NULL;
}

/* Radix paths keep one frame per branch, rather than per byte. */
typedef struct ptrie_path_frame {
  ds_ptrie_node_t *node;
  unsigned char token;
} ptrie_path_frame_t;

static void *ptrie_grow(ds_context_t *context, void *block, size_t *capacity,
                        size_t needed, size_t width);

static bool ptrie_path_push(ds_ptrie_t *trie, ptrie_path_frame_t **path,
                            size_t *used, size_t *cap, ds_ptrie_node_t *node,
                            unsigned char token) {
  if (*used == *cap) {
    void *grown;
    if (*used == (size_t)-1) {
      trie->error = DS_ERR_OVERFLOW;
      return false;
    }
    grown =
        ptrie_grow(trie->config.context, *path, cap, *used + 1, sizeof(**path));
    if (grown == NULL)
      return false;
    *path = (ptrie_path_frame_t *)grown;
  }
  (*path)[*used].node = node;
  (*path)[*used].token = token;
  (*used)++;
  return true;
}

static ds_ptrie_node_t *
ptrie_radix_leaf(ds_ptrie_t *trie, const unsigned char *suffix, size_t len) {
  ds_ptrie_node_t *node = ptrie_node_create(trie);
  if (node != NULL && !ptrie_label(trie, node, suffix, len)) {
    ptrie_node_release(trie, node);
    return NULL;
  }
  if (node != NULL)
    node->count = 1;
  /* Its value is installed only after every path copy has succeeded. */
  return node;
}

static ds_ptrie_node_t *ptrie_radix_insert(ds_ptrie_t *trie,
                                           ds_ptrie_node_t *root,
                                           const unsigned char *key, size_t len,
                                           void *value) {
  ptrie_path_frame_t *path = NULL;
  size_t used = 0, cap = 0, pos = 0;
  ds_ptrie_node_t *node = root, *result = NULL, *leaf = NULL;
  bool added = true;
  for (;;) {
    size_t common = 0;
    ds_ptrie_edge_t *edge;
    while (common < node->prefix_len && common < len - pos &&
           node->prefix[common] == key[pos + common])
      common++;
    if (common != node->prefix_len) {
      ds_ptrie_node_t *old;
      unsigned char token = node->prefix[common];
      result = ptrie_node_create(trie);
      if (result == NULL || !ptrie_label(trie, result, node->prefix, common))
        goto fail;
      old = ptrie_node_clone_shallow(trie, node);
      if (old == NULL)
        goto fail;
      if (!ptrie_label(trie, old, node->prefix + common + 1,
                       node->prefix_len - common - 1) ||
          !ptrie_set_child(trie, result, token, old)) {
        ptrie_node_release(trie, old);
        goto fail;
      }
      pos += common;
      if (pos == len)
        leaf = result;
      else {
        leaf = ptrie_radix_leaf(trie, key + pos + 1, len - pos - 1);
        if (leaf == NULL)
          goto fail;
        if (!ptrie_set_child(trie, result, key[pos], leaf)) {
          ptrie_node_release(trie, leaf);
          goto fail;
        }
      }
      if (node->count == (size_t)-1) {
        trie->error = DS_ERR_OVERFLOW;
        goto fail;
      }
      result->count = node->count + 1;
      break;
    }
    pos += common;
    if (pos == len) {
      added = !node->terminal;
      result = ptrie_node_clone_shallow(trie, node);
      if (result == NULL)
        goto fail;
      if (result->terminal)
        ptrie_value_destroy(trie, result->value);
      result->terminal = false;
      result->value = NULL;
      leaf = result;
      if (added && node->count == (size_t)-1) {
        trie->error = DS_ERR_OVERFLOW;
        goto fail;
      }
      result->count += added ? 1U : 0U;
      break;
    }
    edge = ptrie_edge_find(node->children, key[pos]);
    if (edge == NULL) {
      result = ptrie_node_clone_shallow(trie, node);
      if (result == NULL)
        goto fail;
      leaf = ptrie_radix_leaf(trie, key + pos + 1, len - pos - 1);
      if (leaf == NULL)
        goto fail;
      if (!ptrie_set_child(trie, result, key[pos], leaf)) {
        ptrie_node_release(trie, leaf);
        goto fail;
      }
      if (node->count == (size_t)-1) {
        trie->error = DS_ERR_OVERFLOW;
        goto fail;
      }
      result->count++;
      break;
    }
    if (!ptrie_path_push(trie, &path, &used, &cap, node, key[pos]))
      goto fail;
    pos++;
    node = edge->child;
  }
  while (used != 0) {
    ds_ptrie_node_t *copy;
    ptrie_path_frame_t *frame = &path[--used];
    if (added && frame->node->count == (size_t)-1) {
      trie->error = DS_ERR_OVERFLOW;
      goto fail;
    }
    copy = ptrie_node_clone_shallow(trie, frame->node);
    if (copy == NULL)
      goto fail;
    if (!ptrie_set_child(trie, copy, frame->token, result)) {
      ptrie_node_release(trie, copy);
      goto fail;
    }
    copy->count += added ? 1U : 0U;
    result = copy;
  }
  leaf->terminal = true;
  leaf->value = value;
  ds_context_free(trie->config.context, path);
  return result;
fail:
  ptrie_node_release(trie, result);
  ds_context_free(trie->config.context, path);
  return NULL;
}

/* A private non-root path copy can absorb its sole child. Both the value
 * and children are copied/retained before the old path copy is released. */
static bool ptrie_coalesce(ds_ptrie_t *trie, ds_ptrie_node_t **link) {
  ds_ptrie_node_t *node = *link;
  while (node != NULL && !node->terminal && node->children != NULL &&
         node->children->next == NULL) {
    ds_ptrie_node_t *child = node->children->child, *copy;
    unsigned char *label;
    size_t len;
    if (node->prefix_len == (size_t)-1 ||
        child->prefix_len > (size_t)-1 - node->prefix_len - 1) {
      trie->error = DS_ERR_OVERFLOW;
      return false;
    }
    len = node->prefix_len + 1 + child->prefix_len;
    label = (unsigned char *)ds_context_alloc(trie->config.context, len);
    if (label == NULL)
      return false;
    if (node->prefix_len != 0)
      memcpy(label, node->prefix, node->prefix_len);
    label[node->prefix_len] = node->children->token;
    if (child->prefix_len != 0)
      memcpy(label + node->prefix_len + 1, child->prefix, child->prefix_len);
    copy = ptrie_node_clone_shallow(trie, child);
    if (copy == NULL) {
      ds_context_free(trie->config.context, label);
      return false;
    }
    ds_context_free(trie->config.context, copy->prefix);
    copy->prefix = label;
    copy->prefix_len = len;
    ptrie_node_release(trie, node);
    *link = node = copy;
  }
  return true;
}

static ds_ptrie_node_t *ptrie_radix_remove(ds_ptrie_t *trie,
                                           ds_ptrie_node_t *root,
                                           const unsigned char *key, size_t len,
                                           bool prefix, size_t *removed) {
  ptrie_path_frame_t *path = NULL;
  size_t used = 0, cap = 0, pos = 0, gone = 0;
  ds_ptrie_node_t *node = root, *result = NULL;
  *removed = 0;
  for (;;) {
    size_t common = 0;
    ds_ptrie_edge_t *edge;
    while (common < node->prefix_len && common < len - pos &&
           node->prefix[common] == key[pos + common])
      common++;
    pos += common;
    if (pos == len && (prefix || common == node->prefix_len))
      break;
    if (common != node->prefix_len || pos == len)
      goto missing;
    edge = ptrie_edge_find(node->children, key[pos]);
    if (edge == NULL)
      goto missing;
    if (!ptrie_path_push(trie, &path, &used, &cap, node, key[pos]))
      goto fail;
    pos++;
    node = edge->child;
  }
  if (prefix ? node->count == 0 : !node->terminal)
    goto missing;
  gone = prefix ? node->count : 1;
  if (!prefix) {
    result = ptrie_node_clone_shallow(trie, node);
    if (result == NULL)
      goto fail;
    ptrie_value_destroy(trie, result->value);
    result->value = NULL;
    result->terminal = false;
    result->count--;
    if (result->count == 0) {
      ptrie_node_release(trie, result);
      result = NULL;
    } else if (used != 0 && !ptrie_coalesce(trie, &result))
      goto fail;
  }
  while (used != 0) {
    ptrie_path_frame_t *frame = &path[--used];
    ds_ptrie_node_t *copy = ptrie_node_clone_shallow(trie, frame->node);
    if (copy == NULL)
      goto fail;
    if (!ptrie_set_child(trie, copy, frame->token, result)) {
      ptrie_node_release(trie, copy);
      goto fail;
    }
    copy->count -= gone;
    result = copy;
    if (used != 0 && copy->count == 0) {
      ptrie_node_release(trie, copy);
      result = NULL;
    } else if (used != 0 && !ptrie_coalesce(trie, &result))
      goto fail;
  }
  if (result == NULL)
    result = ptrie_node_create(trie);
  if (result == NULL)
    goto fail;
  ds_context_free(trie->config.context, path);
  *removed = gone;
  return result;
missing:
  trie->error = DS_NOT_FOUND;
fail:
  ptrie_node_release(trie, result);
  ds_context_free(trie->config.context, path);
  return NULL;
}

static ds_ptrie_version_t *ptrie_version_create(ds_ptrie_t *trie,
                                                ds_ptrie_node_t *root) {
  ds_ptrie_version_t *version;
  version = (ds_ptrie_version_t *)ds_context_alloc(trie->config.context,
                                                   sizeof(*version));
  if (version == NULL) {
    return NULL;
  }
  version->refs = 1UL;
  version->root = root;
  version->context = trie->config.context;
  version->owner = trie;
  return version;
}

static ds_ptrie_node_t *ptrie_find_match(ds_ptrie_node_t *node,
                                         const unsigned char *key, size_t len,
                                         bool prefix, size_t *matched) {
  size_t pos = 0;
  if (node == NULL || (key == NULL && len != 0))
    return NULL;
  for (;;) {
    size_t common = 0;
    ds_ptrie_edge_t *edge;
    while (common < node->prefix_len && common < len - pos &&
           node->prefix[common] == key[pos + common])
      common++;
    pos += common;
    if (pos == len && (prefix || common == node->prefix_len)) {
      if (matched != NULL)
        *matched = common;
      return node;
    }
    if (common != node->prefix_len || pos == len)
      return NULL;
    edge = ptrie_edge_find(node->children, key[pos++]);
    if (edge == NULL)
      return NULL;
    node = edge->child;
  }
}

static ds_ptrie_node_t *ptrie_find_node(ds_ptrie_node_t *node,
                                        const unsigned char *key, size_t len) {
  return ptrie_find_match(node, key, len, true, NULL);
}

static int ptrie_key_compare(const unsigned char *a, size_t alen,
                             const unsigned char *b, size_t blen) {
  size_t i;
  size_t n;

  n = alen < blen ? alen : blen;
  for (i = 0U; i < n; i++) {
    if (a[i] < b[i]) {
      return -1;
    }
    if (a[i] > b[i]) {
      return 1;
    }
  }

  if (alen < blen) {
    return -1;
  }
  if (alen > blen) {
    return 1;
  }
  return 0;
}

static bool ptrie_key_in_range(const unsigned char *key, size_t key_len,
                               const unsigned char *lower, size_t lower_len,
                               const unsigned char *upper, size_t upper_len) {
  if (lower != NULL && ptrie_key_compare(key, key_len, lower, lower_len) < 0) {
    return false;
  }
  if (upper != NULL && ptrie_key_compare(key, key_len, upper, upper_len) > 0) {
    return false;
  }
  return true;
}

/* a block of capacity * 2 (at least needed) items with the old ones
 * copied, from the context, or NULL (the old block stays) */
static void *ptrie_grow(ds_context_t *context, void *block, size_t *capacity,
                        size_t needed, size_t width) {
  void *next;
  size_t new_capacity;

  new_capacity = *capacity == 0U ? 16U : *capacity;
  while (new_capacity < needed) {
    if (new_capacity > (size_t)-1 / 2U / width) {
      return NULL;
    }
    new_capacity *= 2U;
  }
  if (new_capacity > (size_t)-1 / width) {
    return NULL;
  }
  next = ds_context_alloc(context, new_capacity * width);
  if (next == NULL) {
    return NULL;
  }
  if (block != NULL) {
    memcpy(next, block, *capacity * width);
    ds_context_free(context, block);
  }
  *capacity = new_capacity;
  return next;
}

/* where the path of length len stands against the range: -1 when every
 * key under it is below the lower bound, 1 when it is above the upper
 * bound (and so is every key after it in key order), 0 otherwise */
static int ptrie_path_position(const ptrie_walk_t *walk, size_t len) {
  size_t n;
  if (walk->upper != NULL &&
      ptrie_key_compare(walk->key, len, walk->upper, walk->upper_len) > 0) {
    return 1;
  }
  if (walk->lower != NULL) {
    n = len < walk->lower_len ? len : walk->lower_len;
    if (n != 0U && memcmp(walk->key, walk->lower, n) < 0) {
      return -1;
    }
  }
  return 0;
}

/* Visits the subtree of node, whose path fills walk->key up to depth, in
 * key order (edges are sorted, and a key comes before its extensions).
 * The walk is iterative: stack[i] is the next edge to take at depth + i,
 * so it is as deep as the longest key without call frames. Subtrees below
 * the range are skipped, and the walk ends at the first path above it. */
typedef struct ptrie_walk_frame {
  ds_ptrie_edge_t *edge;
  size_t depth;
} ptrie_walk_frame_t;

static ds_status_t ptrie_walk(ptrie_walk_t *walk, ds_ptrie_node_t *node,
                              size_t depth) {
  ptrie_walk_frame_t *stack;
  size_t cap = 16, top = 0;
  ds_status_t status = DS_OK;
  void *grown;
  if (node == NULL || ptrie_path_position(walk, depth) != 0)
    return DS_OK;
  if (node->terminal &&
      ptrie_key_in_range(walk->key, depth, walk->lower, walk->lower_len,
                         walk->upper, walk->upper_len) &&
      !walk->visit(walk->key, depth, node->value, walk->user))
    return DS_STOP;
  if (node->children == NULL)
    return DS_OK;
  stack = (ptrie_walk_frame_t *)ds_context_alloc(walk->context,
                                                 cap * sizeof(*stack));
  if (stack == NULL)
    return DS_ERR_ALLOC;
  stack[top].edge = node->children;
  stack[top++].depth = depth;
  while (top != 0) {
    ds_ptrie_edge_t *edge = stack[top - 1].edge;
    size_t at = stack[top - 1].depth, end;
    ds_ptrie_node_t *child;
    int position;
    if (edge == NULL) {
      top--;
      continue;
    }
    stack[top - 1].edge = edge->next;
    child = edge->child;
    if (at == (size_t)-1 || child->prefix_len > (size_t)-1 - at - 1) {
      status = DS_ERR_OVERFLOW;
      break;
    }
    end = at + 1 + child->prefix_len;
    if (end > walk->key_capacity) {
      grown = ptrie_grow(walk->context, walk->key, &walk->key_capacity, end, 1);
      if (grown == NULL) {
        status = DS_ERR_ALLOC;
        break;
      }
      walk->key = (unsigned char *)grown;
    }
    walk->key[at] = edge->token;
    if (child->prefix_len != 0)
      memcpy(walk->key + at + 1, child->prefix, child->prefix_len);
    position = ptrie_path_position(walk, end);
    if (position > 0)
      break;
    if (position < 0)
      continue;
    if (child->terminal &&
        ptrie_key_in_range(walk->key, end, walk->lower, walk->lower_len,
                           walk->upper, walk->upper_len) &&
        !walk->visit(walk->key, end, child->value, walk->user)) {
      status = DS_STOP;
      break;
    }
    if (child->children != NULL) {
      if (top == cap) {
        grown = ptrie_grow(walk->context, stack, &cap, top + 1, sizeof(*stack));
        if (grown == NULL) {
          status = DS_ERR_ALLOC;
          break;
        }
        stack = (ptrie_walk_frame_t *)grown;
      }
      stack[top].edge = child->children;
      stack[top++].depth = end;
    }
  }
  ds_context_free(walk->context, stack);
  return status;
}

static ds_ptrie_t *ptrie_create(const ds_ptrie_config_t *config) {
  ds_ptrie_t *trie;
  ds_ptrie_node_t *node;
  trie = (ds_ptrie_t *)ds_context_alloc(
      config != NULL && config->context != NULL ? config->context
                                                : ds_default_context(),
      sizeof(*trie));
  if (trie == NULL) {
    return NULL;
  }
  if (config != NULL) {
    trie->config = *config;
  } else {
    ds_ptrie_config_init(&trie->config);
  }
  if (trie->config.context == NULL) {
    trie->config.context = ds_default_context();
  }
  trie->error = DS_OK;
  if ((trie->config.storage != DS_PTRIE_BYTES &&
       trie->config.storage != DS_PTRIE_RADIX) ||
      (trie->config.destroy_value != NULL && trie->config.clone_value == NULL &&
       trie->config.clone_value_ex == NULL) ||
      (trie->config.clone_value != NULL &&
       trie->config.clone_value_ex != NULL)) {
    ds_context_free(trie->config.context, trie);
    return NULL;
  }
  node = ptrie_node_create(trie);
  trie->root = node == NULL ? NULL : ptrie_version_create(trie, node);
  if (trie->root == NULL) {
    if (node != NULL) {
      ds_context_free(trie->config.context, node);
    }
    ds_context_free(trie->config.context, trie);
    return NULL;
  }
  return trie;
}

void ds_ptrie_destroy(ds_ptrie_t *trie) {
  if (trie == NULL) {
    return;
  }
  ds_ptrie_version_release(trie, trie->root);
  ds_context_free(trie->config.context, trie);
}

ds_ptrie_version_t *ds_ptrie_root(ds_ptrie_t *trie) {
  if (trie == NULL) {
    return NULL;
  }
  return trie->root;
}

static ds_ptrie_version_t *ptrie_insert_version(ds_ptrie_t *trie,
                                                ds_ptrie_version_t *base,
                                                const unsigned char *key,
                                                size_t len, void *value) {
  ds_ptrie_version_t *version;
  ds_ptrie_node_t *new_root;
  void *stored_value;
  if (trie == NULL || base == NULL || base->owner != trie ||
      (key == NULL && len != 0U)) {
    return NULL;
  }
  if (ptrie_value_clone(trie, value, &stored_value) != DS_OK)
    return NULL;
  new_root = trie->config.storage == DS_PTRIE_RADIX
                 ? ptrie_radix_insert(trie, base->root, key, len, stored_value)
                 : ptrie_insert_path(trie, base->root, key, len, stored_value);
  if (new_root == NULL) {
    ptrie_value_destroy(trie, stored_value);
    return NULL;
  }
  version = ptrie_version_create(trie, new_root);
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  return version;
}

static ds_ptrie_version_t *ptrie_remove_version(ds_ptrie_t *trie,
                                                ds_ptrie_version_t *base,
                                                const unsigned char *key,
                                                size_t len) {
  ds_ptrie_version_t *version;
  ds_ptrie_node_t *new_root;
  size_t removed;
  if (trie == NULL || base == NULL || base->owner != trie ||
      (key == NULL && len != 0U)) {
    return NULL;
  }
  new_root =
      trie->config.storage == DS_PTRIE_RADIX
          ? ptrie_radix_remove(trie, base->root, key, len, false, &removed)
          : ptrie_remove_path(trie, base->root, key, len, false, &removed);
  if (new_root == NULL) {
    return NULL;
  }
  version = ptrie_version_create(trie, new_root);
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  return version;
}

static ds_ptrie_version_t *
ptrie_remove_prefix_version(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                            const unsigned char *prefix, size_t prefix_len,
                            size_t *out_removed) {
  ds_ptrie_version_t *version;
  ds_ptrie_node_t *new_root;
  size_t removed_count;

  if (out_removed != NULL) {
    *out_removed = 0U;
  }
  if (trie == NULL || base == NULL || base->owner != trie ||
      (prefix == NULL && prefix_len != 0U)) {
    return NULL;
  }

  new_root = trie->config.storage == DS_PTRIE_RADIX
                 ? ptrie_radix_remove(trie, base->root, prefix, prefix_len,
                                      true, &removed_count)
                 : ptrie_remove_path(trie, base->root, prefix, prefix_len, true,
                                     &removed_count);
  if (new_root == NULL) {
    return NULL;
  }
  version = ptrie_version_create(trie, new_root);
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  if (out_removed != NULL) {
    *out_removed = removed_count;
  }
  return version;
}

ds_status_t ds_ptrie_get(ds_ptrie_version_t *version, const unsigned char *key,
                         size_t len, void **out_value) {
  ds_ptrie_node_t *node;
  if (out_value != NULL)
    *out_value = NULL;
  if (version == NULL || out_value == NULL || (key == NULL && len != 0))
    return DS_ERR_NULL;
  node = ptrie_find_match(version->root, key, len, false, NULL);
  if (node == NULL || !node->terminal)
    return DS_NOT_FOUND;
  *out_value = node->value;
  return DS_OK;
}

ds_status_t ds_ptrie_contains_prefix(ds_ptrie_version_t *version,
                                     const unsigned char *prefix,
                                     size_t prefix_len) {
  ds_ptrie_node_t *node;

  if (version == NULL || (prefix == NULL && prefix_len != 0U)) {
    return DS_ERR_NULL;
  }

  node = ptrie_find_node(version->root, prefix, prefix_len);
  return node == NULL ? DS_NOT_FOUND : DS_OK;
}

size_t ds_ptrie_count_prefix(ds_ptrie_version_t *version,
                             const unsigned char *prefix, size_t prefix_len) {
  ds_ptrie_node_t *node;

  if (version == NULL || (prefix == NULL && prefix_len != 0U)) {
    return 0U;
  }
  node = ptrie_find_node(version->root, prefix, prefix_len);
  return node == NULL ? 0U : node->count; /* kept along every path copy */
}

typedef struct ptrie_copy_prefix_state {
  ds_ptrie_t *trie;
  ds_ptrie_version_t *current;
  const unsigned char *source_prefix;
  size_t source_prefix_len;
  const unsigned char *destination_prefix;
  size_t destination_prefix_len;
  size_t copied;
  bool failed;
} ptrie_copy_prefix_state_t;

static bool ptrie_copy_prefix_visit(const unsigned char *key, size_t len,
                                    void *value, void *user) {
  ptrie_copy_prefix_state_t *state;
  ds_ptrie_version_t *next;
  unsigned char *new_key;
  size_t suffix_len;
  size_t new_len;

  state = (ptrie_copy_prefix_state_t *)user;
  if (state == NULL || len < state->source_prefix_len) {
    return false;
  }
  suffix_len = len - state->source_prefix_len;
  if (suffix_len > (size_t)-1 - state->destination_prefix_len) {
    state->trie->error = DS_ERR_OVERFLOW;
    state->failed = true;
    return false;
  }
  new_len = state->destination_prefix_len + suffix_len;
  new_key = NULL;
  if (new_len != 0U) {
    new_key =
        (unsigned char *)ds_context_alloc(state->trie->config.context, new_len);
    if (new_key == NULL) {
      state->failed = true;
      return false;
    }
    if (state->destination_prefix_len != 0U) {
      memcpy(new_key, state->destination_prefix, state->destination_prefix_len);
    }
    if (suffix_len != 0U) {
      memcpy(new_key + state->destination_prefix_len,
             key + state->source_prefix_len, suffix_len);
    }
  }
  next = ds_ptrie_insert(state->trie, state->current, new_key, new_len, value);
  if (new_key != NULL) {
    ds_context_free(state->trie->config.context, new_key);
  }
  if (next == NULL) {
    state->failed = true;
    return false;
  }
  ds_ptrie_version_release(state->trie, state->current);
  state->current = next;
  state->copied++;
  return true;
}

static ds_ptrie_version_t *ptrie_copy_prefix_version(
    ds_ptrie_t *trie, ds_ptrie_version_t *destination_base,
    ds_ptrie_version_t *source_version, const unsigned char *source_prefix,
    size_t source_prefix_len, const unsigned char *destination_prefix,
    size_t destination_prefix_len, size_t *out_copied) {
  ptrie_copy_prefix_state_t state;
  ds_status_t status;

  if (out_copied != NULL) {
    *out_copied = 0U;
  }
  if (trie == NULL || destination_base == NULL || source_version == NULL ||
      destination_base->owner != trie ||
      (source_prefix == NULL && source_prefix_len != 0U) ||
      (destination_prefix == NULL && destination_prefix_len != 0U)) {
    return NULL;
  }

  if (ds_ptrie_count_prefix(source_version, source_prefix, source_prefix_len) ==
      0U) {
    return NULL;
  }

  state.trie = trie;
  state.current = destination_base;
  state.source_prefix = source_prefix;
  state.source_prefix_len = source_prefix_len;
  state.destination_prefix = destination_prefix;
  state.destination_prefix_len = destination_prefix_len;
  state.copied = 0U;
  state.failed = false;
  if (ds_ptrie_version_retain_ex(state.current) != DS_OK) {
    trie->error = DS_ERR_OVERFLOW;
    return NULL;
  }

  status =
      ds_ptrie_visit_prefix(source_version, source_prefix, source_prefix_len,
                            ptrie_copy_prefix_visit, &state);
  if (status != DS_OK || state.failed || state.copied == 0U) {
    if (status != DS_OK && status != DS_STOP)
      trie->error = status;
    ds_ptrie_version_release(trie, state.current);
    return NULL;
  }
  if (out_copied != NULL) {
    *out_copied = state.copied;
  }
  return state.current;
}

static ds_ptrie_version_t *
ptrie_move_prefix_version(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                          const unsigned char *source_prefix,
                          size_t source_prefix_len,
                          const unsigned char *destination_prefix,
                          size_t destination_prefix_len, size_t *out_moved) {
  ds_ptrie_version_t *removed;
  ds_ptrie_version_t *moved;
  size_t removed_count;
  size_t copied_count;

  if (out_moved != NULL) {
    *out_moved = 0U;
  }
  if (trie == NULL || base == NULL || base->owner != trie ||
      (source_prefix == NULL && source_prefix_len != 0U) ||
      (destination_prefix == NULL && destination_prefix_len != 0U)) {
    return NULL;
  }
  if (source_prefix_len == destination_prefix_len &&
      (source_prefix_len == 0U ||
       memcmp(source_prefix, destination_prefix, source_prefix_len) == 0)) {
    removed_count =
        ds_ptrie_count_prefix(base, source_prefix, source_prefix_len);
    if (removed_count == 0U) {
      return NULL;
    }
    if (ds_ptrie_version_retain_ex(base) != DS_OK) {
      trie->error = DS_ERR_OVERFLOW;
      return NULL;
    }
    if (out_moved != NULL) {
      *out_moved = removed_count;
    }
    return base;
  }

  removed_count = 0U;
  removed = ds_ptrie_remove_prefix(trie, base, source_prefix, source_prefix_len,
                                   &removed_count);
  if (removed == NULL || removed_count == 0U) {
    return NULL;
  }
  copied_count = 0U;
  moved = ds_ptrie_copy_prefix(trie, removed, base, source_prefix,
                               source_prefix_len, destination_prefix,
                               destination_prefix_len, &copied_count);
  ds_ptrie_version_release(trie, removed);
  if (moved == NULL) {
    return NULL;
  }
  if (out_moved != NULL) {
    *out_moved = copied_count;
  }
  return moved;
}

ds_status_t ds_ptrie_longest_prefix(ds_ptrie_version_t *version,
                                    const unsigned char *key, size_t len,
                                    size_t *out_len, void **out_value) {
  ds_ptrie_node_t *node;
  size_t pos = 0, best_len = 0;
  void *best_value = NULL;
  bool found = false;
  if (out_len != NULL)
    *out_len = 0;
  if (out_value != NULL)
    *out_value = NULL;
  if (version == NULL || out_len == NULL || out_value == NULL ||
      (key == NULL && len != 0))
    return DS_ERR_NULL;
  node = version->root;
  for (;;) {
    size_t common = 0;
    ds_ptrie_edge_t *edge;
    while (common < node->prefix_len && common < len - pos &&
           node->prefix[common] == key[pos + common])
      common++;
    if (common != node->prefix_len)
      break;
    pos += common;
    if (node->terminal) {
      best_value = node->value;
      best_len = pos;
      found = true;
    }
    if (pos == len)
      break;
    edge = ptrie_edge_find(node->children, key[pos++]);
    if (edge == NULL)
      break;
    node = edge->child;
  }
  if (!found)
    return DS_NOT_FOUND;
  *out_len = best_len;
  *out_value = best_value;
  return DS_OK;
}

static void ptrie_walk_init(ptrie_walk_t *walk, ds_ptrie_version_t *version,
                            ds_ptrie_visit_func_t visit, void *user) {
  walk->context = version->context;
  walk->key = NULL;
  walk->key_capacity = 0U;
  walk->visit = visit;
  walk->user = user;
  walk->lower = NULL;
  walk->lower_len = 0U;
  walk->upper = NULL;
  walk->upper_len = 0U;
}

ds_status_t ds_ptrie_visit_prefix(ds_ptrie_version_t *version,
                                  const unsigned char *prefix,
                                  size_t prefix_len,
                                  ds_ptrie_visit_func_t visit, void *user) {
  ds_ptrie_node_t *node;
  ptrie_walk_t walk;
  ds_status_t status;
  size_t matched = 0, full_len;

  if (version == NULL || visit == NULL ||
      (prefix == NULL && prefix_len != 0U)) {
    return DS_ERR_NULL;
  }

  node = ptrie_find_match(version->root, prefix, prefix_len, true, &matched);
  if (node == NULL) {
    return DS_NOT_FOUND;
  }

  if (node->prefix_len - matched > (size_t)-1 - prefix_len)
    return DS_ERR_OVERFLOW;
  full_len = prefix_len + node->prefix_len - matched;
  ptrie_walk_init(&walk, version, visit, user);
  walk.key = (unsigned char *)ptrie_grow(walk.context, NULL, &walk.key_capacity,
                                         full_len, 1U);
  if (walk.key == NULL) {
    return DS_ERR_ALLOC;
  }
  if (prefix_len != 0U) {
    memcpy(walk.key, prefix, prefix_len);
  }
  if (matched < node->prefix_len)
    memcpy(walk.key + prefix_len, node->prefix + matched,
           node->prefix_len - matched);
  status = ptrie_walk(&walk, node, full_len);
  ds_context_free(walk.context, walk.key);
  return status;
}

ds_status_t ds_ptrie_visit_range(ds_ptrie_version_t *version,
                                 const unsigned char *lower, size_t lower_len,
                                 const unsigned char *upper, size_t upper_len,
                                 ds_ptrie_visit_func_t visit, void *user) {
  ptrie_walk_t walk;
  ds_status_t status;

  if (version == NULL || visit == NULL || (lower == NULL && lower_len != 0U) ||
      (upper == NULL && upper_len != 0U)) {
    return DS_ERR_NULL;
  }
  if (lower != NULL && upper != NULL &&
      ptrie_key_compare(lower, lower_len, upper, upper_len) > 0) {
    return DS_ERR_RANGE;
  }

  ptrie_walk_init(&walk, version, visit, user);
  walk.lower = lower;
  walk.lower_len = lower_len;
  walk.upper = upper;
  walk.upper_len = upper_len;
  walk.key = (unsigned char *)ptrie_grow(walk.context, NULL, &walk.key_capacity,
                                         1U, 1U);
  if (walk.key == NULL) {
    return DS_ERR_ALLOC;
  }
  status = ptrie_walk(&walk, version->root, 0U);
  ds_context_free(walk.context, walk.key);
  return status;
}

ds_status_t ds_ptrie_version_retain_ex(ds_ptrie_version_t *version) {
  if (version == NULL)
    return DS_ERR_NULL;
  if (version->refs == ULONG_MAX)
    return DS_ERR_OVERFLOW;
  version->refs++;
  return DS_OK;
}

void ds_ptrie_version_retain(ds_ptrie_version_t *version) {
  (void)ds_ptrie_version_retain_ex(version);
}

void ds_ptrie_version_release(ds_ptrie_t *trie, ds_ptrie_version_t *version) {
  if (trie == NULL || version == NULL || version->owner != trie) {
    return;
  }
  version->refs--;
  if (version->refs == 0UL) {
    ptrie_node_release(trie, version->root);
    ds_context_free(trie->config.context, version);
  }
}

/* Status facade. The legacy pointer forms remain source-compatible. */
ds_status_t ds_ptrie_create_ex(const ds_ptrie_config_t *config,
                               ds_ptrie_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (config != NULL &&
      ((config->storage != DS_PTRIE_BYTES &&
        config->storage != DS_PTRIE_RADIX) ||
       (config->destroy_value != NULL && config->clone_value == NULL &&
        config->clone_value_ex == NULL) ||
       (config->clone_value != NULL && config->clone_value_ex != NULL)))
    return DS_ERR_INVALID;
  *out = ptrie_create(config);
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_ptrie_t *ds_ptrie_create(const ds_ptrie_config_t *config) {
  ds_ptrie_t *out = NULL;
  (void)ds_ptrie_create_ex(config, &out);
  return out;
}

static ds_status_t ptrie_update_args(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                     const unsigned char *key, size_t len,
                                     ds_ptrie_version_t **out) {
  if (out != NULL)
    *out = NULL;
  if (trie == NULL || base == NULL || out == NULL || (key == NULL && len != 0))
    return DS_ERR_NULL;
  if (base->owner != trie)
    return DS_ERR_INVALID;
  trie->error = DS_ERR_ALLOC;
  return DS_OK;
}

ds_status_t ds_ptrie_insert_ex(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                               const unsigned char *key, size_t len,
                               void *value, ds_ptrie_version_t **out) {
  ds_status_t st = ptrie_update_args(trie, base, key, len, out);
  ds_ptrie_node_t *existing;
  if (st != DS_OK)
    return st;
  existing = ptrie_find_match(base->root, key, len, false, NULL);
  if (base->root->count == (size_t)-1 &&
      (existing == NULL || !existing->terminal))
    return DS_ERR_OVERFLOW;
  if (trie->config.storage == DS_PTRIE_BYTES &&
      len >= (size_t)-1 / sizeof(ds_ptrie_node_t *))
    return DS_ERR_OVERFLOW;
  *out = ptrie_insert_version(trie, base, key, len, value);
  return *out == NULL ? trie->error : DS_OK;
}

ds_ptrie_version_t *ds_ptrie_insert(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                    const unsigned char *key, size_t len,
                                    void *value) {
  ds_ptrie_version_t *out = NULL;
  (void)ds_ptrie_insert_ex(trie, base, key, len, value, &out);
  return out;
}

ds_status_t ds_ptrie_remove_ex(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                               const unsigned char *key, size_t len,
                               ds_ptrie_version_t **out) {
  ds_status_t st = ptrie_update_args(trie, base, key, len, out);
  ds_ptrie_node_t *node;
  if (st != DS_OK)
    return st;
  node = ptrie_find_match(base->root, key, len, false, NULL);
  if (node == NULL || !node->terminal)
    return DS_NOT_FOUND;
  *out = ptrie_remove_version(trie, base, key, len);
  return *out == NULL ? trie->error : DS_OK;
}

ds_ptrie_version_t *ds_ptrie_remove(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                    const unsigned char *key, size_t len) {
  ds_ptrie_version_t *out = NULL;
  (void)ds_ptrie_remove_ex(trie, base, key, len, &out);
  return out;
}

ds_status_t ds_ptrie_remove_prefix_ex(ds_ptrie_t *trie,
                                      ds_ptrie_version_t *base,
                                      const unsigned char *prefix, size_t len,
                                      size_t *out_removed,
                                      ds_ptrie_version_t **out) {
  ds_status_t st = ptrie_update_args(trie, base, prefix, len, out);
  if (out_removed != NULL)
    *out_removed = 0;
  if (st != DS_OK)
    return st;
  if (ds_ptrie_count_prefix(base, prefix, len) == 0)
    return DS_NOT_FOUND;
  *out = ptrie_remove_prefix_version(trie, base, prefix, len, out_removed);
  return *out == NULL ? trie->error : DS_OK;
}

ds_ptrie_version_t *ds_ptrie_remove_prefix(ds_ptrie_t *trie,
                                           ds_ptrie_version_t *base,
                                           const unsigned char *prefix,
                                           size_t len, size_t *out_removed) {
  ds_ptrie_version_t *out = NULL;
  (void)ds_ptrie_remove_prefix_ex(trie, base, prefix, len, out_removed, &out);
  return out;
}

ds_status_t ds_ptrie_copy_prefix_ex(
    ds_ptrie_t *trie, ds_ptrie_version_t *destination_base,
    ds_ptrie_version_t *source_version, const unsigned char *source_prefix,
    size_t source_len, const unsigned char *destination_prefix,
    size_t destination_len, size_t *out_copied, ds_ptrie_version_t **out) {
  ds_status_t st = ptrie_update_args(trie, destination_base, destination_prefix,
                                     destination_len, out);
  if (out_copied != NULL)
    *out_copied = 0;
  if (st != DS_OK)
    return st;
  if (source_version == NULL || (source_prefix == NULL && source_len != 0))
    return DS_ERR_NULL;
  if (ds_ptrie_count_prefix(source_version, source_prefix, source_len) == 0)
    return DS_NOT_FOUND;
  *out = ptrie_copy_prefix_version(
      trie, destination_base, source_version, source_prefix, source_len,
      destination_prefix, destination_len, out_copied);
  return *out == NULL ? trie->error : DS_OK;
}

ds_ptrie_version_t *
ds_ptrie_copy_prefix(ds_ptrie_t *trie, ds_ptrie_version_t *destination_base,
                     ds_ptrie_version_t *source_version,
                     const unsigned char *source_prefix, size_t source_len,
                     const unsigned char *destination_prefix,
                     size_t destination_len, size_t *out_copied) {
  ds_ptrie_version_t *out = NULL;
  (void)ds_ptrie_copy_prefix_ex(trie, destination_base, source_version,
                                source_prefix, source_len, destination_prefix,
                                destination_len, out_copied, &out);
  return out;
}

ds_status_t ds_ptrie_move_prefix_ex(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                    const unsigned char *source_prefix,
                                    size_t source_len,
                                    const unsigned char *destination_prefix,
                                    size_t destination_len, size_t *out_moved,
                                    ds_ptrie_version_t **out) {
  ds_status_t st =
      ptrie_update_args(trie, base, source_prefix, source_len, out);
  if (out_moved != NULL)
    *out_moved = 0;
  if (st != DS_OK)
    return st;
  if (destination_prefix == NULL && destination_len != 0)
    return DS_ERR_NULL;
  if (ds_ptrie_count_prefix(base, source_prefix, source_len) == 0)
    return DS_NOT_FOUND;
  *out =
      ptrie_move_prefix_version(trie, base, source_prefix, source_len,
                                destination_prefix, destination_len, out_moved);
  return *out == NULL ? trie->error : DS_OK;
}

ds_ptrie_version_t *
ds_ptrie_move_prefix(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                     const unsigned char *source_prefix, size_t source_len,
                     const unsigned char *destination_prefix,
                     size_t destination_len, size_t *out_moved) {
  ds_ptrie_version_t *out = NULL;
  (void)ds_ptrie_move_prefix_ex(trie, base, source_prefix, source_len,
                                destination_prefix, destination_len, out_moved,
                                &out);
  return out;
}
