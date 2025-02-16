#include "hash_table.h"
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
  size_t c;
  while ((c = (size_t)*str++)) {
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
  size_t i;
  for (i = 0; i < sizeof(double); i++) {
    hash = (hash * 31) + bytes[i];
  }
  return hash;
}

size_t hash_func_default(void *key) { return (size_t)key; }

static hash_node_t *hash_node_create(hash_table_t *table, void *key,
                                     void *value) {
  hash_node_t *node = (hash_node_t *)table->allocator(sizeof(hash_node_t));
  node->key = key;
  node->value = value;
  node->next = NULL;
  node->list_next = NULL;
  node->list_prev = NULL;
  return node;
}

static size_t next_prime_capacity(size_t current_capacity) {
  size_t i;
  for (i = 0; i < sizeof(prime_capacities) / sizeof(prime_capacities[0]); i++) {
    if (prime_capacities[i] > current_capacity) {
      return prime_capacities[i];
    }
  }
  return current_capacity * 2;
}

hash_table_t *FUNC(hash_table_create)(size_t capacity, hash_table_mode_t mode,
                                      hash_probing_func_t probing_func) {
  return FUNC(hash_table_create_alloc)(capacity, mode, probing_func, malloc,
                                       free);
}

hash_table_t *FUNC(hash_table_create_alloc)(size_t capacity,
                                            hash_table_mode_t mode,
                                            hash_probing_func_t probing_func,
                                            void *(*allocator)(size_t),
                                            void (*deallocator)(void *)) {

  size_t i;
  hash_table_t *table = (hash_table_t *)allocator(sizeof(hash_table_t));
  table->allocator = allocator;
  table->deallocator = deallocator;
  table->capacity = next_prime_capacity(capacity);
  table->size = 0;
  table->nil = (void *)allocator(sizeof(void *));
  table->tombstone = (void *)allocator(sizeof(void *));
  table->mode = mode;
  table->last_node = NULL;
  if (mode == HASH_CHAINING) {
    table->store.buckets =
        (hash_node_t **)allocator(table->capacity * sizeof(hash_node_t *));
    memset(table->store.buckets, 0, table->capacity * sizeof(hash_node_t *));
  } else if (mode == HASH_LINEAR_PROBING || mode == HASH_QUADRATIC_PROBING) {
    table->store.entries =
        (hash_node_t *)table->allocator(table->capacity * sizeof(hash_node_t));
    memset(table->store.entries, 0, table->capacity * sizeof(hash_node_t));
    for (i = 0; i < table->capacity; i++) {
      table->store.entries[i].key = table->nil;
    }
  }
  if (probing_func != NULL) {
    table->probing_func = probing_func;
  } else {
    if (mode == HASH_LINEAR_PROBING) {
      table->probing_func = linear_probing;
    } else if (mode == HASH_QUADRATIC_PROBING) {
      table->probing_func = quadratic_probing;
    } else {
      table->probing_func = NULL;
    }
  }

#ifdef HASH_DS_THREAD_SAFE
  LOCK_INIT(table)
#endif

  return table;
}

void FUNC(hash_table_resize)(hash_table_t *table, size_t new_capacity,
                             size_t (*hash_func)(void *),
                             int (*compare)(void *, void *)) {
  hash_table_t *new_table =
      FUNC(hash_table_create)(new_capacity, table->mode, table->probing_func);
  size_t i;
  if (table->mode == HASH_CHAINING) {
    for (i = 0; i < table->capacity; i++) {
      hash_node_t *current = table->store.buckets[i];
      while (current != NULL) {
        FUNC(hash_table_insert)
        (new_table, current->key, current->value, hash_func, compare);
        current = current->next;
      }
    }
  } else if (table->mode == HASH_LINEAR_PROBING ||
             table->mode == HASH_QUADRATIC_PROBING) {
    for (i = 0; i < table->capacity; i++) {
      if (table->store.entries[i].key != table->nil) {
        FUNC(hash_table_insert)
        (new_table, table->store.entries[i].key, table->store.entries[i].value,
         hash_func, compare);
      }
    }
  }

  if (table->mode == HASH_CHAINING) {
    table->deallocator(table->store.buckets);
  } else if (table->mode == HASH_LINEAR_PROBING ||
             table->mode == HASH_QUADRATIC_PROBING) {
    table->deallocator(table->store.entries);
  }

  table->capacity = new_table->capacity;
  table->size = new_table->size;
  if (table->mode == HASH_CHAINING) {
    table->store.buckets = new_table->store.buckets;
  } else if (table->mode == HASH_LINEAR_PROBING ||
             table->mode == HASH_QUADRATIC_PROBING) {
    table->store.entries = new_table->store.entries;
  }

  table->deallocator(new_table);
}

