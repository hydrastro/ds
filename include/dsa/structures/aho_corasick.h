#ifndef DS_AHO_CORASICK_H
#define DS_AHO_CORASICK_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
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
  ds_context_t *context; /* borrowed allocation owner */
} ds_aho_corasick_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_aho_corasick_create_context(ds_context_t *context,
                                           ds_aho_corasick_t **out);

ds_aho_corasick_t *ds_aho_corasick_create(ds_context_t *context);
int ds_aho_corasick_add(ds_aho_corasick_t *ac, const void *pattern,
                        size_t length);
/* DS_OK/new or DS_EXISTS/existing ID; a failed addition leaves the old
 * patterns and compiled automaton unchanged. Empty patterns are invalid. */
ds_status_t ds_aho_corasick_add_ex(ds_aho_corasick_t *ac, const void *pattern,
                                   size_t length, int *out_id);
/* Allocates the queue before changing failure/output links. */
ds_status_t ds_aho_corasick_compile_ex(ds_aho_corasick_t *ac);
void ds_aho_corasick_compile(ds_aho_corasick_t *ac);
/* Stoppable streaming search. NULL visit counts only. On callback stop/error,
 * out_count includes the stopping match. An overflow reports the delivered
 * count. Invalid arguments/compile failure leave out_count unchanged. Visitors
 * may not mutate the automaton or haystack, or retain the borrowed match. */
typedef ds_status_t (*ds_aho_corasick_visit_fn)(
    const ds_aho_corasick_match_t *match, void *user);
ds_status_t ds_aho_corasick_search_with(ds_aho_corasick_t *ac,
                                        const void *haystack, size_t length,
                                        ds_aho_corasick_visit_fn visit,
                                        void *user, size_t *out_count);
ds_status_t ds_aho_corasick_search_ex(
    ds_aho_corasick_t *ac, const void *haystack, size_t length,
    void (*callback)(const ds_aho_corasick_match_t *, void *), void *user,
    size_t *out_count);
ds_status_t ds_aho_corasick_contains_any_ex(ds_aho_corasick_t *ac,
                                            const void *haystack,
                                            size_t length, bool *out);
/* Direct wrappers return -1/0/false on failure. Use _ex to distinguish
 * absence from allocation failure. Match-count overflow may follow callbacks
 * already delivered; no callback may mutate/destroy the automaton. */
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
