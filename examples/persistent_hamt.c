#include "ds.h"
#include <stdio.h>
#include <string.h>

static size_t hash_str(const void *key, void *user) {
  const unsigned char *s = (const unsigned char *)key;
  size_t h = 5381;
  (void)user;
  while (*s != '\0') {
    h = h * 33U + *s++;
  }
  return h;
}

static bool equal_str(const void *a, const void *b, void *user) {
  (void)user;
  return strcmp((const char *)a, (const char *)b) == 0;
}

static void show(const char *name, const ds_phamt_t *map) {
  void *value;
  printf("%s: size=%lu", name, (unsigned long)ds_phamt_size(map));
  if (ds_phamt_get(map, "pi", &value) == DS_OK) {
    printf(" pi=%s", (const char *)value);
  }
  putchar('\n');
}

int main(void) {
  ds_phamt_config_t config;
  ds_phamt_t v0, v1, v2;

  ds_phamt_config_init(&config);
  config.hash = hash_str;
  config.equal = equal_str;

  /* building a private map: same handle in and out edits in place */
  ds_phamt_init(&v0, &config);
  ds_phamt_put(&v0, "e", "2.718", &v0);
  ds_phamt_put(&v0, "pi", "3.14", &v0);

  /* new versions share every untouched node with v0 */
  ds_phamt_put(&v0, "pi", "3.14159", &v1);
  ds_phamt_remove(&v1, "e", &v2);

  show("v0", &v0);
  show("v1", &v1);
  show("v2", &v2);
  printf("v0 == v1: %s\n",
         ds_phamt_equals(&v0, &v1, NULL, NULL) ? "yes" : "no");

  ds_phamt_release(&v2);
  ds_phamt_release(&v1);
  ds_phamt_release(&v0);
  return 0;
}
