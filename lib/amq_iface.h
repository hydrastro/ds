#ifndef DS_AMQ_IFACE_H
#define DS_AMQ_IFACE_H

/*
 * The approximate-membership (AMQ) capability: a set of hash values that
 * answers "possibly present" or "certainly absent", for LSM tables, join
 * filters, caches and deduplication, with the filter chosen by the caller.
 *
 * init takes the expected number of keys and fp_bits, the target
 * false-positive rate 2^-fp_bits (backends with fixed fingerprints ignore
 * it). Traits:
 *
 *   DS_AMQ_DYNAMIC  insert works after init (otherwise build only)
 *   DS_AMQ_DELETE   remove of an inserted key is supported
 *   DS_AMQ_EXACT_CAPACITY  inserts fail (DS_ERR_OVERFLOW) once full
 *
 * Backends:
 *   ds_amq_bloom()     Bloom filter, k = fp_bits probes by double hashing
 *                      (about 1.44 fp_bits bits per key)
 *   ds_amq_cuckoo()    lib/cuckoo_filter (16-bit fingerprints, deletes)
 *   ds_amq_quotient()  lib/quotient_filter (deletes, enumerable)
 *   ds_amq_xor()       lib/xor_filter, static (build only, smallest)
 *
 * Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_AMQ_DYNAMIC 0x1u
#define DS_AMQ_DELETE 0x2u
#define DS_AMQ_EXACT_CAPACITY 0x4u

typedef struct ds_amq_ops {
  const char *name;
  unsigned traits;
  size_t instance_size;
  ds_status_t (*init)(void *f, size_t capacity, unsigned fp_bits,
                      ds_context_t *context);
  void (*destroy)(void *f);
  ds_status_t (*insert)(void *f, unsigned long hash);
  /* add a batch (the only way to fill a static filter, once) */
  ds_status_t (*build)(void *f, const unsigned long *hashes, size_t n);
  bool (*contains)(const void *f, unsigned long hash);
  ds_status_t (*remove)(void *f, unsigned long hash);
  size_t (*bytes)(const void *f);
} ds_amq_ops_t;

const ds_amq_ops_t *ds_amq_bloom(void);
const ds_amq_ops_t *ds_amq_cuckoo(void);
const ds_amq_ops_t *ds_amq_quotient(void);
const ds_amq_ops_t *ds_amq_xor(void);

typedef struct ds_amq {
  const ds_amq_ops_t *ops;
  void *impl;
  ds_context_t *context;
} ds_amq_t;

/* ops NULL selects the Bloom filter */
ds_status_t ds_amq_init(ds_amq_t *f, const ds_amq_ops_t *ops, size_t capacity,
                        unsigned fp_bits, ds_context_t *context);
void ds_amq_destroy(ds_amq_t *f);
/* DS_ERR_UNSUPPORTED on static filters */
ds_status_t ds_amq_insert(ds_amq_t *f, unsigned long hash);
ds_status_t ds_amq_build(ds_amq_t *f, const unsigned long *hashes, size_t n);
bool ds_amq_contains(const ds_amq_t *f, unsigned long hash);
/* DS_ERR_UNSUPPORTED without DS_AMQ_DELETE */
ds_status_t ds_amq_remove(ds_amq_t *f, unsigned long hash);
size_t ds_amq_bytes(const ds_amq_t *f);

#ifdef __cplusplus
}
#endif

#endif /* DS_AMQ_IFACE_H */
