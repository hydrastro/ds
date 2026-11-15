# ds: one source, several libraries

What you built: every function is declared as `FUNC(name)`, which expands
to `name` normally and to `name##_safe` under `DS_THREAD_SAFE`; the
Makefile compiles each `lib/*.c` twice (`%.o` and `%_safe.o` with
`-D DS_THREAD_SAFE`) into `libds` and `libds_safe`; `ds_safe.h` defines the
macro and includes `ds.h`. Inside the functions, `LOCK`/`UNLOCK` take a
recursive mutex when the structure's `is_thread_safe` flag is set.

## Verdict: a real technique, not a mind game

Compiling one source several times with different macros, and mangling
the names so the variants can coexist, is an old and respected technique
("multi-compilation", "templates by preprocessor"). Well-known C projects
are built this way:

| Project | What it builds from one source | How |
|---|---|---|
| **FFTW** | `fftw_`, `fftwf_`, `fftwl_`, `fftwq_` (double, float, long double, quad) | an `X(name)` macro, exactly your `FUNC(name)`, and the sources compiled once per precision |
| **OpenBLAS** | single, double, complex and transposed variants of each kernel | the same `.c` compiled with `-DDOUBLE`, `-DCOMPLEX`, `-DTRANSA`... |
| **glibc libm** | `sinf`, `sin`, `sinl`, `sinf128`... | math "template" files and type macros, compiled per type |
| **ncurses** | `libncurses`, `libncursesw` (wide), `libncursest` (threads) | the same tree configured several ways |
| **SQLite** | single-thread, multi-thread, serialized builds | `SQLITE_THREADSAFE=0/1/2` at compile time, plus a runtime switch: like your macro + `is_thread_safe` |
| **zlib, jemalloc** | symbols with a prefix (`z_`, `je_`) | `Z_PREFIX`, `JEMALLOC_PREFIX`, so a vendored copy doesn't clash |
| **Win32** | `CreateFileA` / `CreateFileW` | `UNICODE` and `TCHAR` macros |
| **BSD `sys/tree.h`, klib, STC** | one red-black tree / hash table per element type | `RB_GENERATE`, `KHASH_INIT` macros that stamp out code |

So: no, it's not "intellectual masturbation". You reinvented a standard
pattern, and it saves you from keeping near-identical copies in sync,
which was your goal.

## What to fix in how ds uses it

The mechanism is fine. What I'd question is what the `_safe` variant
does and one detail of how it does it:

1. **The struct layout changes between variants, but the type name
   doesn't.** With `DS_THREAD_SAFE`, `ds_circular_buffer_t` gains a mutex
   and a flag. A program where one file includes `ds.h` and another
   `ds_safe.h`, and the two pass a buffer to each other, has two
   different structs under one name. The compiler and linker won't
   complain; it just corrupts memory (an ODR violation). Pick one fix:
   - also mangle the types: `TYPE(ds_circular_buffer)` →
     `ds_circular_buffer_safe_t`, so mixing the variants is a compile
     error; or
   - always keep the lock fields, so the layout is identical and only the
     code differs (a few bytes per structure); or
   - make the safe variant a wrapper struct holding the plain one plus the
     lock.
2. **Two switches for one thing.** You have the compile-time variant and
   a runtime `is_thread_safe` flag. SQLite has both deliberately (one
   binary, configured at startup); for ds, one is enough. If you keep the
   flag, the `_safe` build is almost redundant; if you keep the build,
   drop the flag and its branch on every call.
3. **Locking every call doesn't make sequences safe.**
   `if (!is_empty(q)) pop(q);` can still race: each call is atomic, the
   pair isn't. That's what I meant by an **explicit lock wrapper**: give
   users `ds_X_lock(x)` / `ds_X_unlock(x)` (or a `with_lock(x, fn, arg)`)
   so they can make a whole sequence atomic. Your tests pass because each
   test thread does whole operations; a check-then-act test would fail.
   By **real concurrent structures** I meant structures designed for
   concurrency, such as a lock-free queue (Michael-Scott), a concurrent
   skip list, or striped locks in a hash table. Those let several
   threads work at once instead of taking turns behind one mutex.

None of this argues against the mechanism. It argues for using it
where it shines.

## Where the same mechanism pays off

Each example is the pattern you already have: one source, a macro,
mangled names, built N times by `make`.

### 1. A checked variant (the best one for a data-structure library)

