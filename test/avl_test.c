#include "../lib/avl.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int compare_ints(void *a, void *b) {
    int int_a = *(int *)a;
    int int_b = *(int *)b;
    return (int_a > int_b) - (int_a < int_b);
}

void print_int(void *data) {
    printf("%d ", *(int *)data);
}

void free_int(void *data) {
    free(data);
}

int main() {
    srand(time(NULL));
    avl_t *tree = avl_create();

    int *nums = malloc( 10000 * sizeof(int));
    for (int i = 0; i < 10000; i++) {
        nums[i] = rand() % 1000;
        int *num_ptr = malloc(sizeof(int));
        *num_ptr = nums[i];
        avl_insert(tree, num_ptr, compare_ints);
    }

    printf("in-order: ");
    avl_inorder_walk_tree(tree, print_int);
    printf("\n");

    int to_delete = 5;
    printf("deleting %d...\n", to_delete);
    avl_delete(tree, &to_delete, compare_ints);

    printf("after deletion: ");
    avl_inorder_walk_tree(tree, print_int);
    printf("\n");

    avl_destroy_tree(tree, free_int);

    free(nums);
    return 0;
}
