#include "../lib/aho_corasick.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int total_matches;
static int saw_he;
static int saw_she;
static int saw_his;
static int saw_hers;

static void on_match(const ds_aho_corasick_match_t *match, void *user) {
  int *ids = (int *)user;
  total_matches += 1;
  if (match->pattern_id == ids[0]) {
    saw_he += 1;
  } else if (match->pattern_id == ids[1]) {
    saw_she += 1;
  } else if (match->pattern_id == ids[2]) {
    saw_his += 1;
  } else if (match->pattern_id == ids[3]) {
    saw_hers += 1;
  }
}

int main(void) {
  ds_aho_corasick_t *ac = ds_aho_corasick_create();
  const char *text = "ushers";
  int ids[4];
  size_t count;

  /* classic Aho-Corasick example patterns */
  ids[0] = ds_aho_corasick_add(ac, "he", 2);
  ids[1] = ds_aho_corasick_add(ac, "she", 3);
  ids[2] = ds_aho_corasick_add(ac, "his", 3);
  ids[3] = ds_aho_corasick_add(ac, "hers", 4);
  assert(ds_aho_corasick_pattern_count(ac) == 4);

  /* adding a duplicate returns the same id, count unchanged */
  assert(ds_aho_corasick_add(ac, "he", 2) == ids[0]);
  assert(ds_aho_corasick_pattern_count(ac) == 4);

  ds_aho_corasick_compile(ac);

  /* In "ushers": "she" at 1..4, "he" at 2..4, "hers" at 2..6 */
  total_matches = 0;
  saw_he = saw_she = saw_his = saw_hers = 0;
  count = ds_aho_corasick_search(ac, text, strlen(text), on_match, ids);
  printf("matches: %lu (he=%d she=%d his=%d hers=%d)\n", (unsigned long)count,
         saw_he, saw_she, saw_his, saw_hers);
  assert(saw_she == 1);
  assert(saw_he == 1);
  assert(saw_hers == 1);
  assert(saw_his == 0);
  assert(count == 3);

  /* contains_any */
  assert(ds_aho_corasick_contains_any(ac, "ushers", 6));
  assert(!ds_aho_corasick_contains_any(ac, "xyz", 3));
  assert(!ds_aho_corasick_contains_any(ac, "", 0));

  ds_aho_corasick_destroy(ac);

  /* second instance: overlapping repeats */
  ac = ds_aho_corasick_create();
  ds_aho_corasick_add(ac, "aa", 2);
  total_matches = 0;
  /* "aaaa" contains "aa" at positions 0,1,2 -> 3 matches */
  count = ds_aho_corasick_search(ac, "aaaa", 4, NULL, NULL);
  printf("aa in aaaa: %lu\n", (unsigned long)count);
  assert(count == 3);
  ds_aho_corasick_destroy(ac);

  printf("ok\n");
  return 0;
}