void FUNC(hash_table_insert)(hash_table_t *table, void *key, void *value,
                             size_t (*hash_func)(void *),
                             int (*compare)(void *, void *)) {
  size_t base_index;
  size_t index;
  size_t iteration;
  size_t tombstone_index;
  hash_node_t *new_node;
#ifdef HASH_DS_THREAD_SAFE
  LOCK(table)
#endif
  if ((double)table->size / (double)table->capacity >
      HASH_TABLE_RESIZE_FACTOR) {
    FUNC(hash_table_resize)
    (table, next_prime_capacity(table->capacity), hash_func, compare);
  }
  base_index = hash_func(key) % table->capacity;
  index = base_index;
  iteration = 0;
  tombstone_index = (long unsigned int)-1;
  if (table->mode == HASH_CHAINING) {
    hash_node_t *current = table->store.buckets[index];
    while (current != NULL) {
      if (compare(current->key, key) == 0) {
        current->value = value;
#ifdef HASH_DS_THREAD_SAFE
        UNLOCK(table)
#endif
        return;
      }
      current = current->next;
    }
    new_node = hash_node_create(table, key, value);

    if (table->last_node != NULL) {
      table->last_node->list_next = new_node;
    }
    new_node->list_prev = table->last_node;
    table->last_node = new_node;

    new_node->next = table->store.buckets[index];
    table->store.buckets[index] = new_node;
  } else {
    while (table->store.entries[index].key != table->nil) {
      if (table->store.entries[index].key == table->tombstone) {
        if (tombstone_index == (long unsigned int)-1) {
          tombstone_index = index;
        }
      } else if (compare(table->store.entries[index].key, key) == 0) {
        table->store.entries[index].value = value;
#ifdef HASH_DS_THREAD_SAFE
        UNLOCK(table)
#endif
        return;
      }
      iteration++;
      index = table->probing_func(base_index, iteration, table->capacity);
    }

    if (tombstone_index != (long unsigned int)-1) {
      index = tombstone_index;
    }

    if (table->last_node != NULL) {
      table->last_node->list_next = &table->store.entries[index];
    }
    table->store.entries[index].list_prev = table->last_node;
    table->last_node = &table->store.entries[index];

    table->store.entries[index].key = key;
    table->store.entries[index].value = value;
  }

  table->size++;

#ifdef HASH_DS_THREAD_SAFE
  UNLOCK(table)
#endif
}

void *FUNC(hash_table_lookup)(hash_table_t *table, void *key,
                              size_t (*hash_func)(void *),
                              int (*compare)(void *, void *)) {
#ifdef HASH_DS_THREAD_SAFE
  LOCK(table)
#endif
  size_t base_index = hash_func(key) % table->capacity;
  size_t index = base_index;
  size_t iteration = 0;

  if (table->mode == HASH_CHAINING) {
    hash_node_t *current = table->store.buckets[index];
    while (current) {
      if (compare(current->key, key) == 0) {
#ifdef HASH_DS_THREAD_SAFE
        UNLOCK(table)
#endif

        return current->value;
      }
      current = current->next;
    }
  } else {
    while (table->store.entries[index].key != table->nil) {
      if (table->store.entries[index].key != table->tombstone &&
          compare(table->store.entries[index].key, key) == 0) {
#ifdef HASH_DS_THREAD_SAFE
        UNLOCK(table)
#endif
        return table->store.entries[index].value;
      }
      iteration++;
      index = table->probing_func(base_index, iteration, table->capacity);
    }
  }
#ifdef HASH_DS_THREAD_SAFE
  UNLOCK(table)
#endif

  return table->nil;
}

