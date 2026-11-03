#ifndef DS_ALGO_PERFECT_HASH_H
#define DS_ALGO_PERFECT_HASH_H

/*
 * Order-preserving minimal perfect hashing (Czech, Havas and Majewski,
 * 1992): for a static set of n distinct keys, build a function that maps
 * the i-th key to exactly i, using about 2.09 n words.
 *
 * Each key becomes an edge between two vertices chosen by two seeded
 * hashes (one in each half of the vertex set, so no self-loops). If the
 * random graph is acyclic, vertex values g can be assigned so that
 * (g[h1(k)] + g[h2(k)]) mod n is the key's index; acyclicity is checked by
 * peeling degree-1 vertices (no adjacency lists: each vertex keeps the XOR
 * of its incident edge ids), and a cyclic graph is retried with new seeds,
 * which with ratio >= 2.09 needs about 3 attempts on average.
 *
 * Lookups are two hashes and two array reads, O(1) worst case. A key that
 * was not in the set maps to some arbitrary index: store the keys (or a
 * fingerprint) at their index if membership must be checked.
 *
 * Keys are fetched through a callback, so they can live in any container.
 * Build memory comes from a ds_context_t; g can be saved and reloaded with
 * the seeds (the struct is public).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Report key i: *out_data and *out_length. */
typedef void (*ds_perfect_hash_key_func_t)(size_t index, const void **out_data,
                                           size_t *out_length, void *user);

typedef struct ds_perfect_hash {
  unsigned long *g;
  size_t vertices; /* g has this many entries, an even number */
  size_t count;    /* n */
  unsigned long seed1;
  unsigned long seed2;
  ds_context_t *context;
} ds_perfect_hash_t;

/*
 * Build for keys 0 .. count - 1. ratio is vertices / count (0 picks 2.09;
 * at least 2.05 recommended). seed starts the seed sequence (any value).
 * DS_ERR_STATE if no acyclic graph was found in 64 attempts, which in
 * practice means two keys are equal (their edges always form a cycle).
 */
ds_status_t ds_perfect_hash_build(ds_perfect_hash_t *hash, size_t count,
                                  ds_perfect_hash_key_func_t key, void *user,
                                  double ratio, unsigned long seed,
                                  ds_context_t *context);
void ds_perfect_hash_destroy(ds_perfect_hash_t *hash);
/* Index of the key in [0, count) (arbitrary for keys not in the set). */
size_t ds_perfect_hash_lookup(const ds_perfect_hash_t *hash, const void *data,
                              size_t length);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_PERFECT_HASH_H */
