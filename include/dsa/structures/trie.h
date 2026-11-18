#ifndef DS_TRIE_H
#define DS_TRIE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct trie_node {
  size_t data_slice;
  struct trie_node *parent;
  void *children;
  bool is_terminal;
  void *terminal_data;
} ds_trie_node_t;

/*
 * Child stores. Each node keeps its children in a store, keyed by slice:
 * a hash table (below), a Robin Hood map or a sorted vector
 * (trie_stores.h), or your own ds_trie_store_ops_t.
 *
 * next(store, after) gives the child that follows after in the store's
 * order, the first child for after NULL, NULL past the last. It is
 * optional (stores written before it leave it NULL), and with it the trie
 * walks, copies and destroys itself without recursion; without it those
 * go through the store's apply and clone, which recurse as deep as the
 * longest key. The order must hold while the store changes: inserting or
 * removing other children must not reorder the rest, so that a walk that
 * resumes from after meets every remaining child exactly once.
 * Destruction frees children without detaching them, asking for each
 * one's successor while it is still alive: next must not look at children
 * other than after.
 *
 * Walks (with next): ds_trie_apply and ds_trie_walk visit the nodes below
 * a node in post-order, children before their parent. The callback may
 * detach and free the node it is given, and nodes it was given before; it
 * must not remove a node it has not been given yet (every ancestor of the
 * current node is one). Nodes inserted during a walk may or may not be
 * visited. ds_trie_preorder_next iterates without a callback, parents
 * first. ds_trie_clone and ds_trie_clone_node copy all or nothing,
 * calling clone_data only once the copy is complete (without next, a
 * failed copy can leave clone_data's results behind).
 */
typedef struct trie_node *(*ds_trie_next_fn)(void *store,
                                             const struct trie_node *after);

typedef struct trie {
  ds_trie_node_t *root;
  size_t num_splits;
  ds_trie_node_t *(*store_search)(void *, size_t);
  void *(*store_create)(struct trie *, size_t);
  void (*store_insert)(void *, ds_trie_node_t *);
  void (*store_remove)(void *, ds_trie_node_t *);
  void (*store_destroy_entry)(void *, ds_trie_node_t *);
  void (*store_destroy)(void *);
  size_t (*store_get_size)(void *);
  void (*store_apply)(struct trie *, void *,
                      void (*)(struct trie *, ds_trie_node_t *, va_list *),
                      va_list *);
  void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                       void *(*clone_data)(void *));
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  ds_context_t *context;
  bool use_context;           /* nodes and stores come from context */
  ds_trie_next_fn store_next; /* NULL for stores without next */
} ds_trie_t;

typedef struct ds_trie_store_ops {
  ds_trie_node_t *(*search)(void *store, size_t slice);
  void *(*create)(struct trie *trie, size_t size);
  void (*insert)(void *store, ds_trie_node_t *node);
  void (*remove)(void *store, ds_trie_node_t *node);
  void (*destroy_entry)(void *store, ds_trie_node_t *node);
  void (*destroy)(void *store);
  size_t (*get_size)(void *store);
  void (*apply)(struct trie *trie, void *store,
                void (*f)(struct trie *, ds_trie_node_t *, va_list *),
                va_list *args);
  void *(*clone)(struct trie *trie, void *store, ds_trie_node_t *parent_node,
                 void *(*clone_data)(void *));
  ds_trie_next_fn next; /* optional, see above */
} ds_trie_store_ops_t;

typedef struct ds_trie_config {
  size_t num_splits;
  const ds_trie_store_ops_t *store_ops;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  ds_context_t *context;
} ds_trie_config_t;

const ds_trie_store_ops_t *ds_trie_hash_store_ops(void);
void ds_trie_config_init(ds_trie_config_t *config);
ds_trie_t *ds_trie_create_config(const ds_trie_config_t *config);
ds_trie_t *ds_trie_create_hash(size_t num_splits);
ds_status_t ds_trie_insert(ds_trie_t *trie, void *data,
                           size_t (*get_slice)(void *, size_t),
                           bool (*has_slice)(void *, size_t));
