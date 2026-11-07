#include "bitset.h"
#include <limits.h>
#include <stdlib.h>

#define DS_BITSET_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)

static size_t bitset_word_index(size_t bit) {
  return bit / DS_BITSET_BITS_PER_WORD;
}

static unsigned long bitset_bit_mask(size_t bit) {
  return 1UL << (bit % DS_BITSET_BITS_PER_WORD);
}

static size_t bitset_popcount_word(unsigned long word) {
  size_t count = 0;
  while (word != 0UL) {
    word &= word - 1UL;
    count += 1;
  }
  return count;
}

ds_bitset_t *ds_bitset_create(size_t num_bits) {
  return ds_bitset_create_alloc(num_bits, malloc, free);
}

ds_bitset_t *ds_bitset_create_alloc(size_t num_bits, void *(*allocator)(size_t),
                                    void (*deallocator)(void *)) {
  ds_bitset_t *bitset;
  size_t num_words;
  size_t i;
  num_words = num_bits == 0 ? 0
                            : (num_bits + DS_BITSET_BITS_PER_WORD - 1) /
                                  DS_BITSET_BITS_PER_WORD;
  bitset = (ds_bitset_t *)allocator(sizeof(ds_bitset_t));
  bitset->allocator = allocator;
  bitset->deallocator = deallocator;
  bitset->num_bits = num_bits;
  bitset->num_words = num_words;
  bitset->words =
      num_words == 0
          ? NULL
          : (unsigned long *)allocator(num_words * sizeof(unsigned long));
  for (i = 0; i < num_words; i++) {
    bitset->words[i] = 0UL;
  }
  return bitset;
}

void ds_bitset_set(ds_bitset_t *bitset, size_t index) {
  if (index < bitset->num_bits) {
    bitset->words[bitset_word_index(index)] |= bitset_bit_mask(index);
  }
}

void ds_bitset_clear(ds_bitset_t *bitset, size_t index) {
  if (index < bitset->num_bits) {
    bitset->words[bitset_word_index(index)] &= ~bitset_bit_mask(index);
  }
}

void ds_bitset_flip(ds_bitset_t *bitset, size_t index) {
  if (index < bitset->num_bits) {
    bitset->words[bitset_word_index(index)] ^= bitset_bit_mask(index);
  }
}

void ds_bitset_set_value(ds_bitset_t *bitset, size_t index, bool value) {
  if (value) {
    ds_bitset_set(bitset, index);
  } else {
    ds_bitset_clear(bitset, index);
  }
}

bool ds_bitset_test(ds_bitset_t *bitset, size_t index) {
  bool result;
  if (index >= bitset->num_bits) {
    result = false;
  } else {
    result = (bitset->words[bitset_word_index(index)] &
              bitset_bit_mask(index)) != 0UL;
  }
  return result;
}

static void bitset_trim_tail(ds_bitset_t *bitset) {
  size_t used = bitset->num_bits % DS_BITSET_BITS_PER_WORD;
  if (used != 0 && bitset->num_words > 0) {
    unsigned long mask = (1UL << used) - 1UL;
    bitset->words[bitset->num_words - 1] &= mask;
  }
}

void ds_bitset_set_all(ds_bitset_t *bitset) {
  size_t i;
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] = ~0UL;
  }
  bitset_trim_tail(bitset);
}

void ds_bitset_clear_all(ds_bitset_t *bitset) {
  size_t i;
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] = 0UL;
  }
}

size_t ds_bitset_count(ds_bitset_t *bitset) {
  size_t count = 0;
  size_t i;
  for (i = 0; i < bitset->num_words; i++) {
    count += bitset_popcount_word(bitset->words[i]);
  }
  return count;
}

size_t ds_bitset_size(ds_bitset_t *bitset) {
  size_t result;
  result = bitset->num_bits;
  return result;
}

bool ds_bitset_any(ds_bitset_t *bitset) {
  bool result = false;
  size_t i;
  for (i = 0; i < bitset->num_words; i++) {
    if (bitset->words[i] != 0UL) {
      result = true;
      break;
    }
  }
  return result;
}

bool ds_bitset_none(ds_bitset_t *bitset) { return !ds_bitset_any(bitset); }

bool ds_bitset_all(ds_bitset_t *bitset) {
  bool result = true;
  size_t i;
  if (bitset->num_bits == 0) {
    return true;
  }
  for (i = 0; i < bitset->num_bits; i++) {
    if ((bitset->words[bitset_word_index(i)] & bitset_bit_mask(i)) == 0UL) {
      result = false;
      break;
    }
  }
  return result;
}

size_t ds_bitset_find_first(ds_bitset_t *bitset) {
  return ds_bitset_find_next(bitset, DS_BITSET_NPOS);
}

size_t ds_bitset_find_next(ds_bitset_t *bitset, size_t index) {
  size_t start;
  size_t i;
  size_t result = DS_BITSET_NPOS;
  start = index == DS_BITSET_NPOS ? 0 : index + 1;
  for (i = start; i < bitset->num_bits; i++) {
    if ((bitset->words[bitset_word_index(i)] & bitset_bit_mask(i)) != 0UL) {
      result = i;
      break;
    }
  }
  return result;
}

bool ds_bitset_and(ds_bitset_t *bitset, ds_bitset_t *other) {
  size_t i;
  if (bitset->num_bits != other->num_bits) {
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] &= other->words[i];
  }
  return true;
}

bool ds_bitset_or(ds_bitset_t *bitset, ds_bitset_t *other) {
  size_t i;
  if (bitset->num_bits != other->num_bits) {
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] |= other->words[i];
  }
  return true;
}

bool ds_bitset_xor(ds_bitset_t *bitset, ds_bitset_t *other) {
  size_t i;
  if (bitset->num_bits != other->num_bits) {
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] ^= other->words[i];
  }
  return true;
}

void ds_bitset_not(ds_bitset_t *bitset) {
  size_t i;
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] = ~bitset->words[i];
  }
  bitset_trim_tail(bitset);
}

bool ds_bitset_equals(ds_bitset_t *bitset, ds_bitset_t *other) {
  bool result = true;
  size_t i;
  if (bitset->num_bits != other->num_bits) {
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    if (bitset->words[i] != other->words[i]) {
      result = false;
      break;
    }
  }
  return result;
}

ds_bitset_t *ds_bitset_clone(ds_bitset_t *bitset) {
  ds_bitset_t *new_bitset;
  size_t i;
  new_bitset = ds_bitset_create_alloc(bitset->num_bits, bitset->allocator,
                                      bitset->deallocator);
  for (i = 0; i < bitset->num_words; i++) {
    new_bitset->words[i] = bitset->words[i];
  }
  return new_bitset;
}

void ds_bitset_destroy(ds_bitset_t *bitset) {
  if (bitset->words != NULL) {
    bitset->deallocator(bitset->words);
  }
  bitset->deallocator(bitset);
}
