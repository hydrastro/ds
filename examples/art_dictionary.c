/*
 * Adaptive radix tree (ART) as a string-keyed map to integer values.
 *
 * ART is a value-mapping container: insert associates a key with a void* value,
 * get returns that value or NULL when the key is absent, and walk visits keys
 * in lexicographic order. Internally the nodes grow and shrink between four
 * layouts (4/16/48/256 children) so memory tracks the actual fan-out.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static int walk_index;

static void print_entry(const unsigned char *key, size_t len, void *value,
                        void *user) {
  (void)user;
  printf("  %2d. %.*s = %ld\n", walk_index++, (int)len, (const char *)key,
         (long)(size_t)value);
}

static void put(ds_art_t *art, const char *key, long value) {
  ds_art_insert(art, (const unsigned char *)key, strlen(key),
                (void *)(size_t)value);
}

static long get(ds_art_t *art, const char *key) {
  return (long)(size_t)ds_art_get(art, (const unsigned char *)key, strlen(key));
}

int main(void) {
  ds_art_t *art = ds_art_create();

  /* keys with shared prefixes exercise path compression and node growth */
  put(art, "romane", 1);
  put(art, "romanus", 2);
  put(art, "romulus", 3);
  put(art, "rubens", 4);
  put(art, "ruber", 5);
  put(art, "rubicon", 6);
  put(art, "rubicundus", 7);

  printf("size: %lu\n", (unsigned long)ds_art_size(art));

  /* point lookups */
  printf("romulus -> %ld\n", get(art, "romulus"));
  printf("rubicon -> %ld\n", get(art, "rubicon"));
  printf("missing -> %s\n",
         ds_art_get(art, (const unsigned char *)"missing", 7) == NULL
             ? "(absent)"
             : "(present)");

  /* lexicographic iteration */
  printf("all keys in order:\n");
  walk_index = 1;
  ds_art_walk(art, print_entry, NULL);

  ds_art_destroy(art, NULL);
  return 0;
}
