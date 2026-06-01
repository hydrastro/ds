#include "../lib/radix_trie.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int walk_count;

static void count_cb(const unsigned char *key, size_t length, void *value) {
  (void)key;
  (void)length;
  (void)value;
  walk_count += 1;
}

static bool insert_str(ds_radix_trie_t *trie, const char *s, void *v) {
  return radix_trie_insert(trie, s, strlen(s), v);
}

static bool contains_str(ds_radix_trie_t *trie, const char *s) {
  return radix_trie_contains(trie, s, strlen(s));
}

int main(void) {
  ds_radix_trie_t *trie = radix_trie_create();
  int values[8];
  void *got;
  int i;

  for (i = 0; i < 8; ++i) {
    values[i] = i;
  }

  assert(radix_trie_is_empty(trie));

  /* shared prefixes force edge splits */
  assert(insert_str(trie, "romane", &values[0]));
  assert(insert_str(trie, "romanus", &values[1]));
  assert(insert_str(trie, "romulus", &values[2]));
  assert(insert_str(trie, "rubens", &values[3]));
  assert(insert_str(trie, "ruber", &values[4]));
  assert(insert_str(trie, "rubicon", &values[5]));
  assert(insert_str(trie, "rubicundus", &values[6]));
  printf("size: %lu\n", (unsigned long)radix_trie_size(trie));
  assert(radix_trie_size(trie) == 7);

  /* duplicate insert updates value (upsert) and returns false; size unchanged
   */
  assert(!insert_str(trie, "romane", &values[7]));
  assert(radix_trie_size(trie) == 7);
  assert(radix_trie_get(trie, "romane", strlen("romane")) == &values[7]);
  /* restore original value for the remove check below */
  assert(!insert_str(trie, "romane", &values[0]));

  assert(contains_str(trie, "romane"));
  assert(contains_str(trie, "rubicundus"));
  assert(!contains_str(trie, "roman"));    /* prefix, not terminal */
  assert(!contains_str(trie, "rubicund")); /* prefix, not terminal */
  assert(!contains_str(trie, "xyz"));

  got = radix_trie_get(trie, "ruber", strlen("ruber"));
  assert(got == &values[4]);
  printf("get(ruber) -> %d\n", *(int *)got);

  walk_count = 0;
  radix_trie_walk(trie, count_cb);
  printf("walk visited: %d\n", walk_count);
  assert(walk_count == 7);

  /* remove a leaf that triggers merge */
  assert(radix_trie_remove(trie, "romane", strlen("romane"), &got));
  assert(got == &values[0]);
  assert(!contains_str(trie, "romane"));
  assert(contains_str(trie, "romanus")); /* sibling still present */
  assert(radix_trie_size(trie) == 6);

  /* removing absent key fails */
  assert(!radix_trie_remove(trie, "romane", strlen("romane"), &got));
  assert(!radix_trie_remove(trie, "zzz", strlen("zzz"), NULL));

  /* remove the rest */
  assert(radix_trie_remove(trie, "romanus", strlen("romanus"), NULL));
  assert(radix_trie_remove(trie, "romulus", strlen("romulus"), NULL));
  assert(radix_trie_remove(trie, "rubens", strlen("rubens"), NULL));
  assert(radix_trie_remove(trie, "ruber", strlen("ruber"), NULL));
  assert(radix_trie_remove(trie, "rubicon", strlen("rubicon"), NULL));
  assert(radix_trie_remove(trie, "rubicundus", strlen("rubicundus"), NULL));
  assert(radix_trie_is_empty(trie));

  /* reinsert after emptying works */
  assert(insert_str(trie, "test", &values[0]));
  assert(contains_str(trie, "test"));

  radix_trie_destroy(trie, NULL);
  printf("ok\n");
  return 0;
}
