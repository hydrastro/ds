#include "../lib/hamt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int walk_count;

static void count_cb(const void *key, size_t len, void *value, void *user) {
  (void)key;
  (void)len;
  (void)value;
  (void)user;
  walk_count += 1;
}

int main(void) {
  ds_hamt_t *hamt = ds_hamt_create();
  int n = 3000;
  int i;
  char key[32];
  void *got;
  long *values = (long *)malloc(sizeof(long) * (size_t)n);

  assert(ds_hamt_is_empty(hamt));

  for (i = 0; i < n; ++i) {
    values[i] = (long)(i * 3 + 1);
    sprintf(key, "key-%d", i);
    assert(ds_hamt_insert(hamt, key, strlen(key), &values[i]));
  }
  printf("inserted %d, size=%lu\n", n, (unsigned long)ds_hamt_size(hamt));
  assert(ds_hamt_size(hamt) == (size_t)n);

  /* all present with correct values */
  for (i = 0; i < n; ++i) {
    sprintf(key, "key-%d", i);
    assert(ds_hamt_contains(hamt, key, strlen(key)));
    got = ds_hamt_get(hamt, key, strlen(key));
    assert(got == &values[i]);
  }
  printf("all keys retrievable\n");

  /* absent keys */
  assert(!ds_hamt_contains(hamt, "nope", 4));
  assert(ds_hamt_get(hamt, "nope", 4) == NULL);

  /* update returns false and changes the stored value */
  sprintf(key, "key-7");
  assert(!ds_hamt_insert(hamt, key, strlen(key), &values[0]));
  assert(ds_hamt_get(hamt, key, strlen(key)) == &values[0]);
  assert(ds_hamt_size(hamt) == (size_t)n);

  /* walk visits every entry exactly once */
  walk_count = 0;
  ds_hamt_walk(hamt, count_cb, NULL);
  printf("walk visited %d\n", walk_count);
  assert(walk_count == n);

  /* remove half, verify survivors and absences */
  for (i = 0; i < n; i += 2) {
    sprintf(key, "key-%d", i);
    got = NULL;
    assert(ds_hamt_remove(hamt, key, strlen(key), &got));
  }
  assert(ds_hamt_size(hamt) == (size_t)(n / 2));
  for (i = 0; i < n; ++i) {
    sprintf(key, "key-%d", i);
    if (i % 2 == 0) {
      assert(!ds_hamt_contains(hamt, key, strlen(key)));
    } else {
      assert(ds_hamt_contains(hamt, key, strlen(key)));
    }
  }
  printf("after removing half, size=%lu\n", (unsigned long)ds_hamt_size(hamt));

  /* removing an absent key fails */
  assert(!ds_hamt_remove(hamt, "key-0", 5, NULL));

  /* reinsert a removed key works */
  assert(ds_hamt_insert(hamt, "key-0", 5, &values[0]));
  assert(ds_hamt_contains(hamt, "key-0", 5));

  /* remove everything remaining */
  for (i = 1; i < n; i += 2) {
    sprintf(key, "key-%d", i);
    ds_hamt_remove(hamt, key, strlen(key), NULL);
  }
  ds_hamt_remove(hamt, "key-0", 5, NULL);
  printf("final size=%lu\n", (unsigned long)ds_hamt_size(hamt));
  assert(ds_hamt_is_empty(hamt));

  ds_hamt_destroy(hamt, NULL);
  free(values);
  printf("ok\n");
  return 0;
}
