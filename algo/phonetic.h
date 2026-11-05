#ifndef DS_ALGO_PHONETIC_H
#define DS_ALGO_PHONETIC_H

/*
 * Phonetic coding: keys that sound-alike names (mostly English) share.
 *
 *   ds_soundex          American Soundex: first letter and three digits
 *   ds_nysiis           NYSIIS (New York State Identification and
 *                       Intelligence System), Taft's original rules
 *   ds_metaphone        Lawrence Philips' Metaphone (1990) with the usual
 *                       corrections
 *   ds_double_metaphone Philips' Double Metaphone (2000): a primary and an
 *                       alternate key for names of many origins
 *   ds_caverphone       Caverphone 2.0 (David Hood, 2004): ten characters
 *   ds_phonetic_code    one entry point for all of them
 *
 * Input is bytes; only ASCII letters count (case-insensitive), other bytes
 * are ignored (Double Metaphone keeps spaces, which matter for "VAN ",
 * "SAN "). max_length 0 means no limit. Output keys are uppercase ASCII
 * (digits for Soundex and Caverphone, '0' for "th" in the Metaphones);
 * the length goes to *out_length, also with DS_ERR_OVERFLOW when the key
 * does not fit in capacity. No terminating NUL is written.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_phonetic_algorithm {
  DS_PHONETIC_SOUNDEX = 0,
  DS_PHONETIC_NYSIIS = 1,
  DS_PHONETIC_METAPHONE = 2,
  DS_PHONETIC_DOUBLE_METAPHONE = 3, /* primary key only */
  DS_PHONETIC_CAVERPHONE = 4
} ds_phonetic_algorithm_t;

/* "" (length 0) when the name has no letters, else 4 characters. */
ds_status_t ds_soundex(const unsigned char *name, size_t n, unsigned char *out,
                       size_t capacity, size_t *out_length);
ds_status_t ds_nysiis(const unsigned char *name, size_t n, size_t max_length,
                      unsigned char *out, size_t capacity, size_t *out_length,
                      ds_context_t *context);
ds_status_t ds_metaphone(const unsigned char *name, size_t n, size_t max_length,
                         unsigned char *out, size_t capacity,
                         size_t *out_length, ds_context_t *context);
/* The secondary key is empty (length 0) when it equals the primary. Both
 * buffers have `capacity` bytes; DS_ERR_OVERFLOW if either key is longer. */
ds_status_t ds_double_metaphone(const unsigned char *name, size_t n,
                                size_t max_length, unsigned char *primary,
                                unsigned char *secondary, size_t capacity,
                                size_t *out_primary_length,
                                size_t *out_secondary_length,
                                ds_context_t *context);
/* Always 10 characters (padded with '1'). */
ds_status_t ds_caverphone(const unsigned char *name, size_t n,
                          unsigned char *out, size_t capacity,
                          size_t *out_length, ds_context_t *context);
ds_status_t ds_phonetic_code(ds_phonetic_algorithm_t algorithm,
                             const unsigned char *name, size_t n,
                             unsigned char *out, size_t capacity,
                             size_t *out_length, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_PHONETIC_H */
