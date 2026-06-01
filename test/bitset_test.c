#include "../lib/bitset.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  ds_bitset_t *a = ds_bitset_create(100);
  ds_bitset_t *b = ds_bitset_create(100);
  ds_bitset_t *clone;

  assert(ds_bitset_size(a) == 100);
  assert(ds_bitset_none(a));
  assert(!ds_bitset_any(a));

  ds_bitset_set(a, 5);
  ds_bitset_set(a, 50);
  ds_bitset_set(a, 99);
  assert(ds_bitset_test(a, 5));
  assert(ds_bitset_test(a, 50));
  assert(ds_bitset_test(a, 99));
  assert(!ds_bitset_test(a, 6));
  assert(ds_bitset_count(a) == 3);
  printf("count: %lu\n", (unsigned long)ds_bitset_count(a));

  assert(ds_bitset_find_first(a) == 5);
  assert(ds_bitset_find_next(a, 5) == 50);
  assert(ds_bitset_find_next(a, 50) == 99);
  assert(ds_bitset_find_next(a, 99) == DS_BITSET_NPOS);

  ds_bitset_clear(a, 50);
  assert(!ds_bitset_test(a, 50));
  assert(ds_bitset_count(a) == 2);

  ds_bitset_flip(a, 50);
  assert(ds_bitset_test(a, 50));
  ds_bitset_flip(a, 50);
  assert(!ds_bitset_test(a, 50));

  /* set ops: b = {5, 7} */
  ds_bitset_set(b, 5);
  ds_bitset_set(b, 7);
  clone = ds_bitset_clone(a);
  assert(ds_bitset_equals(clone, a));

  /* a AND b -> {5} */
  ds_bitset_and(a, b);
  assert(ds_bitset_test(a, 5));
  assert(!ds_bitset_test(a, 99));
  assert(ds_bitset_count(a) == 1);
  printf("after AND count: %lu\n", (unsigned long)ds_bitset_count(a));

  /* clone OR b -> includes 5,7,99 */
  ds_bitset_or(clone, b);
  assert(ds_bitset_test(clone, 7));
  assert(ds_bitset_test(clone, 99));
  printf("after OR count: %lu\n", (unsigned long)ds_bitset_count(clone));

  ds_bitset_set_all(b);
  assert(ds_bitset_all(b));
  assert(ds_bitset_count(b) == 100);
  ds_bitset_not(b);
  assert(ds_bitset_none(b));

  ds_bitset_clear_all(clone);
  assert(ds_bitset_none(clone));

  ds_bitset_destroy(a);
  ds_bitset_destroy(b);
  ds_bitset_destroy(clone);
  printf("ok\n");
  return 0;
}
