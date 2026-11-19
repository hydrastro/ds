/* Copy a trie between child stores; clone nullable values in a hash table. */
#include <dsa/structures/fibonacci_heap.h>
#include <dsa/structures/hash_table.h>
#include <dsa/structures/trie.h>
#include <dsa/structures/trie_stores.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct word {
  const char *text;
};
struct policy {
  size_t budget, released;
  int direction;
};
static ds_status_t word_slice(void *data, size_t index, size_t *slice,
                              void *user) {
  const char *text = data != NULL ? ((struct word *)data)->text : "";
  unsigned char ch;
  (void)user;
  if (index >= strlen(text)) {
    return DS_STOP;
  }
  ch = (unsigned char)text[index];
  if (ch >= 'A' && ch <= 'Z') {
    ch = (unsigned char)(ch - 'A' + 'a');
  }
  *slice = ch;
  return DS_OK;
}
static ds_status_t copy_word(void *data, void **out, void *user) {
  struct word *copy;
  (void)user;
  if (data == NULL) {
    *out = NULL;
    return DS_OK;
  }
  copy = (struct word *)malloc(sizeof(*copy));
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  *copy = *(struct word *)data;
  *out = copy;
  return DS_OK;
}
static void release_payload(void *data, void *user) {
  ((struct policy *)user)->released += 1;
  free(data);
}
static ds_status_t show_word(ds_trie_t *trie, ds_trie_node_t *node,
                             void *user) {
  struct policy *policy = (struct policy *)user;
  (void)trie;
  if (node->is_terminal) {
    printf(" %s", node->terminal_data != NULL
                      ? ((struct word *)node->terminal_data)->text
                      : "<empty/null>");
    if (policy->budget != 0 && --policy->budget == 0) {
      return DS_STOP;
    }
  }
  return DS_OK;
}
static size_t hash_text(void *key, void *user) {
  (void)user;
  return ds_hash_func_string_djb2(key);
}
static int compare_text(void *a, void *b, void *user) {
  (void)user;
  return strcmp((const char *)a, (const char *)b);
}
static ds_status_t copy_number(void *data, void **out, void *user) {
  int *number;
  (void)user;
  if (data == NULL) {
    *out = NULL;
    return DS_OK;
  }
  number = (int *)malloc(sizeof(*number));
  if (number == NULL) {
    return DS_ERR_ALLOC;
  }
  *number = *(int *)data;
  *out = number;
  return DS_OK;
}
static ds_status_t show_entry(ds_hash_node_t *node, void *user) {
  (void)user;
  printf(" %s=", (char *)node->key);
  if (node->value == NULL) {
    printf("NULL");
  } else {
    printf("%d", *(int *)node->value);
  }
  return DS_OK;
}
struct job {
  ds_fibonacci_heap_node_t node;
  int priority;
};
static int compare_job(ds_fibonacci_heap_node_t *a,
                       ds_fibonacci_heap_node_t *b, void *user) {
  int x = ((struct job *)a)->priority, y = ((struct job *)b)->priority;
  return ((struct policy *)user)->direction * ((x > y) - (x < y));
}
static ds_status_t copy_job(ds_fibonacci_heap_node_t *node,
                            ds_fibonacci_heap_node_t **out, void *user) {
  struct job *copy = (struct job *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->priority = ((struct job *)node)->priority;
  *out = &copy->node;
  return DS_OK;
}
static void release_job(ds_fibonacci_heap_node_t *node, void *user) {
  release_payload(node, user);
}
static ds_status_t show_job(ds_fibonacci_heap_node_t *node, void *user) {
  (void)user;
  printf(" %d", ((struct job *)node)->priority);
  return DS_OK;
}
int main(void) {
  ds_trie_config_t trie_config;
  ds_hash_table_config_t map_config;
  ds_trie_t *trie = NULL, *trie_copy = NULL;
  ds_hash_table_t *map = NULL, *map_copy = NULL;
  ds_fibonacci_heap_t *heap = NULL, *other = NULL, *heap_copy = NULL;
  ds_fibonacci_heap_node_t *extracted;
  struct word words[2] = {{"Maps"}, {"Math"}};
  struct policy policy = {0, 0, 1};
  struct job jobs[5];
  int *number = NULL;
  size_t i;
  int rc = 1;
  ds_status_t status;
  ds_trie_config_init(&trie_config);
  trie_config.store_ops = ds_trie_rh_map_store_ops();
  if (ds_trie_create_config_ex(&trie_config, &trie) != DS_OK ||
      ds_trie_insert_with(trie, &words[0], word_slice, NULL, NULL) != DS_OK ||
      ds_trie_insert_with(trie, &words[1], word_slice, NULL, NULL) != DS_OK ||
      ds_trie_insert_with(trie, NULL, word_slice, NULL, NULL) != DS_OK) {
    goto done;
  }
  trie_config.store_ops = ds_trie_sorted_vec_store_ops();
  if (ds_trie_clone_with(trie, copy_word, release_payload, &policy,
                         &trie_config, &trie_copy) != DS_OK) {
    goto done;
  }
  printf("Sorted-store clone:");
  (void)ds_trie_walk_with(trie_copy, trie_copy->root, false, show_word,
                          &policy);
  printf("\n");
  ds_hash_table_config_init(&map_config);
  map_config.hash = hash_text;
  map_config.compare = compare_text;
  map_config.clone_value_ex = copy_number;
  map_config.destroy_value = release_payload;
  map_config.user = &policy;
  map_config.mode = HASH_LINEAR_PROBING;
  number = (int *)malloc(sizeof(*number));
  if (number == NULL) {
    goto done;
  }
  *number = 42;
  if (ds_hash_table_create_config_ex(&map_config, &map) != DS_OK ||
      ds_hash_table_insert(map, (void *)"answer", number) != DS_OK) {
    goto done;
  }
  number = NULL; /* map owns the accepted allocation */
  if (ds_hash_table_insert(map, (void *)"unset", NULL) != DS_OK ||
      ds_hash_table_clone_ex(map, false, NULL, &map_copy) != DS_OK) {
    goto done;
  }
  printf("Hash clone with nullable values:");
  (void)ds_hash_table_walk_with(map_copy, show_entry, NULL);
  printf("\n");
  if (ds_fibonacci_heap_create_context(compare_job, &policy, NULL, &heap) !=
          DS_OK ||
      ds_fibonacci_heap_create_context(compare_job, &policy, NULL, &other) !=
          DS_OK) {
    goto done;
  }
  for (i = 0; i < 5; ++i) {
    jobs[i].priority = (int)i + 1;
    if (ds_fibonacci_heap_insert_ex(i < 3 ? heap : other, &jobs[i].node) !=
        DS_OK) {
      goto done;
    }
  }
  if (ds_fibonacci_heap_merge_ex(heap, other) != DS_OK ||
      ds_fibonacci_heap_extract_ex(heap, &extracted) != DS_OK) {
    goto done;
  }
  printf("First job after meld: %d\n", ((struct job *)extracted)->priority);
  jobs[4].priority = 0;
  if (ds_fibonacci_heap_decrease_key_ex(heap, &jobs[4].node) != DS_OK ||
      ds_fibonacci_heap_clone_with(heap, copy_job, release_job, &policy, NULL,
                                   &heap_copy) != DS_OK) {
    goto done;
  }
  printf("Fibonacci clone visits:");
  (void)ds_fibonacci_heap_walk_with(heap_copy, show_job, NULL);
  printf("\n");
  status = ds_fibonacci_heap_merge_ex(heap, heap);
  printf("Self-meld status: %s\n", ds_status_name(status));
  rc = 0;
done:
  free(number);
  ds_fibonacci_heap_destroy_with(heap_copy, release_job, &policy);
  ds_fibonacci_heap_destroy(other, NULL);
  ds_fibonacci_heap_destroy(heap, NULL);
  ds_hash_table_destroy(map_copy);
  ds_hash_table_destroy(map);
  (void)ds_trie_destroy_with(trie_copy, release_payload, &policy);
  ds_trie_destroy(trie);
  printf("Payload cleanup calls: %lu\n", (unsigned long)policy.released);
  return rc;
}
