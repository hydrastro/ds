#ifndef DS_ALGO_EXTERNAL_SORT_H
#define DS_ALGO_EXTERNAL_SORT_H

/*
 * External sorting: data larger than memory, kept on sequential "tapes"
 * (or, for external quicksort, a random-access external array).
 *
 * Tapes are reached only through ds_ext_tape_ops_t, so anything that can
 * append, rewind and read sequentially works: files, flash pages, a
 * network store. Two backends are included: memory tapes (growable
 * buffers from a context; handy for tests and for staging) and FILE
 * tapes (tmpfile()). A tape supports
 *   write          append an element at the end
 *   rewind         read again from the beginning
 *   read           next element (DS_NOT_FOUND past the end)
 *   read_backward  consume the last element (the tape end moves back, so
 *                  later writes overwrite it); only oscillating sort
 *                  needs it
 *   clear          empty the tape
 *
 * ds_ext_sort() reads the input from a callback, forms initial runs in a
 * caller memory area (load-sort-store, or replacement selection: runs of
 * about twice the memory on random input), and merges them over the
 * tapes with a selectable pattern:
 *   DS_EXT_BALANCED     balanced k-way merge: half the tapes in, half
 *                       out, every pass merges one run from each input
 *                       (balanced two-way with 4 tapes, balanced
 *                       multiway in general)
 *   DS_EXT_POLYPHASE    polyphase merge: T - 1 way merges onto the one
 *                       empty tape; runs are distributed to the perfect
 *                       (generalised Fibonacci) numbers level by level,
 *                       the missing ones being dummy runs spread over the
 *                       tapes, which is the optimal distribution
 *   DS_EXT_CASCADE      cascade merge (nonbalanced merge sort): each pass
 *                       merges T - 1 ways until a tape empties, then
 *                       T - 2 ways onto it, and so on; the final one-way
 *                       copy is skipped
 *   DS_EXT_OSCILLATING  oscillating merge sort (Sobel): needs tapes that
 *                       read backward and the element count; runs are
 *                       formed and merged alternately, merging reads
 *                       backward so no rewinds are needed
 * The last merge goes straight to the output callback. Not stable.
 *
 * ds_ext_radix_sort() is an LSD radix sort over 2 * 2^bits tapes by an
 * unsigned long key (stable). ds_ext_quicksort() is external quicksort
 * over a random-access array: a memory-sized sorted middle buffer is grown
 * while reading from both ends, smaller elements are written to the left
 * and larger to the right, the middle is written between, and the two
 * sides are sorted the same way.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "elem.h"
#include "sort.h"
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_EXT_UNKNOWN (~(size_t)0)

typedef struct ds_ext_tape_ops {
  ds_status_t (*write)(size_t tape, const void *elem, void *tapes);
  ds_status_t (*read)(size_t tape, void *out_elem, void *tapes);
  ds_status_t (*read_backward)(size_t tape, void *out_elem, void *tapes);
  ds_status_t (*rewind)(size_t tape, void *tapes);
  ds_status_t (*clear)(size_t tape, void *tapes);
} ds_ext_tape_ops_t;

/* Input: copy the next element to out (DS_NOT_FOUND at the end). */
typedef ds_status_t (*ds_ext_source_func_t)(void *out_elem, void *user);
/* Output: receive the next element in sorted order. */
typedef ds_status_t (*ds_ext_sink_func_t)(const void *elem, void *user);

/* ---- memory tapes --------------------------------------------------- */

typedef struct ds_ext_memory_tape {
  unsigned char *data;
  size_t length; /* elements */
  size_t capacity;
  size_t position; /* next forward read */
} ds_ext_memory_tape_t;

typedef struct ds_ext_memory_tapes {
  ds_ext_memory_tape_t *tapes;
  size_t count;
  size_t elem_size;
  ds_context_t *context;
} ds_ext_memory_tapes_t;

extern const ds_ext_tape_ops_t ds_ext_memory_tape_ops;
ds_status_t ds_ext_memory_tapes_init(ds_ext_memory_tapes_t *tapes, size_t count,
                                     size_t elem_size, ds_context_t *context);
