# Building and embedding

You need a C compiler, GNU Make 4.3 or later, and the C math library. The default
warning flags target GCC. The library compiles in C89 mode and uses compiler
support for `<stdbool.h>` and copying `va_list`. Examples compile in C99 mode.
POSIX file helpers use POSIX system calls; their headers document platform
behavior. Unicode 17.0.0 input data is bundled, so building needs no download.

```sh
make -j4
make examples
./build/examples/getting_started
```

`make` produces `build/libdsa.a` and `build/libdsa.so`. Objects, dependency files,
generated Unicode tables and example executables all live under `build/`.
`make clean` removes that directory. You can choose a separate output directory
with `BUILD_DIR=build/debug`; use the same setting for each command in that build.

## Include only what you need

| Header | Includes |
|---|---|
| `<dsa.h>` | All public modules |
| `<dsad.h>` | Data structures and shared services |
| `<dsaa.h>` | Algorithms and shared services |
| `<dsa/structures/vec.h>` | One container and its dependencies |
| `<dsa/algorithms/sort.h>` | One algorithm family and its dependencies |
| `<dsa/core/result.h>` | One shared service and its dependencies |

The umbrellas are conveniences. They do not change allocation, dispatch or
linkage. Each module includes its own dependencies; an algorithm may naturally
depend on a structure. All three umbrellas link to the same library. The C
function and type prefix is `ds_`.

From a source checkout:

```sh
cc -std=c99 -Iinclude examples/getting_started.c build/libdsa.a -lm -o getting_started
./getting_started
```

This links the archive explicitly. A static linker pulls in the object files
needed by your program; including an umbrella does not force the whole archive
into the executable.

## Install

```sh
make install PREFIX="$HOME/.local"
```

Headers go under `PREFIX/include`, libraries under `PREFIX/lib`, and `dsa.pc`
under `PREFIX/lib/pkgconfig`. `DESTDIR` stages an installation for packaging.
`INCLUDEDIR`, `LIBDIR` and `PKGCONFIGDIR` can also be set individually.

With that pkg-config directory on `PKG_CONFIG_PATH`:

```sh
cc -std=c99 $(pkg-config --cflags dsa) my_program.c \
  $(pkg-config --libs dsa) -o my_program
```

For static linking, use the installed `libdsa.a` explicitly and add `-lm`, or use
`pkg-config --static --libs dsa` with your compiler's static-link options.
`pkg-config --static` supplies private dependencies; it does not select the
archive by itself. A shared build also needs the installation's library directory
on the runtime loader's search path.

`make uninstall PREFIX=...` removes the files installed by this layout. Use the
same path settings and `DESTDIR` as for installation.

## Build options

`CC`, `AR`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, `LDLIBS`, `EXAMPLE_CFLAGS` and
`GEN_CFLAGS` are configurable. `GEN_CFLAGS` controls the Unicode generator.
The robust-predicate module always adds `-ffp-contract=off`; its error bounds
require separate floating-point operations. Do not enable fast-math for it.

```sh
make BUILD_DIR=build/debug CFLAGS='-std=c89 -Wall -Wextra -O0 -g -fPIC'
```

Use a fresh output directory when changing compiler flags, since Make does not
track command-line variable changes as file dependencies.

The optional development targets compile every installed header on its own
(`check-headers`), compile all library sources at `-O0` or `-O3`
(`check-noinline`, `check-o3`), format handwritten sources (`format`,
`format-check`, using clang-format), and run static analysis (`lint`, using
cppcheck). These tools are not needed to build or use the library.

The Nix flake provides `nix build` and `nix develop` on x86-64 Linux.
