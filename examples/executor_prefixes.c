/* Noncommutative prefix composition through an executor's round barriers. */
#include <dsa.h>
#include <stdio.h>

typedef struct digits {
  unsigned long power, number;
} digits_t;
static void concatenate(void *out, const void *left, const void *right,
                        void *user) {
  const digits_t *a = (const digits_t *)left, *b = (const digits_t *)right;
  digits_t *result = (digits_t *)out;
  unsigned long power = a->power * b->power;
  unsigned long number = a->number * b->power + b->number;
  (void)user;
  result->power = power;
  result->number = number;
}
/* A small deterministic executor runs each round in reverse task order.
 * A thread pool can implement the same interface, joining before return. */
static void reverse_round(void *self, size_t tasks, ds_task_fn task,
                          void *arg) {
  size_t *rounds = (size_t *)self;
  (*rounds)++;
  while (tasks > 0)
    task(--tasks, arg);
}
int main(void) {
  digits_t input[7], output[7], scratch[7], total;
  const digits_t identity = {1, 0};
  ds_monoid_t monoid;
  ds_executor_t executor;
  ds_status_t st;
  size_t i, rounds = 0;
  for (i = 0; i < 7; i++) {
    input[i].power = 10;
    input[i].number = i + 1;
  }
  monoid.elem_size = sizeof(digits_t);
  monoid.identity = &identity;
  monoid.combine = concatenate;
  monoid.user = NULL;
  monoid.scratch = NULL;
  monoid.context = NULL;
  executor.run = reverse_round;
  executor.self = &rounds;
  executor.workers = 3;
  st =
      ds_scan_hillis_steele_with(&monoid, input, output, 7, scratch, &executor);
  if (st != DS_OK)
    goto done;
  printf("Inclusive:");
  for (i = 0; i < 7; i++)
    printf(" %lu", output[i].number);
  puts("");
  for (i = 0; i < 7; i++)
    output[i] = input[i];
  st = ds_scan_blelloch_with(&monoid, output, 7, &total, &executor);
  if (st != DS_OK)
    goto done;
  printf("Exclusive:");
  for (i = 0; i < 7; i++)
    printf(" %lu", output[i].number);
  printf("; total %lu in %lu executor rounds\n", total.number,
         (unsigned long)rounds);
  st = ds_reduce(&monoid, input, 7, &input[3]);
  if (st == DS_OK)
    printf("Reduction published into input slot 3: %lu\n", input[3].number);
done:
  if (st != DS_OK)
    fprintf(stderr, "prefixes: %s\n", ds_status_name(st));
  return st == DS_OK ? 0 : 1;
}
