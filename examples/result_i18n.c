/* Immutable Result pipelines, owned diagnostic arguments, and translation
 * at the presentation boundary. No process locale or interpreter is needed. */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static ds_status_t record_count(ds_diagnostics_t *log, const char *id,
                                ds_diagnostic_level_t level, size_t count) {
  ds_diagnostic_t event;
  ds_diagnostic_arg_t arg;
  memset(&event, 0, sizeof(event));
  arg.name = "count";
  arg.type = DS_ARG_SIZE;
  arg.value.size = count;
  event.id = id;
  event.level = level;
  event.args = &arg;
  event.arg_count = 1;
  /* The collector copies this stack-local argument before returning. */
  return ds_diagnostics_add(log, &event);
}

static ds_status_t double_count(const ds_result_value_t *input, void *user,
                                ds_result_value_t *out) {
  ds_schema_t schema;
  size_t value = *(const size_t *)input->data;
  (void)user;
  if (value > (size_t)-1 / 2)
    return DS_ERR_OVERFLOW;
  value *= 2;
  ds_schema_init(&schema, sizeof(value));
  return ds_result_value_from_schema(out, &value, &schema, NULL);
}

static ds_status_t accept_count(const ds_result_value_t *input, void *user,
                                ds_result_t *out) {
  const ds_result_config_t *config = (const ds_result_config_t *)user;
  ds_diagnostics_t log;
  ds_status_t st;
  ds_diagnostics_init(&log);
  st = record_count(&log, "demo/accepted", DS_DIAG_WARNING,
                    *(const size_t *)input->data);
  /* A warning does not turn a successful domain result into an error.
   * ok retains input's owner; no borrowed pointer into a temporary result. */
  if (st == DS_OK)
    st = ds_result_ok(out, input, &log, config);
  ds_diagnostics_destroy(&log);
  return st;
}

static ds_status_t format_files(const ds_i18n_request_t *request,
                                ds_i18n_writer_t *writer, void *user) {
  size_t i, count = 0;
  const char *text;
  (void)user;
  for (i = 0; i < request->arg_count; i++) {
    if (request->args[i].name != NULL &&
        strcmp(request->args[i].name, "count") == 0 &&
        request->args[i].type == DS_ARG_SIZE)
      count = request->args[i].value.size;
  }
  if (strcmp(request->locale, "it") == 0)
    text = count == 1 ? "{count} file pronto" : "{count} file pronti";
  else
    text = count == 1 ? "{count} file ready" : "{count} files ready";
  return ds_i18n_interpolate(writer, text, request->args, request->arg_count);
}

static void print_event(ds_diagnostic_sink_t *sink,
                        const ds_diagnostic_t *event) {
  ds_i18n_t *translator = (ds_i18n_t *)sink->user;
  ds_i18n_text_t text;
  ds_status_t st;
  ds_i18n_text_init(&text, NULL);
  st = ds_i18n_render_diagnostic(translator, event, &text);
  if (st == DS_OK)
    printf("  %s\n", ds_i18n_text_cstr(&text));
  else
    fprintf(stderr, "diagnostic rendering failed: %d\n", (int)st);
  ds_i18n_text_destroy(&text);
}

#define TRY(call)                                                              \
  do {                                                                         \
    st = (call);                                                               \
    if (DS_FAILED(st)) {                                                       \
      fprintf(stderr, "%s failed: %d\n", #call, (int)st);                      \
      result = 1;                                                              \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)

int main(void) {
  ds_result_t received, doubled, accepted, null_error;
  ds_result_value_t value;
  ds_result_config_t result_config;
  ds_diagnostics_t log;
  ds_schema_t schema;
  ds_i18n_t translator;
  ds_i18n_config_t translation_config;
  ds_i18n_message_t formatter;
  ds_i18n_text_t text;
  ds_diagnostic_sink_t sink;
  ds_diagnostic_arg_t count_arg;
  ds_status_t st;
  const void *payload = NULL;
  size_t count = 3;
  int result = 0;

  ds_result_init(&received);
  ds_result_init(&doubled);
  ds_result_init(&accepted);
  ds_result_init(&null_error);
  ds_result_value_init(&value);
  ds_diagnostics_init(&log);
  ds_i18n_text_init(&text, NULL);
  ds_result_config_init(&result_config, NULL);
  result_config.diagnostics.bits = 4; /* 16-way persistent diagnostic log */
  ds_i18n_config_init(&translation_config);
  translation_config.map = ds_map_rh_ops();
  TRY(ds_i18n_init(&translator, &translation_config));

  ds_schema_init(&schema, sizeof(count));
  TRY(ds_result_value_from_schema(&value, &count, &schema, NULL));
  TRY(record_count(&log, "demo/received", DS_DIAG_NOTICE, count));
  TRY(ds_result_ok(&received, &value, &log, &result_config));
  ds_result_value_release(&value);
  ds_diagnostics_clear(&log);
  TRY(ds_result_map(&received, double_count, NULL, &doubled));
  TRY(ds_result_and_then(&doubled, accept_count, &result_config, &accepted));
  ds_result_destroy(&received);
  ds_result_destroy(&doubled);
  TRY(ds_result_unwrap(&accepted, &payload));
  printf("Accepted count: %lu; ordered diagnostics: %lu\n",
         (unsigned long)*(const size_t *)payload,
         (unsigned long)ds_result_diagnostic_count(&accepted));

  TRY(ds_i18n_set_text(&translator, "en", "diagnostics", "demo/received",
                       "Received {count} files"));
  TRY(ds_i18n_set_text(&translator, "en", "diagnostics", "demo/accepted",
                       "Accepted {count} files; quota is getting low"));
  TRY(ds_i18n_set_text(&translator, "it", "diagnostics", "demo/received",
                       "Ricevuti {count} file"));
  TRY(ds_i18n_set_text(&translator, "it", "diagnostics", "demo/accepted",
                       "Accettati {count} file; quota quasi esaurita"));
  sink.emit = print_event;
  sink.user = &translator;
  puts("English:");
  ds_result_drain_to(&accepted, &sink);
  TRY(ds_i18n_set_locale(&translator, "it"));
  puts("Italian (same result, rendered again):");
  ds_result_drain_to(&accepted, &sink);

  formatter.text = NULL;
  formatter.format = format_files;
  formatter.user = NULL;
  TRY(ds_i18n_set(&translator, "it", "app", "files", &formatter));
  count_arg.name = "count";
  count_arg.type = DS_ARG_SIZE;
  count_arg.value.size = *(const size_t *)payload;
  TRY(ds_i18n_translate(&translator, "app", "files", &count_arg, 1, NULL,
                        &text));
  puts(ds_i18n_text_cstr(&text));

  TRY(ds_result_err_ptr(&null_error, NULL, NULL, NULL));
  TRY(ds_result_error(&null_error, &payload));
  printf("NULL error: is_err=%d, payload_is_null=%d\n",
         (int)ds_result_is_err(&null_error), payload == NULL);
cleanup:
  ds_i18n_text_destroy(&text);
  ds_i18n_destroy(&translator);
  ds_diagnostics_destroy(&log);
  ds_result_value_release(&value);
  ds_result_destroy(&received);
  ds_result_destroy(&doubled);
  ds_result_destroy(&accepted);
  ds_result_destroy(&null_error);
  return result;
}
