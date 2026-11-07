#ifndef DS_BINARY_TREE_REPR_H
#define DS_BINARY_TREE_REPR_H

/*
 * Binary tree representation of trees: an ordered tree of any degree kept
 * as a binary tree whose left link is the node's first child and whose
 * right link is its next sibling (first child-next sibling, also called
 * the filial-heir chain or the left-child right-sibling representation).
 *
 * ds_lcrs_tree_t stores elements by value in node arrays (a free list
 * recycles removed subtrees) and keeps parent links as well: adding a
 * child at the front, at the back or after a sibling, detaching or
 * deleting subtrees, degree, depth, iterative pre-order, post-order and
 * level-order traversal, conversion from a parent array, and the binary
 * view (ds_lcrs_tree_left/right are the binary tree's links).
 *
 * ds_dc_trie_t is the doubly-chained tree (de la Briandais trie): a trie
 * over byte strings whose children are kept in a sorted sibling chain, so
 * each node needs only two links. Each key carries a size_t value.
 *
 * Stack trees: binary trees in which no node except the root has more than
 * one non-leaf child (the topology of simple fault-tolerant bus networks).
 * ds_binary_tree_is_stack_tree checks a binary tree given by child arrays
 * (any links, e.g. the binary view of an lcrs tree), and
 * ds_stack_tree_build lays one out with a chosen number of internal nodes:
 * two spines from the root, every spine node with one leaf child.
 *
 * Concurrency: a structure may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_LCRS_NONE (~(size_t)0)

typedef struct ds_lcrs_tree {
  unsigned char *data;
  size_t *child;   /* first child: the binary left link */
  size_t *sibling; /* next sibling: the binary right link */
  size_t *parent;
  size_t root;
  size_t size;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  size_t *stack; /* traversal scratch */
  size_t stack_capacity;
  ds_context_t *context;
} ds_lcrs_tree_t;

typedef bool (*ds_lcrs_visit_func_t)(size_t node, const void *elem,
                                     size_t depth, void *user);

ds_status_t ds_lcrs_tree_init(ds_lcrs_tree_t *t, size_t elem_size,
                              ds_context_t *context);
void ds_lcrs_tree_destroy(ds_lcrs_tree_t *t);
void ds_lcrs_tree_clear(ds_lcrs_tree_t *t);
size_t ds_lcrs_tree_size(const ds_lcrs_tree_t *t);
/* A root for an empty tree (DS_ERR_STATE otherwise). */
ds_status_t ds_lcrs_tree_set_root(ds_lcrs_tree_t *t, const void *elem,
                                  size_t *out_node);
ds_status_t ds_lcrs_tree_add_first_child(ds_lcrs_tree_t *t, size_t parent,
                                         const void *elem, size_t *out_node);
ds_status_t ds_lcrs_tree_add_last_child(ds_lcrs_tree_t *t, size_t parent,
                                        const void *elem, size_t *out_node);
ds_status_t ds_lcrs_tree_add_sibling_after(ds_lcrs_tree_t *t, size_t node,
                                           const void *elem, size_t *out_node);
/* Deletes node and its whole subtree. */
ds_status_t ds_lcrs_tree_remove_subtree(ds_lcrs_tree_t *t, size_t node);
/* Moves the subtree at node to be the last child of new_parent (not inside
 * the subtree). */
ds_status_t ds_lcrs_tree_move_subtree(ds_lcrs_tree_t *t, size_t node,
                                      size_t new_parent);
void *ds_lcrs_tree_get(const ds_lcrs_tree_t *t, size_t node);
size_t ds_lcrs_tree_root(const ds_lcrs_tree_t *t);
size_t ds_lcrs_tree_parent(const ds_lcrs_tree_t *t, size_t node);
size_t ds_lcrs_tree_left(const ds_lcrs_tree_t *t, size_t node);
size_t ds_lcrs_tree_right(const ds_lcrs_tree_t *t, size_t node);
size_t ds_lcrs_tree_degree(const ds_lcrs_tree_t *t, size_t node);
size_t ds_lcrs_tree_depth(const ds_lcrs_tree_t *t, size_t node);
/* Traversals from the root; the visitor returns false to stop. */
ds_status_t ds_lcrs_tree_preorder(ds_lcrs_tree_t *t, ds_lcrs_visit_func_t f,
                                  void *user);
ds_status_t ds_lcrs_tree_postorder(ds_lcrs_tree_t *t, ds_lcrs_visit_func_t f,
                                   void *user);
ds_status_t ds_lcrs_tree_levelorder(ds_lcrs_tree_t *t, ds_lcrs_visit_func_t f,
                                    void *user);
/* Builds the tree from a parent array (exactly one DS_LCRS_NONE: the root;
 * children ordered by index) over n elements; node i holds elems[i]. */
ds_status_t ds_lcrs_tree_from_parents(ds_lcrs_tree_t *t, const size_t *parents,
                                      const void *elems, size_t n);

typedef struct ds_dc_trie {
  unsigned char *symbol;
  size_t *child;
  size_t *sibling;
  size_t *value;
  unsigned char *terminal;
  size_t nodes; /* node 0: the root */
  size_t capacity;
  size_t free_list;
  size_t keys;
  ds_context_t *context;
} ds_dc_trie_t;

typedef bool (*ds_dc_trie_visit_func_t)(const unsigned char *key, size_t len,
                                        size_t value, void *user);

ds_status_t ds_dc_trie_init(ds_dc_trie_t *t, ds_context_t *context);
void ds_dc_trie_destroy(ds_dc_trie_t *t);
size_t ds_dc_trie_size(const ds_dc_trie_t *t);
/* DS_EXISTS (value replaced) when the key was present. */
ds_status_t ds_dc_trie_insert(ds_dc_trie_t *t, const unsigned char *key,
                              size_t len, size_t value);
ds_status_t ds_dc_trie_find(const ds_dc_trie_t *t, const unsigned char *key,
                            size_t len, size_t *out_value);
ds_status_t ds_dc_trie_remove(ds_dc_trie_t *t, const unsigned char *key,
                              size_t len);
/* Every key with the prefix, in lexicographic order; buf receives the key
 * (buf_len bytes: longer keys give DS_ERR_RANGE). */
ds_status_t ds_dc_trie_prefix(const ds_dc_trie_t *t,
                              const unsigned char *prefix, size_t len,
                              unsigned char *buf, size_t buf_len,
                              ds_dc_trie_visit_func_t visit, void *user);

/* left/right: n entries, (size_t)-1 for no child. false also when the
 * links do not form a tree rooted at root. */
bool ds_binary_tree_is_stack_tree(const size_t *left, const size_t *right,
                                  size_t n, size_t root, ds_context_t *context);
/* internal >= 1 internal nodes, 2 internal + 1 nodes in all (a full binary
 * tree): left/right need that many entries; the root is node 0. */
ds_status_t ds_stack_tree_build(size_t internal, size_t *left, size_t *right);

#ifdef __cplusplus
}
#endif

#endif /* DS_BINARY_TREE_REPR_H */
