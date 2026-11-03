#include "critbit_tree.h"
#include <string.h>

struct ds_critbit_node {
  bool leaf;
  /* internal nodes */
  size_t byte;
  unsigned char otherbits; /* all bits set except the critical one */
  struct ds_critbit_node *child[2];
  /* leaves */
  char *key;
  size_t key_len;
  void *value;
};

static unsigned char cb_byte(const char *key, size_t len, size_t i) {
  return i < len ? (unsigned char)key[i] : 0;
}

static int cb_direction(const ds_critbit_node_t *n, const char *key,
                        size_t len) {
  unsigned int c = cb_byte(key, len, n->byte);
  return (int)((1U + (n->otherbits | c)) >> 8);
}

static ds_critbit_node_t *cb_best_leaf(ds_critbit_node_t *n, const char *key,
                                       size_t len) {
  while (!n->leaf) {
    n = n->child[cb_direction(n, key, len)];
  }
  return n;
}

static void cb_free_subtree(ds_critbit_tree_t *tree, ds_critbit_node_t *n) {
  if (n == NULL) {
    return;
  }
  if (n->leaf) {
    ds_context_free(tree->context, n->key);
  } else {
    cb_free_subtree(tree, n->child[0]);
    cb_free_subtree(tree, n->child[1]);
  }
  ds_context_free(tree->context, n);
}

static ds_critbit_node_t *cb_new_leaf(ds_critbit_tree_t *tree, const char *key,
                                      size_t len, void *value) {
  ds_critbit_node_t *n = (ds_critbit_node_t *)ds_context_alloc(
      tree->context, sizeof(ds_critbit_node_t));
  if (n == NULL) {
    return NULL;
  }
  n->key = (char *)ds_context_alloc(tree->context, len + 1);
  if (n->key == NULL) {
    ds_context_free(tree->context, n);
    return NULL;
  }
  memcpy(n->key, key, len + 1);
  n->key_len = len;
  n->value = value;
  n->leaf = true;
  n->byte = 0;
  n->otherbits = 0;
  n->child[0] = NULL;
  n->child[1] = NULL;
  return n;
}

void ds_critbit_tree_init(ds_critbit_tree_t *tree, ds_context_t *context) {
  tree->root = NULL;
  tree->size = 0;
  tree->context = context != NULL ? context : ds_default_context();
}

void ds_critbit_tree_destroy(ds_critbit_tree_t *tree) {
  if (tree == NULL) {
    return;
  }
  cb_free_subtree(tree, tree->root);
  tree->root = NULL;
  tree->size = 0;
}

static ds_status_t cb_store(ds_critbit_tree_t *tree, const char *key,
                            void *value, bool overwrite, void **out_old) {
  size_t len;
  ds_critbit_node_t *p;
  ds_critbit_node_t *leaf;
  ds_critbit_node_t *node;
  ds_critbit_node_t **where;
  size_t newbyte;
  unsigned int newotherbits;
  int newdirection;
  if (tree == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  len = strlen(key);
  if (tree->root == NULL) {
    tree->root = cb_new_leaf(tree, key, len, value);
    if (tree->root == NULL) {
      return DS_ERR_ALLOC;
    }
    tree->size = 1;
    return DS_OK;
  }
  p = cb_best_leaf(tree->root, key, len);
  /* Find the first byte and bit where the key differs from that leaf. */
  for (newbyte = 0; newbyte <= len || newbyte <= p->key_len; newbyte++) {
    if (cb_byte(p->key, p->key_len, newbyte) != cb_byte(key, len, newbyte)) {
      break;
    }
  }
  if (newbyte > len && newbyte > p->key_len) {
    if (out_old != NULL) {
      *out_old = p->value;
    }
    if (overwrite) {
      p->value = value;
    }
    return DS_EXISTS;
  }
  newotherbits = (unsigned int)(cb_byte(p->key, p->key_len, newbyte) ^
                                cb_byte(key, len, newbyte));
  while ((newotherbits & (newotherbits - 1)) != 0) {
    newotherbits &= newotherbits - 1;
  }
  newotherbits ^= 255U;
  newdirection =
      (int)((1U + (newotherbits | cb_byte(p->key, p->key_len, newbyte))) >> 8);
  leaf = cb_new_leaf(tree, key, len, value);
  if (leaf == NULL) {
    return DS_ERR_ALLOC;
  }
  node = (ds_critbit_node_t *)ds_context_alloc(tree->context,
                                               sizeof(ds_critbit_node_t));
  if (node == NULL) {
    cb_free_subtree(tree, leaf);
    return DS_ERR_ALLOC;
  }
  node->leaf = false;
  node->byte = newbyte;
  node->otherbits = (unsigned char)newotherbits;
  node->key = NULL;
  node->key_len = 0;
  node->value = NULL;
  node->child[1 - newdirection] = leaf;
  /* Insert where the tree's critical bits pass the new one. */
  where = &tree->root;
  for (;;) {
    ds_critbit_node_t *q = *where;
    if (q->leaf || q->byte > newbyte ||
        (q->byte == newbyte && q->otherbits > newotherbits)) {
      break;
    }
    where = &q->child[cb_direction(q, key, len)];
  }
  node->child[newdirection] = *where;
  *where = node;
  tree->size++;
  return DS_OK;
}

ds_status_t ds_critbit_tree_insert(ds_critbit_tree_t *tree, const char *key,
                                   void *value) {
  return cb_store(tree, key, value, false, NULL);
}

ds_status_t ds_critbit_tree_put(ds_critbit_tree_t *tree, const char *key,
                                void *value, void **out_old) {
  return cb_store(tree, key, value, true, out_old);
}

static ds_critbit_node_t *cb_find(const ds_critbit_tree_t *tree,
                                  const char *key) {
  size_t len;
  ds_critbit_node_t *p;
  if (tree->root == NULL || key == NULL) {
    return NULL;
  }
  len = strlen(key);
  p = cb_best_leaf(tree->root, key, len);
  return strcmp(p->key, key) == 0 ? p : NULL;
}

ds_status_t ds_critbit_tree_get(const ds_critbit_tree_t *tree, const char *key,
                                void **out_value) {
  ds_critbit_node_t *p = cb_find(tree, key);
  if (p == NULL) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = p->value;
  }
  return DS_OK;
}

