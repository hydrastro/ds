#include "persistent_trie.h"
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
  bool terminal;
  void *value;
  ds_ptrie_edge_t *children;
} ds_ptrie_node_t;

struct ds_ptrie_version {
  unsigned long refs;
  ds_ptrie_node_t *root;
};

struct ds_ptrie {
  ds_ptrie_config_t config;
  ds_ptrie_version_t *root;
};

static ds_ptrie_node_t *ptrie_node_create(void);
static void *ptrie_value_clone(ds_ptrie_t *trie, void *value);
static void ptrie_value_destroy(ds_ptrie_t *trie, void *value);
static void ptrie_node_retain(ds_ptrie_node_t *node);
static void ptrie_node_release(ds_ptrie_t *trie, ds_ptrie_node_t *node);
static ds_ptrie_edge_t *ptrie_edge_find(ds_ptrie_edge_t *edge,
                                        unsigned char token);
static ds_ptrie_edge_t *ptrie_edge_clone_list(ds_ptrie_edge_t *edge);
static bool ptrie_node_empty(ds_ptrie_node_t *node);
static size_t ptrie_terminal_count(ds_ptrie_node_t *node);
static void ptrie_release_edge_list(ds_ptrie_t *trie, ds_ptrie_edge_t *edge);
static ds_ptrie_node_t *ptrie_node_clone_shallow(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node);
static ds_ptrie_node_t *ptrie_insert_node(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *node,
                                          const unsigned char *key,
                                          size_t len, size_t pos,
                                          void *value);
static ds_ptrie_node_t *ptrie_remove_node(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *node,
                                          const unsigned char *key,
                                          size_t len, size_t pos,
                                          bool *removed);
static ds_ptrie_node_t *ptrie_remove_prefix_node(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node,
                                                 const unsigned char *prefix,
                                                 size_t prefix_len, size_t pos,
                                                 size_t *removed_count);
static ds_ptrie_version_t *ptrie_version_create(ds_ptrie_node_t *root);
static ds_ptrie_node_t *ptrie_find_node(ds_ptrie_node_t *node,
                                        const unsigned char *key, size_t len);
static int ptrie_key_compare(const unsigned char *a, size_t alen,
                             const unsigned char *b, size_t blen);
static bool ptrie_key_in_range(const unsigned char *key, size_t key_len,
                               const unsigned char *lower, size_t lower_len,
                               const unsigned char *upper, size_t upper_len);
static ds_status_t ptrie_visit_from(ds_ptrie_node_t *node, unsigned char **buffer,
                                    size_t depth, size_t *capacity,
                                    ds_ptrie_visit_func_t visit, void *user,
                                    const unsigned char *lower, size_t lower_len,
                                    const unsigned char *upper, size_t upper_len);

void ds_ptrie_config_init(ds_ptrie_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->clone_value = NULL;
  config->destroy_value = NULL;
  config->user = NULL;
  config->context = NULL;
}

static void *ptrie_value_clone(ds_ptrie_t *trie, void *value) {
  if (trie->config.clone_value != NULL) {
    return trie->config.clone_value(value, trie->config.user);
  }
  return value;
}

static void ptrie_value_destroy(ds_ptrie_t *trie, void *value) {
  if (trie->config.destroy_value != NULL) {
    trie->config.destroy_value(value, trie->config.user);
  }
}

static ds_ptrie_node_t *ptrie_node_create(void) {
  ds_ptrie_node_t *node;
  node = (ds_ptrie_node_t *)malloc(sizeof(*node));
  if (node == NULL) {
    return NULL;
  }
  node->refs = 1UL;
  node->terminal = false;
  node->value = NULL;
  node->children = NULL;
  return node;
}

static void ptrie_node_retain(ds_ptrie_node_t *node) {
  if (node != NULL) {
    node->refs++;
  }
}