void ds_ext_memory_tapes_destroy(ds_ext_memory_tapes_t *tapes);

/* ---- FILE tapes ----------------------------------------------------- */

typedef struct ds_ext_file_tape {
  FILE *file;
  long length;   /* elements */
  long position; /* next forward read */
  int writing;   /* last operation was a write */
} ds_ext_file_tape_t;

typedef struct ds_ext_file_tapes {
  ds_ext_file_tape_t *tapes;
  size_t count;
  size_t elem_size;
  ds_context_t *context;
} ds_ext_file_tapes_t;

extern const ds_ext_tape_ops_t ds_ext_file_tape_ops;
/* Opens `count` tmpfile()s; DS_ERR_STATE if one cannot be created. */
ds_status_t ds_ext_file_tapes_init(ds_ext_file_tapes_t *tapes, size_t count,
                                   size_t elem_size, ds_context_t *context);
void ds_ext_file_tapes_destroy(ds_ext_file_tapes_t *tapes);

/* ---- merge sorts ---------------------------------------------------- */

typedef enum ds_ext_merge {
  DS_EXT_BALANCED = 0,
  DS_EXT_POLYPHASE = 1,
  DS_EXT_CASCADE = 2,
  DS_EXT_OSCILLATING = 3
} ds_ext_merge_t;

typedef enum ds_ext_runs {
  DS_EXT_RUNS_LOAD_SORT = 0,
  DS_EXT_RUNS_REPLACEMENT_SELECTION = 1
} ds_ext_runs_t;

typedef struct ds_ext_sort_config {
  size_t elem_size;
  ds_compare_func_t compare;
  void *user;   /* passed to compare */
  void *memory; /* run formation area */
  size_t memory_size;
  ds_ext_runs_t runs; /* ignored by oscillating sort */
  ds_ext_merge_t merge;
  size_t tapes; /* balanced >= 3, polyphase/cascade/oscillating >= 3 */
  const ds_ext_tape_ops_t *ops;
  void *tape_state;
  size_t count; /* element count, or DS_EXT_UNKNOWN (oscillating needs it;
                   more input than announced gives DS_ERR_RANGE) */
} ds_ext_sort_config_t;

typedef struct ds_ext_sort_stats {
  size_t initial_runs;
  size_t dummy_runs;
  size_t merges;
  size_t passes;           /* merge passes or phases */
  size_t elements_written; /* to tapes */
} ds_ext_sort_stats_t;

/* The tapes must start empty. stats may be NULL. */
ds_status_t ds_ext_sort(const ds_ext_sort_config_t *config,
                        ds_ext_source_func_t source, void *source_user,
                        ds_ext_sink_func_t sink, void *sink_user,
                        ds_ext_sort_stats_t *stats, ds_context_t *context);

/* ---- external radix sort -------------------------------------------- */

/* bits in [1, 8]; needs 2 << bits tapes. Elements up to 256 bytes
 * (DS_ERR_UNSUPPORTED otherwise). Stable. */
ds_status_t ds_ext_radix_sort(size_t elem_size, ds_key_func_t key, void *user,
                              unsigned bits, const ds_ext_tape_ops_t *ops,
                              void *tape_state, ds_ext_source_func_t source,
                              void *source_user, ds_ext_sink_func_t sink,
                              void *sink_user);

/* ---- external quicksort --------------------------------------------- */

typedef struct ds_ext_array_ops {
  ds_status_t (*read)(size_t index, void *out_elem, void *state);
  ds_status_t (*write)(size_t index, const void *elem, void *state);
} ds_ext_array_ops_t;

/* Sorts elements [0, count) of the external array in place. memory holds
 * the middle buffer (at least 3 elements). Pending ranges live on a stack
 * from the context. Not stable. */
ds_status_t ds_ext_quicksort(const ds_ext_array_ops_t *ops, void *state,
                             size_t count, size_t elem_size,
                             ds_compare_func_t compare, void *user,
                             void *memory, size_t memory_size,
                             ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_EXTERNAL_SORT_H */
