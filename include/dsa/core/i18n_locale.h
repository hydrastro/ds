#ifndef DS_I18N_LOCALE_H
#define DS_I18N_LOCALE_H

#include <dsa/core/i18n.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact ASCII decimal view: [+-]digits[.digits]. At least one integral digit;
 * no exponent, whitespace, NaN or infinity. Views borrow the input. Keeping
 * visible fraction digits distinguishes 1, 1.0 and 1.00 for plural selection.
 * Only views returned by decimal_parse should be passed to policy callbacks. */
typedef struct ds_i18n_decimal {
  const char *integer;
  size_t integer_digits;
  const char *fraction;
  size_t fraction_digits;
  bool negative;
} ds_i18n_decimal_t;
ds_status_t ds_i18n_decimal_parse(const char *text, ds_i18n_decimal_t *out);

typedef enum ds_i18n_plural_category {
  DS_I18N_ZERO = 0,
  DS_I18N_ONE,
  DS_I18N_TWO,
  DS_I18N_FEW,
  DS_I18N_MANY,
  DS_I18N_OTHER
} ds_i18n_plural_category_t;
typedef enum ds_i18n_plural_rule {
  DS_I18N_PLURAL_OTHER = 0,
  DS_I18N_PLURAL_ENGLISH,
  DS_I18N_PLURAL_ITALIAN,
  DS_I18N_PLURAL_FRENCH,
  DS_I18N_PLURAL_RUSSIAN,
  DS_I18N_PLURAL_ARABIC
} ds_i18n_plural_rule_t;
/* Cardinal rules for ordinary decimal notation (CLDR 48, exponent operand 0).
 * No ordinal/range rules or automatic locale negotiation are implied. Custom
 * rules receive exact digits and can implement other languages/data versions.
 */
typedef ds_status_t (*ds_i18n_plural_fn)(const ds_i18n_decimal_t *number,
                                         ds_i18n_plural_category_t *out,
                                         void *user);
ds_status_t ds_i18n_plural_select(ds_i18n_plural_rule_t rule,
                                  const char *decimal,
                                  ds_i18n_plural_category_t *out);
const char *ds_i18n_plural_name(ds_i18n_plural_category_t category);

typedef struct ds_i18n_number_policy {
  const char *decimal_separator;
  const char *group_separator;
  const char *minus_sign;
  const char *plus_sign;
  size_t primary_group;        /* rightmost group, 0 disables grouping */
  size_t secondary_group;      /* repeated groups to its left, 0 uses primary */
  size_t minimum_group_digits; /* minimum integral width before grouping */
  bool show_plus;
} ds_i18n_number_policy_t;
/* Defaults: ASCII signs and decimal dot, no grouping. Strings are borrowed.
 * No rounding, digit substitution, currency or binary-float conversion occurs.
 * All supplied digits, including leading/trailing zeros, are preserved. */
void ds_i18n_number_policy_init(ds_i18n_number_policy_t *policy);
ds_status_t ds_i18n_format_number(ds_i18n_writer_t *writer, const char *decimal,
                                  const ds_i18n_number_policy_t *policy);

typedef struct ds_i18n_datetime {
  int year;                                  /* Gregorian, 1..9999 */
  unsigned month, day, hour, minute, second; /* second 0..59 */
  int utc_offset_minutes; /* -1439..1439, caller already resolved time zone */
} ds_i18n_datetime_t;
/* Numeric pattern fields: %Y %m %d %H %M %S %z %%.
 * %z writes signed HHMM. Other bytes are literal UTF-8. No process-locale or
 * time-zone mutation, calendar conversion, localized names or DST inference.
 * Invalid input/pattern sets the writer's sticky error before writing. */
ds_status_t ds_i18n_format_datetime(ds_i18n_writer_t *writer,
                                    const ds_i18n_datetime_t *value,
                                    const char *pattern);

/* Ready-to-register formatter. The descriptor, forms and callback user remain
 * caller-owned. count_argument accepts CSTR (exact decimal), INT, UINT or SIZE;
 * DOUBLE is rejected because it cannot retain visible decimal precision.
 * Missing selected forms fall back to OTHER, which must be present.
 * Forms use the core's single-pass interpolation. The selected catalog locale
 * is available to custom formatter implementations in the request. */
typedef struct ds_i18n_plural_message {
  const char *count_argument;
  const char *forms[6];
  ds_i18n_plural_rule_t rule;
  ds_i18n_plural_fn select;
  void *user;
} ds_i18n_plural_message_t;
void ds_i18n_plural_message_init(ds_i18n_plural_message_t *message);
ds_status_t ds_i18n_format_plural(const ds_i18n_request_t *request,
                                  ds_i18n_writer_t *writer, void *message);

#ifdef __cplusplus
}
#endif
#endif
