#include "bitset.h"
#include <limits.h>
#include <stdlib.h>

#define DS_BITSET_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)

static size_t FUNC(bitset_word_index)(size_t bit) {
  return bit / DS_BITSET_BITS_PER_WORD;
}

static unsigned long FUNC(bitset_bit_mask)(size_t bit) {
  return 1UL << (bit % DS_BITSET_BITS_PER_WORD);
}

static size_t FUNC(bitset_popcount_word)(unsigned long word) {
  size_t count = 0;
  while (word != 0UL) {
    word &= word - 1UL;
    count += 1;
  }
  return count;
}

ds_bitset_t *FUNC(bitset_create)(size_t num_bits) {
  return FUNC(bitset_create_alloc)(num_bits, malloc, free);
}

ds_bitset_t *FUNC(bitset_create_alloc)(size_t num_bits,
                                       void *(*allocator)(size_t),
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
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(bitset);
#endif
  return bitset;
}

void FUNC(bitset_set)(ds_bitset_t *bitset, size_t index) {
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (index < bitset->num_bits) {
    bitset->words[FUNC(bitset_word_index)(index)] |=
        FUNC(bitset_bit_mask)(index);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
}

void FUNC(bitset_clear)(ds_bitset_t *bitset, size_t index) {
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (index < bitset->num_bits) {
    bitset->words[FUNC(bitset_word_index)(index)] &=
        ~FUNC(bitset_bit_mask)(index);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
}

void FUNC(bitset_flip)(ds_bitset_t *bitset, size_t index) {
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (index < bitset->num_bits) {
    bitset->words[FUNC(bitset_word_index)(index)] ^=
        FUNC(bitset_bit_mask)(index);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
}

void FUNC(bitset_set_value)(ds_bitset_t *bitset, size_t index, bool value) {
  if (value) {
    FUNC(bitset_set)(bitset, index);
  } else {
    FUNC(bitset_clear)(bitset, index);
  }
}

bool FUNC(bitset_test)(ds_bitset_t *bitset, size_t index) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (index >= bitset->num_bits) {
    result = false;
  } else {
    result = (bitset->words[FUNC(bitset_word_index)(index)] &
              FUNC(bitset_bit_mask)(index)) != 0UL;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return result;
}

static void FUNC(bitset_trim_tail)(ds_bitset_t *bitset) {
  size_t used = bitset->num_bits % DS_BITSET_BITS_PER_WORD;
  if (used != 0 && bitset->num_words > 0) {
    unsigned long mask = (1UL << used) - 1UL;
    bitset->words[bitset->num_words - 1] &= mask;
  }
}

void FUNC(bitset_set_all)(ds_bitset_t *bitset) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] = ~0UL;
  }
  FUNC(bitset_trim_tail)(bitset);
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
}

void FUNC(bitset_clear_all)(ds_bitset_t *bitset) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] = 0UL;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
}

size_t FUNC(bitset_count)(ds_bitset_t *bitset) {
  size_t count = 0;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  for (i = 0; i < bitset->num_words; i++) {
    count += FUNC(bitset_popcount_word)(bitset->words[i]);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return count;
}

size_t FUNC(bitset_size)(ds_bitset_t *bitset) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  result = bitset->num_bits;
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return result;
}

bool FUNC(bitset_any)(ds_bitset_t *bitset) {
  bool result = false;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  for (i = 0; i < bitset->num_words; i++) {
    if (bitset->words[i] != 0UL) {
      result = true;
      break;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return result;
}

bool FUNC(bitset_none)(ds_bitset_t *bitset) {
  return !FUNC(bitset_any)(bitset);
}

bool FUNC(bitset_all)(ds_bitset_t *bitset) {
  bool result = true;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (bitset->num_bits == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(bitset);
#endif
    return true;
  }
  for (i = 0; i < bitset->num_bits; i++) {
    if ((bitset->words[FUNC(bitset_word_index)(i)] &
         FUNC(bitset_bit_mask)(i)) == 0UL) {
      result = false;
      break;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return result;
}

size_t FUNC(bitset_find_first)(ds_bitset_t *bitset) {
  return FUNC(bitset_find_next)(bitset, DS_BITSET_NPOS);
}

size_t FUNC(bitset_find_next)(ds_bitset_t *bitset, size_t index) {
  size_t start;
  size_t i;
  size_t result = DS_BITSET_NPOS;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  start = index == DS_BITSET_NPOS ? 0 : index + 1;
  for (i = start; i < bitset->num_bits; i++) {
    if ((bitset->words[FUNC(bitset_word_index)(i)] &
         FUNC(bitset_bit_mask)(i)) != 0UL) {
      result = i;
      break;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return result;
}

bool FUNC(bitset_and)(ds_bitset_t *bitset, ds_bitset_t *other) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (bitset->num_bits != other->num_bits) {
#ifdef DS_THREAD_SAFE
    UNLOCK(bitset);
#endif
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] &= other->words[i];
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return true;
}

bool FUNC(bitset_or)(ds_bitset_t *bitset, ds_bitset_t *other) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (bitset->num_bits != other->num_bits) {
#ifdef DS_THREAD_SAFE
    UNLOCK(bitset);
#endif
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] |= other->words[i];
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return true;
}

bool FUNC(bitset_xor)(ds_bitset_t *bitset, ds_bitset_t *other) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (bitset->num_bits != other->num_bits) {
#ifdef DS_THREAD_SAFE
    UNLOCK(bitset);
#endif
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] ^= other->words[i];
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return true;
}

void FUNC(bitset_not)(ds_bitset_t *bitset) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  for (i = 0; i < bitset->num_words; i++) {
    bitset->words[i] = ~bitset->words[i];
  }
  FUNC(bitset_trim_tail)(bitset);
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
}

bool FUNC(bitset_equals)(ds_bitset_t *bitset, ds_bitset_t *other) {
  bool result = true;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  if (bitset->num_bits != other->num_bits) {
#ifdef DS_THREAD_SAFE
    UNLOCK(bitset);
#endif
    return false;
  }
  for (i = 0; i < bitset->num_words; i++) {
    if (bitset->words[i] != other->words[i]) {
      result = false;
      break;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return result;
}

ds_bitset_t *FUNC(bitset_clone)(ds_bitset_t *bitset) {
  ds_bitset_t *new_bitset;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(bitset);
#endif
  new_bitset = FUNC(bitset_create_alloc)(bitset->num_bits, bitset->allocator,
                                         bitset->deallocator);
  for (i = 0; i < bitset->num_words; i++) {
    new_bitset->words[i] = bitset->words[i];
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(bitset);
#endif
  return new_bitset;
}

void FUNC(bitset_destroy)(ds_bitset_t *bitset) {
  if (bitset->words != NULL) {
    bitset->deallocator(bitset->words);
  }
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(bitset);
#endif
  bitset->deallocator(bitset);
}
