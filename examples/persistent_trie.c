#include "ds.h"
#include <stdio.h>

int main(void) {
  ds_ptrie_t *trie;
  ds_ptrie_version_t *root;
  ds_ptrie_version_t *v1;
  void *value;

  trie = ds_ptrie_create(NULL);
  root = ds_ptrie_root(trie);
  v1 = ds_ptrie_insert(trie, root, (const unsigned char *)"cat", 3U,
                       "animal");

  if (ds_ptrie_get(root, (const unsigned char *)"cat", 3U, &value) ==
      DS_NOT_FOUND) {
    puts("root does not contain cat");
  }
  if (ds_ptrie_get(v1, (const unsigned char *)"cat", 3U, &value) == DS_OK) {
    printf("v1 cat=%s\n", (char *)value);
  }

  ds_ptrie_version_release(trie, v1);
  ds_ptrie_destroy(trie);
  return 0;
}
