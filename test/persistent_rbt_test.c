#include "../lib/persistent_rbt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp_long(void *a, void *b, void *user) {
  long x = (long)(size_t)a;
  long y = (long)(size_t)b;
  (void)user;
  if (x < y)
    return -1;
  if (x > y)
    return 1;
  return 0;
}

#define KEY(n) ((void *)(size_t)(long)(n))

static int visit_count;
static long visit_prev;
static int visit_sorted;

static bool count_sorted(void *key, void *value, void *user) {
  long k = (long)(size_t)key;
  (void)value;
  (void)user;
  if (visit_count > 0 && k <= visit_prev) {
    visit_sorted = 0;
  }
  visit_prev = k;
  visit_count += 1;
  return true;
}

int main(void) {
  ds_prbt_config_t cfg;
  ds_prbt_t *tree;
  ds_prbt_version_t *versions[64];
  int nver = 0;
  int i;
  ds_status_t s;
  void *out = NULL;

  ds_prbt_config_init(&cfg);
  cfg.compare = cmp_long;
  tree = ds_prbt_create(&cfg);
  assert(tree != NULL);

  /* version 0 is the empty root */
  versions[nver++] = ds_prbt_root(tree);
  assert(ds_prbt_size(versions[0]) == 0);

  /* each insert returns a NEW version; keep them all */
  for (i = 1; i <= 30; ++i) {
    ds_prbt_version_t *v =
        ds_prbt_insert(tree, versions[nver - 1], KEY(i * 3 % 31), NULL);
    assert(v != NULL);
    versions[nver++] = v;
    /* every version must satisfy red-black invariants */
    s = ds_prbt_validate(tree, v);
    assert(s == DS_OK);
  }
  printf("built %d versions, all RB-valid\n", nver);

  /* PERSISTENCE: the empty version is still empty; early versions keep
   * their old sizes regardless of later inserts */
  assert(ds_prbt_size(versions[0]) == 0);
  for (i = 1; i < nver; ++i) {
    assert(ds_prbt_size(versions[i]) == (size_t)i);
  }
  printf("persistence of sizes verified across versions\n");

  /* a key inserted at version k is absent in version k-1, present from k on */
  {
    ds_prbt_version_t *base = ds_prbt_root(tree);
    ds_prbt_version_t *v1 = ds_prbt_insert(tree, base, KEY(1000), KEY(7));
    ds_prbt_version_t *v2 = ds_prbt_insert(tree, v1, KEY(2000), KEY(9));
    /* v1 has 1000 but not 2000 */
    assert(ds_prbt_get(tree, v1, KEY(1000), &out) == DS_OK);
    assert(ds_prbt_get(tree, v1, KEY(2000), &out) == DS_NOT_FOUND);
    /* v2 has both */
    assert(ds_prbt_get(tree, v2, KEY(1000), &out) == DS_OK);
    assert(ds_prbt_get(tree, v2, KEY(2000), &out) == DS_OK);
    /* base still empty of both */
    assert(ds_prbt_get(tree, base, KEY(1000), &out) == DS_NOT_FOUND);
    printf("structural sharing / version isolation verified\n");
    ds_prbt_version_release(tree, v1);
    ds_prbt_version_release(tree, v2);
  }

  /* in-order visit of the latest full version is sorted */
  visit_count = 0;
  visit_sorted = 1;
  s = ds_prbt_visit(tree, versions[nver - 1], count_sorted, NULL);
  assert(s == DS_OK);
  assert(visit_sorted == 1);
  printf("in-order visit sorted, %d keys\n", visit_count);

  /* rank/select round-trip on the latest version */
  {
    ds_prbt_version_t *v = versions[nver - 1];
    size_t sz = ds_prbt_size(v);
    size_t r;
    void *k = NULL;
    for (r = 0; r < sz; ++r) {
      void *selected = NULL;
      assert(ds_prbt_select(tree, v, r, &k, &selected) == DS_OK);
      {
        size_t back = 0;
        assert(ds_prbt_rank(tree, v, k, &back) == DS_OK);
        assert(back == r);
      }
    }
    printf("rank/select round-trip verified over %lu keys\n",
           (unsigned long)sz);
  }

  /* remove on a version yields a new smaller version; old one unchanged */
  {
    ds_prbt_version_t *full = versions[nver - 1];
    size_t before = ds_prbt_size(full);
    void *probe = NULL;
    void *first = NULL;
    assert(ds_prbt_select(tree, full, 0, &first, &probe) == DS_OK);
    {
      ds_prbt_version_t *removed = ds_prbt_remove(tree, full, first);
      assert(removed != NULL);
      assert(ds_prbt_validate(tree, removed) == DS_OK);
      assert(ds_prbt_size(removed) == before - 1);
      /* original version retains the key and size */
      assert(ds_prbt_size(full) == before);
      assert(ds_prbt_get(tree, full, first, &out) == DS_OK);
      assert(ds_prbt_get(tree, removed, first, &out) == DS_NOT_FOUND);
      ds_prbt_version_release(tree, removed);
    }
    printf("remove produces new version, original immutable\n");
  }

  /* floor/ceiling on the latest version */
  {
    ds_prbt_version_t *v = versions[nver - 1];
    void *fk = NULL;
    void *fv = NULL;
    /* ceiling of a small key is the minimum; floor of a huge key is maximum */
    assert(ds_prbt_ceiling(tree, v, KEY(-1), &fk, &fv) == DS_OK);
    assert(ds_prbt_floor(tree, v, KEY(100000), &fk, &fv) == DS_OK);
    printf("floor/ceiling: ok\n");
  }

  /* the caller owns a reference to every version returned by insert/remove;
   * release them before destroying the tree (version 0 is the tree root, which
   * destroy releases itself) */
  for (i = 1; i < nver; ++i) {
    ds_prbt_version_release(tree, versions[i]);
  }

  ds_prbt_destroy(tree);
  printf("ok\n");
  return 0;
}
