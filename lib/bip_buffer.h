#ifndef DS_BIP_BUFFER_H
#define DS_BIP_BUFFER_H

/*
 * Bip buffer (Simon Cooke): a circular byte buffer that always hands out
 * contiguous regions, for writing and for reading. That is what DMA engines,
 * read()/write() calls and packet parsers need: a plain ring buffer would
 * split a record at the wrap-around point.
 *
 * Writing is two-phase: reserve() returns a contiguous region of up to the
 * requested size, the producer fills it, and commit() publishes the bytes
 * actually written. Reading is the same: read_block() returns the oldest
 * contiguous committed bytes and consume() releases them.
 *
 * Internally there are two regions, A and B; B grows from the start of the
 * buffer once A reaches the end, and becomes A when A is drained.
 *
 * Storage: a caller buffer (ds_bip_buffer_init_buffer, no allocation) or
 * memory from a ds_context_t (ds_bip_buffer_init).
 *
 * Complexity: every operation O(1).
 * Concurrency: one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_bip_buffer {
  unsigned char *data;
  size_t capacity;
  size_t a_start;
  size_t a_end;
  size_t b_end;
  bool b_in_use;
  size_t reserve_start;
  size_t reserve_end;
  ds_context_t *context;
  bool fixed;
} ds_bip_buffer_t;

ds_status_t ds_bip_buffer_init(ds_bip_buffer_t *bip, size_t capacity,
                               ds_context_t *context);
ds_status_t ds_bip_buffer_init_buffer(ds_bip_buffer_t *bip, void *buffer,
                                      size_t capacity);
void ds_bip_buffer_destroy(ds_bip_buffer_t *bip);

void *ds_bip_buffer_reserve(ds_bip_buffer_t *bip, size_t wanted,
                            size_t *out_granted);
void ds_bip_buffer_commit(ds_bip_buffer_t *bip, size_t written);
const void *ds_bip_buffer_read_block(const ds_bip_buffer_t *bip,
                                     size_t *out_size);
void ds_bip_buffer_consume(ds_bip_buffer_t *bip, size_t count);

ds_status_t ds_bip_buffer_write(ds_bip_buffer_t *bip, const void *data,
                                size_t size);
size_t ds_bip_buffer_committed(const ds_bip_buffer_t *bip);
size_t ds_bip_buffer_capacity(const ds_bip_buffer_t *bip);
void ds_bip_buffer_clear(ds_bip_buffer_t *bip);

#ifdef __cplusplus
}
#endif

#endif /* DS_BIP_BUFFER_H */
