#ifndef DS_AHO_CORASICK_H
#define DS_AHO_CORASICK_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aho_corasick_node {
  struct aho_corasick_node *children[256];
  struct aho_corasick_node *fail;
  struct aho_corasick_node *output; /* next node in output chain (a match) */
  int pattern_id;                   /* >= 0 if a pattern ends here */
  size_t pattern_length;
} ds_aho_corasick_node_t;

typedef struct aho_corasick_match {
  int pattern_id;
  size_t start; /* index in haystack where the match begins */
  size_t end;   /* index one past the last matched byte */
} ds_aho_corasick_match_t;

typedef struct aho_corasick {
  ds_aho_corasick_node_t *root;
  size_t num_patterns;
  size_t num_nodes;
  bool compiled;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_aho_corasick_t;

ds_aho_corasick_t *ds_aho_corasick_create(void);
ds_aho_corasick_t *ds_aho_corasick_create_alloc(void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
int ds_aho_corasick_add(ds_aho_corasick_t *ac, const void *pattern,
                        size_t length);
void ds_aho_corasick_compile(ds_aho_corasick_t *ac);
size_t ds_aho_corasick_search(
    ds_aho_corasick_t *ac, const void *haystack, size_t length,
    void (*callback)(const ds_aho_corasick_match_t *, void *), void *user);
bool ds_aho_corasick_contains_any(ds_aho_corasick_t *ac, const void *haystack,
                                  size_t length);
size_t ds_aho_corasick_pattern_count(ds_aho_corasick_t *ac);
void ds_aho_corasick_destroy(ds_aho_corasick_t *ac);

#ifdef __cplusplus
}
#endif

#endif /* DS_AHO_CORASICK_H */
