/*
 * Intrusive singly linked list.
 *
 * The defining idea of an intrusive container: you embed the library's node
 * struct as a member of your own struct, hand the address of that member to the
 * list, and recover your struct from a node pointer with CAST(). The list never
 * allocates your elements, so you stay in full control of their memory.
 */
#include "ds.h"
#include <stdio.h>

typedef struct task {
  list_node_t node; /* embed the list node */
  const char *name;
  int priority;
} task_t;

int main(void) {
  list_t *list = ds_list_create();
  task_t a;
  task_t b;
  task_t c;
  list_node_t *it;

  a.name = "compile";
  a.priority = 2;
  b.name = "test";
  b.priority = 1;
  c.name = "ship";
  c.priority = 3;

  /* append the embedded nodes (no allocation by the list) */
  ds_list_append(list, &a.node);
  ds_list_append(list, &b.node);
  ds_list_append(list, &c.node);

  /* walk from head to the nil sentinel, recovering each task with CAST() */
  printf("tasks in insertion order:\n");
  for (it = list->head; it != list->nil; it = it->next) {
    task_t *t = CAST(it, task_t);
    printf("  [%d] %s\n", t->priority, t->name);
  }

  /* destroy with a NULL callback: the list owns none of the element memory
   * here (the tasks are stack objects), so nothing is freed for us */
  ds_list_destroy(list, NULL);
  return 0;
}
