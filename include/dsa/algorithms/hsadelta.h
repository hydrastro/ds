#ifndef DS_ALGO_HSADELTA_H
#define DS_ALGO_HSADELTA_H

/* In-memory hsadelta: aligned reference-block hashes, a suffix array over
 * those blocks, a one-hash quick-index bit filter, and hash-to-suffix ranges.
 * At a target offset, binary search chooses a longest full-block match;
 * byte comparison extends it in both directions. After the first match, all
 * alignments in that block-width window are considered before emitting it.
 * COPY/INSERT commands reconstruct the exact target; this is an approximation
 * to greedy byte matching, not a minimum-size edit script. Short matches that
 * contain no full aligned reference block can be missed.
 *
 * Hash collisions are resolved by block bytes when ordering symbols AND when
 * comparing target blocks. The default polynomial hash uses unsigned-long
 * wraparound and has a rolling update. Hash, rolling update, suffix builder,
 * lookup map and allocation context are replaceable. Custom callbacks must
 * be deterministic and agree with one another. Callback state is borrowed.
 * The index borrows immutable reference bytes until destroy. No file I/O or
 * particular patch wire format is imposed; a callback can stream commands.
 * Index and query scratch use O(reference_size/block_size + quick_bits/8)
 * memory, plus the chosen map/builder. The owned script also stores literals.
 * Collisions and repetitive data can require long byte comparisons; this
 * implementation does not promise the paper's specialized time/space constants.
 */
#include <dsa/algorithms/sha2.h>
#include <dsa/structures/map_iface.h>
#include <dsa/structures/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long (*ds_hsadelta_hash_fn)(const unsigned char *bytes,
                                             size_t length, void *user);
typedef unsigned long (*ds_hsadelta_roll_fn)(unsigned long hash,
                                             unsigned char removed,
                                             unsigned char added, size_t length,
                                             void *user);
typedef ds_status_t (*ds_hsadelta_suffix_fn)(const size_t *symbols,
                                             size_t count, size_t alphabet,
                                             size_t *out_suffixes, void *user,
                                             ds_context_t *context);
typedef struct ds_hsadelta_config {
  size_t block_size;           /* init: 32, must be positive */
  size_t minimum_match;        /* 0: block_size */
  size_t quick_bits;           /* 0: max(64, 16*block_count) */
  size_t max_blocks;           /* 0: unlimited */
  const ds_map_ops_t *map_ops; /* NULL: open addressing */
  ds_hsadelta_hash_fn hash;    /* NULL: polynomial default */
  ds_hsadelta_roll_fn roll;    /* custom hash may omit this and recompute */
  void *hash_user;
  ds_hsadelta_suffix_fn suffix; /* NULL: ds_sa_build_int */
  void *suffix_user;
} ds_hsadelta_config_t;
typedef struct ds_hsadelta_index {
  void *impl;
  ds_context_t *context;
} ds_hsadelta_index_t;
typedef enum ds_hsadelta_kind {
  DS_HSA_COPY = 0,
  DS_HSA_INSERT
} ds_hsadelta_kind_t;
typedef struct ds_hsadelta_command {
  ds_hsadelta_kind_t kind;
  size_t offset; /* COPY: reference offset; INSERT: target or literal offset */
  size_t length;
} ds_hsadelta_command_t;
typedef ds_status_t (*ds_hsadelta_emit_fn)(const ds_hsadelta_command_t *command,
                                           const unsigned char *literal_bytes,
                                           void *user);
typedef struct ds_hsadelta_progress {
  size_t commands, target_bytes; /* commands acknowledged with DS_OK */
} ds_hsadelta_progress_t;

void ds_hsadelta_config_init(ds_hsadelta_config_t *config);
/* Output must be uninitialized; unchanged on failure. A custom suffix builder
 * must return a lexicographically sorted permutation of the symbol suffixes;
 * the permutation is checked, the order is the callback's contract. */
ds_status_t ds_hsadelta_init(ds_hsadelta_index_t *index, const void *reference,
                             size_t reference_size,
                             const ds_hsadelta_config_t *config,
                             ds_context_t *context);
void ds_hsadelta_destroy(ds_hsadelta_index_t *index);
/* Literal bytes are borrowed target slices and live only during emit. INSERT
 * offsets here refer to target positions. Callbacks must not mutate inputs or
 * the index. Any non-DS_OK stops and is returned. Progress is written even on
 * failure; callback side effects are not rolled back. Scratch allocation
 * happens before the first command, but the callback can fail at any point. */
ds_status_t ds_hsadelta_emit(const ds_hsadelta_index_t *index,
                             const void *target, size_t target_size,
                             ds_hsadelta_emit_fn emit, void *user,
                             ds_hsadelta_progress_t *progress);

/* Optional owned script adapter. INSERT offsets address literals.data.
 * SHA-256 identifies the required reference; this is not authentication. */
typedef struct ds_hsadelta_script {
  ds_vec_t commands; /* ds_hsadelta_command_t */
  ds_vec_t literals; /* bytes */
  size_t reference_size, target_size;
  unsigned char reference_digest[DS_SHA256_SIZE];
} ds_hsadelta_script_t;
ds_status_t ds_hsadelta_build(const ds_hsadelta_index_t *index,
                              const void *target, size_t target_size,
                              ds_hsadelta_script_t *out);
void ds_hsadelta_script_destroy(ds_hsadelta_script_t *script);
/* Validates reference digest, command ranges and total length before allocating
 * output. max_output=0 means unlimited. On success the caller frees *out_data
 * through context; empty output is NULL. Errors preserve both outputs. */
ds_status_t ds_hsadelta_apply(const ds_hsadelta_script_t *script,
                              const void *reference, size_t reference_size,
                              size_t max_output, void **out_data,
                              size_t *out_size, ds_context_t *context);

#ifdef __cplusplus
}
#endif
#endif /* DS_ALGO_HSADELTA_H */
