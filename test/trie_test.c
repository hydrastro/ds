#include "../lib/hash_table.h"
#include "../lib/trie.h"
#include <stdarg.h>
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
  while (word[i] != '\0' && i != (int)slice) {
    i++;
  };
  return word[i] != '\0';
}

size_t my_hash(void *key) { return hash_func_int(&key); }

ds_trie_node_t *my_hash_table_search(void *store, size_t slice) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  ds_trie_node_t *result = (ds_trie_node_t *)hash_table_lookup(
      table, (void *)slice, my_hash, compare_int);
  if (result == table->nil) {
    return NULL;
  }
  return result;
}

void *my_hash_table_create_store(size_t size) {
  return (void *)hash_table_create(size, HASH_CHAINING, NULL);
}

void my_hash_table_insert_store(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  hash_table_insert(table, (void *)node->data_slice, (void *)node, my_hash,
                    compare_int);
}

void my_hash_table_remove_store(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  hash_table_remove(table, (void *)node->data_slice, my_hash, compare_int,
                    NULL);
}

void my_hash_table_destroy_entry(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  hash_table_remove(table, (void *)node->data_slice, my_hash, compare_int,
                    NULL);
}

void my_hash_table_destroy(void *store) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  hash_table_destroy(table, NULL);
}

size_t my_hash_table_get_size(void *store) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  return table->size;
}

void my_hash_table_apply(ds_trie_t *trie, void *store,
                         void (*f)(struct trie *, ds_trie_node_t *,
                                   va_list *args),
                         va_list *args) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  if (table == NULL) {
    return;
  }

  ds_hash_node_t *cur = table->last_node;
  ds_hash_node_t *temp;

  while (cur != NULL) {
    temp = cur->list_prev;

    va_list args_copy1, args_copy2;
    if (args != NULL) {
      va_copy(args_copy1, *args);
      va_copy(args_copy2, *args);
      my_hash_table_apply(trie, ((ds_trie_node_t *)cur->value)->children, f,
                          &args_copy1);
      f(trie, (ds_trie_node_t *)cur->value, &args_copy2);
      va_end(args_copy1);
      va_end(args_copy2);
    } else {
      my_hash_table_apply(trie, ((ds_trie_node_t *)cur->value)->children, f,
                          NULL);
      f(trie, (ds_trie_node_t *)cur->value, NULL);
    }

    cur = temp;
  }
}

bool search_word(ds_trie_t *trie, char *word) {
  ds_trie_node_t *node =
      trie_search(trie, (void *)word, get_char_slice, has_char_slice);
  return node != NULL;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void print_trie_node(ds_trie_t *trie, ds_trie_node_t *node, va_list *args) {
  if (node->is_terminal) {
    if (args != NULL) {
      char *str = va_arg(*args, char *);
      while (str != NULL) {
        printf(">%s<\n", str);
        fflush(stdout);
        str = va_arg(*args, char *);
      }
    }

    printf("%s\n", (char *)node->terminal_data);
    fflush(stdout);
  }
}
#pragma GCC diagnostic pop

void print_trie(ds_trie_t *trie) {
  printf("Printing Trie Structure:\n");
  trie->store_apply(trie, trie->root->children, print_trie_node, NULL);
}

void *clone_trie_data(void *data) {
  if (data == NULL) {
    return NULL;
  }
  return (void *)strdup((char *)data);
}

void *my_hash_table_clone(struct trie *trie, void *store,
                          ds_trie_node_t *parent_node) {
  ds_hash_table_t *table = (ds_hash_table_t *)store;
  ds_hash_table_t *new_table =
      hash_table_create(table->size, table->mode, table->probing_func);
  ds_hash_node_t *cur = table->last_node;
  ds_hash_node_t *temp;

  while (cur != NULL) {
    temp = cur->list_prev;
    ds_trie_node_t *node = trie_clone_node(trie, (ds_trie_node_t *)cur->value,
                                           parent_node, clone_trie_data);
    my_hash_table_insert_store(new_table, node);

    cur = temp;
  }

  return (void *)new_table;
}

void destroy_new_trie_data(ds_trie_t *trie, ds_trie_node_t *node,
                           va_list *args) {
  free(node->terminal_data);
}

int main() {
  ds_trie_node_t *result;
  ds_trie_t *trie = trie_create(
      128, my_hash_table_search, my_hash_table_create_store,
      my_hash_table_insert_store, my_hash_table_remove_store,
      my_hash_table_destroy_entry, my_hash_table_destroy,
      my_hash_table_get_size, my_hash_table_apply, my_hash_table_clone);

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

  trie_apply(trie, trie->root, print_trie_node, " test ", NULL);

  printf("\n--- Cloning Trie ---\n");
  ds_trie_t *new_trie = trie_clone(trie, clone_trie_data);
  print_trie(new_trie);
  printf("\n--- End Cloning Trie ---\n");

  result = trie_search(trie, "grapefruit", get_char_slice, has_char_slice);
  if (result != NULL) {
    printf("\n--- Destroying 'grapefruit' ---\n");
    fflush(stdout);
    trie_delete_node(trie, result);
    print_trie(trie);
  }

  result = trie_search(trie, "banana", get_char_slice, has_char_slice);
  if (result != NULL) {
    printf("\n--- Removing 'banana' ---\n");
    trie_delete_node(trie, result);
    print_trie(trie);
  }

  trie_destroy_trie(trie, NULL);
  trie_destroy_trie(new_trie, destroy_new_trie_data);

  return 0;
}
