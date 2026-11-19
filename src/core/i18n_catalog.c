#include <dsa/core/i18n_catalog.h>
#include <string.h>

static const char ic_header[] = "#!ds-i18n-tsv 1";

typedef struct ic_parser {
  char *at, *end;
  ds_vec_t entries;
  const ds_i18n_catalog_limits_t *limits;
} ic_parser_t;

/* Reject overlong encodings, surrogate scalars and values above U+10FFFF. */
static bool ic_utf8(const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  while (*p != 0) {
    unsigned long cp;
    unsigned need, i;
    unsigned char lead = *p++;
    if (lead < 0x80U) {
      continue;
    }
    if (lead >= 0xc2U && lead <= 0xdfU) {
      cp = lead & 31U;
      need = 1;
    } else if (lead >= 0xe0U && lead <= 0xefU) {
      cp = lead & 15U;
      need = 2;
    } else if (lead >= 0xf0U && lead <= 0xf4U) {
      cp = lead & 7U;
      need = 3;
    } else {
      return false;
    }
    for (i = 0; i < need; i++) {
      if (*p < 0x80U || *p > 0xbfU) {
        return false;
      }
      cp = (cp << 6) | (*p++ & 63U);
    }
    if ((need == 2 && cp < 0x800UL) || (need == 3 && cp < 0x10000UL) ||
        cp > 0x10ffffUL || (cp >= 0xd800UL && cp <= 0xdfffUL)) {
      return false;
    }
  }
  return true;
}

void ds_i18n_catalog_limits_init(ds_i18n_catalog_limits_t *limits) {
  if (limits == NULL) {
    return;
  }
  limits->max_bytes = 16UL * 1024UL * 1024UL;
  limits->max_entries = 100000UL;
  limits->max_field_bytes = 1024UL * 1024UL;
}

static ds_status_t ic_field(ic_parser_t *p, const char *field) {
  size_t n = strlen(field);
  if (p->limits->max_field_bytes != 0 && n > p->limits->max_field_bytes) {
    return DS_ERR_OVERFLOW;
  }
  return ic_utf8(field) ? DS_OK : DS_ERR_INVALID;
}

static ds_status_t ic_entry(ic_parser_t *p, const char *locale,
                            const char *domain, const char *key,
                            const char *text) {
  ds_i18n_entry_t entry;
  ds_status_t st;
  if (p->limits->max_entries != 0 &&
      p->entries.size >= p->limits->max_entries) {
    return DS_ERR_OVERFLOW;
  }
  st = ic_field(p, locale);
  if (st == DS_OK) {
    st = ic_field(p, domain);
  }
  if (st == DS_OK) {
    st = ic_field(p, key);
  }
  if (st == DS_OK) {
    st = ic_field(p, text);
  }
  if (st != DS_OK) {
    return st;
  }
  entry.locale = locale;
  entry.domain = domain;
  entry.key = key;
  entry.message.text = text;
  entry.message.format = NULL;
  entry.message.user = NULL;
  return ds_vec_push(&p->entries, &entry);
}

static ds_status_t ic_tsv(ic_parser_t *p) {
  bool header = false;
  while (p->at < p->end) {
    char *line = p->at, *end, *out, *fields[4];
    size_t field = 0;
    ds_status_t st;
    while (p->at < p->end && *p->at != '\n') {
      p->at++;
    }
    end = p->at;
    if (p->at < p->end) {
      p->at++;
    }
    if (end > line && end[-1] == '\r') {
      end--;
    }
    *end = '\0';
    if (!header) {
      if (strcmp(line, ic_header) != 0) {
        return DS_ERR_INVALID;
      }
      header = true;
      continue;
    }
    if (*line == '\0' || *line == '#') {
      continue;
    }
    out = line;
    fields[0] = out;
    while (line < end) {
      char c = *line++;
      if (c == '\t') {
        if (++field == 4) {
          return DS_ERR_INVALID;
        }
        *out++ = '\0';
        fields[field] = out;
      } else {
        if (c == '\\') {
          if (line == end) {
            return DS_ERR_INVALID;
          }
          c = *line++;
          if (c == 't') {
            c = '\t';
          } else if (c == 'n') {
            c = '\n';
          } else if (c == 'r') {
            c = '\r';
          } else if (c != '\\' && c != '#') {
            return DS_ERR_INVALID;
          }
        } else if (c == '\r') {
          return DS_ERR_INVALID;
        }
        *out++ = c;
      }
    }
    *out = '\0';
    if (field != 3) {
      return DS_ERR_INVALID;
    }
    st = ic_entry(p, fields[0], fields[1], fields[2], fields[3]);
    if (st != DS_OK) {
      return st;
    }
  }
  return header ? DS_OK : DS_ERR_INVALID;
}

