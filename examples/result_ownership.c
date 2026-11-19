/* Owned subviews, immutable log sharing, and recoverable allocation failure.
 */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

typedef struct measurement {
  int count;
  char unit[8];
} measurement_t;
static ds_status_t count_view(const ds_result_value_t *value, void *user,
                              ds_result_value_t *out) {
  measurement_t *m = (measurement_t *)value->data;
  (void)user;
  return ds_result_value_view(value, &m->count, out);
}
static ds_status_t keep_left(const ds_result_value_t *a,
                             const ds_result_value_t *b, void *user,
                             ds_result_value_t *out) {
  (void)b;
  (void)user;
  return ds_result_value_copy(a, out);
}
static ds_status_t print_count(ds_diagnostic_snapshot_t *snapshot,
                               void *user) {
  size_t *count = (size_t *)user;
  (void)snapshot;
  ++*count;
  return DS_OK;
}
int main(void) {
  measurement_t sample = {7, "files"};
  ds_result_t source, view, destination, empty;
  ds_result_value_t payload;
  ds_result_config_t config;
  ds_schema_t schema;
  ds_diagnostics_t diagnostics;
  ds_diagnostic_t event;
  ds_arena_t arena;
  ds_context_t exhausted, context;
  ds_debug_allocator_t debug;
  ds_debug_allocator_stats_t stats;
  ds_status_t st = DS_OK;
  const void *value;
  size_t i, visited = 0;
  int fallback = 42, exit_code = 1;

  ds_context_init(&context);
  ds_debug_allocator_init(&debug, NULL);
  ds_context_use_debug_allocator(&context, &debug);
  ds_result_init(&source);
  ds_result_init(&view);
  ds_result_init(&destination);
  ds_result_init(&empty);
  ds_result_value_init(&payload);
  ds_diagnostics_init_context(&diagnostics, &context);
  ds_result_config_init(&config, &context);
  ds_schema_init(&schema, sizeof(sample));
  memset(&event, 0, sizeof(event));
  event.id = "sample/read";
  event.level = DS_DIAG_INFO;
  for (i = 0; i < 40 && st == DS_OK; ++i)
    st = ds_diagnostics_add(&diagnostics, &event);
  if (st == DS_OK)
    st = ds_result_value_from_schema(&payload, &sample, &schema, &context);
  if (st == DS_OK)
    st = ds_result_ok(&source, &payload, &diagnostics, &config);
  ds_result_value_release(&payload);
  if (st == DS_OK)
    st = ds_result_map(&source, count_view, NULL, &view);
  ds_result_destroy(&source);
  if (st != DS_OK)
    goto done;
  st = ds_result_unwrap(&view, &value);
  if (st != DS_OK)
    goto done;
  printf("Count after releasing original owner handle: %d\n",
         *(const int *)value);
  /* Joining the same immutable log twice keeps both occurrences in order. */
  st = ds_result_zip(&view, &view, keep_left, NULL, NULL, &view);
  if (st != DS_OK)
    goto done;
  st = ds_diagnostic_log_walk(&view.diagnostics, print_count, &visited);
  if (st != DS_OK)
    goto done;
  printf("Shared log records visited: %lu\n", (unsigned long)visited);

  ds_arena_init(&arena, NULL, 0);
  ds_context_init(&exhausted);
  ds_context_use_arena(&exhausted, &arena);
  ds_result_config_init(&config, &exhausted);
  /* An empty borrowed Result needs no allocation, even with zero capacity. */
  st = ds_result_err_ptr(&empty, NULL, NULL, &config);
  if (st != DS_OK)
    goto done;
  printf("NULL domain error is valid: %s\n",
         ds_result_is_err(&empty) ? "yes" : "no");
  st = ds_result_ok_ptr(&destination, &fallback, NULL, NULL);
  if (st != DS_OK)
    goto done;
  st = ds_result_ok_ptr(&destination, &sample, &diagnostics, &config);
  printf("Creating a log in an exhausted context: %s\n", ds_status_name(st));
  if (st != DS_ERR_ALLOC)
    goto done;
  st = ds_result_unwrap(&destination, &value);
  if (st != DS_OK)
    goto done;
  printf("Previous output is still available: %d\n", *(const int *)value);
  exit_code = 0;
done:
  if (exit_code != 0)
    fprintf(stderr, "Operation failed: %s\n", ds_status_name(st));
  ds_result_value_release(&payload);
  ds_result_destroy(&source);
  ds_result_destroy(&view);
  ds_result_destroy(&destination);
  ds_result_destroy(&empty);
  ds_diagnostics_destroy(&diagnostics);
  ds_debug_allocator_get_stats(&debug, &stats);
  printf("Live owned bytes: %lu\n", (unsigned long)stats.bytes_live);
  return exit_code != 0 || stats.bytes_live != 0;
}
