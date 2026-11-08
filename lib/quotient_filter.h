#ifndef DS_QUOTIENT_FILTER_H
#define DS_QUOTIENT_FILTER_H

/*
 * Quotient filter (Bender et al., "Don't thrash: how to cache your hash
 * on flash", 2012): an approximate-membership table of 2^q slots over
 * (q + r)-bit fingerprints. The top q bits of a fingerprint choose its
 * canonical slot, the low r bits (the remainder) are stored, packed, in
 * the slot it lands in; three metadata bits per slot (occupied,
 * continuation, shifted) let runs of equal quotients shift right past
 * their canonical slots, linear-probing style, while staying decodable.
 *
 * Unlike Bloom filters it supports deletion and its contents can be
 * enumerated (as fingerprints). False positives occur with probability
 * about load * 2^-r. Lookups follow the standard run search; insertions
 * and deletions re-encode the affected cluster stretch (expected O(1)
 * slots below ~75% load). Keys enter as hash values; only the low q + r
 * bits are used. Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_quotient_filter {
  unsigned q;
  unsigned r;
  size_t slots;
  size_t count;
  unsigned long *rem;     /* slots * r bits */
  unsigned char *meta;    /* occupied 1, continuation 2, shifted 4 */
  unsigned long *scratch; /* decoded fingerprints of one stretch */
  size_t scratch_cap;
  ds_context_t *context;
} ds_quotient_filter_t;

/* 1 <= q, 1 <= r, q + r <= bits of unsigned long, q < bits of size_t */
ds_status_t ds_quotient_filter_init(ds_quotient_filter_t *f, unsigned q,
                                    unsigned r, ds_context_t *context);
void ds_quotient_filter_destroy(ds_quotient_filter_t *f);
/* DS_EXISTS when the fingerprint is already present; DS_ERR_OVERFLOW when
 * every slot is full */
ds_status_t ds_quotient_filter_insert(ds_quotient_filter_t *f,
                                      unsigned long hash);
bool ds_quotient_filter_contains(const ds_quotient_filter_t *f,
                                 unsigned long hash);
/* only remove what was inserted (fingerprints are shared otherwise) */
ds_status_t ds_quotient_filter_remove(ds_quotient_filter_t *f,
                                      unsigned long hash);
size_t ds_quotient_filter_count(const ds_quotient_filter_t *f);
size_t ds_quotient_filter_bytes(const ds_quotient_filter_t *f);
/* the stored (q + r)-bit fingerprints in increasing order */
typedef bool (*ds_quotient_filter_visit_t)(unsigned long fingerprint,
                                           void *user);
ds_status_t ds_quotient_filter_visit(const ds_quotient_filter_t *f,
                                     ds_quotient_filter_visit_t visit,
                                     void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_QUOTIENT_FILTER_H */
