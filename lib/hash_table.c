#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "hash_table.h"

static const size_t prime_capacities[] = {5, 11, 23, 47, 97, 197, 397, 797, 1597, 3203, 6421, 12853, 25717, 51437, 102877, 205759, 411527, 823117, 1646237, 3292489, 6584983};


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

size_t hash_func_pointer(void *key) {
    return (size_t)key;
}

size_t hash_func_double(void *key) {
    double num = *(double *)key;
    size_t hash = 0;
    unsigned char *bytes = (unsigned char *)&num;
    for (size_t i = 0; i < sizeof(double); i++) {
        hash = (hash * 31) + bytes[i];
    }
    return hash;
}

size_t hash_func_default(void *key) {
    return (size_t)key;
}

static hash_node_t *hash_node_create(void *key, void *value) {
    hash_node_t *node = (hash_node_t *)malloc(sizeof(hash_node_t));
    node->key = key;
    node->value = value;
    node->next = NULL;
    return node;
}

static size_t next_prime_capacity(size_t current_capacity) {
    for (size_t i = 0; i < sizeof(prime_capacities) / sizeof(prime_capacities[0]); i++) {
        if (prime_capacities[i] > current_capacity) {
            return prime_capacities[i];
        }
    }
    return current_capacity * 2;
}

hash_table_t *hash_table_create(size_t capacity, hash_table_mode_t mode) {
    hash_table_t *table = (hash_table_t *)malloc(sizeof(hash_table_t));
    table->capacity = next_prime_capacity(capacity);
    table->size = 0;
    table->nil = (void *)malloc(sizeof(void));
    table->mode = mode;

    if (mode == HASH_CHAINING) {
        table->buckets = (hash_node_t **)calloc(table->capacity, sizeof(hash_node_t *));
    } else {
        table->entries = (hash_node_t *)calloc(table->capacity, sizeof(hash_node_t));
        for (size_t i = 0; i < table->capacity; i++) {
            table->entries[i].key = table->nil;
        }
    }

    return table;
}

void hash_table_resize(hash_table_t *table, size_t new_capacity, size_t (*hash_func)(void *), int (*compare)(void *, void *)) {
    hash_table_t *new_table = hash_table_create(new_capacity, table->mode);

    if (table->mode == HASH_CHAINING) {
        for (size_t i = 0; i < table->capacity; i++) {
            hash_node_t *current = table->buckets[i];
            while (current != NULL) {
                hash_table_insert(new_table, current->key, current->value, hash_func, compare);
                current = current->next;
            }
        }
    } else if (table->mode == HASH_LINEAR_PROBING) {
        for (size_t i = 0; i < table->capacity; i++) {
            if (table->entries[i].key != table->nil) {
                hash_table_insert(new_table, table->entries[i].key, table->entries[i].value, hash_func, compare);
            }
        }
    }

    if (table->mode == HASH_CHAINING) {
        free(table->buckets);
    } else {
        free(table->entries);
    }

    table->capacity = new_table->capacity;
    table->size = new_table->size;
    if (table->mode == HASH_CHAINING) {
        table->buckets = new_table->buckets;
    } else {
        table->entries = new_table->entries;
    }

    free(new_table);
}

void hash_table_insert(hash_table_t *table, void *key, void *value, size_t (*hash_func)(void *), int (*compare)(void *, void *)) {
    if((double)table->size / table->capacity > DS_HASH_TABLE_RESIZE_FACTOR) {
        hash_table_resize(table, next_prime_capacity(table->capacity), hash_func, compare);
    }
    size_t index = hash_func(key) % table->capacity;

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
            if (compare(table->entries[index].key, key) == 0) {
                table->entries[index].value = value;
                return;
            }
            index = (index + 1) % table->capacity;
        }
        table->entries[index].key = key;
        table->entries[index].value = value;
    }

    table->size++;
}

void *hash_table_lookup(hash_table_t *table, void *key, size_t (*hash_func)(void *), int (*compare)(void *, void *)) {
    size_t index = hash_func(key) % table->capacity;

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
            if (compare(table->entries[index].key, key) == 0) {
                return table->entries[index].value;
            }
            index = (index + 1) % table->capacity;
        }
    }

    return table->nil;
}

void hash_table_remove(hash_table_t *table, void *key, size_t (*hash_func)(void *), int (*compare)(void *, void *), void (*destroy_node)(hash_node_t *)) {
    size_t index = hash_func(key) % table->capacity;

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
    } else {
        while (table->entries[index].key != table->nil) {
            if (compare(table->entries[index].key, key) == 0) {
                table->entries[index].key = table->nil;
                table->size--;
                return;
            }
            index = (index + 1) % table->capacity;
        }
    }
}

int hash_table_is_empty(hash_table_t *table) {
    return table->size == 0;
}

void hash_table_destroy(hash_table_t *table, void (*destroy_node)(hash_node_t *)) {
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
    } else {
        free(table->entries);
    }
    free(table->nil);
    free(table);
}
