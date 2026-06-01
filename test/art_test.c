#include "../lib/art.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char *walk_prev_key;
static size_t walk_prev_len;
static int walk_ordered;
static int walk_count;

static int key_cmp(const unsigned char *a, size_t la, const unsigned char *b,
                   size_t lb) {
  size_t m = la < lb ? la : lb;
  int c = memcmp(a, b, m);
  if (c != 0) {
    return c;
  }
  if (la < lb)
    return -1;
  if (la > lb)
    return 1;
  return 0;
}

static unsigned char saved[4096][12];
static size_t saved_len[4096];

static void order_cb(const unsigned char *key, size_t len, void *value,
                     void *user) {
  (void)value;
  (void)user;
  if (walk_count > 0) {
    if (key_cmp(walk_prev_key, walk_prev_len, key, len) >= 0) {
      walk_ordered = 0;
    }
  }
  walk_prev_key = key;
  walk_prev_len = len;
  walk_count += 1;
}

int main(void) {
  ds_art_t *art = art_create();
  int n = 4000;
  int i;
  long *values = (long *)malloc(sizeof(long) * (size_t)n);
  int count = 0;

  assert(art_is_empty(art));

  /* variable-length keys with shared prefixes to exercise all node types,
   * path compression, and prefix splits */
  for (i = 0; i < n; ++i) {
    unsigned char key[12];
    size_t len;
    unsigned v = (unsigned)i * 2654435761U;
    len = 4 + (v % 8U); /* 4..11 bytes */
    {
      size_t j;
      for (j = 0; j < len; ++j) {
        key[j] = (unsigned char)((v >> (j * 3)) % 64U); /* small alphabet */
      }
    }
    values[count] = (long)i;
    if (art_insert(art, key, len, &values[count])) {
      memcpy(saved[count], key, len);
      saved_len[count] = len;
      count += 1;
    }
  }
  printf("inserted %d unique keys, size=%lu\n", count,
         (unsigned long)art_size(art));
  assert(art_size(art) == (size_t)count);

  /* all retrievable */
  for (i = 0; i < count; ++i) {
    assert(art_contains(art, saved[i], saved_len[i]));
    assert(art_get(art, saved[i], saved_len[i]) == &values[i]);
  }
  printf("all keys retrievable\n");

  /* update returns false, changes value */
  assert(!art_insert(art, saved[0], saved_len[0], &values[1]));
  assert(art_get(art, saved[0], saved_len[0]) == &values[1]);

  /* walk yields lexicographic order */
  walk_count = 0;
  walk_ordered = 1;
  art_walk(art, order_cb, NULL);
  assert(walk_count == count);
  assert(walk_ordered == 1);
  printf("walk visited %d keys in lexicographic order\n", walk_count);

  /* remove half (exercises node shrink transitions) */
  for (i = 0; i < count; i += 2) {
    void *out = NULL;
    assert(art_remove(art, saved[i], saved_len[i], &out));
    assert(out == &values[i] || out == &values[1]); /* slot 0 was updated */
  }
  for (i = 0; i < count; ++i) {
    if (i % 2 == 0) {
      assert(!art_contains(art, saved[i], saved_len[i]));
    } else {
      assert(art_contains(art, saved[i], saved_len[i]));
    }
  }
  printf("after removing half, size=%lu\n", (unsigned long)art_size(art));

  /* walk still ordered after deletions */
  walk_count = 0;
  walk_ordered = 1;
  art_walk(art, order_cb, NULL);
  assert(walk_ordered == 1);

  /* removing absent fails */
  assert(!art_remove(art, saved[0], saved_len[0], NULL));

  /* remove the rest */
  for (i = 1; i < count; i += 2) {
    art_remove(art, saved[i], saved_len[i], NULL);
  }
  printf("final size=%lu\n", (unsigned long)art_size(art));
  assert(art_is_empty(art));

  art_destroy(art, NULL);
  free(values);
  printf("ok\n");
  return 0;
}
