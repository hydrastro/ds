/* The same persistent routing workflow with byte edges and compressed labels.
 */
#include <dsa/structures/persistent_trie.h>
#include <stdio.h>
#include <string.h>

static ds_status_t copy_target(void *value, void *user, void **out) {
  const char *text = (const char *)value;
  size_t n;
  (void)user;
  *out = NULL;
  if (text == NULL) {
    return DS_OK;
  }
  n = strlen(text) + 1;
  *out = ds_context_alloc(NULL, n);
  if (*out == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(*out, text, n);
  return DS_OK;
}

static void free_target(void *value, void *user) {
  (void)user;
  ds_context_free(NULL, value);
}

static bool show_route(const unsigned char *key, size_t len, void *value,
                       void *user) {
  size_t i;
  (void)user;
  printf("  ");
  for (i = 0; i < len; i++) {
    if (key[i] == 0) {
      printf("\\0");
    } else {
      putchar((int)key[i]);
    }
  }
  printf(" -> %s\n", value == NULL ? "(no target)" : (const char *)value);
  return true;
}

static ds_status_t routes(ds_ptrie_storage_t storage) {
  static const char *paths[] = {"",
                                "/services/catalog",
                                "/services/catalog/production/eu-west",
                                "/services/catalog/production/us-east",
                                "/services/catalog/preview",
                                "/binary"};
  static const unsigned char binary[] = {'/', 'b', 'i', 'n', 'a', 'r', 'y', 0};
  ds_ptrie_config_t config;
  ds_ptrie_t *trie = NULL;
  ds_ptrie_version_t *current = NULL, *snapshot = NULL, *next = NULL;
  ds_status_t st;
  size_t i, changed = 0, matched = 0;
  void *target = NULL;
  ds_ptrie_config_init(&config);
  config.storage = storage;
  config.clone_value_ex = copy_target;
  config.destroy_value = free_target;
  st = ds_ptrie_create_ex(&config, &trie);
  if (st != DS_OK) {
    return st;
  }
  current = ds_ptrie_root(trie);
  st = ds_ptrie_version_retain_ex(current);
  if (st != DS_OK) {
    current = NULL;
    goto done;
  }
  for (i = 0; i < sizeof(paths) / sizeof(paths[0]) && st == DS_OK; i++) {
    st = ds_ptrie_insert_ex(trie, current, (const unsigned char *)paths[i],
                            strlen(paths[i]), (void *)"origin", &next);
    if (st == DS_OK) {
      ds_ptrie_version_release(trie, current);
      current = next;
    }
  }
  if (st != DS_OK) {
    goto done;
  }
  st = ds_ptrie_insert_ex(trie, current, binary, sizeof(binary), NULL, &next);
  if (st != DS_OK) {
    goto done;
  }
  ds_ptrie_version_release(trie, current);
  current = next;
  st = ds_ptrie_version_retain_ex(current);
  if (st != DS_OK) {
    goto done;
  }
  snapshot = current;
  /* This prefix ends inside the compressed label "production". */
  st = ds_ptrie_copy_prefix_ex(
      trie, current, snapshot, (const unsigned char *)"/services/catalog/pro",
      21, (const unsigned char *)"/archive/pro", 12, &changed, &next);
  if (st != DS_OK) {
    goto done;
  }
  ds_ptrie_version_release(trie, current);
  current = next;
  printf("%s: archived %lu production routes\n",
         storage == DS_PTRIE_RADIX ? "radix labels" : "byte edges",
         (unsigned long)changed);
  st = ds_ptrie_move_prefix_ex(
      trie, current, (const unsigned char *)"/services/catalog/preview", 25,
      (const unsigned char *)"/services/catalog/staging", 25, &changed, &next);
  if (st != DS_OK) {
    goto done;
  }
  ds_ptrie_version_release(trie, current);
  current = next;
  st = ds_ptrie_remove_ex(trie, current, (const unsigned char *)paths[3],
                          strlen(paths[3]), &next);
  if (st != DS_OK) {
    goto done;
  }
  ds_ptrie_version_release(trie, current);
  current = next;
  st = ds_ptrie_longest_prefix(
      current, (const unsigned char *)"/services/catalog/unknown", 25,
      &matched, &target);
  if (st != DS_OK) {
    goto done;
  }
  printf("  fallback prefix: %lu bytes; snapshot/current production counts: "
         "%lu/%lu\n",
         (unsigned long)matched,
         (unsigned long)ds_ptrie_count_prefix(
             snapshot, (const unsigned char *)"/services/catalog/pro", 21),
         (unsigned long)ds_ptrie_count_prefix(
             current, (const unsigned char *)"/services/catalog/pro", 21));
  st = ds_ptrie_visit_prefix(current, (const unsigned char *)"/", 1,
                             show_route, NULL);
done:
  ds_ptrie_version_release(trie, snapshot);
  ds_ptrie_version_release(trie, current);
  ds_ptrie_destroy(trie);
  return st;
}

int main(void) {
  ds_status_t st = routes(DS_PTRIE_BYTES);
  if (st == DS_OK) {
    st = routes(DS_PTRIE_RADIX);
  }
  if (st != DS_OK) {
    fprintf(stderr, "%s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
