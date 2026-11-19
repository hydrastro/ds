/* Independent field validation, typed payload access, and selectable logs. */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

typedef struct issues {
  unsigned fields;
} issues_t;
DS_RESULT_DECLARE_POINTERS(validation_result, int, issues_t);
DS_RESULT_DEFINE_POINTERS(validation_result, int, issues_t)

static ds_status_t validate(int value, int minimum, unsigned field,
                            const char *name, bool flat, ds_context_t *context,
                            validation_result *out) {
  ds_result_config_t config;
  ds_diagnostics_t diagnostics;
  ds_diagnostic_t record;
  ds_schema_t schema;
  ds_result_value_t payload;
  ds_status_t st;
  issues_t issue;
  ds_result_config_init(&config, context);
  if (flat)
    config.diagnostics.ops = *ds_diagnostic_log_flat_ops();
  ds_diagnostics_init_context(&diagnostics, context);
  ds_result_value_init(&payload);
  memset(&record, 0, sizeof(record));
  record.id = name;
  record.level = value >= minimum ? DS_DIAG_INFO : DS_DIAG_ERROR;
  st = ds_diagnostics_add(&diagnostics, &record);
  if (st == DS_OK && value >= minimum) {
    ds_schema_init(&schema, sizeof(value));
    st = ds_result_value_from_schema(&payload, &value, &schema, context);
    if (st == DS_OK)
      st = ds_result_ok(&out->core, &payload, &diagnostics, &config);
  } else if (st == DS_OK) {
    issue.fields = field;
    ds_schema_init(&schema, sizeof(issue));
    st = ds_result_value_from_schema(&payload, &issue, &schema, context);
    if (st == DS_OK)
      st = ds_result_err(&out->core, &payload, &diagnostics, &config);
  }
  ds_result_value_release(&payload);
  ds_diagnostics_destroy(&diagnostics);
  /* config is local: the Result copies it, including the backend table. */
  return st;
}
static ds_status_t add_values(const ds_result_value_t *a,
                              const ds_result_value_t *b, void *user,
                              ds_result_value_t *out) {
  int sum = *(const int *)a->data + *(const int *)b->data;
  ds_schema_t schema;
  ds_schema_init(&schema, sizeof(sum));
  return ds_result_value_from_schema(out, &sum, &schema, (ds_context_t *)user);
}
static ds_status_t combine_issues(const ds_result_value_t *a,
                                  const ds_result_value_t *b, void *user,
                                  ds_result_value_t *out) {
  issues_t issues;
  ds_schema_t schema;
  issues.fields = ((const issues_t *)a->data)->fields |
                  ((const issues_t *)b->data)->fields;
  ds_schema_init(&schema, sizeof(issues));
  return ds_result_value_from_schema(out, &issues, &schema,
                                     (ds_context_t *)user);
}
static void print_record(ds_diagnostic_sink_t *sink,
                         const ds_diagnostic_t *d) {
  (void)sink;
  printf("  %s: %s\n", d->id, ds_diagnostic_level_name(d->level));
}
static ds_status_t run(int age, int quota, bool flat, bool right_flat,
                       ds_context_t *context) {
  validation_result left, right, saved;
  ds_status_t st;
  const int *number;
  const issues_t *issues;
  ds_diagnostic_sink_t sink;
  validation_result_init(&left);
  validation_result_init(&right);
  validation_result_init(&saved);
  memset(&sink, 0, sizeof(sink));
  sink.emit = print_record;
  st = validate(age, 18, 1, "age", flat, context, &left);
  if (st == DS_OK)
    st = validate(quota, 0, 2, "quota", right_flat, context, &right);
  /* Inputs are already computed: no validation is skipped. Mixed backends
   * preserve order; the left Result chooses the output storage policy.
   * Replacing an input in place is supported. */
  if (st == DS_OK)
    st = ds_result_zip(&left.core, &right.core, add_values, combine_issues,
                       context, &left.core);
  if (st == DS_OK)
    st = validation_result_copy(&left, &saved);
  validation_result_destroy(&left);
  validation_result_destroy(&right);
  if (st == DS_OK) {
    printf("age=%d, quota=%d, log=%s: ", age, quota, flat ? "flat" : "RRB");
    if (ds_result_is_ok(&saved.core)) {
      st = validation_result_unwrap(&saved, &number);
      if (st == DS_OK)
        printf("accepted, sum=%d\n", *number);
    } else {
      st = validation_result_error(&saved, &issues);
      if (st == DS_OK)
        printf("invalid field mask=%u\n", issues->fields);
    }
    if (st == DS_OK)
      st = ds_result_drain_to(&saved.core, &sink);
  }
  validation_result_destroy(&saved);
  return st;
}
int main(void) {
  ds_context_t context;
  ds_debug_allocator_t debug;
  ds_debug_allocator_stats_t stats;
  ds_status_t st = DS_OK;
  size_t i;
  int inputs[][2] = {{12, -2}, {24, -2}, {12, 10}, {24, 10}};
  ds_context_init(&context);
  ds_debug_allocator_init(&debug, NULL);
  ds_context_use_debug_allocator(&context, &debug);
  for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]) && st == DS_OK; ++i) {
    st = run(inputs[i][0], inputs[i][1], false, false, &context);
    if (st == DS_OK)
      st = run(inputs[i][0], inputs[i][1], true, true, &context);
  }
  if (st == DS_OK)
    st = run(24, 10, false, true, &context);
  if (st == DS_OK)
    st = run(24, 10, true, false, &context);
  ds_debug_allocator_get_stats(&debug, &stats);
  printf("Live bytes after releasing Results: %lu\n",
         (unsigned long)stats.bytes_live);
  if (st != DS_OK)
    fprintf(stderr, "Operation failed: %s\n", ds_status_name(st));
  return st != DS_OK || stats.bytes_live != 0;
}
