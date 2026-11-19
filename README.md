# dsa

A C library of data structures and algorithms, built around explicit memory
ownership and configurable components.

Use a vector or hash table on its own, or choose the pieces of a larger
structure: the child store of a trie, the priority queue used by a graph
algorithm, or the cache and storage backend behind a pager. Allocator contexts,
comparison callbacks and ownership policies let the library work with your
application's data and memory model.

The library covers containers, trees, tries, hashing, graphs, text indexes,
persistent structures and storage, together with sorting, searching, string,
graph, geometry and numerical algorithms. Browse the [module catalog](docs/catalog.md)
for individual implementations.

## Build and run

You need a C compiler, GNU Make 4.3 or later and the C math library. The default
compiler flags target GCC. Unicode input data is bundled; building needs no
download.

```sh
make -j4
make examples
./build/examples/getting_started
```

This produces `build/libdsa.a`, `build/libdsa.so` and the example executables.
See [building and installation](docs/building.md) for compiler options, installing
the library, pkg-config and Nix.

## A first example

Create a vector, copy three integers into it, sort them and release its storage:

```c
#include <dsa.h>
#include <stdio.h>

static int compare_ints(const void *a, const void *b, void *user) {
  int x = *(const int *)a;
  int y = *(const int *)b;
  (void)user;
  return (x > y) - (x < y);
}

int main(void) {
  int values[] = {3, 1, 2};
  ds_vec_t numbers;
  size_t i;

  if (DS_FAILED(ds_vec_init(&numbers, sizeof(int), NULL))) {
    return 1;
  }
  if (DS_FAILED(ds_vec_append(&numbers, values, 3))) {
    ds_vec_destroy(&numbers);
    return 1;
  }

  ds_vec_sort(&numbers, compare_ints, NULL);
  for (i = 0; i < ds_vec_size(&numbers); ++i) {
    printf("%d\n", *(const int *)ds_vec_at(&numbers, i));
  }

  ds_vec_destroy(&numbers);
  return 0;
}
```

This prints `1`, `2` and `3`, each on its own line. The vector owns its storage;
the original array remains yours. Passing `NULL` as the context selects the
default allocator. Supply a context to choose another allocator, or use
`ds_vec_init_buffer` for a fixed caller-owned buffer.

Save the example as `main.c` and compile it from the repository root:

```sh
cc -std=c99 -Iinclude main.c build/libdsa.a -lm -o demo
./demo
```

## Which header should I include?

Use `<dsa.h>` to explore the library, or include individual modules when you know
which APIs you need.

| Header | What it provides |
|---|---|
| `<dsa.h>` | All public modules |
| `<dsad.h>` | Data structures and shared services |
| `<dsaa.h>` | Algorithms and shared services |
| `<dsa/structures/vec.h>` | The vector API |
| `<dsa/algorithms/sort.h>` | Sorting algorithms |
| `<dsa/core/result.h>` | Results and their dependencies |

The compiler's include path points to `include/`, so the file
`include/dsa/structures/vec.h` is included as `<dsa/structures/vec.h>`.
Public headers live together under `include/`; their implementations live under
`src/` in matching groups. You do not need to include implementation files.

Each module header includes its dependencies. The umbrellas are conveniences;
all modules link to the same `libdsa` library and use the `ds_` function and type
prefix. Including an umbrella does not force every object in the static archive
into your executable.

## Working with the APIs

**Memory and ownership.** Containers document whether they copy, borrow or own
values. Intrusive structures embed links in your records. Contexts and
copy/destroy callbacks control allocation and lifetime; configurable components
carry those choices into nested storage. Read [ownership](docs/ownership.md)
and [mutation and lifetimes](docs/container-lifetimes.md) when sharing values
between containers.

**Interchangeable components.** Maps, ordered maps, priority queues, rank/select
structures and storage backends expose interfaces for composition. Check their
[component contracts](docs/contracts.md): choosing a backend also chooses its
costs, allocation behavior and cursor guarantees.

**Statuses, Results and diagnostics.** Many operations return `ds_status_t`;
check the documented outcome before using their output. `ds_result_t` provides
an explicit success or error branch, payload ownership and an ordered diagnostic
log for workflows that need them. Modules can define their own diagnostic
schemas and stable identifiers without a central registry. Error snapshots can
retain causes, and diagnostic collectors support storage limits. These shared
services live in `dsa/core/` because both structures and algorithms can use them.
See [Results and localization](docs/result-i18n.md) and
[diagnostic schemas and errors](docs/diagnostics.md). Localization renders messages
without changing their identity or the Result's meaning.

**Versions and history.** Persistent containers share immutable storage between
versions. The [history layer](docs/history.md) provides branches, checkpoints,
operation replay and retroactive edits through adapters.

## Explore the repository

| Location | Contents |
|---|---|
| `include/dsa.h`, `include/dsad.h`, `include/dsaa.h` | Public umbrella headers |
| `include/dsa/core/` | Shared service APIs, including allocation and errors |
| `include/dsa/structures/` | Data structure APIs |
| `include/dsa/algorithms/` | Algorithm APIs |
| `src/` | Implementations, grouped like the public headers |
| `examples/` | Runnable programs demonstrating the APIs |
| `docs/` | Guides, contracts and module reference |
| `tools/`, `data/` | Unicode generator and its source data |
| `build/` | Generated files, libraries and executables |

Start with the [API guide](docs/api-overview.md), choose a program from the
[examples](examples/README.md), or browse the [documentation index](docs/README.md).
Public headers describe operations, preconditions and limits.

## License

[Do Whatever License](LICENSE). The [code of conduct](CODE_OF_CONDUCT) applies to
contributions. Bundled Unicode data carries the terms named in its file headers.
