/*
 * One tree in three succinct forms: LOUDS (level order), balanced
 * parentheses (preorder, range min-max tree) and DFUDS (preorder, unary
 * degrees). Each answers navigation in about 2 bits per node plus its
 * directories; they number nodes differently, so each reports which input
 * node it means.
 */
#include "ds.h"
#include <stdio.h>

/*        0
 *      / | \
 *     1  2  3
 *    /|     |
 *   4 5     6
 *          /|\
 *         7 8 9
 */
static const size_t parent[10] = {DS_BP_NONE, 0, 0, 0, 1, 1, 3, 6, 6, 6};

int main(void) {
  ds_bp_tree_t bp;
  ds_dfuds_t df;
  size_t bp_num[10];
  size_t df_num[10];
  size_t bp_of[10]; /* preorder number -> input node */
  size_t i;
  if (ds_bp_from_parents(&bp, parent, 10, bp_num, NULL, NULL) != DS_OK ||
      ds_dfuds_from_parents(&df, parent, 10, df_num, NULL, NULL) != DS_OK) {
    return 1;
  }
  for (i = 0; i < 10; i++) {
    bp_of[bp_num[i]] = i;
  }
  printf("balanced parentheses:");
  for (i = 0; i < bp.p.n; i++) {
    printf("%c", ds_parens_bit(&bp.p, i) ? '(' : ')');
  }
  printf("\nDFUDS:                ");
  for (i = 0; i < df.p.n; i++) {
    printf("%c", ds_parens_bit(&df.p, i) ? '(' : ')');
  }
  printf("\n\nnode  depth  height  subtree  degree  postorder\n");
  for (i = 0; i < 10; i++) {
    size_t v = bp_num[i];
    printf("%4lu  %5lu  %6lu  %7lu  %6lu  %9lu\n", (unsigned long)i,
           (unsigned long)ds_bp_depth(&bp, v),
           (unsigned long)ds_bp_height(&bp, v),
           (unsigned long)ds_bp_subtree_size(&bp, v),
           (unsigned long)ds_dfuds_degree(&df, df_num[i]),
           (unsigned long)ds_bp_postorder(&bp, v));
  }
  printf("\nlca(4, 9) = %lu, lca(7, 9) = %lu (BP)\n",
         (unsigned long)bp_of[ds_bp_lca(&bp, bp_num[4], bp_num[9])],
         (unsigned long)bp_of[ds_bp_lca(&bp, bp_num[7], bp_num[9])]);
  printf("ancestor of 8 two levels up: %lu (BP)\n",
         (unsigned long)bp_of[ds_bp_level_ancestor(&bp, bp_num[8], 2)]);
  printf("third child of 6: %s (DFUDS: one select and one find_close)\n",
         ds_dfuds_child(&df, df_num[6], 2) == df_num[9] ? "9" : "?");
  printf("%lu bytes as BP, %lu as DFUDS\n", (unsigned long)ds_bp_bytes(&bp),
         (unsigned long)ds_dfuds_bytes(&df));
  ds_bp_destroy(&bp);
  ds_dfuds_destroy(&df);
  return 0;
}
