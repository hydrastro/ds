/* Catalog import/export, exact-decimal plural selection and formatting policy.
 * Usage: localized_catalog [output.tsv] */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

typedef struct receipt_policy {
  ds_i18n_number_policy_t number;
  ds_i18n_datetime_t date;
} receipt_policy_t;

static ds_status_t receipt(const ds_i18n_request_t *request,
                           ds_i18n_writer_t *writer, void *user) {
  receipt_policy_t *policy = (receipt_policy_t *)user;
  ds_status_t st;
  (void)request;
  st = ds_i18n_write_cstr(writer, "EUR ");
  if (st == DS_OK) {
    st = ds_i18n_format_number(writer, "1234567.80", &policy->number);
  }
  if (st == DS_OK) {
    st = ds_i18n_write_cstr(writer, " | ");
  }
  if (st == DS_OK) {
    st = ds_i18n_format_datetime(writer, &policy->date, "%d/%m/%Y %H:%M %z");
  }
  return st;
}

static ds_status_t show(ds_i18n_t *t, const char *key,
                        const ds_diagnostic_arg_t *args, size_t count) {
  ds_i18n_text_t text;
  ds_status_t st;
  ds_i18n_text_init(&text, NULL);
  st = ds_i18n_translate(t, "shop", key, args, count, NULL, &text);
  if (st == DS_OK) {
    puts(ds_i18n_text_cstr(&text));
  }
  ds_i18n_text_destroy(&text);
  return st;
}

int main(int argc, char **argv) {
  static const char base[] =
      "#!ds-i18n-tsv 1\n"
      "en\tshop\twelcome\tWelcome, {name}!\n"
      "en\tshop\tcontact\tContact support\\nhelp@example.org\n";
  static const char italian[] =
      "{\"welcome\":\"Ciao, {name}! Caff\\u00e8 \\ud83d\\udce6\","
      "\"contact\":\"Scrivici: help@example.org\"}";
  const char *path = argc > 1 ? argv[1] : "catalog.tsv";
  ds_i18n_config_t config;
  ds_i18n_t catalog, reopened;
  ds_i18n_message_t message;
  ds_i18n_plural_message_t plural;
  ds_diagnostic_arg_t arg;
  receipt_policy_t policy;
  ds_status_t st;
  FILE *file = NULL;
  size_t loaded = 0;
  bool initialized = false;
  ds_i18n_config_init(&config);
  config.locale = "it";
  config.map = ds_map_swiss_ops();
  st = ds_i18n_init(&catalog, &config);
  if (st != DS_OK) {
    return 1;
  }
  st = ds_i18n_catalog_load_memory(&catalog, DS_I18N_CATALOG_TSV, NULL, NULL,
                                   base, sizeof(base) - 1, NULL, &loaded);
  if (st != DS_OK) {
    goto done;
  }
  printf("Loaded %lu base messages\n", (unsigned long)loaded);
  st =
      ds_i18n_catalog_load_memory(&catalog, DS_I18N_CATALOG_JSON, "it", "shop",
                                  italian, sizeof(italian) - 1, NULL, &loaded);
  if (st != DS_OK) {
    goto done;
  }
  printf("Loaded %lu Italian overrides\n", (unsigned long)loaded);
  file = fopen(path, "wb");
  if (file == NULL) {
    st = DS_ERR_STATE;
    goto done;
  }
  st = ds_i18n_catalog_save(&catalog, DS_I18N_CATALOG_TSV, NULL, NULL,
                            ds_i18n_catalog_file_write, file);
  if (fclose(file) != 0 && st == DS_OK) {
    st = DS_ERR_STATE;
  }
  file = NULL;
  if (st != DS_OK) {
    goto done;
  }
  st = ds_i18n_init(&reopened, &config);
  if (st != DS_OK) {
    goto done;
  }
  initialized = true;
  file = fopen(path, "rb");
  if (file == NULL) {
    st = DS_ERR_STATE;
    goto done;
  }
  st = ds_i18n_catalog_load(&reopened, DS_I18N_CATALOG_TSV, NULL, NULL,
                            ds_i18n_catalog_file_read, file, NULL, &loaded);
  if (fclose(file) != 0 && st == DS_OK) {
    st = DS_ERR_STATE;
  }
  file = NULL;
  if (st != DS_OK) {
    goto done;
  }
  arg.name = "name";
  arg.type = DS_ARG_CSTR;
  arg.value.cstr = "Ada";
  st = show(&reopened, "welcome", &arg, 1);
  if (st != DS_OK) {
    goto done;
  }
  ds_i18n_number_policy_init(&policy.number);
  policy.number.decimal_separator = ",";
  policy.number.group_separator = ".";
  policy.number.primary_group = 3;
  policy.date.year = 2026;
  policy.date.month = 11;
  policy.date.day = 17;
  policy.date.hour = 14;
  policy.date.minute = 30;
  policy.date.second = 0;
  policy.date.utc_offset_minutes = 60;
  message.text = NULL;
  message.format = receipt;
  message.user = &policy;
  st = ds_i18n_set(&reopened, "it", "shop", "receipt", &message);
  if (st != DS_OK) {
    goto done;
  }
  st = show(&reopened, "receipt", NULL, 0);
  if (st != DS_OK) {
    goto done;
  }
  ds_i18n_plural_message_init(&plural);
  plural.rule = DS_I18N_PLURAL_ENGLISH;
  plural.forms[DS_I18N_ONE] = "{count} item";
  plural.forms[DS_I18N_OTHER] = "{count} items";
  message.format = ds_i18n_format_plural;
  message.user = &plural;
  st = ds_i18n_set(&reopened, "en", "shop", "stock", &message);
  if (st != DS_OK) {
    goto done;
  }
  st = ds_i18n_set_locale(&reopened, "en");
  arg.name = "count";
  arg.value.cstr = "1";
  if (st == DS_OK) {
    st = show(&reopened, "stock", &arg, 1);
  }
  arg.value.cstr = "1.0";
  if (st == DS_OK) {
    st = show(&reopened, "stock", &arg, 1);
  }
  arg.value.cstr = "123456789012345678901234567890";
  if (st == DS_OK) {
    st = show(&reopened, "stock", &arg, 1);
  }
done:
  if (file != NULL) {
    (void)fclose(file);
  }
  if (initialized) {
    ds_i18n_destroy(&reopened);
  }
  ds_i18n_destroy(&catalog);
  if (st != DS_OK) {
    fprintf(stderr, "catalog: %s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
