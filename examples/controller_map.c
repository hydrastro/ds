#include <dsaa.h>
#include <stdio.h>

static void expression(const ds_kmap_cover_t *c) {
  size_t i, bit;
  if (!c->target) {
    printf("NOT(");
  }
  if (c->count == 0) {
    printf("0");
  }
  for (i = 0; i < c->count; i++) {
    if (i > 0) {
      printf(" + ");
    }
    if (c->cubes[i].mask == 0) {
      printf("1");
    }
    for (bit = 0; bit < c->variables; bit++) {
      if ((c->cubes[i].mask & (1u << bit)) != 0) {
        printf("%s%c", (c->cubes[i].value & (1u << bit)) != 0 ? "" : "!",
               'A' + (int)bit);
      }
    }
  }
  if (!c->target) {
    printf(")");
  }
  printf("; %lu terms, %lu literals, %s\n", (unsigned long)c->count,
         (unsigned long)c->literals, c->optimal ? "minimum" : "best so far");
}
static ds_status_t gate_cost(const ds_kmap_cube_t *cube, unsigned long *out,
                             void *user) {
  unsigned int mask = cube->mask;
  unsigned long literals = 0;
  (void)user;
  while (mask != 0) {
    literals++;
    mask &= mask - 1;
  }
  *out = 1 + 2 * literals;
  return DS_OK;
}
static ds_status_t minimize(const ds_kmap_t *map, bool target,
                            const ds_kmap_policy_t *policy) {
  ds_kmap_cover_t cover;
  ds_status_t st = ds_kmap_minimize(map, target, policy, &cover, NULL);
  if (st != DS_OK && st != DS_STOP) {
    return st;
  }
  expression(&cover);
  ds_kmap_cover_destroy(&cover);
  return DS_OK;
}
int main(void) {
  unsigned char values[16];
  size_t order[] = {1, 0, 3, 2};
  ds_kmap_t map;
  ds_kmap_policy_t policy;
  size_t i, r, c;
  ds_status_t st;
  /* D OR (A AND B); C is an irrelevant input. Two configurations are unused.
   */
  for (i = 0; i < 16; i++) {
    values[i] = ((i & 8) != 0 || (i & 3) == 3) ? 1 : 0;
  }
  values[6] = values[14] = DS_KMAP_DONT_CARE;
  st = ds_kmap_init(&map, 4, 2, order, values);
  if (st != DS_OK) {
    goto fail;
  }
  puts("Gray-code rows BA, columns DC (* = don't care):");
  for (r = 0; r < 4; r++) {
    for (c = 0; c < 4; c++) {
      st = ds_kmap_assignment(&map, r, c, &i);
      if (st != DS_OK) {
        goto fail;
      }
      printf("%c ", values[i] == DS_KMAP_DONT_CARE ? '*' : '0' + values[i]);
    }
    putchar('\n');
  }
  ds_kmap_policy_init(&policy);
  printf("Positive cover: ");
  st = minimize(&map, true, &policy);
  if (st != DS_OK) {
    goto fail;
  }
  printf("Complemented zero cover: ");
  st = minimize(&map, false, &policy);
  if (st != DS_OK) {
    goto fail;
  }
  policy.cost = gate_cost;
  printf("Custom gate cost: ");
  st = minimize(&map, true, &policy);
  if (st != DS_OK) {
    goto fail;
  }
  policy.node_budget = 1;
  printf("One search node: ");
  st = minimize(&map, true, &policy);
  if (st != DS_OK) {
    goto fail;
  }
  return 0;
fail:
  fprintf(stderr, "Map failed: %s\n", ds_status_name(st));
  return 1;
}
