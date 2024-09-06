#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../lib/linked_list.h"

typedef struct my_node {
    linked_list_node_t node;
    int data;
} my_node_t;

int compare_nodes(linked_list_node_t *list_node, void *data) {
    my_node_t *node = CAST(list_node, my_node_t);
    int *search_data = (int *)data;
    return node->data - *search_data;
}

void print_node(void *data) {
    my_node_t *node = CAST(data, my_node_t);
    printf("%d ", node->data);
}

void destroy_node(void *data) {
    my_node_t *node = CAST(data, my_node_t);
    free(node);
}

int main() {
    int i, search_value;
    my_node_t *node, *found_node;
    linked_list_t *list;
    srand(time(NULL));
    list = linked_list_create();
    search_value = 10;

    for (i = 0; i < 10; i++) {
        node = (my_node_t *)malloc(sizeof(my_node_t));
        node->data = rand() % 100;
        linked_list_append(list, &node->node);
    }
    linked_list_walk_forward(list, list->head, print_node);
    printf("\n");
    linked_list_walk_backwards(list, list->head, print_node);
    printf("\n");

    found_node = (my_node_t *)linked_list_search(list, &search_value, compare_nodes);
    if (found_node) {
        print_node(found_node);
        printf("\n");
    } else {
        printf("not found\n", search_value);
    }

    my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
    new_node->data = 999;
    linked_list_prepend(list, &new_node->node);

    printf("prepend: ");
    linked_list_walk_forward(list, list->head, print_node);
    printf("\n");

    if (found_node) {
        linked_list_delete_node(list, &found_node->node);
        printf("delete node: ");
        linked_list_walk_forward(list, list->head, print_node);
        printf("\n");
    }

    linked_list_destroy(list, destroy_node);

    return 0;
}