static void ic_space(ic_parser_t *p) {
  while (p->at < p->end && (*p->at == ' ' || *p->at == '\t' ||
                            *p->at == '\r' || *p->at == '\n')) {
    p->at++;
  }
}

static bool ic_take(ic_parser_t *p, char c) {
  ic_space(p);
  if (p->at == p->end || *p->at != c) {
    return false;
  }
  p->at++;
  return true;
}

static bool ic_hex4(ic_parser_t *p, unsigned long *cp) {
  unsigned i;
  *cp = 0;
  for (i = 0; i < 4; i++) {
    unsigned digit;
    char c;
    if (p->at == p->end) {
      return false;
    }
    c = *p->at++;
    if (c >= '0' && c <= '9') {
      digit = (unsigned)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = (unsigned)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = (unsigned)(c - 'A' + 10);
    } else {
      return false;
    }
    *cp = (*cp << 4) | digit;
  }
  return true;
}

static ds_status_t ic_string(ic_parser_t *p, char **text) {
  char *out;
  if (!ic_take(p, '"')) {
    return DS_ERR_INVALID;
  }
  *text = out = p->at;
  while (p->at < p->end) {
    unsigned char c = (unsigned char)*p->at++;
    if (c == '"') {
      *out = '\0';
      return ic_field(p, *text);
    }
    if (c < 32U) {
      return DS_ERR_INVALID;
    }
    if (c != '\\') {
      *out++ = (char)c;
      continue;
    }
    if (p->at == p->end) {
      return DS_ERR_INVALID;
    }
    c = (unsigned char)*p->at++;
    if (c == 'u') {
      unsigned long cp, low;
      if (!ic_hex4(p, &cp) || cp == 0) {
        return DS_ERR_INVALID;
      }
      if (cp >= 0xd800UL && cp <= 0xdbffUL) {
        if (p->end - p->at < 2 || p->at[0] != '\\' || p->at[1] != 'u') {
          return DS_ERR_INVALID;
        }
        p->at += 2;
        if (!ic_hex4(p, &low) || low < 0xdc00UL || low > 0xdfffUL) {
          return DS_ERR_INVALID;
        }
        cp = 0x10000UL + ((cp - 0xd800UL) << 10) + low - 0xdc00UL;
      } else if (cp >= 0xdc00UL && cp <= 0xdfffUL) {
        return DS_ERR_INVALID;
      }
      if (cp < 0x80UL) {
        *out++ = (char)cp;
      } else {
        if (cp >= 0x10000UL) {
          *out++ = (char)(0xf0UL | (cp >> 18));
          *out++ = (char)(0x80UL | ((cp >> 12) & 63UL));
        } else if (cp >= 0x800UL) {
          *out++ = (char)(0xe0UL | (cp >> 12));
        } else {
          *out++ = (char)(0xc0UL | (cp >> 6));
          *out++ = (char)(0x80UL | (cp & 63UL));
          continue;
        }
        *out++ = (char)(0x80UL | ((cp >> 6) & 63UL));
        *out++ = (char)(0x80UL | (cp & 63UL));
      }
    } else {
      switch (c) {
      case 'b':
        c = '\b';
        break;
      case 'f':
        c = '\f';
        break;
      case 'n':
        c = '\n';
        break;
      case 'r':
        c = '\r';
        break;
      case 't':
        c = '\t';
        break;
      case '"':
      case '\\':
      case '/':
        break;
      default:
        return DS_ERR_INVALID;
      }
      *out++ = (char)c;
    }
  }
  return DS_ERR_INVALID;
}

static ds_status_t ic_json(ic_parser_t *p, const char *locale,
                           const char *domain) {
  if (!ic_take(p, '{')) {
    return DS_ERR_INVALID;
  }
  if (!ic_take(p, '}')) {
    for (;;) {
      char *key, *text;
      ds_status_t st = ic_string(p, &key);
      if (st != DS_OK) {
        return st;
      }
      if (!ic_take(p, ':')) {
        return DS_ERR_INVALID;
      }
      st = ic_string(p, &text);
      if (st == DS_OK) {
        st = ic_entry(p, locale, domain, key, text);
      }
      if (st != DS_OK) {
        return st;
      }
      if (ic_take(p, '}')) {
        break;
      }
      if (!ic_take(p, ',')) {
        return DS_ERR_INVALID;
      }
    }
  }
  ic_space(p);
  return p->at == p->end ? DS_OK : DS_ERR_INVALID;
}

