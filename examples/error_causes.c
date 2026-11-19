/* Capture a native error and preserve it under a higher-level failure. */
#include <dsa.h>
#include <errno.h>
#include <stdio.h>
int main(void) {
  ds_error_t error;
  ds_context_t context;
  ds_debug_allocator_t debug;
  ds_debug_allocator_stats_t stats;
  ds_result_config_t config;
  ds_error_snapshot_t *cause = NULL, *outer = NULL;
  ds_result_t result;
  ds_status_t st;
  const ds_error_snapshot_t *current;
  int rc = 1;
  ds_context_init(&context);
  ds_debug_allocator_init(&debug, NULL);
  ds_context_use_debug_allocator(&context, &debug);
  ds_result_config_init(&config, &context);
  ds_result_init(&result);
  ds_error_set(&error, DS_ERR_IO, "storage/denied", "storage", "Access denied",
               NULL, 0, NULL);
  error.category = DS_ERROR_IO;
  error.native_domain = "posix.errno";
  error.native_code = EACCES;
  st = ds_error_snapshot_create(&error, NULL, &context, &cause);
  if (st != DS_OK) {
    goto done;
  }
  ds_error_set(&error, DS_ERR_IO, "index/open", "index",
               "Cannot open the index", NULL, 0, NULL);
  error.category = DS_ERROR_IO;
  st = ds_error_snapshot_create(&error, cause, &context, &outer);
  if (st == DS_OK) {
    st = ds_result_err_snapshot(&result, outer, NULL, &config);
  }
  ds_error_snapshot_release(cause);
  cause = NULL;
  ds_error_snapshot_release(outer);
  outer = NULL;
  if (st != DS_OK) {
    goto done;
  }
  st = ds_result_error_snapshot(&result, &current);
  if (st != DS_OK) {
    goto done;
  }
  while (current != NULL) {
    const ds_error_t *view = ds_error_snapshot_get(current);
    printf("%s: %s", view->id, view->message);
    if (view->native_domain != NULL) {
      printf(" (%s:%ld)", view->native_domain, view->native_code);
    }
    puts("");
    current = ds_error_snapshot_cause(current);
  }
  rc = 0;
done:
  if (rc) {
    fprintf(stderr, "%s\n", ds_status_name(st));
  }
  ds_result_destroy(&result);
  ds_error_snapshot_release(cause);
  ds_error_snapshot_release(outer);
  ds_debug_allocator_get_stats(&debug, &stats);
  printf("Live error bytes: %lu\n", (unsigned long)stats.bytes_live);
  return rc != 0 || stats.bytes_live != 0;
}
