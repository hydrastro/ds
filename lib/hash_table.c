#include "hash_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t prime_capacities[] = {5,
                                          11,
                                          23,
                                          47,
                                          97,
                                          197,
                                          397,
                                          797,
                                          1597,
                                          3203,
                                          6421,
                                          12853,
                                          25717,
                                          51437,
                                          102877,
                                          205759,
                                          411527,
                                          823117,
                                          1646237,
                                          3292489,
                                          6584983,
                                          13169977,
                                          26339969,
                                          52679969,
                                          105359939,
                                          210719881,
                                          421439783,
                                          842879579,
                                          1685759167,
                                          3371518343,
                                          6743036717,
                                          13486073473,
                                          26972146961,
                                          53944293929,
                                          107888587883,
                                          215777175787,
                                          431554351609,
                                          863108703229,
                                          1726217406467,
                                          3452434812973,
                                          6904869625999,
                                          13809739252051,
                                          27619478504183,
                                          55238957008387,
                                          110477914016779,
                                          220955828033581,
                                          441911656067171,
                                          883823312134381,
                                          1767646624268779,
                                          3535293248537579,
                                          7070586497075177,
                                          14141172994150357,
                                          28282345988300791,
                                          56564691976601587,
                                          113129383953203213,
                                          226258767906406483,
                                          452517535812813007,
                                          905035071625626043,
                                          1810070143251252131,
                                          3620140286502504283,
                                          7240280573005008577};

size_t quadratic_probing(size_t base_index, size_t iteration, size_t capacity) {
  const size_t c1 = 1, c2 = 1;
  return (base_index + c1 * iteration + c2 * iteration * iteration) % capacity;
}

size_t linear_probing(size_t base_index, size_t iteration, size_t capacity) {
  return (base_index + iteration) % capacity;
}

size_t hash_func_string_djb2(void *key) {
  char *str = (char *)key;
  size_t hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

size_t hash_func_int(void *key) {
  int num = *(int *)key;
  num = ((num >> 16) ^ num) * 0x45d9f3b;
  num = ((num >> 16) ^ num) * 0x45d9f3b;
  num = (num >> 16) ^ num;
  return (size_t)num;
}

size_t hash_func_pointer(void *key) { return (size_t)key; }

size_t hash_func_double(void *key) {
  double num = *(double *)key;
  size_t hash = 0;
  unsigned char *bytes = (unsigned char *)&num;
  for (size_t i = 0; i < sizeof(double); i++) {
    hash = (hash * 31) + bytes[i];
  }
  return hash;
}

size_t hash_func_default(void *key) { return (size_t)key; }

static hash_node_t *hash_node_create(void *key, void *value) {
  hash_node_t *node = (hash_node_t *)malloc(sizeof(hash_node_t));
  node->key = key;
  node->value = value;
  node->next = NULL;
  return node;
}

static size_t next_prime_capacity(size_t current_capacity) {
  for (size_t i = 0; i < sizeof(prime_capacities) / sizeof(prime_capacities[0]);
       i++) {
    if (prime_capacities[i] > current_capacity) {
      return prime_capacities[i];
    }
  }
  return current_capacity * 2;
}

hash_table_t *hash_table_create(size_t capacity, hash_table_mode_t mode,
                                hash_probing_func_t probing_func) {
  hash_table_t *table = (hash_table_t *)malloc(sizeof(hash_table_t));
  table->capacity = next_prime_capacity(capacity);
  table->size = 0;
  table->nil = (void *)malloc(sizeof(void));
  table->tombstone = (void *)malloc(sizeof(void));
  table->mode = mode;

  if (mode == HASH_CHAINING) {
    table->buckets =
        (hash_node_t **)calloc(table->capacity, sizeof(hash_node_t *));
  } else if (mode == HASH_LINEAR_PROBING) {
    table->entries =
        (hash_node_t *)calloc(table->capacity, sizeof(hash_node_t));
    for (size_t i = 0; i < table->capacity; i++) {
      table->entries[i].key = table->nil;
    }
  }

  if (probing_func) {
    table->probing_func = probing_func;
  } else {
    if (mode == HASH_LINEAR_PROBING) {
      table->probing_func = linear_probing;
    } else if (mode == HASH_QUADRATIC_PROBING) {
      table->probing_func = quadratic_probing;
    }
  }

  return table;
}

void hash_table_resize(hash_table_t *table, size_t new_capacity,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *)) {
  hash_table_t *new_table =
      hash_table_create(new_capacity, table->mode, table->probing_func);

  if (table->mode == HASH_CHAINING) {
    for (size_t i = 0; i < table->capacity; i++) {
      hash_node_t *current = table->buckets[i];
      while (current != NULL) {
        hash_table_insert(new_table, current->key, current->value, hash_func,
                          compare);
        current = current->next;
      }
    }
  } else if (table->mode == HASH_LINEAR_PROBING) {
    for (size_t i = 0; i < table->capacity; i++) {
      if (table->entries[i].key != table->nil) {
        hash_table_insert(new_table, table->entries[i].key,
                          table->entries[i].value, hash_func, compare);
      }
    }
  }

  if (table->mode == HASH_CHAINING) {
    free(table->buckets);
  } else if (table->mode == HASH_LINEAR_PROBING) {
    free(table->entries);
  }

  table->capacity = new_table->capacity;
  table->size = new_table->size;
  if (table->mode == HASH_CHAINING) {
    table->buckets = new_table->buckets;
  } else if (table->mode == HASH_LINEAR_PROBING) {
    table->entries = new_table->entries;
  }

  free(new_table);
}

