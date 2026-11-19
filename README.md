# dsa

Data structures and algorithms in C, with control over how they store memory,
own values, compare keys and work together.

Use a vector or hash map directly, embed links in your own records, or choose the
components of a larger structure. A trie can use different child stores; graph
algorithms can use different priority queues; a pager can use different caches
and storage backends. These choices are part of the API.

The library includes trees, tries, hash tables, heaps, graphs, text indexes,
persistent containers and storage structures, alongside sorting, searching,
string, graph, geometry and numerical algorithms. The [module catalog](docs/catalog.md)
is the place to browse them.

## Start here

```sh
make -j4
make examples
./build/examples/getting_started
```

This builds the library and the runnable examples. You need a C compiler and
GNU Make 4.3 or later; the default flags target GCC. Unicode data is included.
See [building and installation](docs/building.md) for compiler options, Nix and
using the library in your own project.

Here is a growable vector of integers:

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

  if (DS_FAILED(ds_vec_init(&numbers, sizeof(int), NULL)))
    return 1;
  if (DS_FAILED(ds_vec_append(&numbers, values, 3))) {
    ds_vec_destroy(&numbers);
    return 1;
  }

  ds_vec_sort(&numbers, compare_ints, NULL);
  for (i = 0; i < ds_vec_size(&numbers); ++i)
    printf("%d\n", *(const int *)ds_vec_at(&numbers, i));

  ds_vec_destroy(&numbers);
  return 0;
}
```

The vector owns its storage and copies each integer into it. Passing `NULL` as
the context uses the default allocator. You can instead provide an explicit
context, or use `ds_vec_init_buffer` to work in a fixed caller-owned buffer.

## Choose your headers

| Include | Use it for |
|---|---|
| `<dsa.h>` | The whole library |
| `<dsad.h>` | Data structures and shared services |
| `<dsaa.h>` | Algorithms and shared services |
| `<dsa/structures/vec.h>` | An individual module |

All use the same `libdsa` library and `ds_` function/type prefix. Individual
headers include their dependencies, so you do not have to assemble include
orders yourself.

## How the APIs fit together

**Memory and ownership are explicit.** Intrusive containers hold links in your
records. Value containers copy elements; pointer containers document whether
values are borrowed or owned. Allocator contexts and copy/destroy callbacks let
you choose the lifetime policy. Allocating containers take a context directly or
through their configuration and pass it to their nested storage. Read [ownership](docs/ownership.md) before
sharing values between containers.

**Composition includes contracts.** Map, ordered-map, priority-queue,
rank/select and storage interfaces let you choose backends. Their cost,
allocation and cursor guarantees matter to the structures built on them. See
[component contracts](docs/contracts.md) and [mutation and lifetimes](docs/container-lifetimes.md).

**Errors carry information.** `ds_status_t` reports an operation's outcome.
`ds_result_t` carries a domain success or error, a payload and an ordered log of
diagnostics. i18n renders messages at the presentation boundary, without changing
the Result's meaning. Sequential pipelines and independent validation have
distinct composition operations; diagnostic storage is selectable. See [Results and localization](docs/result-i18n.md).

**Versions and history are available when you need them.** Persistent containers
share immutable storage between versions. The [history layer](docs/history.md)
adds branches, checkpoints, operation replay and retroactive edits through
adapters.

## Find your way around

| Directory | Contents |
|---|---|
| `include/` | Public headers and umbrellas |
| `src/core/` | Allocation, statuses, Results, diagnostics and shared services |
| `src/structures/` | Data structure implementations |
| `src/algorithms/` | Algorithm implementations |
| `examples/` | Runnable programs showing how to use the APIs |
| `docs/` | Guides, contracts and module reference |
| `tools/` | Unicode table generator |
| `data/` | Unicode source data |
| `build/` | Generated files and compiled output |

Start with the [API guide](docs/api-overview.md), browse the
[examples](examples/README.md), or open the [documentation index](docs/README.md).
Each public header describes its module's operations, preconditions and limits.

## License

[Do Whatever License](LICENSE). The repository's [code of conduct](CODE_OF_CONDUCT)
applies to contributions. Bundled Unicode data carries the terms named in its
file headers.
