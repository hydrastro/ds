/* Compose a rope editor, an Aho-Corasick matcher and a B* record index.
 * Checked edits report operational failures before publishing replacements. */
#include <dsa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct hit {
  ds_bplus_tree_node_t node;
  size_t offset;
  int pattern;
} hit_t;

typedef struct hit_index {
  ds_bplus_tree_t *tree;
  hit_t records[512];
  size_t used;
  ds_status_t status;
} hit_index_t;

static int compare_hit(ds_bplus_tree_node_t *a, ds_bplus_tree_node_t *b) {
  const hit_t *x = (const hit_t *)(void *)a;
  const hit_t *y = (const hit_t *)(void *)b;
  return (x->offset > y->offset) - (x->offset < y->offset);
}

static void add_hit(const ds_aho_corasick_match_t *match, void *user) {
  hit_index_t *index = (hit_index_t *)user;
  hit_t *hit;
  if (index->status != DS_OK) {
    return;
  }
  if (index->used == sizeof(index->records) / sizeof(index->records[0])) {
    index->status = DS_ERR_RANGE;
    return;
  }
  hit = &index->records[index->used];
  memset(hit, 0, sizeof(*hit));
  hit->offset = match->start;
  hit->pattern = match->pattern_id;
  index->status = ds_bplus_tree_insert_ex(index->tree, &hit->node);
  if (index->status == DS_OK) {
    index->used++;
  }
}

static void show_hit(ds_bplus_tree_node_t *node) {
  const hit_t *hit = (const hit_t *)(void *)node;
  printf("  byte %lu: pattern %d\n", (unsigned long)hit->offset, hit->pattern);
}

#define EDIT(call)                                                            \
  do {                                                                        \
    st = (call);                                                              \
    if (DS_FAILED(st)) {                                                      \
      goto cleanup;                                                           \
    }                                                                         \
  } while (0)

int main(void) {
  const char *line = "A rope stores text; a tree indexes matches.\n";
  ds_rope_t *rope = ds_rope_create(NULL);
  ds_rope_t *footer = ds_rope_from_string("End of file.\n", 13, NULL);
  ds_aho_corasick_t *matcher = ds_aho_corasick_create(NULL);
  hit_index_t index;
  hit_t low, high;
  ds_status_t st = DS_OK;
  char *text = NULL;
  size_t i, matches = 0;
  int id;
  memset(&index, 0, sizeof(index));
  index.tree = ds_bplus_tree_create(4, true, compare_hit, NULL);
  if (rope == NULL || footer == NULL || matcher == NULL ||
      index.tree == NULL) {
    st = DS_ERR_ALLOC;
    goto cleanup;
  }
  for (i = 0; i < 40; i++) {
    EDIT(ds_rope_append_ex(rope, line, strlen(line)));
  }
  EDIT(ds_rope_insert_ex(rope, 0, "Draft: ", 7));
  EDIT(ds_rope_delete_ex(rope, 0, 7));
  EDIT(ds_rope_concat_ex(rope, footer));
  printf("Edited text: %lu bytes, AVL height %lu; footer now %lu bytes\n",
         (unsigned long)ds_rope_length(rope), (unsigned long)rope->root->depth,
         (unsigned long)ds_rope_length(footer));
  text = ds_rope_to_string(rope);
  if (text == NULL) {
    st = DS_ERR_ALLOC;
    goto cleanup;
  }
  EDIT(ds_aho_corasick_add_ex(matcher, "rope", 4, &id));
  EDIT(ds_aho_corasick_add_ex(matcher, "tree", 4, &id));
  EDIT(ds_aho_corasick_add_ex(matcher, "text", 4, &id));
  EDIT(ds_aho_corasick_search_ex(matcher, text, ds_rope_length(rope), add_hit,
                                 &index, &matches));
  EDIT(index.status);
  printf("Matched %lu occurrences; B* range for the first 100 bytes:\n",
         (unsigned long)matches);
  memset(&low, 0, sizeof(low));
  memset(&high, 0, sizeof(high));
  high.offset = 99;
  ds_bplus_tree_range_walk(index.tree, &low.node, &high.node, show_hit);
cleanup:
  if (text != NULL) {
    ds_context_free(rope->context, text);
  }
  if (index.tree != NULL) {
    ds_bplus_tree_destroy(index.tree, NULL);
  }
  if (matcher != NULL) {
    ds_aho_corasick_destroy(matcher);
  }
  ds_rope_destroy(rope);
  ds_rope_destroy(footer);
  if (DS_FAILED(st)) {
    fprintf(stderr, "text operation failed: %d\n", (int)st);
  }
  return DS_FAILED(st) ? 1 : 0;
}
