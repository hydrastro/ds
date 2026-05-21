#include "../lib/trie.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t get_char_slice(void *data, size_t slice) {
  char *word = (char *)data;
  return (size_t)word[slice];
}

bool has_char_slice(void *data, size_t slice) {
  char *word = (char *)data;
  return word[slice] != '\0';
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

void destroy_new_trie_data(ds_trie_t *trie, ds_trie_node_t *node,
                           va_list *args) {
  (void)trie;
  (void)args;
  free(node->terminal_data);
}

int main(void) {
  ds_trie_node_t *result;
  ds_trie_t *trie;
  ds_trie_t *new_trie;

  trie = trie_create_hash(128);

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
  new_trie = trie_clone(trie, clone_trie_data);
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
