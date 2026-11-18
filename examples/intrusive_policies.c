/* Three intrusive indexes share application records and a comparison policy.
 * Clones own separate records, allocated through the callback's user context.
 */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

typedef struct task {
  ds_bst_node_t plain;
  ds_avl_node_t avl;
  ds_rbt_node_t redblack;
  int priority;
} task_t;
typedef struct policy {
  bool descending;
  size_t remaining; /* zero: unlimited */
  ds_context_t *context;
} policy_t;

#define INDEX_CALLBACKS(kind, member)                                          \
  static int kind##_compare(ds_##kind##_node_t *a, ds_##kind##_node_t *b,      \
                            void *user) {                                      \
    policy_t *policy = (policy_t *)user;                                       \
    int x = DS_CONTAINER_OF(a, task_t, member)->priority;                      \
    int y = DS_CONTAINER_OF(b, task_t, member)->priority;                      \
    int order = (x > y) - (x < y);                                             \
    return policy->descending ? -order : order;                                \
  }                                                                            \
  static ds_status_t kind##_visit(ds_##kind##_node_t *node, void *user) {      \
    policy_t *policy = (policy_t *)user;                                       \
    printf(" %d", DS_CONTAINER_OF(node, task_t, member)->priority);            \
    if (policy->remaining > 0 && --policy->remaining == 0)                     \
      return DS_STOP;                                                          \
    return DS_OK;                                                              \
  }                                                                            \
  static ds_status_t kind##_clone(ds_##kind##_node_t *node, void *user,        \
                                  ds_##kind##_node_t **out) {                  \
    policy_t *policy = (policy_t *)user;                                       \
    task_t *copy =                                                             \
        (task_t *)ds_context_alloc(policy->context, sizeof(task_t));           \
    if (copy == NULL)                                                          \
      return DS_ERR_ALLOC;                                                     \
    memset(copy, 0, sizeof(*copy));                                            \
    copy->priority = DS_CONTAINER_OF(node, task_t, member)->priority;          \
    *out = &copy->member;                                                      \
    return DS_OK;                                                              \
  }                                                                            \
  static void kind##_drop(ds_##kind##_node_t *node, void *user) {              \
    policy_t *policy = (policy_t *)user;                                       \
    ds_context_free(policy->context, DS_CONTAINER_OF(node, task_t, member));   \
  }

INDEX_CALLBACKS(bst, plain)
INDEX_CALLBACKS(avl, avl)
INDEX_CALLBACKS(rbt, redblack)

int main(void) {
  ds_bst_t *plain = NULL, *plain_copy = NULL;
  ds_avl_t *avl = NULL, *avl_copy = NULL;
  ds_rbt_t *redblack = NULL, *redblack_copy = NULL;
  ds_rbt_node_t *found;
  task_t tasks[5], key;
  int priorities[] = {3, 1, 5, 2, 4};
  policy_t policy;
  ds_context_t context;
  ds_status_t st;
  size_t i;
  ds_context_init(&context);
  policy.descending = true;
  policy.remaining = 0;
  policy.context = &context;
  memset(tasks, 0, sizeof(tasks));
  memset(&key, 0, sizeof(key));
  st = ds_bst_create_context(&context, &plain);
  if (st != DS_OK)
    goto done;
  st = ds_avl_create_context(&context, &avl);
  if (st != DS_OK)
    goto done;
  st = ds_rbt_create_context(&context, &redblack);
  if (st != DS_OK)
    goto done;
  for (i = 0; i < 5; i++) {
    tasks[i].priority = priorities[i];
    st = ds_bst_insert_with(plain, &tasks[i].plain, bst_compare, &policy);
    if (st != DS_OK)
      goto done;
    st = ds_avl_insert_with(avl, &tasks[i].avl, avl_compare, &policy);
    if (st != DS_OK)
      goto done;
    st = ds_rbt_insert_with(redblack, &tasks[i].redblack, rbt_compare, &policy);
    if (st != DS_OK)
      goto done;
  }
  printf("BST descending:");
  st = ds_bst_walk_with(plain, bst_visit, &policy);
  putchar('\n');
  if (st != DS_OK)
    goto done;
  printf("AVL descending:");
  st = ds_avl_walk_with(avl, avl_visit, &policy);
  putchar('\n');
  if (st != DS_OK)
    goto done;
  printf("Red-black descending:");
  st = ds_rbt_walk_with(redblack, rbt_visit, &policy);
  putchar('\n');
  if (st != DS_OK)
    goto done;
  key.priority = 3;
  st = ds_rbt_find_with(redblack, &key.redblack, rbt_compare, &policy, &found);
  if (st != DS_OK)
    goto done;
  printf("Found priority %d\n",
         DS_CONTAINER_OF(found, task_t, redblack)->priority);
  st = ds_bst_clone_with(plain, bst_clone, bst_drop, &policy, &context,
                         &plain_copy);
  if (st != DS_OK)
    goto done;
  st =
      ds_avl_clone_with(avl, avl_clone, avl_drop, &policy, &context, &avl_copy);
  if (st != DS_OK)
    goto done;
  st = ds_rbt_clone_with(redblack, rbt_clone, rbt_drop, &policy, &context,
                         &redblack_copy);
  if (st != DS_OK)
    goto done;
  printf("Cloned AVL preorder:");
  st = ds_avl_traverse_with(avl_copy, avl_copy->root, DS_TREE_PREORDER,
                            avl_visit, &policy);
  putchar('\n');
  if (st != DS_OK)
    goto done;
  printf("Cloned BST postorder:");
  st = ds_bst_traverse_with(plain_copy, plain_copy->root, DS_TREE_POSTORDER,
                            bst_visit, &policy);
  putchar('\n');
  if (st != DS_OK)
    goto done;
  policy.remaining = 2;
  printf("First two cloned red-black entries:");
  st = ds_rbt_walk_with(redblack_copy, rbt_visit, &policy);
  printf(" (%s)\n", ds_status_name(st));
  if (st == DS_STOP)
    st = DS_OK;
done:
  ds_bst_destroy_with(plain, NULL, NULL);
  ds_avl_destroy_with(avl, NULL, NULL);
  ds_rbt_destroy_with(redblack, NULL, NULL);
  ds_bst_destroy_with(plain_copy, bst_drop, &policy);
  ds_avl_destroy_with(avl_copy, avl_drop, &policy);
  ds_rbt_destroy_with(redblack_copy, rbt_drop, &policy);
  if (st != DS_OK) {
    fprintf(stderr, "Index operation failed: %s\n", ds_status_name(st));
    return 1;
  }
  return 0;
}
