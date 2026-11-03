#ifndef DS_MERKLE_TREE_H
#define DS_MERKLE_TREE_H

/*
 * Merkle tree (hash tree; Merkle, 1979) over n leaves with a pluggable
 * digest function, supporting leaf updates, appends, audit proofs and
 * proof verification.
 *
 * Leaves hash as D(0x00 || data) and internal nodes as
 * D(0x01 || left || right), the domain separation of RFC 6962, so a leaf can
 * never be passed off as an internal node. The digest is supplied by the
 * caller as a scatter-gather function, so any hash (SHA-256, BLAKE2, a
 * keyed MAC, or a non-cryptographic hash for change detection) plugs in
 * without copying data. ds_merkle_tree_default_digest() is a fast
 * non-cryptographic 16-byte digest for non-adversarial uses only.
 *
 * Odd node policies at each level:
 *   DS_MERKLE_PROMOTE    an unpaired last node moves up unchanged; this is
 *                        exactly RFC 6962 / Certificate Transparency
 *   DS_MERKLE_DUPLICATE  it is paired with itself (Bitcoin's rule)
 *
 * All levels are stored, so update and append recompute one root path in
 * O(log n) digests and proofs are read off directly. A proof for leaf i of
 * a tree of n leaves is the list of sibling digests from the bottom up;
 * verification needs the leaf data, i, n, the proof and the root, nothing
 * else.
 *
 * Storage comes from a ds_context_t.
 * Concurrency: a tree may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include "vec.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_MERKLE_MAX_LEVELS 65
#define DS_MERKLE_MAX_DIGEST 64

/* Digest of the concatenation of `count` parts into out (digest_size). */
typedef void (*ds_merkle_digest_func_t)(const void *const *parts,
                                        const size_t *lengths, size_t count,
                                        unsigned char *out, void *user);

typedef enum ds_merkle_odd {
  DS_MERKLE_PROMOTE = 0,
  DS_MERKLE_DUPLICATE = 1
} ds_merkle_odd_t;

typedef struct ds_merkle_tree_config {
  size_t digest_size; /* 1 .. DS_MERKLE_MAX_DIGEST */
  ds_merkle_digest_func_t digest;
  void *user;
  ds_merkle_odd_t odd;
  ds_context_t *context;
} ds_merkle_tree_config_t;

typedef struct ds_merkle_tree {
  ds_vec_t levels[DS_MERKLE_MAX_LEVELS]; /* level 0: leaf digests */
  size_t level_count;
  ds_merkle_tree_config_t config;
} ds_merkle_tree_t;

/* Leaf provider for build: report leaf i. */
typedef void (*ds_merkle_leaf_func_t)(size_t index, const void **out_data,
                                      size_t *out_length, void *user);

void ds_merkle_tree_config_init(ds_merkle_tree_config_t *config);
/* 16-byte non-cryptographic digest (four seeded MurmurHash3 lanes). */
void ds_merkle_tree_default_digest(const void *const *parts,
                                   const size_t *lengths, size_t count,
                                   unsigned char *out, void *user);

ds_status_t ds_merkle_tree_init(ds_merkle_tree_t *tree,
                                const ds_merkle_tree_config_t *config);
void ds_merkle_tree_destroy(ds_merkle_tree_t *tree);

/* Replace the contents with `count` leaves. */
ds_status_t ds_merkle_tree_build(ds_merkle_tree_t *tree, size_t count,
                                 ds_merkle_leaf_func_t leaf, void *user);
ds_status_t ds_merkle_tree_append(ds_merkle_tree_t *tree, const void *data,
                                  size_t length);
ds_status_t ds_merkle_tree_update(ds_merkle_tree_t *tree, size_t index,
                                  const void *data, size_t length);

size_t ds_merkle_tree_leaf_count(const ds_merkle_tree_t *tree);
/* Root digest; DS_EMPTY for an empty tree. */
ds_status_t ds_merkle_tree_root(const ds_merkle_tree_t *tree,
                                unsigned char *out);
/* Digest of leaf i as stored. */
const unsigned char *ds_merkle_tree_leaf_digest(const ds_merkle_tree_t *tree,
                                                size_t index);

/* Audit path of leaf i: up to `capacity` digests of digest_size bytes
 * written to out, their number in *out_count. */
ds_status_t ds_merkle_tree_proof(const ds_merkle_tree_t *tree, size_t index,
                                 unsigned char *out, size_t capacity,
                                 size_t *out_count);
/* Check that `data` is leaf `index` of an n-leaf tree with this root. */
bool ds_merkle_tree_verify(const ds_merkle_tree_config_t *config,
                           const void *data, size_t length, size_t index,
                           size_t leaf_count, const unsigned char *proof,
                           size_t proof_count, const unsigned char *root);

#ifdef __cplusplus
}
#endif

#endif /* DS_MERKLE_TREE_H */
