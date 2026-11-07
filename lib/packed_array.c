#include "packed_array.h"
#include <limits.h>

#define PA_SIZE_MAX (~(size_t)0)
#define PA_WORD_BITS ((unsigned int)(CHAR_BIT * sizeof(unsigned long)))
#define PA_MIN_WORDS 4

static size_t pa_capacity_of(size_t word_count, unsigned int bits) {
  size_t q = word_count / bits;
  size_t r = word_count % bits;
  if (q > PA_SIZE_MAX / PA_WORD_BITS) {
    return PA_SIZE_MAX / bits;
  }
  return q * PA_WORD_BITS + (r * PA_WORD_BITS) / bits;
}

static unsigned long pa_read(const ds_packed_array_t *array, size_t index) {
  size_t bit = index * array->bits;
  size_t word = bit / PA_WORD_BITS;
  unsigned int offset = (unsigned int)(bit % PA_WORD_BITS);
  unsigned int low_bits = PA_WORD_BITS - offset;
  unsigned long value = array->words[word] >> offset;
  if (array->bits > low_bits) {
    value |= array->words[word + 1] << low_bits;
  }
  return value & array->mask;
}

static void pa_write(ds_packed_array_t *array, size_t index,
                     unsigned long value) {
  size_t bit = index * array->bits;
  size_t word = bit / PA_WORD_BITS;
  unsigned int offset = (unsigned int)(bit % PA_WORD_BITS);
  unsigned int low_bits = PA_WORD_BITS - offset;
  array->words[word] =
      (array->words[word] & ~(array->mask << offset)) | (value << offset);
  if (array->bits > low_bits) {
    array->words[word + 1] =
        (array->words[word + 1] & ~(array->mask >> low_bits)) |
        (value >> low_bits);
  }
}