void hash_table_insert(hash_table_t *table, void *key, void *value,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *)) {
  if ((double)table->size / table->capacity > DS_HASH_TABLE_RESIZE_FACTOR) {
    hash_table_resize(table, next_prime_capacity(table->capacity), hash_func,
                      compare);
  }
  size_t base_index = hash_func(key) % table->capacity;
  size_t index = base_index;
  size_t iteration = 0;
  size_t tombstone_index = -1;
  if (table->mode == HASH_CHAINING) {
    hash_node_t *current = table->buckets[index];
    while (current != NULL) {
      if (compare(current->key, key) == 0) {
        current->value = value;
        return;
      }
      current = current->next;
    }
    hash_node_t *new_node = hash_node_create(key, value);
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
  } else {
    while (table->entries[index].key != table->nil) {
      if (table->entries[index].key == table->tombstone) {
        if (tombstone_index == -1) {
          tombstone_index = index;
        }
      } else if (compare(table->entries[index].key, key) == 0) {
        table->entries[index].value = value;
        return;
      }
      iteration++;
      index = table->probing_func(base_index, iteration, table->capacity);
    }

    if (tombstone_index != -1) {
      index = tombstone_index;
    }

    table->entries[index].key = key;
    table->entries[index].value = value;
  }

  table->size++;
}

void *hash_table_lookup(hash_table_t *table, void *key,
                        size_t (*hash_func)(void *),
                        int (*compare)(void *, void *)) {
  size_t base_index = hash_func(key) % table->capacity;
  size_t index = base_index;
  size_t iteration = 0;

  if (table->mode == HASH_CHAINING) {
    hash_node_t *current = table->buckets[index];
    while (current != NULL) {
      if (compare(current->key, key) == 0) {
        return current->value;
      }
      current = current->next;
    }
  } else {
    while (table->entries[index].key != table->nil) {
      if (table->entries[index].key != table->tombstone &&
          compare(table->entries[index].key, key) == 0) {
        return table->entries[index].value;
      }
      iteration++;
      index = table->probing_func(base_index, iteration, table->capacity);
    }
  }

  return table->nil;
}

void hash_table_remove(hash_table_t *table, void *key,
                       size_t (*hash_func)(void *),
                       int (*compare)(void *, void *),
                       void (*destroy_node)(hash_node_t *)) {
  size_t base_index = hash_func(key) % table->capacity;
  size_t index = base_index;
  size_t iteration = 0;

  if (table->mode == HASH_CHAINING) {
    hash_node_t *current = table->buckets[index];
    hash_node_t *prev = NULL;
    while (current != NULL) {
      if (compare(current->key, key) == 0) {
        if (prev == NULL) {
          table->buckets[index] = current->next;
        } else {
          prev->next = current->next;
        }
        table->size--;
        if (destroy_node) {
          destroy_node(current);
        } else {
          free(current);
        }
        return;
      }
      prev = current;
      current = current->next;
    }
  } else if (table->mode == HASH_LINEAR_PROBING) {
    while (table->entries[index].key != table->nil) {
      if (compare(table->entries[index].key, key) == 0) {
        table->entries[index].key = table->tombstone;
        table->size--;
        return;
      }
      iteration++;
      index = table->probing_func(base_index, iteration, table->capacity);
    }
  }
}

int hash_table_is_empty(hash_table_t *table) { return table->size == 0; }

void hash_table_destroy(hash_table_t *table,
                        void (*destroy_node)(hash_node_t *)) {
  if (table->mode == HASH_CHAINING) {
    for (size_t i = 0; i < table->capacity; i++) {
      hash_node_t *current = table->buckets[i];
      while (current != NULL) {
        hash_node_t *next = current->next;
        if (destroy_node) {
          destroy_node(current);
        } else {
          free(current);
        }
        current = next;
      }
    }
    free(table->buckets);
  } else if (table->mode == HASH_LINEAR_PROBING) {
    free(table->entries);
  }
  free(table->nil);
  free(table->tombstone);
  free(table);
}
