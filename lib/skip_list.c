#include "skip_list.h"
#include <stdlib.h>

static size_t skip_list_random_level(ds_skip_list_t *list) {
  size_t level = 1;
  list->seed = list->seed * 1103515245UL + 12345UL;
  while (((list->seed >> 16) & 1UL) != 0UL && level < list->max_level) {
    list->seed = list->seed * 1103515245UL + 12345UL;
    level += 1;
  }
  return level;
}

static ds_skip_list_node_t *skip_list_make_header(ds_skip_list_t *list,
                                                  size_t level) {
  ds_skip_list_node_t *header;
  size_t i;
  header = (ds_skip_list_node_t *)list->allocator(sizeof(ds_skip_list_node_t));
  header->forward = (ds_skip_list_node_t **)list->allocator(
      level * sizeof(ds_skip_list_node_t *));
  header->level = level;
  for (i = 0; i < level; i++) {
    header->forward[i] = list->nil;
  }
  return header;
}

ds_skip_list_t *ds_skip_list_create(int (*compare)(ds_skip_list_node_t *,
                                                   ds_skip_list_node_t *)) {
  return ds_skip_list_create_alloc(compare, DS_SKIP_LIST_MAX_LEVEL, 0UL, malloc,
                                   free);
}

ds_skip_list_t *ds_skip_list_create_alloc(int (*compare)(ds_skip_list_node_t *,
                                                         ds_skip_list_node_t *),
                                          size_t max_level, unsigned long seed,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *)) {
  ds_skip_list_t *list = (ds_skip_list_t *)allocator(sizeof(ds_skip_list_t));
  list->allocator = allocator;
  list->deallocator = deallocator;
  list->compare = compare;
  list->max_level = max_level > 0 ? max_level : DS_SKIP_LIST_MAX_LEVEL;
  list->seed = seed != 0UL ? seed : 0x2545F491UL;
  list->level = 1;
  list->size = 0;
  list->nil = (ds_skip_list_node_t *)allocator(sizeof(ds_skip_list_node_t));
  list->nil->forward = NULL;
  list->nil->level = 0;
  list->header = skip_list_make_header(list, list->max_level);
  return list;
}

void ds_skip_list_node_init(ds_skip_list_node_t *node) {
  node->forward = NULL;
  node->level = 0;
}

void ds_skip_list_insert(ds_skip_list_t *list, ds_skip_list_node_t *node) {
  ds_skip_list_node_t *update[DS_SKIP_LIST_MAX_LEVEL];
  ds_skip_list_node_t *current;
  size_t i;
  size_t new_level;
  if (node == list->nil) {
    return;
  }
  current = list->header;
  i = list->level;
  while (i > 0) {
    i -= 1;
    while (current->forward[i] != list->nil &&
           list->compare(current->forward[i], node) < 0) {
      current = current->forward[i];
    }
    update[i] = current;
  }
  new_level = skip_list_random_level(list);
  if (new_level > list->level) {
    for (i = list->level; i < new_level; i++) {
      update[i] = list->header;
    }
    list->level = new_level;
  }
  node->level = new_level;
  node->forward = (ds_skip_list_node_t **)list->allocator(
      new_level * sizeof(ds_skip_list_node_t *));
  for (i = 0; i < new_level; i++) {
    node->forward[i] = update[i]->forward[i];
    update[i]->forward[i] = node;
  }
  list->size += 1;
}

ds_skip_list_node_t *ds_skip_list_search(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node) {
  ds_skip_list_node_t *current;
  size_t i;
  current = list->header;
  i = list->level;
  while (i > 0) {
    i -= 1;
    while (current->forward[i] != list->nil &&
           list->compare(current->forward[i], node) < 0) {
      current = current->forward[i];
    }
  }
  current = current->forward[0];
  if (current != list->nil && list->compare(current, node) == 0) {
    return current;
  }
  return list->nil;
}

ds_skip_list_node_t *ds_skip_list_remove(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node) {
  ds_skip_list_node_t *update[DS_SKIP_LIST_MAX_LEVEL];
  ds_skip_list_node_t *current;
  size_t i;
  current = list->header;
  i = list->level;
  while (i > 0) {
    i -= 1;
    while (current->forward[i] != list->nil &&
           list->compare(current->forward[i], node) < 0) {
      current = current->forward[i];
    }
    update[i] = current;
  }
  current = current->forward[0];
  if (current == list->nil || list->compare(current, node) != 0) {
    return list->nil;
  }
  for (i = 0; i < list->level; i++) {
    if (update[i]->forward[i] != current) {
      break;
    }
    update[i]->forward[i] = current->forward[i];
  }
  list->deallocator(current->forward);
  current->forward = NULL;
  current->level = 0;
  while (list->level > 1 &&
         list->header->forward[list->level - 1] == list->nil) {
    list->level -= 1;
  }
  list->size -= 1;
  return current;
}

ds_skip_list_node_t *ds_skip_list_first(ds_skip_list_t *list) {
  ds_skip_list_node_t *result;
  result = list->header->forward[0];
  return result;
}

ds_skip_list_node_t *ds_skip_list_next(ds_skip_list_t *list,
                                       ds_skip_list_node_t *node) {
  ds_skip_list_node_t *result;
  if (node == list->nil || node->forward == NULL) {
    return list->nil;
  }
  result = node->forward[0];
  return result;
}

bool ds_skip_list_is_empty(ds_skip_list_t *list) {
  bool result;
  result = list->header->forward[0] == list->nil;
  return result;
}

void ds_skip_list_delete_node(ds_skip_list_t *list, ds_skip_list_node_t *node) {
  ds_skip_list_node_t *removed;
  removed = ds_skip_list_remove(list, node);
  (void)removed;
}

void ds_skip_list_destroy_node(ds_skip_list_t *list, ds_skip_list_node_t *node,
                               void (*destroy)(ds_skip_list_node_t *)) {
  ds_skip_list_node_t *removed;
  removed = ds_skip_list_remove(list, node);
  if (removed != list->nil && destroy != NULL) {
    destroy(removed);
  }
}

void ds_skip_list_walk_forward(ds_skip_list_t *list,
                               ds_skip_list_node_t *start_node,
                               void (*callback)(ds_skip_list_node_t *)) {
  ds_skip_list_node_t *current;
  current = start_node != list->nil ? start_node : list->header->forward[0];
  while (current != list->nil) {
    callback(current);
    current = current->forward[0];
  }
}

void ds_skip_list_destroy(ds_skip_list_t *list,
                          void (*destroy)(ds_skip_list_node_t *)) {
  ds_skip_list_node_t *current = list->header->forward[0];
  while (current != list->nil) {
    ds_skip_list_node_t *next = current->forward[0];
    list->deallocator(current->forward);
    current->forward = NULL;
    if (destroy != NULL) {
      destroy(current);
    }
    current = next;
  }
  list->deallocator(list->header->forward);
  list->deallocator(list->header);
  list->deallocator(list->nil);
  list->deallocator(list);
}

ds_skip_list_t *
ds_skip_list_clone(ds_skip_list_t *list,
                   ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *)) {
  ds_skip_list_t *new_list;
  ds_skip_list_node_t *current;
  new_list =
      ds_skip_list_create_alloc(list->compare, list->max_level, list->seed,
                                list->allocator, list->deallocator);
  current = list->header->forward[0];
  while (current != list->nil) {
    ds_skip_list_node_t *cloned = clone_node(current);
    ds_skip_list_insert(new_list, cloned);
    current = current->forward[0];
  }
  return new_list;
}
