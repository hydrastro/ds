#ifndef DS_INVERSION_LIST_H
#define DS_INVERSION_LIST_H

/*
 * Inversion list: a set of unsigned longs stored as the sorted boundaries
 * of its maximal ranges. Boundaries at even indexes start a range, those at
 * odd indexes are the first value past it, so { 10..14, 25, 37..42 } is
 * 10 15 25 26 37 43. An odd number of boundaries means the last range runs
 * to ULONG_MAX inclusive, which keeps every set (including the full
 * universe) representable and makes complement O(1) extra work.
 *
 * Membership is a binary search (a value is in the set iff an odd number
 * of boundaries are <= it). Range insertion and removal splice the
 * boundary array in O(log r + r) for r ranges. Union, intersection,
 * difference, symmetric difference and complement are linear merges; any
 * of the 16 boolean combinations is available through
 * ds_inversion_list_combine() and a truth table.
 *
 * The same representation stores a bit sequence as the positions where it
 * switches between 0 and 1 (run-length style); see
 * ds_inversion_list_from_bits() and ds_inversion_list_to_bits().
 *
 * Storage is growable (from the ds_context_t) or a fixed caller buffer
 * (ds_inversion_list_init_buffer, never allocates; operations that would
 * need more boundaries return DS_ERR_OVERFLOW and leave the set as it
 * was). The struct is public so a list can live on the stack.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_inversion_list {
  unsigned long *bounds;
  size_t count;
  size_t capacity;
  bool fixed;
  ds_context_t *context;
} ds_inversion_list_t;

/* Truth tables for ds_inversion_list_combine(): bit (in_a * 2 + in_b) of
 * the table is the result for a value with those memberships. */
#define DS_INVLIST_UNION 0xEu
#define DS_INVLIST_INTERSECTION 0x8u
#define DS_INVLIST_DIFFERENCE 0x4u /* a and not b */
#define DS_INVLIST_SYMMETRIC_DIFFERENCE 0x6u
#define DS_INVLIST_COMPLEMENT_A 0x3u

ds_status_t ds_inversion_list_init(ds_inversion_list_t *list,
                                   ds_context_t *context);
/* `capacity` boundaries in `buffer`; a set of r ranges needs 2r. */
ds_status_t ds_inversion_list_init_buffer(ds_inversion_list_t *list,
                                          unsigned long *buffer,
                                          size_t capacity);
void ds_inversion_list_destroy(ds_inversion_list_t *list);
void ds_inversion_list_clear(ds_inversion_list_t *list);
/* Replace the contents with a copy of src. */
ds_status_t ds_inversion_list_assign(ds_inversion_list_t *list,
                                     const ds_inversion_list_t *src);
/* Ensure room for `count` boundaries. */
ds_status_t ds_inversion_list_reserve(ds_inversion_list_t *list, size_t count);

/* Inclusive ranges [low, high]; DS_ERR_INVALID if low > high. */
ds_status_t ds_inversion_list_add_range(ds_inversion_list_t *list,
                                        unsigned long low, unsigned long high);
ds_status_t ds_inversion_list_remove_range(ds_inversion_list_t *list,
                                           unsigned long low,
                                           unsigned long high);
/* Toggle membership of every value in [low, high]. */
ds_status_t ds_inversion_list_flip_range(ds_inversion_list_t *list,
                                         unsigned long low, unsigned long high);
ds_status_t ds_inversion_list_add(ds_inversion_list_t *list,
                                  unsigned long value);
ds_status_t ds_inversion_list_remove(ds_inversion_list_t *list,
                                     unsigned long value);

bool ds_inversion_list_contains(const ds_inversion_list_t *list,
                                unsigned long value);
bool ds_inversion_list_is_empty(const ds_inversion_list_t *list);
size_t ds_inversion_list_range_count(const ds_inversion_list_t *list);
/* Range `index` (0-based, increasing) as inclusive bounds. */
ds_status_t ds_inversion_list_get_range(const ds_inversion_list_t *list,
                                        size_t index, unsigned long *out_low,
                                        unsigned long *out_high);
/* Index of the range containing value: DS_NOT_FOUND if absent. */
ds_status_t ds_inversion_list_find(const ds_inversion_list_t *list,
                                   unsigned long value, size_t *out_index);
/* Smallest member >= value, or smallest non-member >= value. */
ds_status_t ds_inversion_list_next_member(const ds_inversion_list_t *list,
                                          unsigned long value,
                                          unsigned long *out_value);
ds_status_t ds_inversion_list_next_gap(const ds_inversion_list_t *list,
                                       unsigned long value,
                                       unsigned long *out_value);
/* Number of members: DS_ERR_OVERFLOW if it exceeds ULONG_MAX (only the
 * full universe does). */
ds_status_t ds_inversion_list_cardinality(const ds_inversion_list_t *list,
                                          unsigned long *out_count);
/* Members < value (rank) and the member with `rank` smaller members. */
unsigned long ds_inversion_list_rank(const ds_inversion_list_t *list,
                                     unsigned long value);
ds_status_t ds_inversion_list_select(const ds_inversion_list_t *list,
                                     unsigned long rank,
                                     unsigned long *out_value);

bool ds_inversion_list_equal(const ds_inversion_list_t *a,
                             const ds_inversion_list_t *b);
bool ds_inversion_list_is_subset(const ds_inversion_list_t *a,
                                 const ds_inversion_list_t *b);
bool ds_inversion_list_intersects(const ds_inversion_list_t *a,
                                  const ds_inversion_list_t *b);

/* out = table(a, b), linear time. out may alias a or b (a temporary is
 * used then; with a fixed out buffer the result must still fit). */
ds_status_t ds_inversion_list_combine(ds_inversion_list_t *out,
                                      const ds_inversion_list_t *a,
                                      const ds_inversion_list_t *b,
                                      unsigned table);
ds_status_t ds_inversion_list_union(ds_inversion_list_t *out,
                                    const ds_inversion_list_t *a,
                                    const ds_inversion_list_t *b);
ds_status_t ds_inversion_list_intersection(ds_inversion_list_t *out,
                                           const ds_inversion_list_t *a,
                                           const ds_inversion_list_t *b);
ds_status_t ds_inversion_list_difference(ds_inversion_list_t *out,
                                         const ds_inversion_list_t *a,
                                         const ds_inversion_list_t *b);
ds_status_t
ds_inversion_list_symmetric_difference(ds_inversion_list_t *out,
                                       const ds_inversion_list_t *a,
                                       const ds_inversion_list_t *b);
/* In place: every value not in the set. */
ds_status_t ds_inversion_list_complement(ds_inversion_list_t *list);

/* Bits [0, bit_count) of `bits` (bit i = bits[i / 8] >> (i % 8) & 1) as
 * the set of positions holding 1; and back (bits past the set are 0). */
ds_status_t ds_inversion_list_from_bits(ds_inversion_list_t *list,
                                        const unsigned char *bits,
                                        unsigned long bit_count);
void ds_inversion_list_to_bits(const ds_inversion_list_t *list,
                               unsigned char *bits, unsigned long bit_count);

#ifdef __cplusplus
}
#endif

#endif /* DS_INVERSION_LIST_H */