ds_status_t ds_i18n_catalog_load_memory(ds_i18n_t *t,
                                        ds_i18n_catalog_format_t format,
                                        const char *locale, const char *domain,
                                        const void *data, size_t size,
                                        const ds_i18n_catalog_limits_t *limits,
                                        size_t *out_count) {
  ds_i18n_catalog_limits_t defaults;
  ic_parser_t p;
  char *buffer;
  ds_status_t st;
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (t == NULL || (data == NULL && size != 0)) {
    return DS_ERR_NULL;
  }
  if (t->catalog.ops == NULL || t->depth != 0) {
    return DS_ERR_STATE;
  }
  if (format != DS_I18N_CATALOG_TSV && format != DS_I18N_CATALOG_JSON) {
    return DS_ERR_INVALID;
  }
  if (format == DS_I18N_CATALOG_JSON && (locale == NULL || domain == NULL)) {
    return DS_ERR_NULL;
  }
  if (limits == NULL) {
    ds_i18n_catalog_limits_init(&defaults);
    limits = &defaults;
  }
  if (size == (size_t)-1 ||
      (limits->max_bytes != 0 && size > limits->max_bytes)) {
    return DS_ERR_OVERFLOW;
  }
  if (size != 0 && memchr(data, 0, size) != NULL) {
    return DS_ERR_INVALID;
  }
  buffer = (char *)ds_context_alloc(t->context, size + 1);
  if (buffer == NULL) {
    return DS_ERR_ALLOC;
  }
  if (size != 0) {
    memcpy(buffer, data, size);
  }
  buffer[size] = '\0';
  p.at = buffer;
  p.end = buffer + size;
  p.limits = limits;
  st = ds_vec_init(&p.entries, sizeof(ds_i18n_entry_t), t->context);
  if (st == DS_OK) {
    st = format == DS_I18N_CATALOG_TSV ? ic_tsv(&p)
                                       : ic_json(&p, locale, domain);
  }
  if (st == DS_OK) {
    st = ds_i18n_set_many(t, (const ds_i18n_entry_t *)p.entries.data,
                          p.entries.size);
  }
  if (st == DS_OK && out_count != NULL) {
    *out_count = p.entries.size;
  }
  ds_vec_destroy(&p.entries);
  ds_context_free(t->context, buffer);
  return st;
}

ds_status_t ds_i18n_catalog_load(ds_i18n_t *t, ds_i18n_catalog_format_t format,
                                 const char *locale, const char *domain,
                                 ds_i18n_catalog_read_fn read, void *user,
                                 const ds_i18n_catalog_limits_t *limits,
                                 size_t *out_count) {
  ds_i18n_catalog_limits_t defaults;
  ds_vec_t bytes;
  ds_status_t st;
  unsigned char chunk[4096];
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (t == NULL || read == NULL) {
    return DS_ERR_NULL;
  }
  if (t->catalog.ops == NULL || t->depth != 0) {
    return DS_ERR_STATE;
  }
  if (format != DS_I18N_CATALOG_TSV && format != DS_I18N_CATALOG_JSON) {
    return DS_ERR_INVALID;
  }
  if (format == DS_I18N_CATALOG_JSON && (locale == NULL || domain == NULL)) {
    return DS_ERR_NULL;
  }
  if (limits == NULL) {
    ds_i18n_catalog_limits_init(&defaults);
    limits = &defaults;
  }
  st = ds_vec_init(&bytes, 1, t->context);
  while (st == DS_OK) {
    size_t count = 0;
    st = read(user, chunk, sizeof(chunk), &count);
    if (st > DS_OK || count > sizeof(chunk)) {
      st = DS_ERR_CALLBACK;
    }
    if (st != DS_OK || count == 0) {
      break;
    }
    if (count > (size_t)-1 - bytes.size ||
        (limits->max_bytes != 0 && (bytes.size > limits->max_bytes ||
                                    count > limits->max_bytes - bytes.size))) {
      st = DS_ERR_OVERFLOW;
      break;
    }
    st = ds_vec_append(&bytes, chunk, count);
  }
  if (st == DS_OK) {
    st = ds_i18n_catalog_load_memory(t, format, locale, domain, bytes.data,
                                     bytes.size, limits, out_count);
  }
  ds_vec_destroy(&bytes);
  return st;
}

ds_status_t ds_i18n_catalog_file_read(void *stream, void *buffer,
                                      size_t capacity, size_t *count) {
  FILE *file = (FILE *)stream;
  if (file == NULL || count == NULL || (buffer == NULL && capacity != 0)) {
    return DS_ERR_NULL;
  }
  *count = capacity == 0 ? 0 : fread(buffer, 1, capacity, file);
  return ferror(file) ? DS_ERR_STATE : DS_OK;
}

ds_status_t ds_i18n_catalog_file_write(void *stream, const void *buffer,
                                       size_t count) {
  if (stream == NULL || (buffer == NULL && count != 0)) {
    return DS_ERR_NULL;
  }
  return count == 0 || fwrite(buffer, 1, count, (FILE *)stream) == count
             ? DS_OK
             : DS_ERR_STATE;
}

typedef struct ic_output {
  ds_i18n_catalog_format_t format;
  const char *locale, *domain;
  ds_i18n_catalog_write_fn write;
  void *user;
  ds_status_t status;
  bool preflight, first;
} ic_output_t;

