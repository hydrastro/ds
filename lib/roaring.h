#ifndef DS_ROARING_H
#define DS_ROARING_H

/*
 * Roaring bitmaps (Chambi, Lemire et al.): compressed sets of 32-bit
 * integers. Values are split by their high 16 bits into chunks kept in a
 * sorted directory; each chunk's low halves live in the container that
 * suits its density:
 *
 *   array   a sorted array of up to 4096 16-bit values (2 bytes each)
 *   bitmap  65536 bits (8 KiB) for denser chunks
 *   run     sorted (start, length - 1) pairs, produced by
 *           ds_roaring_optimize where runs are smaller
 *
 * Arrays become bitmaps above 4096 values and bitmaps arrays at or below
 * it; a run container is turned back into an array or bitmap when it is
 * modified. Membership is a binary search plus a container probe;
 * cardinality, rank and select use per-container counts; and, or, xor and
 * andnot work chunk by chunk (array/array by merging, otherwise on
 * bitmaps). Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_roaring_kind {
  DS_ROARING_ARRAY,
  DS_ROARING_BITMAP,
  DS_ROARING_RUN
} ds_roaring_kind_t;

typedef struct ds_roaring_container {
  ds_roaring_kind_t kind;
  unsigned short *values; /* array values or run (start, len-1) pairs */
  unsigned long *bits;    /* bitmap words */
  size_t count;           /* values, or runs for a run container */
  size_t capacity;
  size_t cardinality;
} ds_roaring_container_t;

typedef struct ds_roaring {
  unsigned short *keys; /* sorted high halves */
  ds_roaring_container_t *containers;
  size_t size;
  size_t capacity;
  ds_context_t *context;
} ds_roaring_t;

void ds_roaring_init(ds_roaring_t *r, ds_context_t *context);
void ds_roaring_destroy(ds_roaring_t *r);
/* values must be < 2^32 (DS_ERR_RANGE). DS_EXISTS / DS_NOT_FOUND when
 * nothing changes. */
ds_status_t ds_roaring_add(ds_roaring_t *r, unsigned long x);
ds_status_t ds_roaring_remove(ds_roaring_t *r, unsigned long x);
bool ds_roaring_contains(const ds_roaring_t *r, unsigned long x);
unsigned long ds_roaring_cardinality(const ds_roaring_t *r);
/* number of elements <= x */
unsigned long ds_roaring_rank(const ds_roaring_t *r, unsigned long x);
/* k-th smallest (0-based); DS_NOT_FOUND past the end */
ds_status_t ds_roaring_select(const ds_roaring_t *r, unsigned long k,
                              unsigned long *out);
/* smallest element >= x */
ds_status_t ds_roaring_next(const ds_roaring_t *r, unsigned long x,
                            unsigned long *out);
typedef enum ds_roaring_op {
  DS_ROARING_AND,
  DS_ROARING_OR,
  DS_ROARING_XOR,
  DS_ROARING_ANDNOT
} ds_roaring_op_t;
/* out (initialised by the call; may not alias a or b) = a op b */
ds_status_t ds_roaring_combine(const ds_roaring_t *a, const ds_roaring_t *b,
                               ds_roaring_op_t op, ds_roaring_t *out,
                               ds_context_t *context);
bool ds_roaring_equals(const ds_roaring_t *a, const ds_roaring_t *b);
/* convert containers to runs where that is smaller */
ds_status_t ds_roaring_optimize(ds_roaring_t *r);
/* bytes used by container payloads and the directory */
size_t ds_roaring_bytes(const ds_roaring_t *r);
/* containers of each kind (for inspection) */
void ds_roaring_stats(const ds_roaring_t *r, size_t *arrays, size_t *bitmaps,
                      size_t *runs);

#ifdef __cplusplus
}
#endif

#endif /* DS_ROARING_H */