void FUNC(hash_table_remove)(hash_table_t *table, void *key,
                             size_t (*hash_func)(void *),
                             int (*compare)(void *, void *),
                             void (*destroy)(hash_node_t *)) {
#ifdef HASH_DS_THREAD_SAFE
  LOCK(table)
#endif
  size_t base_index = hash_func(key) % table->capacity;
  size_t index = base_index;
  size_t iteration = 0;

  if (table->mode == HASH_CHAINING) {
    hash_node_t *current = table->store.buckets[index];
    hash_node_t *prev = NULL;
    while (current != NULL) {
      if (compare(current->key, key) == 0) {
        if (prev == NULL) {
          table->store.buckets[index] = current->next;
        } else {
          prev->next = current->next;
        }

        if (table->last_node == current) {
          table->last_node = current->list_prev;
        }
        if (current->list_prev != NULL) {
          current->list_prev->list_next = current->list_next;
        }
        if (current->list_next != NULL) {
          current->list_next->list_prev = current->list_prev;
        }

        table->size--;
        if (destroy != NULL) {
          destroy(current);
        }
        table->deallocator(current);
#ifdef HASH_DS_THREAD_SAFE
        UNLOCK(table)
#endif
        return;
      }
      prev = current;
      current = current->next;
    }
  } else if (table->mode == HASH_LINEAR_PROBING ||
             table->mode == HASH_QUADRATIC_PROBING) {
    while (table->store.entries[index].key != table->nil) {
      if (compare(table->store.entries[index].key, key) == 0) {
        if (destroy != NULL) {
          destroy(&table->store.entries[index]);
        }
        table->store.entries[index].key = table->tombstone;

        if (table->store.entries[index].list_prev != NULL) {
          table->store.entries[index].list_prev->list_next =
              table->store.entries[index].list_next;
        }
        if (table->store.entries[index].list_next != NULL) {
          table->store.entries[index].list_next->list_prev =
              table->store.entries[index].list_prev;
        }

        table->size--;
#ifdef HASH_DS_THREAD_SAFE
        UNLOCK(table)
#endif
        return;
      }
      iteration++;
      index = table->probing_func(base_index, iteration, table->capacity);
    }
  }
#ifdef HASH_DS_THREAD_SAFE
  UNLOCK(table)
#endif
}

bool FUNC(hash_table_is_empty)(hash_table_t *table) { return table->size == 0; }

void FUNC(hash_table_destroy)(hash_table_t *table,
                              void (*destroy)(hash_node_t *)) {
  size_t i;
  if (table->mode == HASH_CHAINING) {
    for (i = 0; i < table->capacity; i++) {
      hash_node_t *current = table->store.buckets[i];
      while (current != NULL) {
        hash_node_t *next = current->next;
        if (destroy) {
          destroy(current);
        }
        table->deallocator(current);
        current = next;
      }
    }
    table->deallocator(table->store.buckets);
  } else if (table->mode == HASH_LINEAR_PROBING ||
             table->mode == HASH_QUADRATIC_PROBING) {
    table->deallocator(table->store.entries);
  }
#ifdef HASH_DS_THREAD_SAFE
  LOCK_DESTROY(table)
#endif
  table->deallocator(table->nil);
  table->deallocator(table->tombstone);
  table->deallocator(table);
}

hash_table_t *FUNC(hash_table_clone)(hash_table_t *table,
                                     void *(*clone_key)(void *),
                                     void *(*clone_value)(void *)) {
#ifdef HASH_DS_THREAD_SAFE
  LOCK(table);
#endif
  void *key, *value;
  size_t i;
  hash_node_t *current;
  hash_node_t *new_node;
  hash_table_t *new_table = FUNC(hash_table_create_alloc)(
      table->capacity, table->mode, table->probing_func, table->allocator,
      table->deallocator);
  new_table->size = table->size;
  if (table->mode == HASH_CHAINING) {
    for (i = 0; i < table->capacity; i++) {
      current = table->store.buckets[i];
      while (current) {
        key = clone_key(current->key);
        value = clone_value(current->value);
        new_node = hash_node_create(table, key, value);
        new_node->next = new_table->store.buckets[i];
        new_table->store.buckets[i] = new_node;
        if (new_table->last_node) {
          new_table->last_node->list_next = new_node;
        }
        new_node->list_prev = new_table->last_node;
        new_table->last_node = new_node;

        current = current->next;
      }
    }
  } else if (table->mode == HASH_LINEAR_PROBING ||
             table->mode == HASH_QUADRATIC_PROBING) {
    for (i = 0; i < table->capacity; i++) {
      if (table->store.entries[i].key != table->nil &&
          table->store.entries[i].key != table->tombstone) {
        new_table->store.entries[i].key =
            clone_key(table->store.entries[i].key);
        new_table->store.entries[i].value =
            clone_value(table->store.entries[i].value);

        if (new_table->last_node) {
          new_table->last_node->list_next = &new_table->store.entries[i];
        }
        new_table->store.entries[i].list_prev = new_table->last_node;
        new_table->last_node = &new_table->store.entries[i];
      }
    }
  }

#ifdef HASH_DS_THREAD_SAFE
  UNLOCK(table);
#endif

  return new_table;
}
