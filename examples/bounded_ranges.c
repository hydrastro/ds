#include "ds.h"
#include <limits.h>
#include <stdio.h>

typedef struct queued_event {
  ds_circular_buffer_node_t node;
  int id;
} queued_event_t;

int main(void) {
  ds_context_t context;
  ds_bitset_t *flags = NULL;
  ds_bitvector_t *index = NULL;
  ds_fenwick_tree_t *balances = NULL;
  ds_wavelet_tree_t *symbols = NULL;
  ds_circular_buffer_t *queue = NULL;
  ds_circular_buffer_node_t *displaced = NULL, *taken = NULL;
  queued_event_t events[3];
  const long amounts[] = {12, 7, -3, 10};
  const size_t sequence[] = {3, 1, 3, 2, 1};
  ds_status_t st;
  size_t i, rank, position;
  long total;
  bool flag;
  ds_context_init(&context);
  st = ds_bitset_create_context(8, &context, &flags);
  if (st != DS_OK)
    goto done;
  st = ds_bitset_set_ex(flags, 3, true);
  if (st != DS_OK)
    goto done;
  st = ds_bitset_get_ex(flags, 3, &flag);
  if (st != DS_OK)
    goto done;
  printf("Flag 3: %s\n", flag ? "set" : "clear");
  st = ds_bitvector_create_context(8, &context, &index);
  if (st != DS_OK)
    goto done;
  for (i = 1; i < 7; i += 2) {
    st = ds_bitvector_set_ex(index, i, true);
    if (st != DS_OK)
      goto done;
  }
  st = ds_bitvector_build_ex(index);
  if (st != DS_OK)
    goto done;
  st = ds_bitvector_rank1_ex(index, 6, &rank);
  if (st != DS_OK)
    goto done;
  st = ds_bitvector_select1_ex(index, 1, &position);
  if (st != DS_OK)
    goto done;
  printf("Rank before 6: %lu; second set bit: %lu\n", (unsigned long)rank,
         (unsigned long)position);
  printf("Write after freeze: %s\n",
         ds_status_name(ds_bitvector_set_ex(index, 4, true)));
  st = ds_fenwick_tree_create_context(4, &context, &balances);
  if (st != DS_OK)
    goto done;
  for (i = 0; i < 4; i++) {
    st = ds_fenwick_tree_set_ex(balances, i, amounts[i]);
    if (st != DS_OK)
      goto done;
  }
  st = ds_fenwick_tree_range_ex(balances, 1, 3, &total);
  if (st != DS_OK)
    goto done;
  printf("Balance over [1,3): %ld\n", total);
  printf("Oversized adjustment: %s\n",
         ds_status_name(ds_fenwick_tree_update_ex(balances, 0, LONG_MAX)));
  st = ds_fenwick_tree_prefix_ex(balances, 4, &total);
  if (st != DS_OK)
    goto done;
  printf("Total after rejected adjustment: %ld\n", total);
  st = ds_wavelet_tree_create_context(sequence, 5, 4, &context, &symbols);
  if (st != DS_OK)
    goto done;
  printf("Symbol 3 in first four positions: %lu\n",
         (unsigned long)ds_wavelet_tree_rank(symbols, 3, 4));
  st = ds_circular_buffer_create_context(2, true, &context, &queue);
  if (st != DS_OK)
    goto done;
  for (i = 0; i < 3; i++) {
    events[i].id = (int)i + 1;
    st = ds_circular_buffer_push_ex(queue, &events[i].node, &displaced);
    if (st != DS_OK)
      goto done;
    if (displaced != NULL)
      printf("Evicted event: %d\n",
             DS_CONTAINER_OF(displaced, queued_event_t, node)->id);
  }
  st = ds_circular_buffer_pop_ex(queue, &taken);
  if (st != DS_OK)
    goto done;
  printf("Next event: %d\n", DS_CONTAINER_OF(taken, queued_event_t, node)->id);
done:
  if (flags != NULL)
    ds_bitset_destroy(flags);
  if (index != NULL)
    ds_bitvector_destroy(index);
  if (balances != NULL)
    ds_fenwick_tree_destroy(balances);
  if (symbols != NULL)
    ds_wavelet_tree_destroy(symbols);
  if (queue != NULL)
    ds_circular_buffer_destroy(queue, NULL);
  if (st != DS_OK) {
    fprintf(stderr, "Range operation failed: %s\n", ds_status_name(st));
    return 1;
  }
  return 0;
}
