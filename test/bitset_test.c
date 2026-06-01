#include "../lib/bitset.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  ds_bitset_t *a = bitset_create(100);
  ds_bitset_t *b = bitset_create(100);
  ds_bitset_t *clone;

  assert(bitset_size(a) == 100);
  assert(bitset_none(a));
  assert(!bitset_any(a));

  bitset_set(a, 5);
  bitset_set(a, 50);
  bitset_set(a, 99);
  assert(bitset_test(a, 5));
  assert(bitset_test(a, 50));
  assert(bitset_test(a, 99));
  assert(!bitset_test(a, 6));
  assert(bitset_count(a) == 3);
  printf("count: %lu\n", (unsigned long)bitset_count(a));

  assert(bitset_find_first(a) == 5);
  assert(bitset_find_next(a, 5) == 50);
  assert(bitset_find_next(a, 50) == 99);
  assert(bitset_find_next(a, 99) == DS_BITSET_NPOS);

  bitset_clear(a, 50);
  assert(!bitset_test(a, 50));
  assert(bitset_count(a) == 2);

  bitset_flip(a, 50);
  assert(bitset_test(a, 50));
  bitset_flip(a, 50);
  assert(!bitset_test(a, 50));

  /* set ops: b = {5, 7} */
  bitset_set(b, 5);
  bitset_set(b, 7);
  clone = bitset_clone(a);
  assert(bitset_equals(clone, a));

  /* a AND b -> {5} */
  bitset_and(a, b);
  assert(bitset_test(a, 5));
  assert(!bitset_test(a, 99));
  assert(bitset_count(a) == 1);
  printf("after AND count: %lu\n", (unsigned long)bitset_count(a));

  /* clone OR b -> includes 5,7,99 */
  bitset_or(clone, b);
  assert(bitset_test(clone, 7));
  assert(bitset_test(clone, 99));
  printf("after OR count: %lu\n", (unsigned long)bitset_count(clone));

  bitset_set_all(b);
  assert(bitset_all(b));
  assert(bitset_count(b) == 100);
  bitset_not(b);
  assert(bitset_none(b));

  bitset_clear_all(clone);
  assert(bitset_none(clone));

  bitset_destroy(a);
  bitset_destroy(b);
  bitset_destroy(clone);
  printf("ok\n");
  return 0;
}
