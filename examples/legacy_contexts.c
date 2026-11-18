/* Original containers can share an explicit allocation owner without globals.
 */
#include "ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_owner {
  const char *name;
  size_t allocations, releases;
} allocation_owner_t;
static void *owned_alloc(size_t size, void *user) {
  allocation_owner_t *owner = (allocation_owner_t *)user;
  void *p = malloc(size);
  if (p != NULL)
    owner->allocations++;
  return p;
}
static void owned_free(void *p, void *user) {
  allocation_owner_t *owner = (allocation_owner_t *)user;
  if (p != NULL)
    owner->releases++;
  free(p);
}
typedef struct event {
  ds_list_node_t link;
  int id;
} event_t;
static ds_list_node_t *copy_event(ds_list_node_t *node) {
  event_t *copy = (event_t *)malloc(sizeof(event_t));
  if (copy == NULL)
    return NULL;
  *copy = *(event_t *)(void *)node;
  return &copy->link;
}
static void drop_event(ds_list_node_t *node) { free(node); }
static ds_status_t use_containers(ds_context_t *context) {
  ds_list_t *events = NULL, *event_copy = NULL;
  ds_hamt_t *names = NULL;
  ds_art_t *routes = NULL;
  ds_radix_trie_t *prefixes = NULL;
  ds_rope_t *text = NULL, *tail = NULL;
  ds_bst_t *ordered = NULL;
  ds_avl_t *balanced = NULL;
  ds_rbt_t *colored = NULL;
  ds_status_t st;
  const char *value = "retained by application";
  char *rendered;
  event_t event;
  st = ds_list_create_context(context, &events);
  if (st != DS_OK)
    goto done;
  event.id = 7;
  ds_list_append(events, &event.link);
  st = ds_list_clone_ex(events, copy_event, drop_event, &event_copy);
  if (st != DS_OK)
    goto done;
  st = ds_hamt_create_context(context, &names);
  if (st != DS_OK)
    goto done;
  st = ds_art_create_context(context, &routes);
  if (st != DS_OK)
    goto done;
  st = ds_radix_trie_create_context(context, &prefixes);
  if (st != DS_OK)
    goto done;
  st = ds_rope_create_context(context, &text);
  if (st != DS_OK)
    goto done;
  st = ds_rope_create_context(context, &tail);
  if (st != DS_OK)
    goto done;
  st = ds_bst_create_context(context, &ordered);
  if (st != DS_OK)
    goto done;
  st = ds_avl_create_context(context, &balanced);
  if (st != DS_OK)
    goto done;
  st = ds_rbt_create_context(context, &colored);
  if (st != DS_OK)
    goto done;
  st = ds_hamt_insert_ex(names, "route", 5, (void *)value);
  if (st != DS_OK)
    goto done;
  st = ds_art_insert_ex(routes, (const unsigned char *)"long/shared/prefix/one",
                        22, (void *)value);
  if (st != DS_OK)
    goto done;
  st = ds_radix_trie_insert_ex(prefixes, "long/shared/prefix/two", 22,
                               (void *)value);
  if (st != DS_OK)
    goto done;
  st = ds_rope_append_ex(text, "owned by ", 9);
  if (st != DS_OK)
    goto done;
  st = ds_rope_append_ex(tail, "the selected context", 20);
  if (st != DS_OK)
    goto done;
  st = ds_rope_concat_ex(text, tail);
  if (st != DS_OK)
    goto done;
  rendered = ds_rope_to_string(text);
  if (rendered == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  puts(rendered);
  ds_context_free(context, rendered);
done:
  if (events != NULL)
    ds_list_destroy(events, NULL);
  if (event_copy != NULL)
    ds_list_destroy(event_copy, drop_event);
  if (names != NULL)
    ds_hamt_destroy(names, NULL);
  if (routes != NULL)
    ds_art_destroy(routes, NULL);
  if (prefixes != NULL)
    ds_radix_trie_destroy(prefixes, NULL);
  if (text != NULL)
    ds_rope_destroy(text);
  if (tail != NULL)
    ds_rope_destroy(tail);
  if (ordered != NULL)
    ds_bst_destroy_tree(ordered, NULL);
  if (balanced != NULL)
    ds_avl_destroy_tree(balanced, NULL);
  if (colored != NULL)
    ds_rbt_destroy_tree(colored, NULL);
  return st;
}
int main(void) {
  allocation_owner_t owner = {"request", 0, 0};
  ds_context_t context;
  ds_status_t st;
  ds_context_init(&context);
  context.alloc = owned_alloc;
  context.free = owned_free;
  context.user = &owner;
  st = use_containers(&context);
  printf("%s owner: %lu allocations, %lu releases\n", owner.name,
         (unsigned long)owner.allocations, (unsigned long)owner.releases);
  if (st != DS_OK) {
    fprintf(stderr, "Container operation failed: %s\n", ds_status_name(st));
    return 1;
  }
  return 0;
}
