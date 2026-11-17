# Result, diagnostics and i18n

The C implementation follows the domain model in the supplied PHP packages:
[Result at 88b0ff9](https://github.com/Vxmdzhb/result/tree/88b0ff9b4c24b16167c50e70acf1e92ece4a24ec)
and [i18n at 9ddc58d](https://github.com/Vxmdzhb/i18n/tree/9ddc58df418ce9e8301c9ef11695734b04769555).
Memory ownership, operational failure and callbacks are explicit in C.

## What a Result means

`ds_result_t` contains exactly one domain branch, a payload handle and an ordered
diagnostic sequence. `ok(NULL)` and `err(NULL)` are both valid and distinct.
Diagnostics never decide the branch: a successful operation can carry warnings
or errors, and a failed operation need not carry any diagnostics. The domain
error may be an application struct, enum, string or other value, not necessarily
`ds_error_t` or a diagnostic.

The status returned by a Result operation answers whether that operation could
be completed. For example, `ds_result_err_ptr` returning `DS_OK` means that it
successfully constructed a domain error. `DS_ERR_ALLOC` means construction
failed and the initialized output remains unchanged. This separation avoids
turning memory exhaustion into a fabricated application error.

| PHP concept | C API and behavior |
|---|---|
| `ok`, `err` | `ds_result_ok`, `ds_result_err`; `_ptr` helpers borrow a pointer |
| `isOk`, `isErr` | explicit state predicates, independent of payload and log |
| `unwrap`, `error` | status plus output pointer; wrong branch is `DS_ERR_STATE`, with output untouched |
| `valueOr` | returns the success value, including NULL, or the supplied fallback |
| `withDiagnostics` | appends snapshots to a new persistent version |
| `map` | transforms only success; retains the ordered log |
| `andThen` | runs only on success; preceding diagnostics come before callback diagnostics |
| error mapping/recovery | `map_error`, `or_else`, with the corresponding error-branch behavior |
| `drainTo` | emits without consuming the source, matching the PHP behavior |
| immutable `Diagnostics` | Result logs use the existing RRB vector of immutable snapshot handles |

An initialized, empty Result is `DS_RESULT_UNINITIALIZED`, not a domain error.
Use a constructor before querying or composing it. All outputs must be
initialized; `copy`, `move` and compositional operations handle replacement and
may use an input as their output. Do not copy owning handles with `=` or
`memcpy`. Callback outputs arrive initialized and empty; fill them and return
`DS_OK`, or return a negative operational failure. A positive callback status
other than `DS_OK` becomes `DS_ERR_CALLBACK`.

## Payload ownership

`ds_result_value_t` makes ownership independent of the domain branch:

- `borrow` stores an application-owned pointer. It must remain alive for every
  Result that uses it. Borrowing the exact same pointer into an already owning
  handle preserves that owner to avoid a dangling value.
- `from_schema` copies a value with `ds_schema_t`; the owner stores the schema
  and later destroys the value and its allocation. A plain schema copies bytes,
  not pointed-to objects. Use copy/destroy callbacks for a deep owned value.
- `take` transfers a value and destroy callback only on `DS_OK`; its final
  release invokes that callback exactly once, including for a NULL value.
- `copy` retains the same owner. `view` retains it while exposing a subobject;
  use `view` when a map callback returns part of its input. A raw borrowed
  pointer to an owned input could otherwise outlive the original Result.

Sharing is immutable by contract. Callbacks must not mutate the input payload,
the source Result or its diagnostic records. The pointed-to bytes are not made
const by the C type system. A schema's callback state and every allocation
context must outlive all retained owners that use them. Reference counts are
not atomic; synchronize shared ownership operations externally.

The cheap existing `ds_result_ptr_t` and `ds_result_size_t` status/value pairs
remain available. Existing DS APIs are not forced to allocate an owned Result;
applications wrap outcomes where composition and accumulated diagnostics help.

## Diagnostics and sharing

`ds_diagnostics_add` copies the record's strings, typed arguments and argument
names into one immutable snapshot allocation. The strings may be stack-local
or temporary. Pointer arguments copy the pointer only. Supported types are
string, signed/unsigned long, size, pointer, double and boolean.

Collector append and filter retain snapshots. Result construction also retains
them, so clearing or destroying the source collector does not invalidate a
Result. Check direct collection return statuses. A collector used as a void
sink records its first failure in `status`; constructing or appending a Result
from that incomplete collector fails with that status. `clear` resets it.

Result logs use the library's RRB vector with branching 32 by default. Initialize
a `ds_result_config_t` and set `diagnostics.bits` to 2..8 to choose branching
4..256; its context selects allocation. Other RRB policy fields are managed by
the Result implementation. The config is borrowed and must remain unchanged
and alive until all its versions are released; NULL selects a static default.

Retaining a Result is constant work. Same-config binding concatenates logs in
O(M log_M n), sharing untouched RRB nodes, where M is branching. Different
configs import the preceding log into the callback Result's configuration by
repeated append; that conversion is not a logarithmic join. Snapshot strings
are still shared. Their original allocator contexts must remain alive. Replacing
an existing output additionally pays the cost of releasing its old version.

Callbacks invoked by emission or filtering must not mutate or release their
source. Emission borrows each record only during the callback; retain a snapshot
or add it to a collector to keep it.

## Catalog composition

`ds_i18n_config_t.map` selects any compatible `ds_map_ops_t` backend, with Robin
Hood hashing as the default. Catalog identity is `(locale, domain, key)`;
changing locale cannot accidentally reuse entries from another locale.
Registered strings are copied. Formatter/provider callback state is borrowed.

For each lookup the order is:

1. A locally registered entry for the selected locale.
2. The optional provider for that locale.
3. If absent, local entries then the provider for the explicit fallback locale.

Within one locale/domain, later registration replaces earlier registration.
Register module catalogs first and application overrides last. A provider can
read an application file format or another storage system; only `DS_NOT_FOUND`
means absence. I/O and other operational errors propagate instead of silently
choosing fallback text. `has` probes the same chain without emitting a missing
translation diagnostic. Catalog walk enumerates local entries in reverse
insertion order, not provider-owned entries.

Messages contain either literal text or a formatter callback. Literal text
supports single-pass `{name}` substitution: unknown placeholders remain, NULL
strings become empty text, and substituted values are never interpreted again.
A formatter writes to a supplied writer and may explicitly call `interpolate`.
Unlike the PHP callable-return convention, its output is final; the C core does
not run a second interpolation pass. This prevents substituted user text from
becoming a template. Writer errors are sticky even if a formatter ignores an
append/interpolation return value. The completed text replaces the initialized
output only on success; otherwise previous output remains available.

Initialize `ds_i18n_text_t` with the desired output context, which may differ
from the translator's catalog context. Destroy it with `ds_i18n_text_destroy`.
Nested translation/provider calls have a configurable depth limit (32 by
default); callbacks must not mutate the catalogs or locale or destroy the
translator while rendering. Objects require external synchronization.

## Diagnostic presentation

Ordinary missing entries emit `ds.i18n/missing_translation` at NOTICE, then
render explicit fallback text, the key or empty text according to config.
Diagnostic rendering uses the `diagnostics` domain keyed by diagnostic ID;
missing entries produce `[FALLBACK:LEVEL] id` without emitting another event.
The `levels` domain localizes DEBUG through EMERGENCY, with quiet fallback to
those names. This separation prevents missing-diagnostic-message recursion.

`ds_i18n_render_filtered` applies a viewer callback that can hide an event or
change its effective severity. It then applies the viewer's minimum level.
`DS_NOT_FOUND` means hidden and leaves the previous output unchanged. The
source diagnostic and Result branch remain unchanged. Severity increases from
DEBUG=0 to EMERGENCY=7, as in the supplied PHP package.

Context remains opt-in: callers choose which arguments and source fields to
capture. The C implementation does not introspect arbitrary application objects.

## Deliberate boundaries

The core accepts UTF-8-compatible byte strings but does not validate or normalize
Unicode. It does not execute PHP, edit PHP catalog files, supply CLDR data,
implement universal plural rules or mutate the process locale. Built-in numeric
substitution uses C formatting (including the process numeric locale for
floating-point values); formatters supply language-specific behavior. The
provider and formatter APIs are the extension points for those policies.

Run `make examples` and `build/examples/result_i18n`. That example owns a count
through a schema, maps and binds it, destroys intermediate Results, and renders
the retained ordered log in English and Italian. It also shows a callable plural
message and a valid NULL domain error.
