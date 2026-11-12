/*
 * Element ownership in persistent structures, through one element schema.
 *
 * The elements are heap strings. With an owned schema (copy and destroy)
 * every structure that keeps an element holds its own clone and releases
 * it exactly once; versions share nodes, and a clone is made only where a
 * path is copied. A copy-only schema would clone without releasing (for
 * data owned elsewhere); a plain schema copies the pointer bytes and owns
 * nothing. Destroy without copy is rejected.
 *
 * The program counts live strings to show when clones are made and
 * released.
 */
#include "ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long live;

static bool str_copy(void *dst, const void *src, void *user) {
  const char *s = *(const char *const *)src;
  char *d = (char *)malloc(strlen(s) + 1);
  (void)user;
  if (d == NULL) {
    return false;
  }
  memcpy(d, s, strlen(s) + 1);
  *(char **)dst = d;
  live++;
  return true;
}

static void str_destroy(void *elem, void *user) {
  (void)user;
  free(*(char **)elem);
  live--;
}

int main(void) {
  ds_schema_t schema;
  ds_pvec_config_t cfg;
  ds_pvec_t v1;
  ds_pvec_t v2;
  const char *words[4] = {"alpha", "beta", "gamma", "delta"};
  int i;

  ds_schema_init(&schema, sizeof(char *));
  schema.copy = str_copy;
  schema.destroy = str_destroy;
  printf("policy: %s\n",
         ds_schema_policy(&schema) == DS_SCHEMA_OWNED ? "owned" : "other");

  if (ds_pvec_config_from_schema(&cfg, &schema) != DS_OK ||
      ds_pvec_init(&v1, &cfg) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 4; i++) {
    /* the vector clones the borrowed string it is given */
    if (ds_pvec_push_back(&v1, &words[i], &v1) != DS_OK) {
      return 1;
    }
  }
  printf("after 4 pushes: %ld live clones\n", live);

  /* a new version shares every untouched node with v1 */
  {
    const char *w = "BETA";
    if (ds_pvec_set(&v1, 1, &w, &v2) != DS_OK) {
      return 1;
    }
  }
  printf("v1[1] = %s, v2[1] = %s, %ld live clones\n",
         *(char *const *)ds_pvec_get(&v1, 1),
         *(char *const *)ds_pvec_get(&v2, 1), live);

  ds_pvec_release(&v1); /* clones only v1 reached are released */
  printf("after releasing v1: %ld live clones\n", live);
  ds_pvec_release(&v2);
  printf("after releasing v2: %ld live clones\n", live);

  /* the rejected policy */
  schema.copy = NULL;
  printf("destroy without copy: %s\n",
         ds_schema_validate(&schema) == DS_ERR_INVALID ? "rejected"
                                                       : "accepted");
  return 0;
}
