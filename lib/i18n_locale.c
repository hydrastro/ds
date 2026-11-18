#include "i18n_locale.h"
#include <limits.h>
#include <string.h>

static bool il_digit(char c) { return c >= '0' && c <= '9'; }

ds_status_t ds_i18n_decimal_parse(const char *text, ds_i18n_decimal_t *out) {
  ds_i18n_decimal_t n;
  const char *p;
  if (text == NULL || out == NULL)
    return DS_ERR_NULL;
  memset(&n, 0, sizeof(n));
  p = text;
  if (*p == '+' || *p == '-') {
    n.negative = *p == '-';
    p++;
  }
  n.integer = p;
  while (il_digit(*p))
    p++;
  n.integer_digits = (size_t)(p - n.integer);
  if (n.integer_digits == 0)
    return DS_ERR_INVALID;
  if (*p == '.') {
    n.fraction = ++p;
    while (il_digit(*p))
      p++;
    n.fraction_digits = (size_t)(p - n.fraction);
    if (n.fraction_digits == 0)
      return DS_ERR_INVALID;
  }
  if (*p != '\0')
    return DS_ERR_INVALID;
  *out = n;
  return DS_OK;
}

/* Moduli here are at most 1000000, safely below ULONG_MAX / 10. */
static unsigned long il_mod(const ds_i18n_decimal_t *n, unsigned long m) {
  size_t i;
  unsigned long value = 0;
  for (i = 0; i < n->integer_digits; i++)
    value = (value * 10UL + (unsigned long)(n->integer[i] - '0')) % m;
  return value;
}

static bool il_equal(const ds_i18n_decimal_t *n, unsigned digit) {
  size_t i;
  for (i = 0; i + 1 < n->integer_digits; i++)
    if (n->integer[i] != '0')
      return false;
  return n->integer[n->integer_digits - 1] == (char)('0' + digit);
}

static bool il_integral(const ds_i18n_decimal_t *n) {
  size_t i;
  for (i = 0; i < n->fraction_digits; i++)
    if (n->fraction[i] != '0')
      return false;
  return true;
}

static ds_status_t il_plural(ds_i18n_plural_rule_t rule,
                             const ds_i18n_decimal_t *n,
                             ds_i18n_plural_category_t *out) {
  ds_i18n_plural_category_t result = DS_I18N_OTHER;
  unsigned long tail;
  if (rule < DS_I18N_PLURAL_OTHER || rule > DS_I18N_PLURAL_ARABIC)
    return DS_ERR_INVALID;
  if (rule == DS_I18N_PLURAL_FRENCH && (il_equal(n, 0) || il_equal(n, 1)))
    result = DS_I18N_ONE;
  else if ((rule == DS_I18N_PLURAL_ENGLISH || rule == DS_I18N_PLURAL_ITALIAN) &&
           n->fraction_digits == 0 && il_equal(n, 1))
    result = DS_I18N_ONE;
  else if ((rule == DS_I18N_PLURAL_FRENCH || rule == DS_I18N_PLURAL_ITALIAN) &&
           n->fraction_digits == 0 && !il_equal(n, 0) &&
           il_mod(n, 1000000UL) == 0)
    result = DS_I18N_MANY;
  else if (rule == DS_I18N_PLURAL_RUSSIAN && n->fraction_digits == 0) {
    tail = il_mod(n, 100);
    if (tail % 10 == 1 && tail != 11)
      result = DS_I18N_ONE;
    else if (tail % 10 >= 2 && tail % 10 <= 4 && (tail < 12 || tail > 14))
      result = DS_I18N_FEW;
    else
      result = DS_I18N_MANY;
  } else if (rule == DS_I18N_PLURAL_ARABIC && il_integral(n)) {
    tail = il_mod(n, 100);
    if (il_equal(n, 0))
      result = DS_I18N_ZERO;
    else if (il_equal(n, 1))
      result = DS_I18N_ONE;
    else if (il_equal(n, 2))
      result = DS_I18N_TWO;
    else if (tail >= 3 && tail <= 10)
      result = DS_I18N_FEW;
    else if (tail >= 11)
      result = DS_I18N_MANY;
  }
  *out = result;
  return DS_OK;
}

ds_status_t ds_i18n_plural_select(ds_i18n_plural_rule_t rule,
                                  const char *decimal,
                                  ds_i18n_plural_category_t *out) {
  ds_i18n_decimal_t n;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  st = ds_i18n_decimal_parse(decimal, &n);
  return st == DS_OK ? il_plural(rule, &n, out) : st;
}

