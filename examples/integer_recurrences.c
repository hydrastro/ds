/* Exact recurrences and congruences at the machine-word boundary. */
#include <dsaa.h>
#include <limits.h>
#include <stdio.h>

int main(void) {
  unsigned long n = 0, value = 0, last = 0, kvalue, modulus = ULONG_MAX;
  unsigned long residue = ULONG_MAX - 1, solution, period;
  ds_status_t st;
  while (ds_fibonacci(n, &value) == DS_OK) {
    last = value;
    n++;
  }
  st = ds_kbonacci(2, n - 1, &kvalue);
  if (st != DS_OK) {
    goto done;
  }
  printf("Largest word-sized Fibonacci term: F(%lu) = %lu\n", n - 1, last);
  printf("The two-step recurrence gives %lu\n", kvalue);
  printf("F(100) modulo ULONG_MAX = %lu\n", ds_fibonacci_mod(100, modulus));
  st = ds_ackermann(3, sizeof(unsigned long) * CHAR_BIT - 3, &value);
  if (st != DS_OK) {
    goto done;
  }
  printf("Boundary Ackermann value: %lu\n", value);
  printf("Ackermann with ULONG_MAX input: %s\n",
         ds_status_name(ds_ackermann(3, ULONG_MAX, &value)));
  st = ds_crt(&residue, &modulus, 1, &solution, &period);
  if (st == DS_OK) {
    printf("Congruence: %lu modulo %lu\n", solution, period);
  }
done:
  if (st != DS_OK) {
    fprintf(stderr, "recurrences: %s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
