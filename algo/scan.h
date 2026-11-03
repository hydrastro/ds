#ifndef DS_ALGO_SCAN_H
#define DS_ALGO_SCAN_H

/*
 * Scans (prefix computations) over a pluggable monoid.
 *
 * A ds_monoid_t describes elements of elem_size bytes with an associative
 * combine and its identity; nothing assumes commutativity, so string
 * concatenation, matrix products, function composition or "last
 * non-null" all work. combine(out, left, right) must allow out to alias
 * left or right.
 *
 *   ds_scan_inclusive        out[i] = in[0] . in[1] . ... . in[i]
 *   ds_scan_exclusive        out[i] = in[0] . ... . in[i - 1] (identity
 *                            first), the total optionally returned
 *   ds_scan_suffix           out[i] = in[i] . ... . in[n - 1]
 *   ds_scan_segmented        scan restarting where flags[i] != 0
 *   ds_multiprefix           exclusive scan within each label class:
 *                            out[i] combines the earlier elements whose
 *                            label equals labels[i]; per-label totals
 *   ds_scan_hillis_steele    parallel prefix computation by recursive
 *                            doubling (log n rounds, O(n log n) work),
 *                            simulated round by round
 *   ds_scan_blelloch         work-efficient parallel prefix computation
 *                            (up-sweep / down-sweep balanced tree, O(n)
 *                            work, in place), any n
 *   ds_reduce                fold of the whole array
 * plus prefix sums of long, unsigned long, size_t and double (the
 * double version compensated, Neumaier).
 *
 * Every output may alias its input. Functions need up to three
 * temporary elements: from monoid.scratch when set (at least
 * DS_SCAN_SCRATCH_ELEMS * elem_size bytes), else the stack for small
 * elements, else the monoid's context.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SCAN_SCRATCH_ELEMS 3

typedef void (*ds_monoid_combine_func_t)(void *out, const void *left,
                                         const void *right, void *user);

typedef struct ds_monoid {
  size_t elem_size;
  const void *identity;
  ds_monoid_combine_func_t combine;
  void *user;
  void *scratch;         /* optional temporaries */
  ds_context_t *context; /* for temporaries of large elements */
} ds_monoid_t;

ds_status_t ds_scan_inclusive(const ds_monoid_t *m, const void *in, void *out,
                              size_t count);
/* out_total (may be NULL) receives the combination of all elements. */
ds_status_t ds_scan_exclusive(const ds_monoid_t *m, const void *in, void *out,
                              size_t count, void *out_total);
ds_status_t ds_scan_suffix(const ds_monoid_t *m, const void *in, void *out,
                           size_t count);
/* flags[i] != 0 starts a new segment at i (index 0 always does). */
ds_status_t ds_scan_segmented(const ds_monoid_t *m, const void *in, void *out,
                              size_t count, const unsigned char *flags,
                              bool inclusive);
/* labels[i] < label_count; totals (label_count elements) receives each
 * label's total. DS_ERR_RANGE (nothing written) on a bad label. */
ds_status_t ds_multiprefix(const ds_monoid_t *m, const void *in,
                           const size_t *labels, size_t label_count, void *out,
                           size_t count, void *totals);
/* Inclusive; scratch holds count elements. */
ds_status_t ds_scan_hillis_steele(const ds_monoid_t *m, const void *in,
                                  void *out, size_t count, void *scratch);
/* In place, exclusive. */
ds_status_t ds_scan_blelloch(const ds_monoid_t *m, void *data, size_t count,
                             void *out_total);
ds_status_t ds_reduce(const ds_monoid_t *m, const void *in, size_t count,
                      void *out);

/* Inclusive prefix sums (out may equal in). Integer versions wrap like
 * the underlying unsigned arithmetic for unsigned types. */
void ds_prefix_sums_long(const long *in, long *out, size_t count);
void ds_prefix_sums_ulong(const unsigned long *in, unsigned long *out,
                          size_t count);
void ds_prefix_sums_size(const size_t *in, size_t *out, size_t count);
void ds_prefix_sums_double(const double *in, double *out, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SCAN_H */
