/* Nullable values and stoppable visitors, with caller-owned policy data. */
#include "lib/aho_corasick.h"
#include "lib/art.h"
#include "lib/disjoint_set.h"
#include "lib/hamt.h"
#include "lib/radix_trie.h"
#include <stdio.h>

static ds_status_t visit_bytes(const unsigned char *key, size_t length,
                               void *value, void *user) {
  const char *name = (const char *)user;
  (void)key;
  printf("%s first key: %lu bytes, %s value\n", name, (unsigned long)length,
         value == NULL ? "NULL" : "non-NULL");
  return DS_STOP;
}
static ds_status_t visit_hamt(const void *key, size_t length, void *value,
                              void *user) {
  return visit_bytes((const unsigned char *)key, length, value, user);
}
static ds_status_t visit_match(const ds_aho_corasick_match_t *match,
                               void *user) {
  size_t *budget = (size_t *)user;
  printf("Pattern %d at [%lu,%lu)\n", match->pattern_id,
         (unsigned long)match->start, (unsigned long)match->end);
  *budget -= 1;
  return *budget == 0 ? DS_STOP : DS_OK;
}
static void release_value(void *value, void *user) {
  size_t *released = (size_t *)user;
  (void)value;
  *released += 1; /* This example stores borrowed values. */
}
int main(void) {
  ds_art_t *art = NULL;
  ds_hamt_t *hamt = NULL;
  ds_radix_trie_t *radix = NULL;
  ds_aho_corasick_t *ac = NULL;
  ds_disjoint_set_t *set = NULL;
  ds_disjoint_set_node_t a, b;
  ds_status_t st;
  const unsigned char key[] = {'a', 0};
  void *value = NULL;
  size_t count = 0, budget = 2, released = 0;
  int id, rc = 1;
  if (ds_art_create_context(NULL, &art) != DS_OK ||
      ds_hamt_create_context(NULL, &hamt) != DS_OK ||
      ds_radix_trie_create_context(NULL, &radix) != DS_OK ||
      ds_aho_corasick_create_context(NULL, &ac) != DS_OK ||
      ds_disjoint_set_create_context(NULL, &set) != DS_OK)
    goto done;
  if (ds_art_insert_ex(art, key, sizeof(key), NULL) != DS_OK ||
      ds_hamt_insert_ex(hamt, key, sizeof(key), NULL) != DS_OK ||
      ds_radix_trie_insert_ex(radix, key, sizeof(key), NULL) != DS_OK)
    goto done;
  st = ds_art_find_ex(art, key, sizeof(key), &value);
  printf("ART nullable lookup: %s, value %s\n", ds_status_name(st),
         value == NULL ? "NULL" : "present");
  st = ds_hamt_find_ex(hamt, key, sizeof(key), &value);
  printf("HAMT nullable lookup: %s\n", ds_status_name(st));
  st = ds_radix_trie_find_ex(radix, key, sizeof(key), &value);
  printf("Radix nullable lookup: %s\n", ds_status_name(st));
  (void)ds_art_walk_with(art, visit_bytes, "ART");
  (void)ds_hamt_walk_with(hamt, visit_hamt, "HAMT");
  (void)ds_radix_trie_walk_with(radix, visit_bytes, "Radix");
  if (ds_aho_corasick_add_ex(ac, "he", 2, &id) != DS_OK ||
      ds_aho_corasick_add_ex(ac, "she", 3, &id) != DS_OK)
    goto done;
  st = ds_aho_corasick_search_with(ac, "she he", 6, visit_match, &budget,
                                   &count);
  printf("Search: %s after %lu delivered matches\n", ds_status_name(st),
         (unsigned long)count);
  if (ds_disjoint_set_make_ex(set, &a) != DS_OK ||
      ds_disjoint_set_make_ex(set, &b) != DS_OK)
    goto done;
  st = ds_disjoint_set_union_ex(set, &a, &b);
  printf("Connect: %s; connect again: %s\n", ds_status_name(st),
         ds_status_name(ds_disjoint_set_union_ex(set, &a, &b)));
  rc = 0;
done:
  ds_art_destroy_with(art, release_value, &released);
  ds_hamt_destroy_with(hamt, release_value, &released);
  ds_radix_trie_destroy_with(radix, release_value, &released);
  if (ac != NULL)
    ds_aho_corasick_destroy(ac);
  if (set != NULL)
    ds_disjoint_set_destroy(set);
  printf("Value cleanup callbacks: %lu\n", (unsigned long)released);
  return rc;
}
