#include "../lib/hash_table.h"
#include "../lib/trie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int compare_int(void *a, void *b) { return (int)((size_t)a - (size_t)b); }

size_t get_char_slice(void *data, size_t slice) {
  char *word = (char *)data;
  return (size_t)word[slice];
}

bool has_char_slice(void *data, size_t slice) {
int i = 0;
  char *word = (char *)data;
  while(word[i] != '\0' && i != (int)slice){i++;};
  return word[i] != '\0';
}

size_t my_hash(void *key) { return hash_func_int(&key); }

trie_node_t *my_hash_table_search(void *store, size_t slice) {
  hash_table_t *table = (hash_table_t *)store;
  trie_node_t *result = (trie_node_t *)hash_table_lookup(table, (void *)slice,
                                                         my_hash, compare_int);
  if (result == table->nil) {
    return NULL;
  }
  return result;
}

void *my_hash_table_create_store(size_t size) {
  return (void *)hash_table_create(size, HASH_CHAINING, NULL);
}

void my_hash_table_insert_store(void *store, trie_node_t *node) {
  hash_table_t *table = (hash_table_t *)store;
  hash_table_insert(table, (void *)node->data_slice, (void *)node, my_hash,
                    compare_int);
}

void my_hash_table_remove_store(void *store, trie_node_t *node) {
  hash_table_t *table = (hash_table_t *)store;
  hash_table_remove(table, (void *)node->data_slice, my_hash, compare_int,
                    NULL);
}

void my_hash_table_destroy_entry(void *store, trie_node_t *node) {
  hash_table_t *table = (hash_table_t *)store;
  hash_table_remove(table, (void *)node->data_slice, my_hash, compare_int,
                    NULL);
}

void my_hash_table_destroy(void *store) {
  hash_table_t *table = (hash_table_t *)store;
  hash_table_destroy(table, NULL);
}

size_t my_hash_table_get_size(void *store) {
  hash_table_t *table = (hash_table_t *)store;
  return table->size;
}

void my_hash_table_apply(trie_t *trie, void *store,
                         void (*f)(struct trie *, trie_node_t *)) {
  hash_table_t *table = (hash_table_t *)store;
  if(table == NULL){return;}
  hash_node_t *cur = table->last_node;
  hash_node_t *temp;
  while (cur != NULL) {
    temp = cur->list_prev;
    my_hash_table_apply(trie, ((trie_node_t *)cur->value)->children, f);
    f(trie, (trie_node_t *)cur->value);
    cur = temp;
  }

}

bool search_word(trie_t *trie, char *word) {
  trie_node_t *node =
      trie_search(trie, (void *)word, get_char_slice, has_char_slice);
  return node != NULL;
}

void print_trie_node_hm(trie_t *trie, trie_node_t *node) {
  if (node->is_terminal) {
    printf("%s\n", (char *)node->terminal_data);
  }
}

void print_trie(trie_t *trie) {
  printf("Printing Trie Structure:\n");
  trie->store_apply(trie, trie->root->children, print_trie_node_hm);
}

int main() {
  trie_node_t *result;
  trie_t *trie =
      trie_create(128, my_hash_table_search, my_hash_table_create_store,
                  my_hash_table_insert_store, my_hash_table_remove_store,
                  my_hash_table_destroy_entry, my_hash_table_destroy,
                  my_hash_table_get_size, my_hash_table_apply);

  print_trie(trie);

  trie_insert(trie, (void *)"apple", get_char_slice, has_char_slice);
  trie_insert(trie, (void *)"banana", get_char_slice, has_char_slice);
  trie_insert(trie, (void *)"grape", get_char_slice, has_char_slice);
  trie_insert(trie, (void *)"grapefruit", get_char_slice, has_char_slice);

  printf("Searching for 'apple': %s\n",
         search_word(trie, "apple") ? "Found" : "Not Found");
  printf("Searching for 'banana': %s\n",
         search_word(trie, "banana") ? "Found" : "Not Found");
  printf("Searching for 'grape': %s\n",
         search_word(trie, "grape") ? "Found" : "Not Found");
  printf("Searching for 'grapefruit': %s\n",
         search_word(trie, "grapefruit") ? "Found" : "Not Found");
  printf("Searching for 'orange': %s\n",
         search_word(trie, "orange") ? "Found" : "Not Found");

  print_trie(trie);

  result = trie_search(trie, "grapefruit", get_char_slice, has_char_slice);
  if(result != NULL) {
  printf("\n--- Destroying 'grapefruit' ---\n");
  fflush(stdout);
  trie_remove_node(
      trie, result);
  print_trie(trie);
  }

  result = trie_search(trie, "banana", get_char_slice, has_char_slice);
  if(result != NULL) {
  printf("\n--- Removing 'banana' ---\n");
  trie_remove_node(trie, result);
  print_trie(trie);
  }

  trie_destroy_tree(trie);

  return 0;
}