ds_status_t ds_trie_get(ds_trie_t *trie, void *data,
                        size_t (*get_slice)(void *, size_t),
                        bool (*has_slice)(void *, size_t),
                        ds_trie_node_t **out_node);
ds_status_t ds_trie_remove(ds_trie_t *trie, void *data,
                           size_t (*get_slice)(void *, size_t),
                           bool (*has_slice)(void *, size_t));
void ds_trie_destroy(ds_trie_t *trie);

/* NULL when the node or its child store cannot be allocated */
ds_trie_node_t *ds_trie_create_node(ds_trie_t *trie);
/* Allocation for the trie and its child stores: through the configured
 * context when one was given to ds_trie_create_config (and no explicit
 * allocator), else through the allocator functions. Custom stores should
 * use these. */
void *ds_trie_alloc(ds_trie_t *trie, size_t size);
void ds_trie_free(ds_trie_t *trie, void *ptr);

ds_trie_t *ds_trie_create_hash_alloc(size_t num_splits,
                                     void *(*allocator)(size_t),
                                     void (*deallocator)(void *));
ds_trie_node_t *ds_trie_hash_table_store_search(void *store, size_t slice);
void *ds_trie_hash_table_store_create(struct trie *trie, size_t size);
void ds_trie_hash_table_store_insert(void *store, ds_trie_node_t *node);
void ds_trie_hash_table_store_remove(void *store, ds_trie_node_t *node);
void ds_trie_hash_table_store_destroy_entry(void *store, ds_trie_node_t *node);
void ds_trie_hash_table_store_destroy(void *store);
size_t ds_trie_hash_table_store_get_size(void *store);
void ds_trie_hash_table_store_apply(struct trie *trie, void *store,
                                    void (*f)(struct trie *, ds_trie_node_t *,
                                              va_list *),
                                    va_list *args);
void *ds_trie_hash_table_store_clone(struct trie *trie, void *store,
                                     ds_trie_node_t *parent_node,
                                     void *(*clone_data)(void *));
ds_trie_node_t *ds_trie_hash_table_store_next(void *store,
                                              const ds_trie_node_t *after);
ds_trie_t *ds_trie_create(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(struct trie *, size_t),
    void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                         void *(*clone_data)(void *)));
ds_trie_t *ds_trie_create_alloc(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(struct trie *, size_t),
    void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                         void *(*clone_data)(void *)),
    void *(*allocator)(size_t), void (*deallocator)(void *));

ds_trie_node_t *ds_trie_search(ds_trie_t *trie, void *data,
                               size_t (*get_slice)(void *, size_t),
                               bool (*has_slice)(void *, size_t));
void ds_trie_delete_node(ds_trie_t *trie, ds_trie_node_t *node);
void ds_trie_destroy_node(ds_trie_t *trie, ds_trie_node_t *node,
                          void (*destroy)(ds_trie_t *, ds_trie_node_t *,
                                          va_list *));
void ds_trie_destroy_callback(ds_trie_t *trie, ds_trie_node_t *node,
                              va_list *args);
void ds_trie_delete_trie(ds_trie_t *trie);
void ds_trie_destroy_trie(ds_trie_t *trie,
                          void (*destroy)(ds_trie_t *, ds_trie_node_t *,
                                          va_list *));
void ds_trie_apply(ds_trie_t *trie, ds_trie_node_t *node,
                   void (*f)(ds_trie_t *, ds_trie_node_t *, va_list *), ...);
ds_trie_node_t *ds_trie_clone_node(ds_trie_t *trie, ds_trie_node_t *node,
                                   ds_trie_node_t *parent_node,
                                   void *(*clone_data)(void *));
ds_trie_t *ds_trie_clone(ds_trie_t *trie, void *(*clone_data)(void *));

