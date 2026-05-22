#include "persistent_rbt.h"
#include <stdlib.h>

typedef struct ds_prbt_node {
  ds_rbt_node_t rb;
  void *key;
  void *value;
} ds_prbt_node_t;

struct ds_prbt_version {
  unsigned long refs;
  ds_rbt_t *tree;
};

struct ds_prbt {
  ds_prbt_config_t config;
  ds_prbt_version_t *root;
};

static int prbt_compare_nodes(ds_rbt_node_t *a, ds_rbt_node_t *b);
static ds_prbt_node_t *prbt_node_create(ds_prbt_t *tree, void *key,
                                        void *value, int clone);
static void prbt_node_destroy_with_tree(ds_prbt_t *tree, ds_rbt_node_t *node);
static ds_rbt_node_t *prbt_clone_node_recursive(ds_prbt_t *owner,
                                                ds_rbt_t *src,
                                                ds_rbt_t *dst,
                                                ds_rbt_node_t *node,
                                                ds_rbt_node_t *parent);
static ds_rbt_t *prbt_clone_tree(ds_prbt_t *owner, ds_rbt_t *src);
static ds_prbt_node_t *prbt_search_node(ds_prbt_t *tree,
                                        ds_prbt_version_t *version,
                                        void *key);
static ds_prbt_t *prbt_compare_owner;
static ds_prbt_t *prbt_destroy_owner;

void ds_prbt_config_init(ds_prbt_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->compare = NULL;
  config->clone_key = NULL;
  config->clone_value = NULL;
  config->destroy_key = NULL;
  config->destroy_value = NULL;
  config->user = NULL;
  config->context = NULL;
}

static int prbt_compare_nodes(ds_rbt_node_t *a, ds_rbt_node_t *b) {
  ds_prbt_node_t *na;
  ds_prbt_node_t *nb;
  na = (ds_prbt_node_t *)a;
  nb = (ds_prbt_node_t *)b;
  return prbt_compare_owner->config.compare(na->key, nb->key,
                                            prbt_compare_owner->config.user);
}

static ds_prbt_node_t *prbt_node_create(ds_prbt_t *tree, void *key,
                                        void *value, int clone) {
  ds_prbt_node_t *node;
  node = (ds_prbt_node_t *)malloc(sizeof(*node));
  if (node == NULL) {
    return NULL;
  }
  node->rb.left = NULL;
  node->rb.right = NULL;
  node->rb.parent_color = RBT_RED;
  if (clone && tree->config.clone_key != NULL) {
    node->key = tree->config.clone_key(key, tree->config.user);
  } else {
    node->key = key;
  }
  if (clone && tree->config.clone_value != NULL) {
    node->value = tree->config.clone_value(value, tree->config.user);
  } else {
    node->value = value;
  }
  return node;
}

static void prbt_node_destroy_with_tree(ds_prbt_t *tree, ds_rbt_node_t *node) {
  ds_prbt_node_t *pnode;
  if (node == NULL) {
    return;
  }
  pnode = (ds_prbt_node_t *)node;
  if (tree->config.destroy_key != NULL) {
    tree->config.destroy_key(pnode->key, tree->config.user);
  }
  if (tree->config.destroy_value != NULL) {
    tree->config.destroy_value(pnode->value, tree->config.user);
  }
  free(pnode);
}

static void prbt_destroy_node_bridge(ds_rbt_node_t *node) {
  prbt_node_destroy_with_tree(prbt_destroy_owner, node);
}

static ds_rbt_node_t *prbt_clone_node_recursive(ds_prbt_t *owner,
                                                ds_rbt_t *src,
                                                ds_rbt_t *dst,
                                                ds_rbt_node_t *node,
                                                ds_rbt_node_t *parent) {
  ds_prbt_node_t *copy;
  ds_prbt_node_t *old;
  if (node == src->nil) {
    return dst->nil;
  }
  old = (ds_prbt_node_t *)node;
  copy = prbt_node_create(owner, old->key, old->value, 1);
  if (copy == NULL) {
    return dst->nil;
  }
  FUNC(rbt_set_parent_color)(&copy->rb, parent, (int)RBT_GET_COLOR_FROM_NODE(node));
  copy->rb.left = prbt_clone_node_recursive(owner, src, dst, node->left,
                                            &copy->rb);
  copy->rb.right = prbt_clone_node_recursive(owner, src, dst, node->right,
                                             &copy->rb);
  return &copy->rb;
}

