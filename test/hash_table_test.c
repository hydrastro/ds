#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/hash_table.h"

size_t string_hash(void *key) {
    char *str = (char *)key;
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }printf("%d\n",hash);
    return hash;
}

int string_compare(void *key1, void *key2) {
    return strcmp((char *)key1, (char *)key2);
}

int main() {
    hash_table_t *table = hash_table_create(400, HASH_CHAINING);

int *keys = (int*)malloc(sizeof(int) * 300);

for(int i =0;i< 300; i++) {
keys[i] = i;

    hash_table_insert(table, &keys[i], "asd", hash_func_int, string_compare);
//    hash_table_insert(table, "key1", "value1", string_hash, string_compare);
//    hash_table_insert(table, "key2", "value2", string_hash, string_compare);
//    hash_table_insert(table, "key3", "value3", string_hash, string_compare);
}

    char *value = CAST(hash_table_lookup(table, "key2", string_hash, string_compare), char);
    if (value) {
        printf("Found value for key2: %s\n", value);
    }

    hash_table_remove(table, "key2", string_hash, string_compare, NULL);

    value = CAST(hash_table_lookup(table, "key2", string_hash, string_compare), char);
    if (value == NULL) {
        printf("Key 'key2' not found\n");
    }

    hash_table_destroy(table, NULL);

    return 0;
}
