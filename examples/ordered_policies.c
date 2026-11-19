/* Owning clones inherit a caller-selected comparison policy. */
#include <dsa/structures/bplus_tree.h>
#include <dsa/structures/order_statistic_tree.h>
#include <dsa/structures/pairing_heap.h>
#include <dsa/structures/skip_list.h>
#include <dsa/structures/splay_tree.h>
#include <stdio.h>
#include <stdlib.h>

struct selection {
  int direction;
  size_t budget;
  size_t released;
};
struct splay_tree_record {
  ds_splay_tree_node_t node;
  int key;
};
static int splay_tree_compare(ds_splay_tree_node_t *a, ds_splay_tree_node_t *b,
                              void *user) {
  int x = ((struct splay_tree_record *)a)->key,
      y = ((struct splay_tree_record *)b)->key;
  return ((struct selection *)user)->direction * ((x > y) - (x < y));
}
static ds_status_t splay_tree_visit(ds_splay_tree_node_t *node, void *user) {
  struct selection *selection = (struct selection *)user;
  printf(" %d", ((struct splay_tree_record *)node)->key);
  if (selection->budget != 0 && --selection->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t splay_tree_copy(ds_splay_tree_node_t *node,
                                   ds_splay_tree_node_t **out, void *user) {
  struct splay_tree_record *copy =
      (struct splay_tree_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->key = ((struct splay_tree_record *)node)->key;

  *out = &copy->node;
  return DS_OK;
}
static void splay_tree_release(ds_splay_tree_node_t *node, void *user) {
  ((struct selection *)user)->released += 1;
  free(node);
}
static int show_splay_tree(void) {
  ds_splay_tree_t *original = NULL, *copy = NULL;
  struct splay_tree_record records[5];
  struct selection selection = {-1, 0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;
  if (ds_splay_tree_create_context(splay_tree_compare, &selection, NULL,
                                   &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; ++i) {
    records[i].key = (int)i + 1;

    if (ds_splay_tree_insert_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  if (ds_splay_tree_clone_with(original, splay_tree_copy, splay_tree_release,
                               &selection, NULL, &copy) != DS_OK) {
    goto done;
  }
  printf("splay_tree descending policy:");
  (void)ds_splay_tree_walk_with(original, splay_tree_visit, &selection);
  printf("; first two clone visits:");
  selection.budget = 2;
  st = ds_splay_tree_walk_with(copy, splay_tree_visit, &selection);
  printf(" (%s)", ds_status_name(st));
  rc = 0;
done:
  ds_splay_tree_destroy_with(copy, splay_tree_release, &selection);
  ds_splay_tree_destroy(original, NULL);
  printf("; released %lu clones\n", (unsigned long)selection.released);
  return rc;
}
struct skip_list_record {
  ds_skip_list_node_t node;
  int key;
};
static int skip_list_compare(ds_skip_list_node_t *a, ds_skip_list_node_t *b,
                             void *user) {
  int x = ((struct skip_list_record *)a)->key,
      y = ((struct skip_list_record *)b)->key;
  return ((struct selection *)user)->direction * ((x > y) - (x < y));
}
static ds_status_t skip_list_visit(ds_skip_list_node_t *node, void *user) {
  struct selection *selection = (struct selection *)user;
  printf(" %d", ((struct skip_list_record *)node)->key);
  if (selection->budget != 0 && --selection->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t skip_list_copy(ds_skip_list_node_t *node,
                                  ds_skip_list_node_t **out, void *user) {
  struct skip_list_record *copy =
      (struct skip_list_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->key = ((struct skip_list_record *)node)->key;
  ds_skip_list_node_init(&copy->node);
  *out = &copy->node;
  return DS_OK;
}
static void skip_list_release(ds_skip_list_node_t *node, void *user) {
  ((struct selection *)user)->released += 1;
  free(node);
}
static int show_skip_list(void) {
  ds_skip_list_t *original = NULL, *copy = NULL;
  struct skip_list_record records[5];
  struct selection selection = {-1, 0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;
  if (ds_skip_list_create_context(0, 123UL, skip_list_compare, &selection,
                                  NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; ++i) {
    records[i].key = (int)i + 1;
    ds_skip_list_node_init(&records[i].node);
    if (ds_skip_list_insert_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  if (ds_skip_list_clone_with(original, skip_list_copy, skip_list_release,
                              &selection, NULL, &copy) != DS_OK) {
    goto done;
  }
  printf("skip_list descending policy:");
  (void)ds_skip_list_walk_with(original, skip_list_visit, &selection);
  printf("; first two clone visits:");
  selection.budget = 2;
  st = ds_skip_list_walk_with(copy, skip_list_visit, &selection);
  printf(" (%s)", ds_status_name(st));
  rc = 0;
done:
  ds_skip_list_destroy_with(copy, skip_list_release, &selection);
  ds_skip_list_destroy(original, NULL);
  printf("; released %lu clones\n", (unsigned long)selection.released);
  return rc;
}
struct bplus_tree_record {
  ds_bplus_tree_node_t node;
  int key;
};
static int bplus_tree_compare(ds_bplus_tree_node_t *a, ds_bplus_tree_node_t *b,
                              void *user) {
  int x = ((struct bplus_tree_record *)a)->key,
      y = ((struct bplus_tree_record *)b)->key;
  return ((struct selection *)user)->direction * ((x > y) - (x < y));
}
static ds_status_t bplus_tree_visit(ds_bplus_tree_node_t *node, void *user) {
  struct selection *selection = (struct selection *)user;
  printf(" %d", ((struct bplus_tree_record *)node)->key);
  if (selection->budget != 0 && --selection->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t bplus_tree_copy(ds_bplus_tree_node_t *node,
                                   ds_bplus_tree_node_t **out, void *user) {
  struct bplus_tree_record *copy =
      (struct bplus_tree_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->key = ((struct bplus_tree_record *)node)->key;

  *out = &copy->node;
  return DS_OK;
}
static void bplus_tree_release(ds_bplus_tree_node_t *node, void *user) {
  ((struct selection *)user)->released += 1;
  free(node);
}
static int show_bplus_tree(void) {
  ds_bplus_tree_t *original = NULL, *copy = NULL;
  struct bplus_tree_record records[5];
  struct selection selection = {-1, 0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;
  if (ds_bplus_tree_create_context(4, true, bplus_tree_compare, &selection,
                                   NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; ++i) {
    records[i].key = (int)i + 1;

    if (ds_bplus_tree_insert_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  if (ds_bplus_tree_clone_with(original, bplus_tree_copy, bplus_tree_release,
                               &selection, NULL, &copy) != DS_OK) {
    goto done;
  }
  printf("bplus_tree descending policy:");
  (void)ds_bplus_tree_walk_with(original, bplus_tree_visit, &selection);
  printf("; first two clone visits:");
  selection.budget = 2;
  st = ds_bplus_tree_walk_with(copy, bplus_tree_visit, &selection);
  printf(" (%s)", ds_status_name(st));
  rc = 0;
done:
  ds_bplus_tree_destroy_with(copy, bplus_tree_release, &selection);
  ds_bplus_tree_destroy(original, NULL);
  printf("; released %lu clones\n", (unsigned long)selection.released);
  return rc;
}
struct ost_record {
  ds_ost_node_t node;
  int key;
};
static int ost_compare(ds_ost_node_t *a, ds_ost_node_t *b, void *user) {
  int x = ((struct ost_record *)a)->key, y = ((struct ost_record *)b)->key;
  return ((struct selection *)user)->direction * ((x > y) - (x < y));
}
static ds_status_t ost_visit(ds_ost_node_t *node, void *user) {
  struct selection *selection = (struct selection *)user;
  printf(" %d", ((struct ost_record *)node)->key);
  if (selection->budget != 0 && --selection->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t ost_copy(ds_ost_node_t *node, ds_ost_node_t **out,
                            void *user) {
  struct ost_record *copy = (struct ost_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->key = ((struct ost_record *)node)->key;

  *out = &copy->node;
  return DS_OK;
}
static void ost_release(ds_ost_node_t *node, void *user) {
  ((struct selection *)user)->released += 1;
  free(node);
}
static int show_ost(void) {
  ds_ost_t *original = NULL, *copy = NULL;
  struct ost_record records[5];
  struct selection selection = {-1, 0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;
  if (ds_ost_create_context(ost_compare, &selection, NULL, &original) !=
      DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; ++i) {
    records[i].key = (int)i + 1;

    if (ds_ost_insert_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  if (ds_ost_clone_with(original, ost_copy, ost_release, &selection, NULL,
                        &copy) != DS_OK) {
    goto done;
  }
  printf("ost descending policy:");
  (void)ds_ost_walk_with(original, ost_visit, &selection);
  printf("; first two clone visits:");
  selection.budget = 2;
  st = ds_ost_walk_with(copy, ost_visit, &selection);
  printf(" (%s)", ds_status_name(st));
  rc = 0;
done:
  ds_ost_destroy_with(copy, ost_release, &selection);
  ds_ost_destroy(original, NULL);
  printf("; released %lu clones\n", (unsigned long)selection.released);
  return rc;
}
struct pairing_heap_record {
  ds_pairing_heap_node_t node;
  int key;
};
static int pairing_heap_compare(ds_pairing_heap_node_t *a,
                                ds_pairing_heap_node_t *b, void *user) {
  int x = ((struct pairing_heap_record *)a)->key,
      y = ((struct pairing_heap_record *)b)->key;
  return ((struct selection *)user)->direction * ((x > y) - (x < y));
}
static ds_status_t pairing_heap_visit(ds_pairing_heap_node_t *node,
                                      void *user) {
  struct selection *selection = (struct selection *)user;
  printf(" %d", ((struct pairing_heap_record *)node)->key);
  if (selection->budget != 0 && --selection->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t pairing_heap_copy(ds_pairing_heap_node_t *node,
                                     ds_pairing_heap_node_t **out,
                                     void *user) {
  struct pairing_heap_record *copy =
      (struct pairing_heap_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->key = ((struct pairing_heap_record *)node)->key;

  *out = &copy->node;
  return DS_OK;
}
static void pairing_heap_release(ds_pairing_heap_node_t *node, void *user) {
  ((struct selection *)user)->released += 1;
  free(node);
}
static int show_pairing_heap(void) {
  ds_pairing_heap_t *original = NULL, *copy = NULL;
  struct pairing_heap_record records[5];
  struct selection selection = {-1, 0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;
  if (ds_pairing_heap_create_context(pairing_heap_compare, &selection, NULL,
                                     &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; ++i) {
    records[i].key = (int)i + 1;

    if (ds_pairing_heap_insert_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  if (ds_pairing_heap_clone_with(original, pairing_heap_copy,
                                 pairing_heap_release, &selection, NULL,
                                 &copy) != DS_OK) {
    goto done;
  }
  printf("pairing_heap descending policy:");
  (void)ds_pairing_heap_walk_with(original, pairing_heap_visit, &selection);
  printf("; first two clone visits:");
  selection.budget = 2;
  st = ds_pairing_heap_walk_with(copy, pairing_heap_visit, &selection);
  printf(" (%s)", ds_status_name(st));
  rc = 0;
done:
  ds_pairing_heap_destroy_with(copy, pairing_heap_release, &selection);
  ds_pairing_heap_destroy(original, NULL);
  printf("; released %lu clones\n", (unsigned long)selection.released);
  return rc;
}
int main(void) {
  if (show_splay_tree() != 0) {
    return 1;
  }
  if (show_skip_list() != 0) {
    return 1;
  }
  if (show_bplus_tree() != 0) {
    return 1;
  }
  if (show_ost() != 0) {
    return 1;
  }
  if (show_pairing_heap() != 0) {
    return 1;
  }
  return 0;
}
