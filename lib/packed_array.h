#ifndef DS_PACKED_ARRAY_H
#define DS_PACKED_ARRAY_H

/*
 * Packed integer array: a sequence of unsigned values that each occupy
 * exactly `bits` bits (1 .. bits in an unsigned long), packed back to back
 * in unsigned long words. 3-bit states, 12-bit ADC samples or 20-bit indices
 * take 3, 12 or 20 bits each instead of a whole machine word; values may
 * straddle two words.
 *
 * Two storage modes: dynamic (ds_packed_array_init, grows through a
 * ds_context_t) or a caller array of unsigned long words
 * (ds_packed_array_init_buffer, no allocation; use
 * ds_packed_array_words_for() to size it).
 *
 * Complexity: get, set, push, pop O(1) (push amortized when growing);
 * resize O(new elements). Concurrency: one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_packed_array {
  unsigned long *words;
  size_t word_count;
  size_t size;
  size_t capacity;
  unsigned int bits;
  unsigned long mask;
  ds_context_t *context;
  bool fixed;
} ds_packed_array_t;

size_t FUNC(ds_packed_array_words_for)(size_t count, unsigned int bits);
ds_status_t FUNC(ds_packed_array_init)(ds_packed_array_t *array,
                                       unsigned int bits,
                                       ds_context_t *context);
ds_status_t FUNC(ds_packed_array_init_buffer)(ds_packed_array_t *array,
                                              unsigned int bits,
                                              unsigned long *words,
                                              size_t word_count);
void FUNC(ds_packed_array_destroy)(ds_packed_array_t *array);

ds_status_t FUNC(ds_packed_array_get)(const ds_packed_array_t *array,
                                      size_t index, unsigned long *out_value);
unsigned long FUNC(ds_packed_array_at)(const ds_packed_array_t *array,
                                       size_t index);
ds_status_t FUNC(ds_packed_array_set)(ds_packed_array_t *array, size_t index,
                                      unsigned long value);
ds_status_t FUNC(ds_packed_array_push)(ds_packed_array_t *array,
                                       unsigned long value);
ds_status_t FUNC(ds_packed_array_pop)(ds_packed_array_t *array,
                                      unsigned long *out_value);
ds_status_t FUNC(ds_packed_array_resize)(ds_packed_array_t *array, size_t size);
ds_status_t FUNC(ds_packed_array_reserve)(ds_packed_array_t *array,
                                          size_t capacity);
void FUNC(ds_packed_array_clear)(ds_packed_array_t *array);

size_t FUNC(ds_packed_array_size)(const ds_packed_array_t *array);
size_t FUNC(ds_packed_array_capacity)(const ds_packed_array_t *array);
unsigned int FUNC(ds_packed_array_bits)(const ds_packed_array_t *array);
unsigned long FUNC(ds_packed_array_max_value)(const ds_packed_array_t *array);

#ifdef __cplusplus
}
#endif

#endif /* DS_PACKED_ARRAY_H */
