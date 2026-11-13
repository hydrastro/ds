/*
 * Fully dynamic connectivity: a ring network loses and regains links,
 * and connectivity is answered after every change without recomputing
 * components. Deleting a link of the spanning forest triggers the search
 * for a replacement; deleting a link outside it costs almost nothing.
 */
#include "ds.h"
#include <stdio.h>

static void report(const ds_dynconn_t *g, const char *what) {
  printf("%-34s components %lu, 0~5 %s, 0~9 %s\n", what,
         (unsigned long)ds_dc_components(g),
         ds_dc_connected(g, 0, 5) ? "yes" : "no ",
         ds_dc_connected(g, 0, 9) ? "yes" : "no ");
}

int main(void) {
  ds_dynconn_t g;
  size_t i;
  if (ds_dc_init(&g, 10, NULL) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 10; i++) {
    (void)ds_dc_insert(&g, i, (i + 1) % 10); /* a ring */
  }
  report(&g, "ring of 10:");
  printf("  edge 9-0 in the spanning forest: %s\n",
         ds_dc_is_tree_edge(&g, 9, 0) ? "yes" : "no (it closed the ring)");
  (void)ds_dc_delete(&g, 2, 3);
  report(&g, "cut 2-3 (the ring still holds):");
  (void)ds_dc_delete(&g, 7, 8);
  report(&g, "cut 7-8 (two arcs now):");
  (void)ds_dc_insert(&g, 0, 5);
  report(&g, "link 0-5 (arcs joined again):");
  (void)ds_dc_delete(&g, 0, 9);
  (void)ds_dc_delete(&g, 0, 1);
  report(&g, "cut 0-9 and 0-1:");
  printf("component of 0 has %lu vertices; %lu promotions, %lu "
         "replacements\n",
         (unsigned long)ds_dc_component_size(&g, 0), g.promotions,
         g.replacements);
  ds_dc_destroy(&g);
  return 0;
}
