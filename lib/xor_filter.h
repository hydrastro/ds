#ifndef DS_XOR_FILTER_H
#define DS_XOR_FILTER_H

/*
 * Xor filter (Graf & Lemire, "Xor filters: faster and smaller than Bloom
 * and cuckoo filters", 2020): a static approximate-membership structure.
 * Every key maps to three slots, one in each third of a table of about
 * 1.23 n fingerprints, and the table is solved by peeling (repeatedly
 * removing a slot hit by a single remaining key) so that the xor of a
 * key's three slots equals its fingerprint. Queries read three slots.
 *
 * Space is about 1.23 * fp_bits bits per key for a false-positive rate of
 * 2^-fp_bits (fp_bits 1..32, packed). Duplicate hashes are removed before
 * building; a build that fails to peel is retried with a new seed. Keys
 * enter as hash values. Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_xor_filter {
  unsigned long *table; /* capacity * fp_bits bits */
  size_t capacity;
  size_t segment;
  size_t count;
  unsigned fp_bits;
  unsigned long seed;
  ds_context_t *context;
} ds_xor_filter_t;

ds_status_t ds_xor_filter_build(ds_xor_filter_t *f, const unsigned long *hashes,
                                size_t n, unsigned fp_bits,
                                ds_context_t *context);
void ds_xor_filter_destroy(ds_xor_filter_t *f);
bool ds_xor_filter_contains(const ds_xor_filter_t *f, unsigned long hash);
size_t ds_xor_filter_count(const ds_xor_filter_t *f);
size_t ds_xor_filter_bytes(const ds_xor_filter_t *f);

#ifdef __cplusplus
}
#endif

#endif /* DS_XOR_FILTER_H */
