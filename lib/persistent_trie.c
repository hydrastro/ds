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
static void ptrie_node_retain(ds_ptrie_node_t *node);
static void ptrie_node_release(ds_ptrie_t *trie, ds_ptrie_node_t *node);
static ds_ptrie_edge_t *ptrie_edge_find(ds_ptrie_edge_t *edge,
                                        unsigned char token);
static ds_ptrie_edge_t *ptrie_edge_clone_list(ds_ptrie_edge_t *edge);
static ds_ptrie_node_t *ptrie_node_clone_shallow(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node);
static ds_ptrie_node_t *ptrie_insert_node(ds_ptrie_t *trie,
                                          ds_ptrie_node_t *node,
                                          const unsigned char *key,
                                          size_t len, size_t pos,
                                          void *value);

void ds_ptrie_config_init(ds_ptrie_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->clone_value = NULL;
  config->destroy_value = NULL;
  config->user = NULL;
  config->context = NULL;
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
  if (node->terminal && trie->config.destroy_value != NULL) {
    trie->config.destroy_value(node->value, trie->config.user);
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
      return head;
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

static ds_ptrie_node_t *ptrie_node_clone_shallow(ds_ptrie_t *trie,
                                                 ds_ptrie_node_t *node) {
  ds_ptrie_node_t *copy;
  (void)trie;
  copy = ptrie_node_create();
  if (copy == NULL) {
    return NULL;
  }
  copy->terminal = node->terminal;
  copy->value = node->value;
  copy->children = ptrie_edge_clone_list(node->children);
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
    if (copy->terminal && trie->config.destroy_value != NULL &&
        copy->value != value) {
      trie->config.destroy_value(copy->value, trie->config.user);
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
  trie->root = (ds_ptrie_version_t *)malloc(sizeof(*trie->root));
  if (trie->root == NULL) {
    free(trie);
    return NULL;
  }
  trie->root->refs = 1UL;
  trie->root->root = ptrie_node_create();
  if (trie->root->root == NULL) {
    free(trie->root);
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
  if (trie == NULL || base == NULL || (key == NULL && len != 0U)) {
    return NULL;
  }
  if (trie->config.clone_value != NULL) {
    value = trie->config.clone_value(value, trie->config.user);
  }
  new_root = ptrie_insert_node(trie, base->root, key, len, 0U, value);
  if (new_root == NULL) {
    return NULL;
  }
  version = (ds_ptrie_version_t *)malloc(sizeof(*version));
  if (version == NULL) {
    ptrie_node_release(trie, new_root);
    return NULL;
  }
  version->refs = 1UL;
  version->root = new_root;
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
