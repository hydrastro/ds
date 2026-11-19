/* Containers can share an explicit allocation owner without globals.
 */
#include <dsa.h>
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
  if (p != NULL) {
    owner->allocations++;
  }
  return p;
}
static void owned_free(void *p, void *user) {
  allocation_owner_t *owner = (allocation_owner_t *)user;
  if (p != NULL) {
    owner->releases++;
  }
  free(p);
}
static void *owned_calloc(size_t count, size_t size, void *user) {
  void *p;
  if (size != 0 && count > (size_t)-1 / size) {
    return NULL;
  }
  p = owned_alloc(count * size, user);
  if (p != NULL) {
    memset(p, 0, count * size);
  }
  return p;
}
static void *owned_realloc(void *p, size_t size, void *user) {
  if (p == NULL) {
    return owned_alloc(size, user);
  }
  if (size == 0) {
    owned_free(p, user);
    return NULL;
  }
  return realloc(p, size);
}
typedef struct event {
  ds_list_node_t link;
  int id;
} event_t;
static ds_status_t copy_event(ds_list_node_t *node, ds_list_node_t **out,
                              void *user) {
  event_t *copy =
      (event_t *)ds_context_alloc((ds_context_t *)user, sizeof(event_t));
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  *copy = *(event_t *)(void *)node;
  *out = &copy->link;
  return DS_OK;
}
static void drop_event(ds_list_node_t *node, void *user) {
  ds_context_free((ds_context_t *)user, node);
}
static ds_status_t byte_slice(void *data, size_t index, size_t *slice,
                              void *user) {
  const unsigned char *text = (const unsigned char *)data;
  (void)user;
  if (index >= strlen((const char *)text)) {
    return DS_STOP;
  }
  *slice = text[index];
  return DS_OK;
}
static ds_status_t use_containers(ds_context_t *context) {
  size_t sequence[] = {3, 1, 3, 2, 1};
  ds_wavelet_tree_t *wavelet = NULL;
  ds_trie_t *words = NULL, *sorted_words = NULL;
  ds_trie_config_t trie_config;
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
  wavelet = ds_wavelet_tree_create(sequence, 5, 4, context);
  if (wavelet == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  printf("Wavelet rank of 3: %lu\n",
         (unsigned long)ds_wavelet_tree_rank(wavelet, 3, 5));
  ds_trie_config_init(&trie_config);
  trie_config.context = context;
  trie_config.store_ops = ds_trie_rh_map_store_ops();
  st = ds_trie_create_config_ex(&trie_config, &words);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_trie_insert_with(words, (void *)"owner", byte_slice, NULL, NULL);
  if (st != DS_OK) {
    goto done;
  }
  trie_config.store_ops = ds_trie_sorted_vec_store_ops();
  st =
      ds_trie_clone_with(words, NULL, NULL, NULL, &trie_config, &sorted_words);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_list_create_context(context, &events);
  if (st != DS_OK) {
    goto done;
  }
  event.id = 7;
  ds_list_append(events, &event.link);
  st = ds_list_clone_with(events, copy_event, drop_event, context, NULL,
                          &event_copy);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_hamt_create_context(context, &names);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_art_create_context(context, &routes);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_radix_trie_create_context(context, &prefixes);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rope_create_context(context, &text);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rope_create_context(context, &tail);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_bst_create_context(context, &ordered);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_avl_create_context(context, &balanced);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rbt_create_context(context, &colored);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_hamt_insert_ex(names, "route", 5, (void *)value);
  if (st != DS_OK) {
    goto done;
  }
  st =
      ds_art_insert_ex(routes, (const unsigned char *)"long/shared/prefix/one",
                       22, (void *)value);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_radix_trie_insert_ex(prefixes, "long/shared/prefix/two", 22,
                               (void *)value);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rope_append_ex(text, "owned by ", 9);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rope_append_ex(tail, "the selected context", 20);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rope_concat_ex(text, tail);
  if (st != DS_OK) {
    goto done;
  }
  rendered = ds_rope_to_string(text);
  if (rendered == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  puts(rendered);
  ds_context_free(context, rendered);
done:
  ds_wavelet_tree_destroy(wavelet);
  ds_trie_destroy(words);
  ds_trie_destroy(sorted_words);
  if (events != NULL) {
    ds_list_destroy(events, NULL);
  }
  if (event_copy != NULL) {
    ds_list_destroy_with(event_copy, drop_event, context);
  }
  if (names != NULL) {
    ds_hamt_destroy(names, NULL);
  }
  if (routes != NULL) {
    ds_art_destroy(routes, NULL);
  }
  if (prefixes != NULL) {
    ds_radix_trie_destroy(prefixes, NULL);
  }
  if (text != NULL) {
    ds_rope_destroy(text);
  }
  if (tail != NULL) {
    ds_rope_destroy(tail);
  }
  if (ordered != NULL) {
    ds_bst_destroy_tree(ordered, NULL);
  }
  if (balanced != NULL) {
    ds_avl_destroy_tree(balanced, NULL);
  }
  if (colored != NULL) {
    ds_rbt_destroy_tree(colored, NULL);
  }
  return st;
}
int main(void) {
  allocation_owner_t owner = {"request", 0, 0};
  ds_context_t context;
  ds_status_t st;
  ds_context_init(&context);
  context.alloc = owned_alloc;
  context.calloc = owned_calloc;
  context.realloc = owned_realloc;
  context.free = owned_free;
  context.user = &owner;
  st = use_containers(&context);
  printf("%s owner: %lu allocations, %lu releases\n", owner.name,
         (unsigned long)owner.allocations, (unsigned long)owner.releases);
  if (st != DS_OK) {
    fprintf(stderr, "Container operation failed: %s\n", ds_status_name(st));
    return 1;
  }
  return owner.allocations != owner.releases;
}
