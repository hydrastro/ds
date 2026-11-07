#ifndef DS_ALGO_SPACE_ORDER_H
#define DS_ALGO_SPACE_ORDER_H

/*
 * Space ordering methods: mappings from a discrete k-dimensional space to a
 * linear order (traversing the image in order gives a space-filling curve).
 *
 * Two-dimensional orders, each with encode (cell -> position) and decode
 * (position -> cell):
 *   DS_ORDER_ROW          row-major: y w + x
 *   DS_ORDER_ROW_PRIME    boustrophedon: odd rows run right to left
 *   DS_ORDER_MORTON       Z order: the bits of x and y interleaved
 *   DS_ORDER_HILBERT      Peano-Hilbert order: quadrants visited in a U,
 *                         rotated and reflected at every level so that
 *                         consecutive cells are adjacent
 *   DS_ORDER_GRAY         Gray order: the rank of the interleaved bits in
 *                         the binary reflected Gray code
 *   DS_ORDER_DOUBLE_GRAY  double Gray order: the coordinates are Gray-coded
 *                         before interleaving, then ranked as in Gray order
 *   DS_ORDER_U            U order: Morton digits relabelled so that every
 *                         level visits its quadrants in a U, without the
 *                         Hilbert rotations
 *   DS_ORDER_CANTOR       Cantor-diagonal order of the quadrant x, y >= 0,
 *                         diagonals x + y = 0, 1, 2, ... in turn
 *   DS_ORDER_SPIRAL       square spiral of the whole lattice around the
 *                         origin (signed coordinates)
 * The row orders take the grid width; the bit orders take the bits per
 * coordinate (positions need 2 bits per coordinate bit in an unsigned
 * long); Cantor and spiral orders are unbounded and report overflow.
 *
 * k dimensions: ds_morton_encode_nd and ds_hilbert_encode_nd (Skilling's
 * transposition algorithm) with their inverses; positions are arrays of
 * 32-bit words, most significant first.
 */

#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_space_order {
  DS_ORDER_ROW = 0,
  DS_ORDER_ROW_PRIME,
  DS_ORDER_MORTON,
  DS_ORDER_HILBERT,
  DS_ORDER_GRAY,
  DS_ORDER_DOUBLE_GRAY,
  DS_ORDER_U,
  DS_ORDER_CANTOR,
  DS_ORDER_SPIRAL
} ds_space_order_t;

/* param: the width (row orders), the bits per coordinate (bit orders),
 * ignored otherwise. Coordinates are signed only for the spiral. */
ds_status_t ds_space_order_encode(ds_space_order_t order, unsigned long param,
                                  long x, long y, unsigned long *out);
ds_status_t ds_space_order_decode(ds_space_order_t order, unsigned long param,
                                  unsigned long position, long *out_x,
                                  long *out_y);

/* dims coordinates of bits each (dims * bits <= 32 * words); out gets
 * `words` 32-bit words. */
ds_status_t ds_morton_encode_nd(const unsigned long *coords, size_t dims,
                                unsigned bits, unsigned long *out,
                                size_t words);
ds_status_t ds_morton_decode_nd(const unsigned long *position, size_t words,
                                size_t dims, unsigned bits,
                                unsigned long *out_coords);
ds_status_t ds_hilbert_encode_nd(const unsigned long *coords, size_t dims,
                                 unsigned bits, unsigned long *out,
                                 size_t words);
ds_status_t ds_hilbert_decode_nd(const unsigned long *position, size_t words,
                                 size_t dims, unsigned bits,
                                 unsigned long *out_coords);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SPACE_ORDER_H */
