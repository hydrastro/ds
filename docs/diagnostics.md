# Diagnostics and errors

`dsa` splits failure reporting into status, errors, and diagnostics.

- `ds_status_t` is the cheap return value used for control flow.
- `ds_error_t` is the primary machine-readable failure object.
- `ds_diagnostic_t` is a structured reportable event that may be emitted on
  success or failure.
- `ds_result_t` carries a domain success or error payload and an immutable,
  ordered diagnostic sequence. Its branch is independent of severity and NULL.

A function returns exactly one primary status. A failing function may set one
primary error in its `ds_context_t`. A function may emit zero or more diagnostics
to the context's diagnostic sink.

Diagnostics are intentionally not exceptions. They are observable events such as:

```txt
ds.history/checkpoint_created
ds.hash_table/high_load_factor
ds.history/retroactive_edit
ds.alloc/bad_free
```

## Status values

Nonnegative statuses are conditions:

```txt
DS_OK
DS_NOT_FOUND
DS_EXISTS
DS_EMPTY
DS_STOP
```

Negative statuses are failures:

```txt
DS_ERR_ALLOC
DS_ERR_INVALID
DS_ERR_NULL
DS_ERR_RANGE
DS_ERR_OVERFLOW
DS_ERR_UNDERFLOW
DS_ERR_STATE
DS_ERR_CALLBACK
DS_ERR_UNSUPPORTED
DS_ERR_INTERNAL
DS_ERR_IO
```

`DS_IS_OK(status)` checks exactly `DS_OK`. `DS_SUCCEEDED(status)` checks for
any nonnegative condition, including not-found and stop; it does not imply an
output value was produced. Inspect the operation's documented statuses.

Use:

```c
if (DS_FAILED(status)) {
  /* actual failure */
}
```

## Context errors

`ds_context_t` stores the last primary error. This is useful for diagnostics and
human-readable reporting, but it should not replace the returned status value.
The status remains the authoritative control-flow result.

For multi-threaded programs, prefer explicit contexts rather than sharing the
default context if code reads or writes `last_error` concurrently.

## Diagnostic sinks

Diagnostics are emitted into a `ds_diagnostic_sink_t`. The built-in collector
`ds_diagnostics_t` can be attached to a context:

```c
ds_context_t ctx;
ds_diagnostics_t diagnostics;

ds_context_init(&ctx);
ds_diagnostics_init(&diagnostics);
ds_context_set_diagnostic_sink(&ctx, ds_diagnostics_sink(&diagnostics));
```

Modules can then emit structured diagnostics through the context.

The collector owns snapshots: it copies IDs, messages, source strings,
argument names, and string argument values. A producer may pass stack-local
arguments. `DS_ARG_PTR` copies the pointer only; its pointee remains borrowed.
The public `items` array is a read-only view, valid until the collector changes.
Use `ds_diagnostic_snapshot_create`, retain and release to keep an independent
immutable event. Snapshot reference counts are not atomic.

`ds_diagnostics_add`, `append` and `filter` report failures with a status and
leave their destination sequence unchanged on failure. Check those returns.
An emission through the void sink cannot return a failure; the collector
records the first failure in `diagnostics.status`. Clearing the collector resets
that status. Result construction refuses a collector with a recorded failure
instead of silently treating a partial sequence as complete.

`ds_diagnostics_emit` and `ds_result_drain_to` emit without clearing the source.
Sink callbacks borrow each record for the callback's duration and must not
mutate or release the source during emission. Initialize owning handles before
use, release them explicitly, and do not duplicate them with struct assignment.

## Result and localization

`ds_result_map` transforms a success payload. `ds_result_and_then` runs a
success-producing callback and concatenates the old diagnostics before the
callback's diagnostics, including when that callback returns a domain error.
`map_error` and `or_else` provide the corresponding error operations. The
functions' returned `ds_status_t` reports operational failure (for example an
allocation failure); `is_ok` and `is_err` inspect the domain outcome.

`dsa/core/i18n.h` translates ordinary messages and renders diagnostic IDs through
separate catalog domains. Missing ordinary translations emit a NOTICE; missing
diagnostic translations use a quiet fallback to prevent recursive reporting.
Severity is ordered DEBUG=0 through EMERGENCY=7. Neither a diagnostic nor a
viewer's filtering or severity override changes the Result branch.

See [Result and i18n](result-i18n.md) for ownership, configuration, the mapping
between operations, and a complete runnable example.

## Formatting

`ds_error_format` and `ds_diagnostic_format` are bounded formatting helpers. They
respect the caller's buffer size and return the `snprintf`-style result.

## Module-defined vocabulary

