/* Intrusive sequence policies: owning clones, borrowed records and early
 * stops.
 */
#include <dsa/structures/circular_buffer.h>
#include <dsa/structures/deque.h>
#include <dsa/structures/dlist.h>
#include <dsa/structures/heap.h>
#include <dsa/structures/list.h>
#include <dsa/structures/queue.h>
#include <dsa/structures/ring_list.h>
#include <dsa/structures/stack.h>
#include <stdio.h>
#include <stdlib.h>

struct policy {
  size_t budget;
  size_t released;
};
struct list_record {
  ds_list_node_t node;
  int id;
};
static ds_status_t list_visit(ds_list_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct list_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t list_copy(ds_list_node_t *node, ds_list_node_t **out,
                             void *user) {
  struct list_record *copy = (struct list_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct list_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void list_release(ds_list_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_list(void) {
  ds_list_t *original = NULL, *copy = NULL;
  struct list_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_list_create_context(NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_list_append_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  st = ds_list_clone_with(original, list_copy, list_release, &policy, NULL,
                          &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("list original:");
  (void)ds_list_walk_with(original, list_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_list_walk_reverse_with(copy, list_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_list_walk_with(copy, list_visit, &policy);
  rc = 0;
done:
  ds_list_destroy_with(copy, list_release, &policy);
  ds_list_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct dlist_record {
  ds_dlist_node_t node;
  int id;
};
static ds_status_t dlist_visit(ds_dlist_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct dlist_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t dlist_copy(ds_dlist_node_t *node, ds_dlist_node_t **out,
                              void *user) {
  struct dlist_record *copy = (struct dlist_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct dlist_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void dlist_release(ds_dlist_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_dlist(void) {
  ds_dlist_t *original = NULL, *copy = NULL;
  struct dlist_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_dlist_create_context(NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_dlist_append_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  st = ds_dlist_clone_with(original, dlist_copy, dlist_release, &policy, NULL,
                           &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("dlist original:");
  (void)ds_dlist_walk_with(original, dlist_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_dlist_walk_reverse_with(copy, dlist_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_dlist_walk_with(copy, dlist_visit, &policy);
  rc = 0;
done:
  ds_dlist_destroy_with(copy, dlist_release, &policy);
  ds_dlist_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct ring_list_record {
  ds_ring_list_node_t node;
  int id;
};
static ds_status_t ring_list_visit(ds_ring_list_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct ring_list_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t ring_list_copy(ds_ring_list_node_t *node,
                                  ds_ring_list_node_t **out, void *user) {
  struct ring_list_record *copy =
      (struct ring_list_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct ring_list_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void ring_list_release(ds_ring_list_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_ring_list(void) {
  ds_ring_list_t *original = NULL, *copy = NULL;
  struct ring_list_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_ring_list_create_context(NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_ring_list_push_back_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  st = ds_ring_list_clone_with(original, ring_list_copy, ring_list_release,
                               &policy, NULL, &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("ring_list original:");
  (void)ds_ring_list_walk_with(original, ring_list_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_ring_list_walk_reverse_with(copy, ring_list_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_ring_list_walk_with(copy, ring_list_visit, &policy);
  rc = 0;
done:
  ds_ring_list_destroy_with(copy, ring_list_release, &policy);
  ds_ring_list_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct stack_record {
  ds_stack_node_t node;
  int id;
};
static ds_status_t stack_visit(ds_stack_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct stack_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t stack_copy(ds_stack_node_t *node, ds_stack_node_t **out,
                              void *user) {
  struct stack_record *copy = (struct stack_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct stack_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void stack_release(ds_stack_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_stack(void) {
  ds_stack_t *original = NULL, *copy = NULL;
  struct stack_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_stack_create_context(NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_stack_push_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  st = ds_stack_clone_with(original, stack_copy, stack_release, &policy, NULL,
                           &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("stack original:");
  (void)ds_stack_walk_with(original, stack_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_stack_walk_reverse_with(copy, stack_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_stack_walk_with(copy, stack_visit, &policy);
  rc = 0;
done:
  ds_stack_destroy_with(copy, stack_release, &policy);
  ds_stack_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct queue_record {
  ds_queue_node_t node;
  int id;
};
static ds_status_t queue_visit(ds_queue_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct queue_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t queue_copy(ds_queue_node_t *node, ds_queue_node_t **out,
                              void *user) {
  struct queue_record *copy = (struct queue_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct queue_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void queue_release(ds_queue_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_queue(void) {
  ds_queue_t *original = NULL, *copy = NULL;
  struct queue_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_queue_create_context(NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_queue_enqueue_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  st = ds_queue_clone_with(original, queue_copy, queue_release, &policy, NULL,
                           &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("queue original:");
  (void)ds_queue_walk_with(original, queue_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_queue_walk_reverse_with(copy, queue_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_queue_walk_with(copy, queue_visit, &policy);
  rc = 0;
done:
  ds_queue_destroy_with(copy, queue_release, &policy);
  ds_queue_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct deque_record {
  ds_deque_node_t node;
  int id;
};
static ds_status_t deque_visit(ds_deque_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct deque_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t deque_copy(ds_deque_node_t *node, ds_deque_node_t **out,
                              void *user) {
  struct deque_record *copy = (struct deque_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct deque_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void deque_release(ds_deque_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_deque(void) {
  ds_deque_t *original = NULL, *copy = NULL;
  struct deque_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_deque_create_context(NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_deque_push_back_ex(original, &records[i].node) != DS_OK) {
      goto done;
    }
  }
  st = ds_deque_clone_with(original, deque_copy, deque_release, &policy, NULL,
                           &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("deque original:");
  (void)ds_deque_walk_with(original, deque_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_deque_walk_reverse_with(copy, deque_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_deque_walk_with(copy, deque_visit, &policy);
  rc = 0;
done:
  ds_deque_destroy_with(copy, deque_release, &policy);
  ds_deque_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct circular_buffer_record {
  ds_circular_buffer_node_t node;
  int id;
};
static ds_status_t circular_buffer_visit(ds_circular_buffer_node_t *node,
                                         void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct circular_buffer_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t circular_buffer_copy(ds_circular_buffer_node_t *node,
                                        ds_circular_buffer_node_t **out,
                                        void *user) {
  struct circular_buffer_record *copy =
      (struct circular_buffer_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct circular_buffer_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void circular_buffer_release(ds_circular_buffer_node_t *node,
                                    void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int show_circular_buffer(void) {
  ds_circular_buffer_t *original = NULL, *copy = NULL;
  struct circular_buffer_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;

  if (ds_circular_buffer_create_context(4, false, NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_circular_buffer_push_ex(original, &records[i].node, NULL) !=
        DS_OK) {
      goto done;
    }
  }
  st = ds_circular_buffer_clone_with(original, circular_buffer_copy,
                                     circular_buffer_release, &policy, NULL,
                                     &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("circular_buffer original:");
  (void)ds_circular_buffer_walk_with(original, circular_buffer_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_circular_buffer_walk_reverse_with(copy, circular_buffer_visit,
                                            &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_circular_buffer_walk_with(copy, circular_buffer_visit, &policy);
  rc = 0;
done:
  ds_circular_buffer_destroy_with(copy, circular_buffer_release, &policy);
  ds_circular_buffer_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
struct heap_record {
  ds_heap_node_t node;
  int id;
};
static ds_status_t heap_visit(ds_heap_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  printf(" %d", ((struct heap_record *)node)->id);
  if (policy->budget != 0 && --policy->budget == 0) {
    return DS_STOP;
  }
  return DS_OK;
}
static ds_status_t heap_copy(ds_heap_node_t *node, ds_heap_node_t **out,
                             void *user) {
  struct heap_record *copy = (struct heap_record *)malloc(sizeof(*copy));
  (void)user;
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  copy->id = ((struct heap_record *)node)->id;
  *out = &copy->node;
  return DS_OK;
}
static void heap_release(ds_heap_node_t *node, void *user) {
  struct policy *policy = (struct policy *)user;
  policy->released += 1;
  free(node);
}
static int heap_compare(ds_heap_node_t *a, ds_heap_node_t *b, void *user) {
  int direction = *(int *)user;
  int x = ((struct heap_record *)a)->id, y = ((struct heap_record *)b)->id;
  return direction * ((x > y) - (x < y));
}
static int show_heap(void) {
  ds_heap_t *original = NULL, *copy = NULL;
  struct heap_record records[3];
  struct policy policy = {0, 0};
  ds_status_t st;
  size_t i;
  int rc = 1;
  int direction = 1;
  if (ds_heap_create_context(4, NULL, &original) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 3; ++i) {
    records[i].id = (int)i + 1;
    if (ds_heap_insert_with(original, &records[i].node, heap_compare,
                            &direction) != DS_OK) {
      goto done;
    }
  }
  st = ds_heap_clone_with(original, heap_copy, heap_release, &policy, NULL,
                          &copy);
  if (st != DS_OK) {
    goto done;
  }
  printf("heap original:");
  (void)ds_heap_walk_with(original, heap_visit, &policy);
  printf("; last two cloned entries:");
  policy.budget = 2;
  st = ds_heap_walk_reverse_with(copy, heap_visit, &policy);
  printf(" (%s); clone forward:", ds_status_name(st));
  policy.budget = 0;
  (void)ds_heap_walk_with(copy, heap_visit, &policy);
  rc = 0;
done:
  ds_heap_destroy_with(copy, heap_release, &policy);
  ds_heap_destroy(original, NULL);
  printf("; released %lu cloned records\n", (unsigned long)policy.released);
  return rc;
}
int main(void) {
  if (show_list() != 0) {
    return 1;
  }
  if (show_dlist() != 0) {
    return 1;
  }
  if (show_ring_list() != 0) {
    return 1;
  }
  if (show_stack() != 0) {
    return 1;
  }
  if (show_queue() != 0) {
    return 1;
  }
  if (show_deque() != 0) {
    return 1;
  }
  if (show_circular_buffer() != 0) {
    return 1;
  }
  if (show_heap() != 0) {
    return 1;
  }
  return 0;
}
