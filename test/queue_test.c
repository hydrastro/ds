#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../lib/queue.h"

typedef struct my_queue_node {
    queue_node_t node;
    int data;
} my_queue_node_t;

void print_queue_node(queue_node_t *queue_node) {
    my_queue_node_t *node = CAST(queue_node, my_queue_node_t);
    printf("%d ", node->data);
}

void destroy_queue_node(queue_node_t *queue_node) {
    my_queue_node_t *node = CAST(queue_node, my_queue_node_t);
    free(node);
}

int main() {
    int i;
    queue_t *queue;
    my_queue_node_t *node;
    srand(time(NULL));

    queue = queue_create();

    for (i = 0; i < 10; i++) {
        node = (my_queue_node_t *)malloc(sizeof(my_queue_node_t));
        node->data = rand() % 100;
        queue_enqueue(queue, &node->node);
    }

    while (!queue_is_empty(queue)) {
        node = CAST(queue_dequeue(queue), my_queue_node_t);
        printf("%d ", node->data);
        free(node);
    }
    printf("\n");

    for (i = 0; i < 5; i++) {
        node = (my_queue_node_t *)malloc(sizeof(my_queue_node_t));
        node->data = rand() % 100;
        queue_enqueue(queue, &node->node);
    }

    my_queue_node_t *front_node = CAST(queue_peek(queue), my_queue_node_t);
    my_queue_node_t *tail_node = CAST(queue_peek_tail(queue), my_queue_node_t);

    if (front_node) {
        printf("front: %d\n", front_node->data);
    }

    if (tail_node) {
        printf("tail: %d\n", tail_node->data);
    }

    printf("enqueue:\n");
    queue_node_t *current_node = queue->head;
    while (current_node != queue->nil) {
        print_queue_node(current_node);
        current_node = current_node->next;
    }
    printf("\n");

    queue_destroy(queue, destroy_queue_node);

    return 0;
}
