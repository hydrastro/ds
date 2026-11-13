#ifndef DS_SUFFIX_ARRAY_H
#define DS_SUFFIX_ARRAY_H

/*
 * Suffix arrays by induced sorting (SA-IS: Nong, Zhang and Chan, 2009), in
 * O(n) time for byte strings and for integer strings over [0, k).
 *
 * Suffixes are classified as S or L (smaller or larger than the next one);
 * the leftmost S suffixes of each run (LMS) are placed, the L suffixes are
 * induced from them by a left-to-right scan and the S suffixes by a
 * right-to-left scan. That sorts the LMS substrings; they are named, and
 * if two names coincide the string of names (at most half as long) is
 * sorted recursively, so the recursion is O(log n) deep. A final induced
 * sort from the sorted LMS suffixes gives the suffix array.
 *
 * The order is the usual one in which a proper prefix comes first (as if
 * the string ended with a marker smaller than every symbol), so
 * ds_sa_build() fits wherever lib/fm_index takes a suffix array builder.
 * Also here: the inverse suffix array, the LCP array (Kasai et al., O(n))
 * and the Burrows-Wheeler transform of the string plus its end marker.
 *
 * Working memory, from the context: one byte per symbol for the types, a
 * bucket array of k + 1 words per level and the reduced problem (at most
 * half the size, recursively), on top of the caller's output array.
 */

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* out_sa: n entries, the suffix starting positions in order */
ds_status_t ds_sa_build(const unsigned char *s, size_t n, size_t *out_sa,
                        ds_context_t *context);
/* integer symbols s[i] < k (DS_ERR_INVALID otherwise) */
ds_status_t ds_sa_build_int(const size_t *s, size_t n, size_t k, size_t *out_sa,
                            ds_context_t *context);
/* out_isa[sa[i]] = i */
void ds_sa_inverse(const size_t *sa, size_t n, size_t *out_isa);
/* out_lcp[i]: the longest common prefix of the suffixes at ranks i - 1
 * and i (out_lcp[0] = 0) */
ds_status_t ds_sa_lcp(const unsigned char *s, size_t n, const size_t *sa,
                      size_t *out_lcp, ds_context_t *context);
/* the BWT of s plus a marker: n + 1 bytes; out_bwt[*out_primary] stands
 * for the marker (it holds 0) */
void ds_sa_bwt(const unsigned char *s, size_t n, const size_t *sa,
               unsigned char *out_bwt, size_t *out_primary);

#ifdef __cplusplus
}
#endif

#endif /* DS_SUFFIX_ARRAY_H */