/* Pre-order iteration over the nodes below top (top excluded), parents
 * before their children: node NULL gives the first, NULL ends it (and is
 * all a trie whose stores lack next gives). To prune while iterating, get
 * the next node before detaching the current one, which by then must have
 * no children. Insertions may or may not come up. */
ds_trie_node_t *ds_trie_preorder_next(ds_trie_t *trie, ds_trie_node_t *top,
                                      ds_trie_node_t *node);
/* For stores with next: a post-order walk below node (the contract above),
 * and a copy of a store with every subtree under it, all or nothing, its
 * children's parent set to parent_node. Stores can build apply and clone
 * on these. */
void ds_trie_walk(ds_trie_t *trie, ds_trie_node_t *node, ds_trie_next_fn next,
                  void (*f)(ds_trie_t *, ds_trie_node_t *, va_list *),
                  va_list *args);
void *ds_trie_clone_store(ds_trie_t *trie, void *store,
                          ds_trie_node_t *parent_node,
                          void *(*clone_data)(void *), ds_trie_next_fn next);

/* Typed key extraction: DS_OK produces one slice, DS_STOP ends the key;
 * negative errors abort, other positive statuses are callback errors.
 * Extraction must be deterministic for one call. NULL data is passed through to
 * the policy. Insert borrows data and rolls back newly created nodes on
 * failure; updates return DS_EXISTS. out_previous (optional) is set only on
 * success, NULL for a new key. Get leaves out unchanged on absence/error.
 * Remove returns borrowed payload through optional out_data and never destroys
 * it. */
typedef ds_status_t (*ds_trie_slice_fn)(void *data, size_t index, size_t *slice,
                                        void *user);
typedef ds_status_t (*ds_trie_visit_fn)(ds_trie_t *, ds_trie_node_t *,
                                        void *user);
typedef ds_status_t (*ds_trie_clone_fn)(void *data, void **out, void *user);
typedef void (*ds_trie_data_destroy_fn)(void *data, void *user);
ds_status_t ds_trie_create_config_ex(const ds_trie_config_t *config,
                                     ds_trie_t **out);
ds_status_t ds_trie_insert_with(ds_trie_t *trie, void *data,
                                ds_trie_slice_fn slice, void *user,
                                void **out_previous);
ds_status_t ds_trie_get_with(ds_trie_t *trie, void *data,
                             ds_trie_slice_fn slice, void *user,
                             ds_trie_node_t **out);
ds_status_t ds_trie_remove_with(ds_trie_t *trie, void *data,
                                ds_trie_slice_fn slice, void *user,
                                void **out_data);
/* Checked walk includes top; preorder when postorder is false. Visitors must
 * not mutate/destroy the trie. Requires store.next; returns DS_ERR_UNSUPPORTED
 * otherwise. Destroy invokes the callback on terminal payloads (including a
 * stored NULL), then releases the tree; it likewise requires next. */
ds_status_t ds_trie_walk_with(ds_trie_t *trie, ds_trie_node_t *top,
                              bool postorder, ds_trie_visit_fn visit,
                              void *user);
ds_status_t ds_trie_destroy_with(ds_trie_t *trie,
                                 ds_trie_data_destroy_fn destroy, void *user);
/* Config NULL inherits store/owner; an explicit config may select a different
 * child store and owner. Both stores must provide next. clone/destroy are both
 * NULL for a borrowed copy, or both non-NULL for an owning copy. DS_OK may
 * clone a NULL payload. Failed callbacks own unfinished outputs; completed
 * payloads are rolled back. Positive callback statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure; source is unchanged. Release owning results
 * through destroy_with using the matching payload policy. */
ds_status_t ds_trie_clone_with(ds_trie_t *trie, ds_trie_clone_fn clone,
                               ds_trie_data_destroy_fn destroy, void *user,
                               const ds_trie_config_t *config, ds_trie_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_TRIE_H */