After every mutating call, verify the structure's invariants (red-black
colours, heap order, sizes, parent links). Tests, fuzzing and users chasing
a bug link `libds_checked`; production links `libds`. It costs nothing
when it's off.

```c
/* common.h */
#ifdef DS_CHECKED
#define FUNC(name) name##_checked
#define CHECK(type, s) ds_##type##_check_invariants(s, __FILE__, __LINE__)
#else
#define FUNC(name) name
#define CHECK(type, s) ((void)0)
#endif

/* avl.c */
void FUNC(ds_avl_insert)(ds_avl_t *t, ds_avl_node_t *n) {
	/* ... */
	CHECK(avl, t);  /* heights, balance factors, ordering, parent links */
}
```

With your fuzzing and sanitizer setup, this turns "something crashed
later" into "insert broke the balance factor at line N".

### 2. Element-type specialisations: by-value containers

The by-value contiguous containers that ds is missing (see the
roadmap), done once and stamped per type, like FFTW and libm:

```c
/* vec.inc: no include guard, meant to be included several times.
 * DS_CAT(a, b) is the usual two-step a##b, so T_NAME gets expanded. */
#define V(name) DS_CAT(ds_vec_, DS_CAT(T_NAME, _##name))
typedef struct { T *data; size_t size, cap; } V(t);
bool V(push)(V(t) *v, T x) { /* grow, then v->data[v->size++] = x */ }
T    V(get)(const V(t) *v, size_t i) { return v->data[i]; }
#undef V

/* vec_types.c */
#define T int
#define T_NAME int
#include "vec.inc"
#undef T
#undef T_NAME
#define T double
#define T_NAME double
#include "vec.inc"
```

That gives `ds_vec_int_push`, `ds_vec_double_push`... as real functions,
with the compiler's full type checking, unlike `void *` containers.

### 3. Index-width variants for small machines

Intrusive structures stored in arrays can link by index instead of by
pointer. With `uint16_t` indices a node shrinks from 16–24 bytes of
pointers to 4–6, which matters on microcontrollers and is cache-friendlier
on your old ThinkPads too.

```c
#if DS_INDEX_BITS == 16
typedef uint16_t ds_idx_t;
#define FUNC(name) name##_16
#else
typedef uint32_t ds_idx_t;
#define FUNC(name) name##_32
#endif
```

### 4. Symbol prefixing for vendoring

If two libraries each bundle a copy of ds, the linker sees two
`ds_avl_insert`s. zlib and jemalloc solve this with a prefix macro; so
can you:

```c
#ifdef DS_PREFIX              /* make CFLAGS=-DDS_PREFIX=myapp_ */
#define FUNC(name) DS_CAT(DS_PREFIX, name)
#endif
```

### 5. An instrumented variant for benchmarks and teaching

Count comparisons, rotations, allocations and probe lengths. It fits
your algorithms list well: "AVL vs red-black vs treap vs skip list,
comparisons per insert" becomes a table you can measure and publish.

```c
#ifdef DS_STATS
#define FUNC(name) name##_stats
#define COUNT(field) (ds_stats.field++)
#else
#define FUNC(name) name
#define COUNT(field) ((void)0)
#endif
```

### 6. Numeric precision variants

Fenwick trees, segment trees, sparse tables and matrices over `int64_t`,
`double` or `long double`: the FFTW case exactly.

## Making the mechanism nicer to live with

- **Name the variant once.** Instead of every feature redefining `FUNC`,
  build the suffix from the enabled options in one place
  (`DS_VARIANT_SUFFIX` = `_safe`, `_checked`, `_safe_checked`...). The
  Makefile then loops over a variant list instead of hard-coding two.
- **Mangle types as well as functions** (see point 1 above).
- **Tooling:** `FUNC(...)` hides names from grep, ctags and
  "go to definition". Universal ctags can expand the macro:
  `ctags -D 'FUNC(name)=name' -R lib` indexes `FUNC(ds_avl_insert)` as
  `ds_avl_insert`. clangd already sees through it, since it runs the
  preprocessor. A short note in CONTRIBUTING helps too.
- **Test every variant.** You already run the tests twice (`test` and
  `test-safe`); a checked variant would give the most valuable third run.
- **The flake** can expose each variant as a package output
  (`packages.ds`, `packages.ds-safe`, `packages.ds-checked`) by passing
  the variant to `make`, so users pick exactly what they link.

**In short:** keep the mechanism, fix the type-name/layout trap, choose
between the build switch and the runtime flag, and add a `_checked`
variant: it's the most useful thing this design can give you.
