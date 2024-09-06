#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../lib/doubly_linked_list.h"

typedef struct my_node {
    doubly_linked_list_node_t node;
    int data;
} my_node_t;

int compare_nodes(doubly_linked_list_node_t *list_node, void *data) {
    my_node_t *node = CAST(list_node, my_node_t);
    int *search_data = (int *)data;
    return node->data - *search_data;
}

void print_node(void *node) {
    my_node_t *my_node = CAST(node, my_node_t);
    printf("%d ", my_node->data);
}

void destroy_node(doubly_linked_list_node_t *node) {
    my_node_t *my_node = CAST(node, my_node_t);
    free(my_node);
}

int main() {
    int i, search_value;
    my_node_t *node, *found_node;
    doubly_linked_list_t *list;
    srand(time(NULL));
    list = doubly_linked_list_create();
    search_value = 10;

    for (i = 0; i < 10; i++) {
        node = (my_node_t *)malloc(sizeof(my_node_t));
        node->data = rand() % 100;
        doubly_linked_list_append(list, &node->node);
    }

    doubly_linked_list_walk_forward(list, list->head, print_node);
    printf("\n");
    doubly_linked_list_walk_backwards(list, list->head, print_node);

    found_node = (my_node_t *)doubly_linked_list_search(list, &search_value, compare_nodes);
    if (found_node) {
        printf("found %d\n", found_node->data);
    } else {
        printf("not found\n", search_value);
    }

    my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
    new_node->data = 999;
    doubly_linked_list_prepend(list, &new_node->node);

    printf("prepend: ");
    doubly_linked_list_walk_forward(list, list->head, print_node);

    if (found_node) {
        doubly_linked_list_delete_node(list, &found_node->node);
        printf("delete node: ");
        doubly_linked_list_walk_forward(list, list->head, print_node);
    }

    doubly_linked_list_destroy(list, destroy_node);

    return 0;
}

