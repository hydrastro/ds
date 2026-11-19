#include <dsa/structures/bitset.h>
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

static ds_bitset_t *bitset_create_internal(size_t num_bits,
                                           ds_context_t *context) {
  ds_bitset_t *bitset;
  size_t num_words;
  size_t i;
  num_words = num_bits / DS_BITSET_BITS_PER_WORD +
              (num_bits % DS_BITSET_BITS_PER_WORD != 0 ? 1 : 0);
  if (num_words > ((size_t)-1) / sizeof(unsigned long)) {
    return NULL;
  }
  bitset = (ds_bitset_t *)ds_context_alloc(context, sizeof(ds_bitset_t));
  if (bitset == NULL) {
    return NULL;
  }
  bitset->context = context != NULL ? context : ds_default_context();

  bitset->num_bits = num_bits;
  bitset->num_words = num_words;
  bitset->words = num_words == 0
                      ? NULL
                      : (unsigned long *)ds_context_alloc(
                            context, num_words * sizeof(unsigned long));
  if (num_words > 0 && bitset->words == NULL) {
    ds_context_free(context, bitset);
    return NULL;
  }
  for (i = 0; i < num_words; i++) {
    bitset->words[i] = 0UL;
  }
  return bitset;
}
ds_bitset_t *ds_bitset_create(size_t num_bits, ds_context_t *context) {
  return bitset_create_internal(num_bits, context);
}
ds_status_t ds_bitset_create_context(size_t num_bits, ds_context_t *context,
                                     ds_bitset_t **out) {
  size_t words = num_bits / (sizeof(unsigned long) * CHAR_BIT) +
                 (num_bits % (sizeof(unsigned long) * CHAR_BIT) != 0);
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (words > (size_t)-1 / sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  *out = bitset_create_internal(
      num_bits, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
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

ds_status_t ds_bitset_set_ex(ds_bitset_t *bitset, size_t index, bool value) {
  if (bitset == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= bitset->num_bits) {
    return DS_ERR_RANGE;
  }
  ds_bitset_set_value(bitset, index, value);
  return DS_OK;
}
ds_status_t ds_bitset_get_ex(const ds_bitset_t *bitset, size_t index,
                             bool *out) {
  if (bitset == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= bitset->num_bits) {
    return DS_ERR_RANGE;
  }
  *out = (bitset->words[bitset_word_index(index)] & bitset_bit_mask(index)) !=
         0UL;
  return DS_OK;
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

/* the bits of word w that lie inside the set */
static unsigned long bitset_word_mask(const ds_bitset_t *bitset, size_t w) {
  size_t used = bitset->num_bits % DS_BITSET_BITS_PER_WORD;
  return w + 1 == bitset->num_words && used != 0 ? (1UL << used) - 1UL : ~0UL;
}

/* whole words at a time */
bool ds_bitset_all(ds_bitset_t *bitset) {
  size_t w;
  for (w = 0; w < bitset->num_words; w++) {
    unsigned long mask = bitset_word_mask(bitset, w);
    if ((bitset->words[w] & mask) != mask) {
      return false;
    }
  }
  return true;
}

size_t ds_bitset_find_first(ds_bitset_t *bitset) {
  return ds_bitset_find_next(bitset, DS_BITSET_NPOS);
}

static size_t bitset_lowest_bit(unsigned long word) {
  size_t bit = 0;
  while ((word & 0xFFUL) == 0UL) {
    word >>= 8;
    bit += 8;
  }
  while ((word & 1UL) == 0UL) {
    word >>= 1;
    bit++;
  }
  return bit;
}

/* skips empty words whole */
size_t ds_bitset_find_next(ds_bitset_t *bitset, size_t index) {
  size_t start;
  size_t w;
  unsigned long word;
  if (index == DS_BITSET_NPOS) {
    start = 0;
  } else if (index >= bitset->num_bits) {
    return DS_BITSET_NPOS;
  } else {
    start = index + 1;
  }
  if (start >= bitset->num_bits) {
    return DS_BITSET_NPOS;
  }
  w = bitset_word_index(start);
  word = bitset->words[w] & ~(bitset_bit_mask(start) - 1UL);
  for (;;) {
    word &= bitset_word_mask(bitset, w);
    if (word != 0UL) {
      return w * DS_BITSET_BITS_PER_WORD + bitset_lowest_bit(word);
    }
    if (++w >= bitset->num_words) {
      return DS_BITSET_NPOS;
    }
    word = bitset->words[w];
  }
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
  new_bitset = bitset_create_internal(bitset->num_bits, bitset->context);
  if (new_bitset == NULL) {
    return NULL;
  }
  for (i = 0; i < bitset->num_words; i++) {
    new_bitset->words[i] = bitset->words[i];
  }
  return new_bitset;
}

void ds_bitset_destroy(ds_bitset_t *bitset) {
  if (bitset->words != NULL) {
    ds_context_free(bitset->context, bitset->words);
  }
  ds_context_free(bitset->context, bitset);
}
