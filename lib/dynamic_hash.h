#ifndef DS_DYNAMIC_HASH_H
#define DS_DYNAMIC_HASH_H

/*
 * More dynamic hashing schemes (keys and values stored by value; hashing
 * and equality pluggable with a user pointer). They complement
 * lib/linear_hash_map (linear hashing) and lib/extendible_hash.
 *
 * ds_virtual_hash_t   virtual hashing (Litwin): a family of hash
 *                     functions h_j(k) = H(k) mod (n0 * 2^j) and a bit
 *                     table of the buckets in use. An overflowing bucket
 *                     m at level j splits right away into m and
 *                     m + n0 * 2^j (unlike linear hashing, which splits in
 *                     a fixed order); a lookup starts at the highest level
 *                     and steps down until it reaches a bucket in use.
 *                     The address space is kept within a constant factor
 *                     of the number of keys; past that (a degenerate
 *                     hash) a bucket overflows its nominal size instead.
 * ds_expandable_hash_t expandable hashing (Knott): a binary trie on the
 *                     hash bits with buckets at the leaves; a full bucket
 *                     splits on the next bit, two sibling buckets that fit
 *                     in one merge again. No directory to double.
 * ds_hashbelt_t       a hashbelt: a conveyor of generations, each a small
 *                     hash table. New entries go to the newest generation;
 *                     ds_hashbelt_rotate retires the oldest generation
 *                     (handing its entries to a callback) and opens a new
 *                     one, so entries expire in bulk without per-entry
 *                     timers. Optionally a hit refreshes the entry into
 *                     the newest generation.
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

typedef size_t (*ds_dyn_hash_func_t)(const void *key, void *user);
typedef bool (*ds_dyn_equal_func_t)(const void *a, const void *b, void *user);
typedef bool (*ds_dyn_visit_func_t)(const void *key, const void *value,
                                    void *user);

typedef struct ds_virtual_hash {
  unsigned char **bkeys; /* per bucket, grown on demand */
  unsigned char **bvals;
  size_t *bcap;
  size_t *load;
  unsigned char *in_use; /* bit table, one byte per bucket */
  unsigned char *level;  /* level of each bucket in use */
  size_t buckets;        /* allocated buckets (n0 * 2^max_level) */
  size_t n0;
  size_t max_level;
  size_t bucket_size;
  size_t size;
  size_t key_size;
  size_t value_size;
  ds_dyn_hash_func_t hash;
  ds_dyn_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_virtual_hash_t;

/* n0 initial buckets (0: 4), bucket_size entries each (0: 4). */
ds_status_t ds_virtual_hash_init(ds_virtual_hash_t *h, size_t key_size,
                                 size_t value_size, ds_dyn_hash_func_t hash,
                                 ds_dyn_equal_func_t equal, void *user,
                                 size_t n0, size_t bucket_size,
                                 ds_context_t *context);
void ds_virtual_hash_destroy(ds_virtual_hash_t *h);
size_t ds_virtual_hash_size(const ds_virtual_hash_t *h);
ds_status_t ds_virtual_hash_put(ds_virtual_hash_t *h, const void *key,
                                const void *value);
void *ds_virtual_hash_get(const ds_virtual_hash_t *h, const void *key);
ds_status_t ds_virtual_hash_remove(ds_virtual_hash_t *h, const void *key,
                                   void *out_value);
size_t ds_virtual_hash_buckets_in_use(const ds_virtual_hash_t *h);
void ds_virtual_hash_foreach(const ds_virtual_hash_t *h, ds_dyn_visit_func_t f,
                             void *user);

typedef struct ds_expandable_hash {
  size_t *left; /* trie nodes: children, or DS_NONE for leaves */
  size_t *right;
  size_t *bucket; /* leaf: bucket index */
  size_t nodes;
  size_t node_capacity;
  size_t node_free;
  unsigned char *keys; /* bucket storage */
  unsigned char *values;
  size_t *load;
  size_t bucket_count;
  size_t bucket_capacity;
  size_t bucket_free;
  size_t root;
  size_t bucket_size;
  size_t size;
  size_t key_size;
  size_t value_size;
  ds_dyn_hash_func_t hash;
  ds_dyn_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_expandable_hash_t;

ds_status_t ds_expandable_hash_init(ds_expandable_hash_t *h, size_t key_size,
                                    size_t value_size, ds_dyn_hash_func_t hash,
                                    ds_dyn_equal_func_t equal, void *user,
                                    size_t bucket_size, ds_context_t *context);
void ds_expandable_hash_destroy(ds_expandable_hash_t *h);
size_t ds_expandable_hash_size(const ds_expandable_hash_t *h);
/* DS_ERR_OVERFLOW if more than bucket_size keys share all hash bits. */
ds_status_t ds_expandable_hash_put(ds_expandable_hash_t *h, const void *key,
                                   const void *value);
void *ds_expandable_hash_get(const ds_expandable_hash_t *h, const void *key);
ds_status_t ds_expandable_hash_remove(ds_expandable_hash_t *h, const void *key,
                                      void *out_value);
size_t ds_expandable_hash_buckets(const ds_expandable_hash_t *h);
void ds_expandable_hash_foreach(const ds_expandable_hash_t *h,
                                ds_dyn_visit_func_t f, void *user);

typedef struct ds_hashbelt_gen {
  unsigned char *keys;
  unsigned char *values;
  unsigned char *used;
  size_t slots; /* power of two or 0 */
  size_t size;
} ds_hashbelt_gen_t;

typedef struct ds_hashbelt {
  ds_hashbelt_gen_t *gen; /* ring */
  size_t generations;
  size_t newest;
  size_t size;
  bool refresh;
  size_t key_size;
  size_t value_size;
  ds_dyn_hash_func_t hash;
  ds_dyn_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_hashbelt_t;

/* generations >= 2; refresh: a hit moves the entry to the newest. */
ds_status_t ds_hashbelt_init(ds_hashbelt_t *b, size_t key_size,
                             size_t value_size, ds_dyn_hash_func_t hash,
                             ds_dyn_equal_func_t equal, void *user,
                             size_t generations, bool refresh,
                             ds_context_t *context);
void ds_hashbelt_destroy(ds_hashbelt_t *b);
size_t ds_hashbelt_size(const ds_hashbelt_t *b);
/* Inserts or replaces; the entry (re)starts in the newest generation. */
ds_status_t ds_hashbelt_put(ds_hashbelt_t *b, const void *key,
                            const void *value);
/* Copies the value to out (may be NULL); refreshes when configured. */
ds_status_t ds_hashbelt_get(ds_hashbelt_t *b, const void *key, void *out);
ds_status_t ds_hashbelt_remove(ds_hashbelt_t *b, const void *key, void *out);
/* Generation holding key (0 = newest), or DS_NOT_FOUND. */
ds_status_t ds_hashbelt_age(const ds_hashbelt_t *b, const void *key,
                            size_t *out_age);
/* Retires the oldest generation: expired (may be NULL) sees each entry. */
ds_status_t ds_hashbelt_rotate(ds_hashbelt_t *b, ds_dyn_visit_func_t expired,
                               void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_DYNAMIC_HASH_H */
