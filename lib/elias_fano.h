#ifndef DS_ELIAS_FANO_H
#define DS_ELIAS_FANO_H

/*
 * Elias-Fano encoding of a nondecreasing sequence of n integers below a
 * universe u, in about n (2 + log2(u / n)) bits: each value's low
 * l = floor(log2(u / n)) bits are packed verbatim, its high bits are
 * written in unary into a bitvector of n + (u >> l) + 1 bits.
 *
 * Costs are stated separately for the three layers:
 *
 *   select    select1 / select0 on the high part are O(1) worst case: a
 *             three-level directory per bit value (blocks of DS_EF_SAMPLE
 *             occurrences; a block spanning DS_EF_LONG_BLOCK bits or more
 *             stores every position; otherwise 16-bit offsets of every
 *             DS_EF_SUB-th occurrence, and a sub-block spanning
 *             DS_EF_LONG_SUB bits or more stores all its offsets). At
 *             most DS_EF_LONG_SUB / word bits + 1 words are scanned, long
 *             gaps of the other bit value included.
 *   access    get(i) is one select1 plus a packed read: O(1).
 *   search    next_geq(x) (the first element >= x, the core of posting-list
 *             intersection) takes two select0 to find x's high-part bucket
 *             and a binary search of the bucket's low parts: O(1 + log b)
 *             for a bucket of b elements (b is small for distinct
 *             values; duplicates and clusters make it larger). prev_leq
 *             is one next_geq plus one access.
 *
 * Space: n * low_bits + high_bits bits encoded (ds_elias_fano_bits), plus
 * the directories (ds_elias_fano_index_bytes): about 51 bytes per
 * DS_EF_SAMPLE occurrences of each bit value, plus explicit positions for
 * sparse regions: at most half a bit (blocks) plus a quarter bit
 * (sub-blocks) per high-part bit for each bit value. An empty sequence uses
 * O(1) space whatever the universe. Static: built once from a sorted array.
 * Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_EF_SAMPLE 512       /* occurrences per directory block */
#define DS_EF_SUB 32           /* occurrences per sub-block */
#define DS_EF_LONG_BLOCK 65536 /* span (bits) of a block stored explicitly */
#define DS_EF_LONG_SUB 2048    /* span (bits) of a sub-block stored */

/* select directory for one bit value of the high part */
typedef struct ds_ef_select {
  size_t count; /* occurrences */
  size_t blocks;
  size_t *start;        /* position of occurrence b * DS_EF_SAMPLE */
  size_t *dir;          /* long block: first slot in lpos; otherwise
                           first slot in spos */
  unsigned short *mask; /* stored sub-blocks of a short block */
  unsigned char *is_long;
  unsigned short *sub;  /* offsets of every DS_EF_SUB-th occurrence */
  size_t *lpos;         /* positions, DS_EF_SAMPLE per long block */
  unsigned short *spos; /* offsets, DS_EF_SUB per stored sub-block */
} ds_ef_select_t;

typedef struct ds_elias_fano {
  size_t n;
  unsigned long universe;
  unsigned low_bits;
  unsigned long *low;  /* n * low_bits bits */
  unsigned long *high; /* high_bits bits */
  size_t high_bits;
  ds_ef_select_t sel1; /* select over the ones */
  ds_ef_select_t sel0; /* select over the zeros */
  size_t zeros;
  ds_context_t *context;
} ds_elias_fano_t;

/* DS_ERR_INVALID unless values are nondecreasing and < universe */
ds_status_t ds_elias_fano_build(ds_elias_fano_t *ef,
                                const unsigned long *values, size_t n,
                                unsigned long universe, ds_context_t *context);
void ds_elias_fano_destroy(ds_elias_fano_t *ef);
size_t ds_elias_fano_size(const ds_elias_fano_t *ef);
unsigned long ds_elias_fano_get(const ds_elias_fano_t *ef, size_t i);
/* first element >= x: its index and value (DS_NOT_FOUND if none) */
ds_status_t ds_elias_fano_next_geq(const ds_elias_fano_t *ef, unsigned long x,
                                   size_t *out_index, unsigned long *out);
/* last element <= x */
ds_status_t ds_elias_fano_prev_leq(const ds_elias_fano_t *ef, unsigned long x,
                                   size_t *out_index, unsigned long *out);
/* encoded size in bits (low + high parts, without the directories) */
size_t ds_elias_fano_bits(const ds_elias_fano_t *ef);
/* bytes of the two select directories */
size_t ds_elias_fano_index_bytes(const ds_elias_fano_t *ef);

#ifdef __cplusplus
}
#endif

#endif /* DS_ELIAS_FANO_H */