static void ptrie_node_release(ds_ptrie_t *trie, ds_ptrie_node_t *node) {
  ds_ptrie_edge_t *edge;
  ds_ptrie_edge_t *next;
  if (node == NULL) {
    return;
  }
  node->refs--;
  if (node->refs != 0UL) {
    return;
  }
  if (node->terminal) {
    ptrie_value_destroy(trie, node->value);
  }
  edge = node->children;
  while (edge != NULL) {
    next = edge->next;
    ptrie_node_release(trie, edge->child);
    free(edge);
    edge = next;
  }
  free(node);
}

static ds_ptrie_edge_t *ptrie_edge_find(ds_ptrie_edge_t *edge,
                                        unsigned char token) {
  while (edge != NULL) {
    if (edge->token == token) {
      return edge;
    }
    edge = edge->next;
  }
  return NULL;
}

static ds_ptrie_edge_t *ptrie_edge_clone_list(ds_ptrie_edge_t *edge) {
  ds_ptrie_edge_t *head;
  ds_ptrie_edge_t *tail;
  ds_ptrie_edge_t *copy;
  head = NULL;
  tail = NULL;
  while (edge != NULL) {
    copy = (ds_ptrie_edge_t *)malloc(sizeof(*copy));
    if (copy == NULL) {
      while (head != NULL) {
        copy = head->next;
        ptrie_node_release(NULL, head->child);
        free(head);
        head = copy;
      }
      return NULL;
    }
    copy->token = edge->token;
    copy->child = edge->child;
    ptrie_node_retain(copy->child);
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

static size_t ptrie_terminal_count(ds_ptrie_node_t *node) {
  ds_ptrie_edge_t *edge;
  size_t count;

  if (node == NULL) {
    return 0U;
  }

  count = node->terminal ? 1U : 0U;
  edge = node->children;
  while (edge != NULL) {
    count += ptrie_terminal_count(edge->child);
    edge = edge->next;
  }
  return count;
}

static void ptrie_release_edge_list(ds_ptrie_t *trie, ds_ptrie_edge_t *edge) {
  ds_ptrie_edge_t *next;

  while (edge != NULL) {
    next = edge->next;
    ptrie_node_release(trie, edge->child);
    free(edge);
    edge = next;
  }
}

static ds_ptrie_node_t *ptrie_node_clone_shallow(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node) {
  ds_ptrie_node_t *copy;
  copy = ptrie_node_create();
  if (copy == NULL) {
    return NULL;
  }
  copy->terminal = node->terminal;
  copy->value = NULL;
  if (node->terminal) {
    copy->value = ptrie_value_clone(trie, node->value);
    if (copy->value == NULL && node->value != NULL &&
        trie->config.clone_value != NULL) {
      free(copy);
      return NULL;
    }
  }
  copy->children = ptrie_edge_clone_list(node->children);
  if (node->children != NULL && copy->children == NULL) {
    if (copy->terminal) {
      ptrie_value_destroy(trie, copy->value);
    }
    free(copy);
    return NULL;
  }
  return copy;
}

static ds_ptrie_node_t *ptrie_insert_node(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *node,
                                          const unsigned char *key,
                                          size_t len, size_t pos,
                                          void *value) {
  ds_ptrie_node_t *copy;
  ds_ptrie_edge_t *edge;
  ds_ptrie_node_t *old_child;
  ds_ptrie_node_t *new_child;

  copy = ptrie_node_clone_shallow(trie, node);
  if (copy == NULL) {
    return NULL;
  }
  if (pos == len) {
    if (copy->terminal) {
      ptrie_value_destroy(trie, copy->value);
    }
    copy->terminal = true;
    copy->value = value;
    return copy;
  }

  edge = ptrie_edge_find(copy->children, key[pos]);
  if (edge != NULL) {
    old_child = edge->child;
    new_child = ptrie_insert_node(trie, old_child, key, len, pos + 1U, value);
    if (new_child == NULL) {
      ptrie_node_release(trie, copy);
      return NULL;
    }
    edge->child = new_child;
    ptrie_node_release(trie, old_child);
  } else {
    old_child = ptrie_node_create();
    if (old_child == NULL) {
      ptrie_node_release(trie, copy);
      return NULL;
    }
    new_child = ptrie_insert_node(trie, old_child, key, len, pos + 1U, value);
    ptrie_node_release(trie, old_child);
    if (new_child == NULL) {
      ptrie_node_release(trie, copy);
      return NULL;
    }
    edge = (ds_ptrie_edge_t *)malloc(sizeof(*edge));
    if (edge == NULL) {
      ptrie_node_release(trie, new_child);
      ptrie_node_release(trie, copy);
      return NULL;
    }
    edge->token = key[pos];
    edge->child = new_child;
    edge->next = copy->children;
    copy->children = edge;
  }
  return copy;
}

static ds_ptrie_node_t *ptrie_remove_node(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *node,
                                          const unsigned char *key,
                                          size_t len, size_t pos,
                                          bool *removed) {
  ds_ptrie_node_t *copy;
  ds_ptrie_edge_t *edge;
  ds_ptrie_edge_t *prev;
  ds_ptrie_node_t *old_child;
  ds_ptrie_node_t *new_child;

  if (node == NULL) {
    *removed = false;
    return NULL;
  }
  copy = ptrie_node_clone_shallow(trie, node);
  if (copy == NULL) {
    *removed = false;
    return NULL;
  }
  if (pos == len) {
    if (!copy->terminal) {
      ptrie_node_release(trie, copy);
      *removed = false;
      return NULL;
    }
    ptrie_value_destroy(trie, copy->value);
    copy->terminal = false;
    copy->value = NULL;
    *removed = true;
  } else {
    prev = NULL;
    edge = copy->children;
    while (edge != NULL && edge->token != key[pos]) {
      prev = edge;
      edge = edge->next;
    }
    if (edge == NULL) {
      ptrie_node_release(trie, copy);
      *removed = false;
      return NULL;
    }
    old_child = edge->child;
    new_child = ptrie_remove_node(trie, old_child, key, len, pos + 1U,
                                  removed);
    if (!*removed) {
      ptrie_node_release(trie, copy);
      return NULL;
    }
    if (new_child == NULL) {
      if (prev == NULL) {
        copy->children = edge->next;
      } else {
        prev->next = edge->next;
      }
      ptrie_node_release(trie, old_child);
      free(edge);
    } else {
      edge->child = new_child;
      ptrie_node_release(trie, old_child);
    }
  }
  if (pos != 0U && ptrie_node_empty(copy)) {
    ptrie_node_release(trie, copy);
    return NULL;
  }
  return copy;
}

static ds_ptrie_node_t *ptrie_remove_prefix_node(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node,
                                                 const unsigned char *prefix,
                                                 size_t prefix_len, size_t pos,
                                                 size_t *removed_count) {
  ds_ptrie_node_t *copy;
  ds_ptrie_edge_t *edge;
  ds_ptrie_edge_t *prev;
  ds_ptrie_node_t *old_child;
  ds_ptrie_node_t *new_child;

  if (node == NULL) {
    return NULL;
  }

  copy = ptrie_node_clone_shallow(trie, node);
  if (copy == NULL) {
    return NULL;
  }

  if (pos == prefix_len) {
    *removed_count = ptrie_terminal_count(copy);
    if (*removed_count == 0U) {
      ptrie_node_release(trie, copy);
      return NULL;
    }
    if (copy->terminal) {
      ptrie_value_destroy(trie, copy->value);
      copy->terminal = false;
      copy->value = NULL;
    }
    ptrie_release_edge_list(trie, copy->children);
    copy->children = NULL;
  } else {
    prev = NULL;
    edge = copy->children;
    while (edge != NULL && edge->token != prefix[pos]) {
      prev = edge;
      edge = edge->next;
    }
    if (edge == NULL) {
      ptrie_node_release(trie, copy);
      *removed_count = 0U;
      return NULL;
    }
    old_child = edge->child;
    new_child = ptrie_remove_prefix_node(trie, old_child, prefix, prefix_len,
                                         pos + 1U, removed_count);
    if (*removed_count == 0U) {
      ptrie_node_release(trie, copy);
      return NULL;
    }
    if (new_child == NULL) {
      if (prev == NULL) {
        copy->children = edge->next;
      } else {
        prev->next = edge->next;
      }
      ptrie_node_release(trie, old_child);
      free(edge);
    } else {
      edge->child = new_child;
      ptrie_node_release(trie, old_child);
    }
  }

  if (pos != 0U && ptrie_node_empty(copy)) {
    ptrie_node_release(trie, copy);
    return NULL;
  }
  return copy;
}

static ds_ptrie_version_t *ptrie_version_create(ds_ptrie_node_t *root) {
  ds_ptrie_version_t *version;
  version = (ds_ptrie_version_t *)malloc(sizeof(*version));
  if (version == NULL) {
    return NULL;
  }
  version->refs = 1UL;
  version->root = root;
  return version;
}


static ds_ptrie_node_t *ptrie_find_node(ds_ptrie_node_t *node,
                                        const unsigned char *key, size_t len) {
  ds_ptrie_edge_t *edge;
  size_t i;

  if (node == NULL || (key == NULL && len != 0U)) {
    return NULL;
  }

  for (i = 0U; i < len; i++) {
    edge = ptrie_edge_find(node->children, key[i]);
    if (edge == NULL) {
      return NULL;
    }
    node = edge->child;
  }

  return node;
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

static ds_status_t ptrie_buffer_grow(unsigned char **buffer, size_t *capacity,
                                     size_t needed) {
  unsigned char *next;
  size_t new_capacity;

  if (needed <= *capacity) {
    return DS_OK;
  }

  new_capacity = *capacity == 0U ? 16U : *capacity;
  while (new_capacity < needed) {
    new_capacity *= 2U;
  }

  next = (unsigned char *)realloc(*buffer, new_capacity);
  if (next == NULL) {
    return DS_ERR_ALLOC;
  }
  *buffer = next;
  *capacity = new_capacity;
  return DS_OK;
}

static ds_status_t ptrie_visit_from(ds_ptrie_node_t *node, unsigned char **buffer,
                                    size_t depth, size_t *capacity,
                                    ds_ptrie_visit_func_t visit, void *user,
                                    const unsigned char *lower, size_t lower_len,
                                    const unsigned char *upper, size_t upper_len) {
  ds_ptrie_edge_t *edge;
  ds_status_t status;

  if (node == NULL) {
    return DS_OK;
  }

  if (node->terminal && ptrie_key_in_range(*buffer, depth, lower, lower_len,
                                           upper, upper_len)) {
    if (!visit(*buffer, depth, node->value, user)) {
      return DS_STOP;
    }
  }

  edge = node->children;
  while (edge != NULL) {
    status = ptrie_buffer_grow(buffer, capacity, depth + 1U);
    if (status != DS_OK) {
      return status;
    }
    (*buffer)[depth] = edge->token;
    status = ptrie_visit_from(edge->child, buffer, depth + 1U, capacity, visit,
                              user, lower, lower_len, upper, upper_len);
    if (status != DS_OK) {
      return status;
    }
    edge = edge->next;
  }

  return DS_OK;
}

ds_ptrie_t *FUNC(ds_ptrie_create)(const ds_ptrie_config_t *config) {
  ds_ptrie_t *trie;
  trie = (ds_ptrie_t *)malloc(sizeof(*trie));
  if (trie == NULL) {
    return NULL;
  }
  if (config != NULL) {
    trie->config = *config;
  } else {
    ds_ptrie_config_init(&trie->config);
  }
  if (trie->config.destroy_value != NULL && trie->config.clone_value == NULL) {
    free(trie);
    return NULL;
  }
  trie->root = ptrie_version_create(ptrie_node_create());
  if (trie->root == NULL || trie->root->root == NULL) {
    if (trie->root != NULL) {
      free(trie->root);
    }
    free(trie);
    return NULL;
  }
  return trie;
}

void FUNC(ds_ptrie_destroy)(ds_ptrie_t *trie) {
  if (trie == NULL) {
    return;
  }
  FUNC(ds_ptrie_version_release)(trie, trie->root);
  free(trie);
}

ds_ptrie_version_t *FUNC(ds_ptrie_root)(ds_ptrie_t *trie) {
  if (trie == NULL) {
    return NULL;
  }
  return trie->root;
}

ds_ptrie_version_t *FUNC(ds_ptrie_insert)(ds_ptrie_t *trie,
                                           ds_ptrie_version_t *base,
                                           const unsigned char *key,
                                           size_t len, void *value) {
  ds_ptrie_version_t *version;
  ds_ptrie_node_t *new_root;
  void *stored_value;
  if (trie == NULL || base == NULL || (key == NULL && len != 0U)) {
    return NULL;
  }
  stored_value = ptrie_value_clone(trie, value);
  if (stored_value == NULL && value != NULL && trie->config.clone_value != NULL) {
    return NULL;
  }
  new_root = ptrie_insert_node(trie, base->root, key, len, 0U, stored_value);
  if (new_root == NULL) {
    ptrie_value_destroy(trie, stored_value);
    return NULL;
  }
  version = ptrie_version_create(new_root);
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  return version;
}

ds_ptrie_version_t *FUNC(ds_ptrie_remove)(ds_ptrie_t *trie,
                                           ds_ptrie_version_t *base,
                                           const unsigned char *key,
                                           size_t len) {
  ds_ptrie_version_t *version;
  ds_ptrie_node_t *new_root;
  bool removed;
  if (trie == NULL || base == NULL || (key == NULL && len != 0U)) {
    return NULL;
  }
  removed = false;
  new_root = ptrie_remove_node(trie, base->root, key, len, 0U, &removed);
  if (!removed || new_root == NULL) {
    return NULL;
  }
  version = ptrie_version_create(new_root);
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  return version;
}

ds_ptrie_version_t *FUNC(ds_ptrie_remove_prefix)(ds_ptrie_t *trie,
                                                  ds_ptrie_version_t *base,
                                                  const unsigned char *prefix,
                                                  size_t prefix_len,
                                                  size_t *out_removed) {
  ds_ptrie_version_t *version;
  ds_ptrie_node_t *new_root;
  size_t removed_count;

  if (out_removed != NULL) {
    *out_removed = 0U;
  }
  if (trie == NULL || base == NULL || (prefix == NULL && prefix_len != 0U)) {
    return NULL;
  }

  removed_count = 0U;
  new_root = ptrie_remove_prefix_node(trie, base->root, prefix, prefix_len, 0U,
                                      &removed_count);
  if (removed_count == 0U || new_root == NULL) {
    return NULL;
  }
  version = ptrie_version_create(new_root);
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  if (out_removed != NULL) {
    *out_removed = removed_count;
  }
  return version;
}

ds_status_t FUNC(ds_ptrie_get)(ds_ptrie_version_t *version,
                                const unsigned char *key, size_t len,
                                void **out_value) {
  ds_ptrie_node_t *node;
  ds_ptrie_edge_t *edge;
  size_t i;
  if (out_value != NULL) {
    *out_value = NULL;
  }
  if (version == NULL || out_value == NULL || (key == NULL && len != 0U)) {
    return DS_ERR_NULL;
  }
  node = version->root;
  for (i = 0U; i < len; i++) {
    edge = ptrie_edge_find(node->children, key[i]);
    if (edge == NULL) {
      return DS_NOT_FOUND;
    }
    node = edge->child;
  }
  if (!node->terminal) {
    return DS_NOT_FOUND;
  }
  *out_value = node->value;
  return DS_OK;
}


ds_status_t FUNC(ds_ptrie_contains_prefix)(ds_ptrie_version_t *version,
                                            const unsigned char *prefix,
                                            size_t prefix_len) {
  ds_ptrie_node_t *node;

  if (version == NULL || (prefix == NULL && prefix_len != 0U)) {
    return DS_ERR_NULL;
  }

  node = ptrie_find_node(version->root, prefix, prefix_len);
  return node == NULL ? DS_NOT_FOUND : DS_OK;
}

ds_status_t FUNC(ds_ptrie_longest_prefix)(ds_ptrie_version_t *version,
                                           const unsigned char *key, size_t len,
                                           size_t *out_len, void **out_value) {
  ds_ptrie_node_t *node;
  ds_ptrie_edge_t *edge;
  size_t i;
  size_t best_len;
  void *best_value;
  bool found;

  if (out_len != NULL) {
    *out_len = 0U;
  }
  if (out_value != NULL) {
    *out_value = NULL;
  }
  if (version == NULL || out_len == NULL || out_value == NULL ||
      (key == NULL && len != 0U)) {
    return DS_ERR_NULL;
  }

  node = version->root;
  best_len = 0U;
  best_value = NULL;
  found = false;
  if (node->terminal) {
    best_value = node->value;
    found = true;
  }

  for (i = 0U; i < len; i++) {
    edge = ptrie_edge_find(node->children, key[i]);
    if (edge == NULL) {
      break;
    }
    node = edge->child;
    if (node->terminal) {
      best_len = i + 1U;
      best_value = node->value;
      found = true;
    }
  }

  if (!found) {
    return DS_NOT_FOUND;
  }

  *out_len = best_len;
  *out_value = best_value;
  return DS_OK;
}

ds_status_t FUNC(ds_ptrie_visit_prefix)(ds_ptrie_version_t *version,
                                         const unsigned char *prefix,
                                         size_t prefix_len,
                                         ds_ptrie_visit_func_t visit,
                                         void *user) {
  ds_ptrie_node_t *node;
  unsigned char *buffer;
  size_t capacity;
  ds_status_t status;

  if (version == NULL || visit == NULL || (prefix == NULL && prefix_len != 0U)) {
    return DS_ERR_NULL;
  }

  node = ptrie_find_node(version->root, prefix, prefix_len);
  if (node == NULL) {
    return DS_NOT_FOUND;
  }

  capacity = prefix_len == 0U ? 16U : prefix_len;
  buffer = (unsigned char *)malloc(capacity);
  if (buffer == NULL) {
    return DS_ERR_ALLOC;
  }
  if (prefix_len != 0U) {
    memcpy(buffer, prefix, prefix_len);
  }

  status = ptrie_visit_from(node, &buffer, prefix_len, &capacity, visit, user,
                            NULL, 0U, NULL, 0U);
  free(buffer);
  return status;
}

ds_status_t FUNC(ds_ptrie_visit_range)(ds_ptrie_version_t *version,
                                        const unsigned char *lower,
                                        size_t lower_len,
                                        const unsigned char *upper,
                                        size_t upper_len,
                                        ds_ptrie_visit_func_t visit,
                                        void *user) {
  unsigned char *buffer;
  size_t capacity;
  ds_status_t status;

  if (version == NULL || visit == NULL || (lower == NULL && lower_len != 0U) ||
      (upper == NULL && upper_len != 0U)) {
    return DS_ERR_NULL;
  }
  if (lower != NULL && upper != NULL &&
      ptrie_key_compare(lower, lower_len, upper, upper_len) > 0) {
    return DS_ERR_RANGE;
  }

  capacity = 16U;
  buffer = (unsigned char *)malloc(capacity);
  if (buffer == NULL) {
    return DS_ERR_ALLOC;
  }

  status = ptrie_visit_from(version->root, &buffer, 0U, &capacity, visit, user,
                            lower, lower_len, upper, upper_len);
  free(buffer);
  return status;
}

void FUNC(ds_ptrie_version_retain)(ds_ptrie_version_t *version) {
  if (version != NULL) {
    version->refs++;
  }
}

void FUNC(ds_ptrie_version_release)(ds_ptrie_t *trie,
                                    ds_ptrie_version_t *version) {
  if (trie == NULL || version == NULL) {
    return;
  }
  version->refs--;
  if (version->refs == 0UL) {
    ptrie_node_release(trie, version->root);
    free(version);
  }
}