const char *ds_i18n_plural_name(ds_i18n_plural_category_t category) {
  static const char *const names[6] = {"zero", "one",  "two",
                                       "few",  "many", "other"};
  return category >= DS_I18N_ZERO && category <= DS_I18N_OTHER
             ? names[(unsigned)category]
             : NULL;
}

void ds_i18n_number_policy_init(ds_i18n_number_policy_t *p) {
  if (p == NULL)
    return;
  memset(p, 0, sizeof(*p));
  p->decimal_separator = ".";
  p->group_separator = "";
  p->minus_sign = "-";
  p->plus_sign = "+";
  p->minimum_group_digits = 4;
}

static ds_status_t il_error(ds_i18n_writer_t *w, ds_status_t st) {
  if (w == NULL)
    return DS_ERR_NULL;
  if (w->status == DS_OK)
    w->status = st;
  return w->status;
}

ds_status_t ds_i18n_format_number(ds_i18n_writer_t *w, const char *decimal,
                                  const ds_i18n_number_policy_t *policy) {
  ds_i18n_number_policy_t defaults;
  ds_i18n_decimal_t n;
  size_t i, repeated;
  ds_status_t st;
  if (w == NULL)
    return DS_ERR_NULL;
  if (w->status != DS_OK)
    return w->status;
  if (policy == NULL) {
    ds_i18n_number_policy_init(&defaults);
    policy = &defaults;
  }
  if (policy->decimal_separator == NULL || policy->group_separator == NULL ||
      policy->minus_sign == NULL || policy->plus_sign == NULL)
    return il_error(w, DS_ERR_NULL);
  st = ds_i18n_decimal_parse(decimal, &n);
  if (st != DS_OK)
    return il_error(w, st);
  repeated = policy->secondary_group == 0 ? policy->primary_group
                                          : policy->secondary_group;
  if (n.negative || policy->show_plus)
    (void)ds_i18n_write_cstr(w, n.negative ? policy->minus_sign
                                           : policy->plus_sign);
  for (i = 0; i < n.integer_digits && w->status == DS_OK; i++) {
    size_t remaining = n.integer_digits - i;
    if (i != 0 && policy->primary_group != 0 &&
        n.integer_digits >= policy->minimum_group_digits &&
        remaining >= policy->primary_group &&
        (remaining - policy->primary_group) % repeated == 0)
      (void)ds_i18n_write_cstr(w, policy->group_separator);
    (void)ds_i18n_write(w, n.integer + i, 1);
  }
  if (n.fraction_digits != 0) {
    (void)ds_i18n_write_cstr(w, policy->decimal_separator);
    (void)ds_i18n_write(w, n.fraction, n.fraction_digits);
  }
  return w->status;
}

static void il_digits(ds_i18n_writer_t *w, unsigned value, unsigned width) {
  char digits[4];
  unsigned i = width;
  while (i != 0) {
    digits[--i] = (char)('0' + value % 10U);
    value /= 10U;
  }
  (void)ds_i18n_write(w, digits, width);
}

