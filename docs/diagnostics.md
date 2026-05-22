# Diagnostics and errors

`ds` splits failure reporting into status, errors, and diagnostics.

- `ds_status_t` is the cheap return value used for control flow.
- `ds_error_t` is the primary machine-readable failure object.
- `ds_diagnostic_t` is a structured reportable event that may be emitted on
  success or failure.

A function returns exactly one primary status. A failing function may set one
primary error in its `ds_context_t`. A function may emit zero or more diagnostics
to the context's diagnostic sink.

Diagnostics are intentionally not exceptions. They are observable events such as
`ds.history/checkpoint_created`, `ds.hash_table/high_load_factor`, or
`ds.history/retroactive_edit`.
