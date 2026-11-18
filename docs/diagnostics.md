# Diagnostics and errors

`ds` splits failure reporting into status, errors, and diagnostics.

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
```

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

The collector now owns snapshots: it copies IDs, messages, source strings,
argument names, and string argument values. A producer may pass stack-local
arguments. `DS_ARG_PTR` copies the pointer only; its pointee remains borrowed.
The public `items` array is a read-only view, valid until the collector changes.
Use `ds_diagnostic_snapshot_create`, retain and release to keep an independent
immutable event. Snapshot reference counts are not atomic.

`ds_diagnostics_add`, `append` and `filter` report failures with a status and
leave their destination sequence unchanged on failure. Check those returns.
An emission through the legacy void sink cannot return a failure; the collector
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

`lib/i18n` translates ordinary messages and renders diagnostic IDs through
separate catalog domains. Missing ordinary translations emit a NOTICE; missing
diagnostic translations use a quiet fallback to prevent recursive reporting.
Severity is ordered DEBUG=0 through EMERGENCY=7. Neither a diagnostic nor a
viewer's filtering or severity override changes the Result branch.

See [Result and i18n](result-i18n.md) for ownership, configuration, the mapping
between operations, and a complete runnable example.

## Formatting

`ds_error_format` and `ds_diagnostic_format` are bounded formatting helpers. They
respect the caller's buffer size and return the `snprintf`-style result.