ds_status_t ds_i18n_format_datetime(ds_i18n_writer_t *w,
                                    const ds_i18n_datetime_t *v,
                                    const char *pattern) {
  static const unsigned days[12] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  unsigned max_day;
  const char *p;
  if (w == NULL)
    return DS_ERR_NULL;
  if (w->status != DS_OK)
    return w->status;
  if (v == NULL || pattern == NULL)
    return il_error(w, DS_ERR_NULL);
  if (v->year < 1 || v->year > 9999 || v->month < 1 || v->month > 12 ||
      v->hour > 23 || v->minute > 59 || v->second > 59 ||
      v->utc_offset_minutes < -1439 || v->utc_offset_minutes > 1439)
    return il_error(w, DS_ERR_RANGE);
  max_day = days[v->month - 1];
  if (v->month == 2 && v->year % 4 == 0 &&
      (v->year % 100 != 0 || v->year % 400 == 0))
    max_day++;
  if (v->day < 1 || v->day > max_day)
    return il_error(w, DS_ERR_RANGE);
  for (p = pattern; *p != '\0'; p++) {
    if (*p == '%' && (*++p == '\0' || strchr("YmdHMSz%", *p) == NULL))
      return il_error(w, DS_ERR_INVALID);
  }
  for (p = pattern; *p != '\0' && w->status == DS_OK; p++) {
    if (*p != '%') {
      (void)ds_i18n_write(w, p, 1);
      continue;
    }
    p++;
    switch (*p) {
    case 'Y':
      il_digits(w, (unsigned)v->year, 4);
      break;
    case 'm':
      il_digits(w, v->month, 2);
      break;
    case 'd':
      il_digits(w, v->day, 2);
      break;
    case 'H':
      il_digits(w, v->hour, 2);
      break;
    case 'M':
      il_digits(w, v->minute, 2);
      break;
    case 'S':
      il_digits(w, v->second, 2);
      break;
    case 'z': {
      unsigned offset =
          (unsigned)(v->utc_offset_minutes < 0 ? -v->utc_offset_minutes
                                               : v->utc_offset_minutes);
      (void)ds_i18n_write_cstr(w, v->utc_offset_minutes < 0 ? "-" : "+");
      il_digits(w, offset / 60, 2);
      il_digits(w, offset % 60, 2);
      break;
    }
    default:
      (void)ds_i18n_write(w, "%", 1);
      break;
    }
  }
  return w->status;
}

void ds_i18n_plural_message_init(ds_i18n_plural_message_t *m) {
  if (m == NULL)
    return;
  memset(m, 0, sizeof(*m));
  m->count_argument = "count";
}

ds_status_t ds_i18n_format_plural(const ds_i18n_request_t *request,
                                  ds_i18n_writer_t *w, void *message) {
  const ds_i18n_plural_message_t *m = (const ds_i18n_plural_message_t *)message;
  ds_i18n_decimal_t n;
  ds_i18n_plural_category_t category = DS_I18N_OTHER;
  const ds_diagnostic_arg_t *arg = NULL;
  const char *decimal, *form;
  char buffer[CHAR_BIT * (sizeof(size_t) + sizeof(long)) + 2];
  size_t i, pos = sizeof(buffer) - 1;
  ds_status_t st;
  bool negative = false;
  if (w == NULL)
    return DS_ERR_NULL;
  if (w->status != DS_OK)
    return w->status;
  if (request == NULL || m == NULL || m->count_argument == NULL ||
      m->forms[DS_I18N_OTHER] == NULL ||
      (request->args == NULL && request->arg_count != 0))
    return il_error(w, DS_ERR_NULL);
  for (i = 0; i < request->arg_count; i++)
    if (request->args[i].name != NULL &&
        strcmp(request->args[i].name, m->count_argument) == 0) {
      arg = request->args + i;
      break;
    }
  if (arg == NULL)
    return il_error(w, DS_ERR_INVALID);
  buffer[pos] = '\0';
  if (arg->type == DS_ARG_CSTR)
    decimal = arg->value.cstr;
  else if (arg->type == DS_ARG_SIZE) {
    size_t value = arg->value.size;
    do {
      buffer[--pos] = (char)('0' + value % 10);
      value /= 10;
    } while (value != 0);
    decimal = buffer + pos;
  } else if (arg->type == DS_ARG_INT || arg->type == DS_ARG_UINT) {
    unsigned long value;
    if (arg->type == DS_ARG_INT) {
      negative = arg->value.i < 0;
      value = negative ? 0UL - (unsigned long)arg->value.i
                       : (unsigned long)arg->value.i;
    } else
      value = arg->value.u;
    do {
      buffer[--pos] = (char)('0' + value % 10UL);
      value /= 10UL;
    } while (value != 0);
    if (negative)
      buffer[--pos] = '-';
    decimal = buffer + pos;
  } else
    return il_error(w, DS_ERR_UNSUPPORTED);
  st = ds_i18n_decimal_parse(decimal, &n);
  if (st == DS_OK) {
    st = m->select == NULL ? il_plural(m->rule, &n, &category)
                           : m->select(&n, &category, m->user);
    if (st > DS_OK ||
        (st == DS_OK && (category < DS_I18N_ZERO || category > DS_I18N_OTHER)))
      st = DS_ERR_CALLBACK;
  }
  if (st != DS_OK)
    return il_error(w, st);
  form = m->forms[(unsigned)category];
  if (form == NULL)
    form = m->forms[DS_I18N_OTHER];
  return ds_i18n_interpolate(w, form, request->args, request->arg_count);
}
