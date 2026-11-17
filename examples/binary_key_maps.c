/* Byte-string keys include empty strings, NUL bytes and long shared paths.
 * Values here are borrowed integer objects; each map owns its key copies. */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static void show_key(const unsigned char *key, size_t length, void *value,
                     void *user) {
  size_t i;
  (void)user;
  printf("  %d: ", *(int *)value);
  for (i = 0; i < length; i++)
    printf("%02x", (unsigned)key[i]);
  if (length == 0)
    printf("(empty)");
  putchar('\n');
}

int main(void) {
  static const unsigned char prefix[] = "shared/path/longer/than/ten/bytes";
  static const unsigned char zero[] = "shared/path/longer/than/ten/bytes\0";
  static const unsigned char suffix[] = "shared/path/longer/than/ten/bytes/x";
  const unsigned char *keys[4];
  size_t lengths[4], i;
  int values[6] = {10, 20, 30, 40, 50, 60};
  ds_art_t *art = ds_art_create();
  ds_radix_trie_t *radix = ds_radix_trie_create();
  ds_hamt_t *hamt = ds_hamt_create();
  ds_status_t st = DS_OK;
  int result = 0;
  keys[0] = NULL;
  keys[1] = prefix;
  keys[2] = zero;
  keys[3] = suffix;
  lengths[0] = 0;
  lengths[1] = sizeof(prefix) - 1;
  lengths[2] = sizeof(zero) - 1;
  lengths[3] = sizeof(suffix) - 1;
  if (art == NULL || radix == NULL || hamt == NULL) {
    st = DS_ERR_ALLOC;
    goto cleanup;
  }
  for (i = 0; i < 4; i++) {
    st = ds_art_insert_ex(art, keys[i], lengths[i], &values[i]);
    if (DS_FAILED(st))
      goto cleanup;
    st = ds_radix_trie_insert_ex(radix, keys[i], lengths[i], &values[i]);
    if (DS_FAILED(st))
      goto cleanup;
    st = ds_hamt_insert_ex(hamt, keys[i], lengths[i], &values[i]);
    if (DS_FAILED(st))
      goto cleanup;
  }
  puts("ART byte order:");
  ds_art_walk(art, show_key, NULL);
  printf("Exact prefix: ART=%d, radix=%d, HAMT=%d\n",
         *(int *)ds_art_get(art, prefix, sizeof(prefix) - 1),
         *(int *)ds_radix_trie_get(radix, prefix, sizeof(prefix) - 1),
         *(int *)ds_hamt_get(hamt, prefix, sizeof(prefix) - 1));

  /* These different strings share the 32-bit FNV-1a hash 0x5e4daa9d.
   * A collision list must keep both exact keys available. */
  st = ds_hamt_insert_ex(hamt, "costarring", 10, &values[4]);
  if (DS_FAILED(st))
    goto cleanup;
  st = ds_hamt_insert_ex(hamt, "liquid", 6, &values[5]);
  if (DS_FAILED(st))
    goto cleanup;
  printf("Same hash, separate values: costarring=%d, liquid=%d\n",
         *(int *)ds_hamt_get(hamt, "costarring", 10),
         *(int *)ds_hamt_get(hamt, "liquid", 6));
  (void)ds_art_remove(art, prefix, sizeof(prefix) - 1, NULL);
  puts("After removing the prefix, its extensions remain:");
  ds_art_walk(art, show_key, NULL);
cleanup:
  if (DS_FAILED(st)) {
    fprintf(stderr, "map operation failed: %d\n", (int)st);
    result = 1;
  }
  ds_art_destroy(art, NULL);
  if (radix != NULL)
    ds_radix_trie_destroy(radix, NULL);
  if (hamt != NULL)
    ds_hamt_destroy(hamt, NULL);
  return result;
}