static ds_rbt_t *prbt_clone_tree(ds_prbt_t *owner, ds_rbt_t *src) {
  ds_rbt_t *dst;
  dst = FUNC(rbt_create)();
  if (dst == NULL) {
    return NULL;
  }
  dst->size = src->size;
  dst->root = prbt_clone_node_recursive(owner, src, dst, src->root, dst->nil);
  return dst;
}

static ds_prbt_node_t *prbt_search_node(ds_prbt_t *tree,
                                        ds_prbt_version_t *version,
                                        void *key) {
  ds_prbt_node_t needle;
  ds_rbt_node_t *node;
  if (tree == NULL || version == NULL) {
    return NULL;
  }
  needle.key = key;
  prbt_compare_owner = tree;
  node = FUNC(rbt_search)(version->tree, version->tree->root, &needle.rb,
                          prbt_compare_nodes);
  if (node == version->tree->nil) {
    return NULL;
  }
  return (ds_prbt_node_t *)node;
}

ds_prbt_t *FUNC(ds_prbt_create)(const ds_prbt_config_t *config) {
  ds_prbt_t *tree;
  if (config == NULL || config->compare == NULL) {
    return NULL;
  }
  tree = (ds_prbt_t *)malloc(sizeof(*tree));
  if (tree == NULL) {
    return NULL;
  }
  tree->config = *config;
  tree->root = (ds_prbt_version_t *)malloc(sizeof(*tree->root));
  if (tree->root == NULL) {
    free(tree);
    return NULL;
  }
  tree->root->refs = 1UL;
  tree->root->tree = FUNC(rbt_create)();
  if (tree->root->tree == NULL) {
    free(tree->root);
    free(tree);
    return NULL;
  }
  return tree;
}

void FUNC(ds_prbt_destroy)(ds_prbt_t *tree) {
  if (tree == NULL) {
    return;
  }
  FUNC(ds_prbt_version_release)(tree, tree->root);
  free(tree);
}

ds_prbt_version_t *FUNC(ds_prbt_root)(ds_prbt_t *tree) {
  if (tree == NULL) {
    return NULL;
  }
  return tree->root;
}

ds_prbt_version_t *FUNC(ds_prbt_insert)(ds_prbt_t *tree,
                                         ds_prbt_version_t *base, void *key,
                                         void *value) {
  ds_prbt_version_t *version;
  ds_prbt_node_t *existing;
  ds_prbt_node_t *node;
  if (tree == NULL || base == NULL) {
    return NULL;
  }
  version = (ds_prbt_version_t *)malloc(sizeof(*version));
  if (version == NULL) {
    return NULL;
  }
  version->refs = 1UL;
  version->tree = prbt_clone_tree(tree, base->tree);
  if (version->tree == NULL) {
    free(version);
    return NULL;
  }
  existing = prbt_search_node(tree, version, key);
  if (existing != NULL) {
    if (tree->config.destroy_value != NULL) {
      tree->config.destroy_value(existing->value, tree->config.user);
    }
    existing->value = tree->config.clone_value != NULL
                          ? tree->config.clone_value(value, tree->config.user)
                          : value;
    return version;
  }
  node = prbt_node_create(tree, key, value, 1);
  if (node == NULL) {
    FUNC(ds_prbt_version_release)(tree, version);
    return NULL;
  }
  prbt_compare_owner = tree;
  FUNC(rbt_insert)(version->tree, &node->rb, prbt_compare_nodes);
  return version;
}

ds_status_t FUNC(ds_prbt_get)(ds_prbt_t *tree, ds_prbt_version_t *version,
                               void *key, void **out_value) {
  ds_prbt_node_t *node;
  if (out_value != NULL) {
    *out_value = NULL;
  }
  if (tree == NULL || version == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  node = prbt_search_node(tree, version, key);
  if (node == NULL) {
    return DS_NOT_FOUND;
  }
  *out_value = node->value;
  return DS_OK;
}

void FUNC(ds_prbt_version_retain)(ds_prbt_version_t *version) {
  if (version != NULL) {
    version->refs++;
  }
}

void FUNC(ds_prbt_version_release)(ds_prbt_t *tree,
                                   ds_prbt_version_t *version) {
  if (tree == NULL || version == NULL) {
    return;
  }
  version->refs--;
  if (version->refs != 0UL) {
    return;
  }
  prbt_destroy_owner = tree;
  FUNC(rbt_destroy_tree)(version->tree, prbt_destroy_node_bridge);
  free(version);
}