static void ic_write(ic_output_t *s, const char *text, size_t count) {
  if (s->status == DS_OK && count != 0) {
    s->status = s->write(s->user, text, count);
    if (s->status > DS_OK) {
      s->status = DS_ERR_CALLBACK;
    }
  }
}

static void ic_escape(ic_output_t *s, const char *text) {
  static const char hex[] = "0123456789abcdef";
  const char *start = text;
  bool json = s->format == DS_I18N_CATALOG_JSON;
  if (json) {
    ic_write(s, "\"", 1);
  }
  while (*text != '\0' && s->status == DS_OK) {
    unsigned char c = (unsigned char)*text;
    const char *escape = NULL;
    char code[6];
    size_t length = 2;
    if (c == '\\') {
      escape = "\\\\";
    } else if (c == '\n') {
      escape = "\\n";
    } else if (c == '\r') {
      escape = "\\r";
    } else if (c == '\t') {
      escape = "\\t";
    } else if (json && c == '"') {
      escape = "\\\"";
    } else if (!json && c == '#') {
      escape = "\\#";
    } else if (json && c < 32U) {
      code[0] = '\\';
      code[1] = 'u';
      code[2] = '0';
      code[3] = '0';
      code[4] = hex[c >> 4];
      code[5] = hex[c & 15U];
      escape = code;
      length = 6;
    }
    if (escape != NULL) {
      ic_write(s, start, (size_t)(text - start));
      ic_write(s, escape, length);
      start = text + 1;
    }
    text++;
  }
  ic_write(s, start, (size_t)(text - start));
  if (json) {
    ic_write(s, "\"", 1);
  }
}

static bool ic_save_entry(const char *locale, const char *domain,
                          const char *key, const ds_i18n_message_t *message,
                          void *user) {
  ic_output_t *s = (ic_output_t *)user;
  if (s->format == DS_I18N_CATALOG_JSON &&
      (strcmp(locale, s->locale) != 0 || strcmp(domain, s->domain) != 0)) {
    return true;
  }
  if (s->preflight) {
    if (message->text == NULL) {
      s->status = DS_ERR_UNSUPPORTED;
    } else if (!ic_utf8(key) || !ic_utf8(message->text) ||
               (s->format == DS_I18N_CATALOG_TSV &&
                (!ic_utf8(locale) || !ic_utf8(domain)))) {
      s->status = DS_ERR_INVALID;
    }
    return s->status == DS_OK;
  }
  if (s->format == DS_I18N_CATALOG_TSV) {
    ic_escape(s, locale);
    ic_write(s, "\t", 1);
    ic_escape(s, domain);
    ic_write(s, "\t", 1);
    ic_escape(s, key);
    ic_write(s, "\t", 1);
    ic_escape(s, message->text);
    ic_write(s, "\n", 1);
  } else {
    ic_write(s, s->first ? "\n  " : ",\n  ", s->first ? 3 : 4);
    ic_escape(s, key);
    ic_write(s, ": ", 2);
    ic_escape(s, message->text);
  }
  s->first = false;
  return s->status == DS_OK;
}

ds_status_t ds_i18n_catalog_save(const ds_i18n_t *t,
                                 ds_i18n_catalog_format_t format,
                                 const char *locale, const char *domain,
                                 ds_i18n_catalog_write_fn write, void *user) {
  ic_output_t s;
  if (t == NULL || write == NULL) {
    return DS_ERR_NULL;
  }
  if (t->catalog.ops == NULL) {
    return DS_ERR_STATE;
  }
  if (format != DS_I18N_CATALOG_TSV && format != DS_I18N_CATALOG_JSON) {
    return DS_ERR_INVALID;
  }
  if (format == DS_I18N_CATALOG_JSON && (locale == NULL || domain == NULL)) {
    return DS_ERR_NULL;
  }
  memset(&s, 0, sizeof(s));
  s.format = format;
  s.locale = locale;
  s.domain = domain;
  s.write = write;
  s.user = user;
  s.preflight = true;
  s.first = true;
  (void)ds_i18n_catalog_walk(t, ic_save_entry, &s);
  if (s.status != DS_OK) {
    return s.status;
  }
  s.preflight = false;
  if (format == DS_I18N_CATALOG_TSV) {
    ic_write(&s, ic_header, sizeof(ic_header) - 1);
    ic_write(&s, "\n", 1);
  } else {
    ic_write(&s, "{", 1);
  }
  if (s.status == DS_OK) {
    (void)ds_i18n_catalog_walk(t, ic_save_entry, &s);
  }
  if (format == DS_I18N_CATALOG_JSON) {
    ic_write(&s, s.first ? "}\n" : "\n}\n", s.first ? 2 : 3);
  }
  return s.status;
}
