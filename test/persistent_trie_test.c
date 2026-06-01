#include "../lib/persistent_trie.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int visit_count;

static bool count_cb(const unsigned char *key, size_t len, void *value,
                     void *user) {
  (void)key;
  (void)len;
  (void)value;
  (void)user;
  visit_count += 1;
  return true;
}

static ds_ptrie_version_t *ins(ds_ptrie_t *t, ds_ptrie_version_t *base,
                               const char *k, void *v) {
  return ds_ptrie_insert(t, base, (const unsigned char *)k, strlen(k), v);
}

static ds_status_t get(ds_ptrie_version_t *v, const char *k, void **out) {
  return ds_ptrie_get(v, (const unsigned char *)k, strlen(k), out);
}

int main(void) {
  ds_ptrie_config_t cfg;
  ds_ptrie_t *trie;
  ds_ptrie_version_t *v0;
  ds_ptrie_version_t *v1;
  ds_ptrie_version_t *v2;
  ds_ptrie_version_t *v3;
  void *out = NULL;
  long values[4];
  size_t n;

  values[0] = 10;
  values[1] = 20;
  values[2] = 30;
  values[3] = 40;

  ds_ptrie_config_init(&cfg);
  trie = ds_ptrie_create(&cfg);
  assert(trie != NULL);

  v0 = ds_ptrie_root(trie);

  /* build versions with shared "ca" prefix */
  v1 = ins(trie, v0, "cat", &values[0]);
  v2 = ins(trie, v1, "car", &values[1]);
  v3 = ins(trie, v2, "card", &values[2]);
  assert(v1 && v2 && v3);

  /* persistence: v1 only knows "cat" */
  assert(get(v1, "cat", &out) == DS_OK && out == &values[0]);
  assert(get(v1, "car", &out) == DS_NOT_FOUND);
  /* v3 knows all three */
  assert(get(v3, "cat", &out) == DS_OK);
  assert(get(v3, "car", &out) == DS_OK);
  assert(get(v3, "card", &out) == DS_OK && out == &values[2]);
  /* base empty */
  assert(get(v0, "cat", &out) == DS_NOT_FOUND);
  printf("insert + version isolation: ok\n");

  /* prefix membership and counting on v3 */
  assert(ds_ptrie_contains_prefix(v3, (const unsigned char *)"ca", 2) == DS_OK);
  assert(ds_ptrie_contains_prefix(v3, (const unsigned char *)"do", 2) ==
         DS_NOT_FOUND);
  n = ds_ptrie_count_prefix(v3, (const unsigned char *)"car", 3);
  /* "car" and "card" share the "car" prefix */
  assert(n == 2);
  n = ds_ptrie_count_prefix(v3, (const unsigned char *)"ca", 2);
  assert(n == 3); /* cat, car, card */
  printf("prefix contains/count: ok\n");

  /* longest-prefix match */
  {
    size_t mlen = 0;
    ds_status_t s = ds_ptrie_longest_prefix(v3, (const unsigned char *)"cards",
                                            5, &mlen, &out);
    assert(s == DS_OK);
    assert(mlen == 4); /* "card" is the longest stored prefix of "cards" */
  }
  printf("longest prefix: ok\n");

  /* visit all keys under "ca" */
  visit_count = 0;
  ds_ptrie_visit_prefix(v3, (const unsigned char *)"ca", 2, count_cb, NULL);
  assert(visit_count == 3);
  printf("visit prefix: ok\n");

  /* remove "car" -> new version; "card" must survive (distinct key) */
  {
    ds_ptrie_version_t *v4 =
        ds_ptrie_remove(trie, v3, (const unsigned char *)"car", 3);
    assert(v4 != NULL);
    assert(get(v4, "car", &out) == DS_NOT_FOUND);
    assert(get(v4, "card", &out) == DS_OK);
    assert(get(v4, "cat", &out) == DS_OK);
    /* v3 still has "car" (immutability) */
    assert(get(v3, "car", &out) == DS_OK);
    ds_ptrie_version_release(trie, v4);
    printf("remove + immutability: ok\n");
  }

  /* remove_prefix removes a whole subtree */
  {
    size_t removed = 0;
    ds_ptrie_version_t *v5 = ds_ptrie_remove_prefix(
        trie, v3, (const unsigned char *)"car", 3, &removed);
    assert(v5 != NULL);
    assert(removed == 2); /* car, card */
    assert(get(v5, "car", &out) == DS_NOT_FOUND);
    assert(get(v5, "card", &out) == DS_NOT_FOUND);
    assert(get(v5, "cat", &out) == DS_OK); /* cat survives */
    ds_ptrie_version_release(trie, v5);
    printf("remove_prefix: ok\n");
  }

  ds_ptrie_version_release(trie, v1);
  ds_ptrie_version_release(trie, v2);
  ds_ptrie_version_release(trie, v3);
  ds_ptrie_destroy(trie);
  printf("ok\n");
  return 0;
}
