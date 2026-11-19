# Result, diagnostics and i18n

Results keep domain outcomes, diagnostic events and presentation policy separate.
Payload ownership, operational failure and callback lifetimes are explicit.

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

| Operation | API and behavior |
|---|---|
| `ok`, `err` | `ds_result_ok`, `ds_result_err`; `_ptr` helpers borrow a pointer |
| `isOk`, `isErr` | explicit state predicates, independent of payload and log |
| `unwrap`, `error` | status plus output pointer; wrong branch is `DS_ERR_STATE`, with output untouched |
| `valueOr` | returns the success value, including NULL, or the supplied fallback |
| `withDiagnostics` | appends snapshots to a new persistent version |
| `map` | transforms only success; retains the ordered log |
| `andThen` | runs only on success; preceding diagnostics come before callback diagnostics |
| error mapping/recovery | `map_error`, `or_else`, with the corresponding error-branch behavior |
| `drainTo` | emits without consuming the source, preserving the source sequence |
| `zip` | combines independent Results; explicit callbacks combine successes or two errors |
| immutable `Diagnostics` | replaceable log backend; RRB and flat-array implementations |

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

`ds_result_err_snapshot` retains an immutable error with owned strings, optional
native metadata and a causal chain. This payload is inspected with the error
snapshot API. `ds_result_diagnostic_dropped` reports explicit truncation inherited
from collectors and composition; it is independent of the domain branch.

The lightweight `ds_result_ptr_t` and `ds_result_size_t` types pair an
operational status with a value. Use an owned Result when domain composition
and accumulated diagnostics help; each application chooses that boundary.

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

A Result copies its `ds_result_config_t`, including the diagnostic backend's
operation table. A stack-local configuration can go out of scope after
construction. Allocation contexts and custom backend user state remain borrowed
and must outlive every version using them. An empty Result with a borrowed
payload allocates nothing.

The default log uses an RRB vector with branching 32. Set `diagnostics.bits`
to 2..8 for branching 4..256. Set `diagnostics.ops` to
`*ds_diagnostic_log_flat_ops()` to select a flat array, or supply an immutable
backend through `ds_diagnostic_log_ops_t`. `diagnostics.context` selects log
allocation; payload and snapshot owners can use different contexts.

| Backend | Copy | Append | Compatible concatenation | Traversal |
|---|---|---|---|---|
| RRB (branching M) | O(1) | O(M log_M n) | O(M log_M n) | O(n) |
| Flat array | O(1) | O(n) | O(n + m) | O(n) |

These costs exclude releasing an existing destination. RRB updates share
untouched nodes. Flat updates allocate a new array of retained snapshot handles.
Both preserve immutable versions. Configurations with matching operations,
context, branching and user state can use native concatenation. Otherwise the
right log is imported by ordered appends into the **left** log's policy.
An empty left log still determines that policy. Records retain their original
snapshot owners; their contexts must remain alive after an import.

A custom backend provides retain, release, count, indexed access and append.
Concatenation and traversal are optional; ordered import and indexed traversal
provide fallbacks. Its states own retained snapshot references. An update must
return a separately owned state on success and leave inputs unchanged on failure.
A callback returning a positive status is rejected. The log owns its configuration
copy; backend state must copy any policy data it needs rather than keep a pointer
to the operation's temporary configuration. See
[diagnostic_log.h](../include/dsa/core/diagnostic_log.h) for the full contract.

## Sequential composition and independent validation

`and_then` runs the next operation only after success. `or_else` runs recovery
only after an error. Both append callback diagnostics after the input diagnostics
and preserve the input log policy. `map` and `map_error` change a payload on the
selected branch without changing the log.

`ds_result_zip` combines two already-computed Results. Two successes call
`combine_ok`; two errors call `combine_error`; one error retains that error's
payload. Only the callback for the selected case is required. The error callback
can build an ordered collection, merge a field-error map, combine a bitset, or
apply another domain-specific rule. There is no implicit policy that discards
one error. Both logs are retained, left before right. A failing combine callback
leaves the initialized output unchanged. These guarantees cover library state,
not external side effects performed by callbacks.

`DS_RESULT_DECLARE_POINTERS` in a header and `DS_RESULT_DEFINE_POINTERS` in one
source file generate typed borrowed-pointer constructors and checked accessors.
They avoid repeated pointer casts at application boundaries. The wrapper exposes
its generic `core` for owned schema payloads and composition; using that escape
hatch with the wrong payload type defeats the facade's type discipline.

[The validation example](../examples/result_validation.c) combines independent
field errors using both log backends and retains the result after destroying its
inputs. [The ownership example](../examples/result_ownership.c) keeps a subobject
alive through a mapped Result and handles an exhausted allocation context without
replacing an existing output.

Callbacks invoked by emission or filtering must not mutate or release their
source. Emission borrows each record only during the callback; retain a snapshot
or add it to a collector to keep it. `ds_result_drain_to` returns a traversal
status; a void sink records its own presentation failures if it needs to report them.

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
Its output is final; rendering does not run a second interpolation pass. This prevents substituted user text from
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
DEBUG=0 to EMERGENCY=7.

Context remains opt-in: callers choose which arguments and source fields to
capture. The C implementation does not introspect arbitrary application objects.

## Deliberate boundaries

The core accepts UTF-8-compatible byte strings but does not validate or normalize
Unicode. Locale data, plural rules and catalog-file formats are separate
policies; translation does not mutate the process locale. Built-in numeric
substitution uses C formatting (including the process numeric locale for
floating-point values); formatters supply language-specific behavior. The
provider and formatter APIs are the extension points for those policies.

Run `make examples` and `build/examples/result_i18n`. That example owns a count
through a schema, maps and binds it, destroys intermediate Results, and renders
the retained ordered log in English and Italian. It also shows a callable plural
message and a valid NULL domain error.
