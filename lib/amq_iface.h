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
 * Semantics of the operations, per backend:
 *
 *   hashes      keys enter as unsigned long hash values. The backends remix
 *               them before use (quotient, xor) or take independent bits
 *               (Bloom double hashing, cuckoo index and fingerprint), but
 *               distinct keys must have distinct hashes: equal hashes are
 *               one key, and correlated hashes raise the false-positive
 *               rate above 2^-fp_bits.
 *   duplicates  Bloom: inserting a key again changes nothing. Cuckoo and
 *               quotient: each insert stores another copy of the
 *               fingerprint (a multiset), and each remove takes one copy
 *               away, so inserting k times needs k removes. Xor: build
 *               keeps one copy of each distinct hash.
 *   deletion    only keys that were inserted may be removed. Removing a
 *               key that was never inserted but shares a fingerprint with
 *               one that was deletes that one's copy, a false negative no
 *               filter can detect. remove reports DS_NOT_FOUND when no
 *               matching fingerprint exists.
 *   capacity    Bloom: an estimate; inserts never fail and the
 *               false-positive rate rises past it. Cuckoo: about 94%
 *               load; once an eviction chain fails the last fingerprint
 *               is kept aside (no key is lost) and further inserts fail
 *               with DS_ERR_OVERFLOW. Quotient: sized for 75% load, and
 *               inserts fail with DS_ERR_OVERFLOW when the table is full.
 *               Xor: exactly the built set.
 *   false negatives  none, apart from removing keys that were never
 *               inserted.
 *   fp_bits     Bloom: values above 64 count as 64. Quotient: at most
 *               the bits of unsigned long left after the quotient. Xor:
 *               values above 32 count as 32.
 *
 * Images: ds_amq_save writes a filter as bytes and ds_amq_load restores
 * it, for filters kept in files, pages or SSTables. An image is
 *
 *   0    magic "DSAQ"
 *   4    format version (1)
 *   5    backend id
 *   6    bits in unsigned long where the image was written
 *   7    zero
 *   8    image length (8 bytes)
 *   16   the backend's payload
 *   end  CRC-32C of everything before it (4 bytes)
 *
 * with integers little-endian and bit arrays as little-endian bit
 * streams, zero-padded to whole bytes. Payloads:
 *
 *   Bloom (1)     m (8), k (4), the m bits
 *   cuckoo (2)    buckets (8), count (8), victim bucket (8), generator
 *                 state (4), victim (2), slots per bucket (1), then the
 *                 buckets * slots 16-bit fingerprints
 *   quotient (3)  q (1), r (1), count (8), the 2^q r-bit remainders, then
 *                 the 2^q 3-bit slot flags (occupied 1, continuation 2,
 *                 shifted 4)
 *   xor (4)       fp_bits (1), built (1); once built also segment (8),
 *                 count (8), seed (8) and the 3 * segment fp_bits-bit
 *                 slots
 *
 * Hash positions depend on the width of unsigned long, so an image loads
 * only where unsigned long has the width it was written with; another
 * width, an unknown format version or an unknown backend is
 * DS_ERR_UNSUPPORTED. Loading checks everything else: the checksum, every
 * size against the image length (a load never allocates much more than
 * the image's own size), zero padding, counts against the stored entries,
 * the cuckoo victim's bucket, and the whole slot structure of a quotient
 * filter (ds_quotient_filter_check). A malformed image is DS_ERR_INVALID;
 * a loaded filter is one its backend could have built, so every
 * operation on it behaves as on a built one. What an image cannot reveal
 * is which keys built it: a well-formed image may answer for any set, and
 * the checksum detects accidents, not tampering.
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
  /* images (optional: save and load NULL when the backend has none). id
   * names the backend in images: 1 to 4 are the built-in backends, 128 to
   * 255 are free for others. */
  unsigned id;
  /* the payload into out[0 .. cap) and its length into *out_len; out
   * NULL computes the length only; DS_ERR_RANGE when cap is short */
  ds_status_t (*save)(const void *f, unsigned char *out, size_t cap,
                      size_t *out_len);
  /* initialize f, as init does, from a payload of len bytes, checking all
   * of it: DS_ERR_INVALID for a malformed payload, and on any failure f
   * needs no destroy */
  ds_status_t (*load)(void *f, const unsigned char *in, size_t len,
                      ds_context_t *context);
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

/* ---- images ---- */

#define DS_AMQ_IMAGE_OVERHEAD 20 /* header and checksum */

typedef struct ds_amq_image {
  unsigned version;
  unsigned id;        /* backend */
  unsigned word_bits; /* bits in unsigned long where it was written */
  size_t length;      /* the whole image */
  size_t payload_offset;
  size_t payload_length;
} ds_amq_image_t;

/* the built-in backend with this id, or NULL */
const ds_amq_ops_t *ds_amq_builtin(unsigned id);
/* the image of f into out[0 .. cap) and its length into *out_len; out
 * NULL computes the length only. DS_ERR_RANGE when cap is short (with
 * *out_len set), DS_ERR_UNSUPPORTED for a backend without images */
ds_status_t ds_amq_save(const ds_amq_t *f, void *out, size_t cap,
                        size_t *out_len);
/* the header of an image at the start of avail bytes (bytes after the
 * image are allowed), with its length and checksum checked:
 * DS_ERR_INVALID, or DS_ERR_UNSUPPORTED for an unknown format version */
ds_status_t ds_amq_image_info(const void *in, size_t avail,
                              ds_amq_image_t *info);
/* restore a filter from an image of exactly len bytes, with the built-in
 * backend its id names; on failure f needs no destroy */
ds_status_t ds_amq_load(ds_amq_t *f, const void *in, size_t len,
                        ds_context_t *context);
/* the same with the given backend, whose id must be the image's
 * (DS_ERR_INVALID otherwise) */
ds_status_t ds_amq_load_with(ds_amq_t *f, const ds_amq_ops_t *ops,
                             const void *in, size_t len, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_AMQ_IFACE_H */
