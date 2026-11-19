/* A module defines its diagnostic vocabulary without a central registry. */
#include <dsa.h>
#include <stdio.h>
#define EXPIRED_FIELDS(X) X(CSTR, token_id) X(INT, expired_at)
DS_DIAGNOSTIC_DECLARE(captcha_expired, EXPIRED_FIELDS);
DS_DIAGNOSTIC_DEFINE(captcha_expired, "captcha/expired", DS_DIAG_NOTICE,
                     "captcha", "Token {token_id} expired at {expired_at}",
                     EXPIRED_FIELDS)
static ds_status_t retain_payload(const ds_result_value_t *a,
                                  const ds_result_value_t *b, void *user,
                                  ds_result_value_t *out) {
  (void)b;
  (void)user;
  return ds_result_value_copy(a, out);
}
static bool keep_all(const ds_diagnostic_t *d, void *user) {
  (void)d;
  (void)user;
  return true;
}
int main(void) {
  unsigned char bytes[8192];
  ds_arena_t arena;
  ds_context_t context;
  ds_diagnostic_t records[2];
  ds_diagnostic_snapshot_t *slots[2];
  ds_diagnostics_t log, filtered;
  ds_result_t result;
  captcha_expired_values values = {"abc", 1234}, readback;
  ds_status_t st;
  int rc = 1;
  ds_arena_init(&arena, bytes, sizeof(bytes));
  ds_context_init(&context);
  ds_context_use_arena(&context, &arena);
  ds_result_init(&result);
  ds_diagnostics_init(&filtered);
  st = ds_diagnostics_init_buffer(&log, records, slots, 2,
                                  DS_DIAGNOSTICS_TRUNCATE, &context);
  if (st != DS_OK) {
    return 1;
  }
  st = captcha_expired_add(&log, &values);
  if (st == DS_OK) {
    st = captcha_expired_add(&log, &values);
  }
  if (st == DS_OK) {
    st = captcha_expired_add(&log, &values);
  }
  if (st != DS_OK) {
    goto done;
  }
  st = captcha_expired_get(&log.items[0], &readback);
  if (st != DS_OK) {
    goto done;
  }
  printf("%s expired at %ld; retained=%lu dropped=%lu\n", readback.token_id,
         readback.expired_at, (unsigned long)log.size,
         (unsigned long)ds_diagnostics_dropped(&log));
  st = ds_diagnostics_filter(&log, keep_all, NULL, &filtered);
  if (st == DS_OK) {
    st = ds_diagnostics_filter(&filtered, keep_all, NULL, &log);
  }
  if (st == DS_OK) {
    st = ds_result_err_ptr(&result, NULL, &log, NULL);
  }
  if (st == DS_OK) {
    st = ds_result_zip(&result, &result, NULL, retain_payload, NULL, &result);
  }
  if (st != DS_OK) {
    goto done;
  }
  printf("Composed result: records=%lu dropped=%lu\n",
         (unsigned long)ds_result_diagnostic_count(&result),
         (unsigned long)ds_result_diagnostic_dropped(&result));
  rc = 0;
done:
  if (rc) {
    fprintf(stderr, "%s\n", ds_status_name(st));
  }
  ds_result_destroy(&result);
  ds_diagnostics_destroy(&filtered);
  ds_diagnostics_destroy(&log);
  ds_arena_reset(&arena);
  return rc;
}