static ds_status_t pa_set_words(ds_packed_array_t *array, size_t word_count) {
  unsigned long *words;
  size_t i;
  if (array->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (word_count > PA_SIZE_MAX / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  if (array->words == NULL) {
    words = (unsigned long *)ds_context_alloc(
        array->context, word_count * sizeof(unsigned long));
  } else {
    words = (unsigned long *)ds_context_realloc(
        array->context, array->words, word_count * sizeof(unsigned long));
  }
  if (words == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = array->word_count; i < word_count; i++) {
    words[i] = 0;
  }
  array->words = words;
  array->word_count = word_count;
  array->capacity = pa_capacity_of(word_count, array->bits);
  return DS_OK;
}

static ds_status_t pa_check_bits(unsigned int bits) {
  return bits == 0 || bits > PA_WORD_BITS ? DS_ERR_INVALID : DS_OK;
}

static unsigned long pa_mask(unsigned int bits) {
  return bits == PA_WORD_BITS ? ~0UL : (1UL << bits) - 1UL;
}

size_t ds_packed_array_words_for(size_t count, unsigned int bits) {
  if (pa_check_bits(bits) != DS_OK || count == 0) {
    return 0;
  }
  if (count > PA_SIZE_MAX / bits) {
    return 0;
  }
  return (count * bits + PA_WORD_BITS - 1) / PA_WORD_BITS;
}

ds_status_t ds_packed_array_init(ds_packed_array_t *array, unsigned int bits,
                                 ds_context_t *context) {
  if (array == NULL) {
    return DS_ERR_NULL;
  }
  if (pa_check_bits(bits) != DS_OK) {
    return DS_ERR_INVALID;
  }
  array->words = NULL;
  array->word_count = 0;
  array->size = 0;
  array->capacity = 0;
  array->bits = bits;
  array->mask = pa_mask(bits);
  array->context = context != NULL ? context : ds_default_context();
  array->fixed = false;
  return DS_OK;
}

ds_status_t ds_packed_array_init_buffer(ds_packed_array_t *array,
                                        unsigned int bits, unsigned long *words,
                                        size_t word_count) {
  if (array == NULL || (words == NULL && word_count > 0)) {
    return DS_ERR_NULL;
  }
  if (pa_check_bits(bits) != DS_OK) {
    return DS_ERR_INVALID;
  }
  array->words = words;
  array->word_count = word_count;
  array->size = 0;
  array->bits = bits;
  array->mask = pa_mask(bits);
  array->capacity = pa_capacity_of(word_count, bits);
  array->context = NULL;
  array->fixed = true;
  return DS_OK;
}

void ds_packed_array_destroy(ds_packed_array_t *array) {
  if (array == NULL) {
    return;
  }
  if (!array->fixed) {
    ds_context_free(array->context, array->words);
    array->words = NULL;
    array->word_count = 0;
    array->capacity = 0;
  }
  array->size = 0;
}

ds_status_t ds_packed_array_get(const ds_packed_array_t *array, size_t index,
                                unsigned long *out_value) {
  if (out_value == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= array->size) {
    return DS_ERR_RANGE;
  }
  *out_value = pa_read(array, index);
  return DS_OK;
}

unsigned long ds_packed_array_at(const ds_packed_array_t *array, size_t index) {
  return index < array->size ? pa_read(array, index) : 0UL;
}

ds_status_t ds_packed_array_set(ds_packed_array_t *array, size_t index,
                                unsigned long value) {
  if (index >= array->size) {
    return DS_ERR_RANGE;
  }
  if ((value & ~array->mask) != 0) {
    return DS_ERR_OVERFLOW;
  }
  pa_write(array, index, value);
  return DS_OK;
}

ds_status_t ds_packed_array_reserve(ds_packed_array_t *array, size_t capacity) {
  size_t words;
  if (capacity <= array->capacity) {
    return DS_OK;
  }
  words = ds_packed_array_words_for(capacity, array->bits);
  if (words == 0) {
    return DS_ERR_OVERFLOW;
  }
  return pa_set_words(array, words);
}

ds_status_t ds_packed_array_push(ds_packed_array_t *array,
                                 unsigned long value) {
  ds_status_t status;
  size_t words;
  if ((value & ~array->mask) != 0) {
    return DS_ERR_OVERFLOW;
  }
  if (array->size == array->capacity) {
    if (array->fixed) {
      return DS_ERR_OVERFLOW;
    }
    if (array->word_count > PA_SIZE_MAX / 2) {
      return DS_ERR_OVERFLOW;
    }
    words =
        array->word_count < PA_MIN_WORDS ? PA_MIN_WORDS : array->word_count * 2;
    status = pa_set_words(array, words);
    if (status != DS_OK) {
      return status;
    }
  }
  pa_write(array, array->size, value);
  array->size++;
  return DS_OK;
}

ds_status_t ds_packed_array_pop(ds_packed_array_t *array,
                                unsigned long *out_value) {
  if (array->size == 0) {
    return DS_EMPTY;
  }
  array->size--;
  if (out_value != NULL) {
    *out_value = pa_read(array, array->size);
  }
  return DS_OK;
}

ds_status_t ds_packed_array_resize(ds_packed_array_t *array, size_t size) {
  ds_status_t status;
  size_t i;
  if (size > array->capacity) {
    status = ds_packed_array_reserve(array, size);
    if (status != DS_OK) {
      return status;
    }
  }
  for (i = array->size; i < size; i++) {
    pa_write(array, i, 0UL);
  }
  array->size = size;
  return DS_OK;
}

void ds_packed_array_clear(ds_packed_array_t *array) { array->size = 0; }

size_t ds_packed_array_size(const ds_packed_array_t *array) {
  return array->size;
}

size_t ds_packed_array_capacity(const ds_packed_array_t *array) {
  return array->capacity;
}

unsigned int ds_packed_array_bits(const ds_packed_array_t *array) {
  return array->bits;
}

unsigned long ds_packed_array_max_value(const ds_packed_array_t *array) {
  return array->mask;
}