bool ds_critbit_tree_contains(const ds_critbit_tree_t *tree, const char *key) {
  return cb_find(tree, key) != NULL;
}

ds_status_t ds_critbit_tree_remove(ds_critbit_tree_t *tree, const char *key,
                                   void **out_value) {
  size_t len;
  ds_critbit_node_t **wherep;
  ds_critbit_node_t **whereq = NULL;
  ds_critbit_node_t *p;
  ds_critbit_node_t *q = NULL;
  int direction = 0;
  if (tree == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (tree->root == NULL) {
    return DS_NOT_FOUND;
  }
  len = strlen(key);
  wherep = &tree->root;
  p = tree->root;
  while (!p->leaf) {
    whereq = wherep;
    q = p;
    direction = cb_direction(q, key, len);
    wherep = &q->child[direction];
    p = *wherep;
  }
  if (strcmp(p->key, key) != 0) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = p->value;
  }
  ds_context_free(tree->context, p->key);
  ds_context_free(tree->context, p);
  if (whereq == NULL) {
    tree->root = NULL;
  } else {
    *whereq = q->child[1 - direction];
    ds_context_free(tree->context, q);
  }
  tree->size--;
  return DS_OK;
}

size_t ds_critbit_tree_size(const ds_critbit_tree_t *tree) {
  return tree->size;
}

static bool cb_walk(const ds_critbit_node_t *n, ds_critbit_visit_func_t visit,
                    void *user) {
  if (n->leaf) {
    return visit(n->key, n->value, user);
  }
  return cb_walk(n->child[0], visit, user) && cb_walk(n->child[1], visit, user);
}

ds_status_t ds_critbit_tree_visit(const ds_critbit_tree_t *tree,
                                  ds_critbit_visit_func_t visit, void *user) {
  if (tree == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (tree->root == NULL) {
    return DS_OK;
  }
  return cb_walk(tree->root, visit, user) ? DS_OK : DS_STOP;
}

ds_status_t ds_critbit_tree_visit_prefix(const ds_critbit_tree_t *tree,
                                         const char *prefix,
                                         ds_critbit_visit_func_t visit,
                                         void *user) {
  size_t len;
  const ds_critbit_node_t *p;
  const ds_critbit_node_t *top;
  const ds_critbit_node_t *leaf;
  if (tree == NULL || prefix == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (tree->root == NULL) {
    return DS_OK;
  }
  len = strlen(prefix);
  /* Descend while the critical byte lies inside the prefix. */
  p = tree->root;
  top = p;
  while (!p->leaf) {
    if (p->byte >= len) {
      break;
    }
    p = p->child[cb_direction(p, prefix, len)];
    top = p;
  }
  leaf = top;
  while (!leaf->leaf) {
    leaf = leaf->child[0];
  }
  if (strncmp(leaf->key, prefix, len) != 0) {
    return DS_OK; /* nothing has this prefix */
  }
  return cb_walk(top, visit, user) ? DS_OK : DS_STOP;
}

const char *ds_critbit_tree_min(const ds_critbit_tree_t *tree) {
  const ds_critbit_node_t *n = tree->root;
  if (n == NULL) {
    return NULL;
  }
  while (!n->leaf) {
    n = n->child[0];
  }
  return n->key;
}

const char *ds_critbit_tree_max(const ds_critbit_tree_t *tree) {
  const ds_critbit_node_t *n = tree->root;
  if (n == NULL) {
    return NULL;
  }
  while (!n->leaf) {
    n = n->child[1];
  }
  return n->key;
}
