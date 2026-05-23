# Diagnostics and errors

`ds` splits failure reporting into status, errors, and diagnostics.

- `ds_status_t` is the cheap return value used for control flow.
- `ds_error_t` is the primary machine-readable failure object.
- `ds_diagnostic_t` is a structured reportable event that may be emitted on
  success or failure.

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

## Formatting

`ds_error_format` and `ds_diagnostic_format` are bounded formatting helpers. They
respect the caller's buffer size and return the `snprintf`-style result.