A stable namespaced ID, such as `captcha/expired`, identifies a record across
modules and shared libraries. Compare with `ds_diagnostic_matches` or
`ds_error_matches`; message text, severity and descriptor addresses do not define
identity. Keep an ID's field schema stable; incompatible changes need a new ID.
No registration or central enum extension is required.

`ds_diagnostic_schema_t` declares required field names and types. `build` creates
a borrowed validated view; `add_schema` snapshots it into a collector. Validation
rejects mismatched identity, missing or extra fields, duplicates and wrong types.
Field order does not matter. `arg_get` checks a requested field's type and leaves
its output unchanged on failure; DS_NOT_FOUND means no field was returned.

For typed constructors and accessors, declare a field list:

```c
#define EXPIRED_FIELDS(X) X(CSTR, token_id) X(INT, expired_at)
DS_DIAGNOSTIC_DECLARE(captcha_expired, EXPIRED_FIELDS);
```

In exactly one source file, following that declaration:

```c
DS_DIAGNOSTIC_DEFINE(captcha_expired, "captcha/expired", DS_DIAG_NOTICE,
                    "captcha", "Token {token_id} expired at {expired_at}",
                    EXPIRED_FIELDS)
```

This generates `captcha_expired_values`, `captcha_expired_schema`,
`captcha_expired_add` and `captcha_expired_get`. Add copies strings; get returns
borrowed views into the record. CSTR, INT, UINT, SIZE, PTR, DOUBLE and BOOL map to
const char pointer, long, unsigned long, size_t, const void pointer, double and
bool. Lists must contain at least one field; zero-field records use the schema
functions directly. The macros are optional: ordinary typed module functions
can call the same generic API. See [module_diagnostics.c](../examples/module_diagnostics.c).

## Owned errors and causes

`ds_error_t` is a borrowed view. Its strings, including `native_domain`, must
outlive its use. Initialize it with clear/set before setting category or native
metadata. Set resets these metadata fields. Categories classify failures for
generic handling; the module's stable ID remains the specific identity. Native
codes are interpreted within their namespace, such as `posix.errno`.

`ds_error_snapshot_create` copies every string and retains an optional owned
cause. Release each acquired reference. Causes are immutable, cannot form cycles
through this API, and release iteratively without recursion proportional to chain
length. Contexts must outlive all retained snapshots. Source locations describe
the operation recorded by the module; they are not automatic propagation traces.

`ds_result_err_snapshot` retains an error snapshot as the domain payload.
`ds_result_error_snapshot` checks both branch and payload ownership type before
yielding a borrowed snapshot pointer. Other payloads are DS_ERR_INVALID, and a
success branch is DS_ERR_STATE. Inspect it with the snapshot accessors. `ds_error_snapshot_diagnostic` exposes a borrowed renderable record,
including native_domain and native_code arguments, for the existing i18n API.
A cause chain explains one failure; independent validation errors remain a
separate payload collection selected by the application's zip callback.

The POSIX page store captures errno immediately on failed I/O and records it in
the context with DS_ERR_IO. Context NO_LAST_ERROR disables recording. Absence
reported as DS_NOT_FOUND remains a condition. Context last_error is supplementary:
not every failing function writes it, successful calls need not clear it, and
nested operations can overwrite it. Snapshot a relevant error before another call.
See [error_causes.c](../examples/error_causes.c).

## Bounded collection

Default collectors are unlimited and strict. `init_bounded` sets a record limit:
zero admits no records. STRICT returns DS_ERR_RANGE on capacity exhaustion;
TRUNCATE keeps the earliest records, drops the incoming suffix and returns DS_OK
with an explicit dropped count. Count arithmetic never wraps. Allocation failures
remain errors under either policy. Direct checked failures preserve the sequence;
a void sink records its first failure in status. Clear resets status and drops.

`init_buffer` uses caller-owned record and snapshot-pointer arrays. Snapshots
still allocate through the selected context; combine it with a caller-backed
arena for a bounded total memory budget. Arena releases reclaim memory only on
reset, after every referencing Result and collector is destroyed. Filtering stages
its result in temporary storage, even for a fixed destination; it may allocate.

Append and filter preserve prior dropped counts. Filtering cannot determine which
missing records would match, so it conservatively retains that count. Results
carry it through construction, copy, map, bind, zip and added diagnostics; query
`ds_result_diagnostic_dropped`. Counts measure omitted occurrences, so composing a
Result with itself doubles its count. Emission visits retained records only:
check dropped/status separately when presenting completeness. No fabricated event
is inserted into the log. Borrowed outcomes and immediate error views remain
available when there is no memory to build richer diagnostics.
