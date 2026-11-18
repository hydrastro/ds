# Catalog data and locale policies

`i18n_catalog.h` supplies two data-only formats over exact writer and chunked
reader callbacks. The FILE helpers borrow the stream. Callers select the map
backend on the translator; parsing uses its context and the existing vector.
See `examples/localized_catalog.c` for importing, exporting and rendering.

| Format | Scope | Representation |
|---|---|---|
| TSV version 1 | Multiple locales and domains | Header `#!ds-i18n-tsv 1`, then locale, domain, key, text separated by tabs |
| Flat JSON | One explicitly supplied locale/domain | One object whose keys and values are strings |

TSV escapes are `\\`, `\t`, `\n`, `\r`, `\#`. Blank lines and lines beginning
with `#` after the header are ignored. LF and CRLF endings are accepted. JSON
uses [RFC 8259 string syntax](https://www.rfc-editor.org/rfc/rfc8259.html),
including escaped Unicode and paired surrogate escapes. Non-string values,
trailing data, BOMs, malformed UTF-8 and embedded NUL are rejected. Duplicate
keys are applied in input order, with the last value winning.

Loads publish one atomic batch. The default limits are 16 MiB input, 100000
entries and 1 MiB per decoded field; initialize and change a limits structure
when a different policy is required. Zero disables the corresponding limit.
The reader stages its bytes, and publication copies existing records, so peak
memory includes both old and new catalogs. This is appropriate for translation
catalogs, not a bounded-memory database import.

Exports enumerate local records in catalog-walk order. JSON filters by the
specified locale/domain; TSV includes every local record. Providers cannot be
enumerated through the lookup interface. A selected formatter callback prevents
export with `DS_ERR_UNSUPPORTED` before bytes are written. I/O errors can leave
partial output. The caller controls temporary-file replacement, flush and sync.

## Exact values and explicit policy

`i18n_locale.h` accepts ordinary ASCII decimal strings, retaining visible fraction
digits and avoiding overflow in large-count plural selection. Signs do not alter
plural categories. Named cardinal profiles use
[CLDR 48 rules](https://github.com/unicode-org/cldr-json/blob/48.0.0/cldr-json/cldr-core/supplemental/plurals.json)
for English, Italian, French, Russian and Arabic with exponent operand zero.
These profiles do not imply coverage of all locales, ordinal forms or ranges.
Custom callbacks receive a borrowed exact decimal view.

A plural-message descriptor registers through the normal formatter interface.
Its strings and callback user remain application-owned. Count arguments may be
exact strings or C integer types; floating-point arguments are rejected because
they do not preserve visible fraction precision. Missing category text uses the
required `other` form. Substitution remains a single pass.

Number policy controls decimal/group separators, primary and secondary grouping
widths, a grouping threshold and signs. Digits are preserved; there is no implicit
rounding or process-locale dependency. Date formatting accepts explicit Gregorian
fields and an already resolved UTC offset, with `%Y %m %d %H %M %S %z %%`.
Other calendars, localized names, time-zone resolution, currency rules and digit
systems can use ordinary formatters with application-owned policy.

Every helper reports errors through the writer's sticky status. The surrounding
translation publishes output only after the complete formatter succeeds.
