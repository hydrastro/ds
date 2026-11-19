/* Fallible cloning of intrusive nodes with caller-defined rollback. */
#include <dsa.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct named_item {
  char *name;
  ds_list_node_t link; /* the intrusive node need not be the first member */
} named_item_t;

static named_item_t *item_of(ds_list_node_t *node) {
  return (named_item_t *)(void *)((unsigned char *)node -
                                  offsetof(named_item_t, link));
}

static ds_list_node_t *clone_item(ds_list_node_t *node) {
  const named_item_t *source = item_of(node);
  named_item_t *copy = (named_item_t *)malloc(sizeof(*copy));
  size_t bytes = strlen(source->name) + 1;
  if (copy == NULL) {
    return NULL;
  }
  copy->name = (char *)malloc(bytes);
  if (copy->name == NULL) {
    free(copy);
    return NULL;
  }
  memcpy(copy->name, source->name, bytes);
  return &copy->link;
}

static void release_item(ds_list_node_t *node) {
  named_item_t *item = item_of(node);
  free(item->name);
  free(item);
}

int main(void) {
  char first[] = "parser";
  char second[] = "index";
  named_item_t a, b;
  ds_list_t *source = ds_list_create(NULL);
  ds_list_t *copy = NULL;
  ds_list_node_t *node;
  ds_status_t st;
  if (source == NULL) {
    fputs("Could not allocate the source list.\n", stderr);
    return 1;
  }
  a.name = first;
  b.name = second;
  ds_list_append(source, &a.link);
  ds_list_append(source, &b.link);
  st = ds_list_clone_ex(source, clone_item, release_item, &copy);
  ds_list_destroy(source,
                  NULL); /* source nodes and strings are on the stack */
  if (st != DS_OK) {
    fprintf(stderr, "Cloning failed (%d); completed clones were released.\n",
            (int)st);
    return 1;
  }
  for (node = copy->head; node != copy->nil; node = node->next) {
    printf("Owned clone: %s\n", item_of(node)->name);
  }
  ds_list_destroy(copy, release_item);
  return 0;
}
