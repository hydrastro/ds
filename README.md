# Data Structures

A comprehensive collection of intrusive, thread-safe data structures implemented in C89.  

## Features

Available data structures:
- AVL Tree
- Binary Search Tree
- Binary Tree
- Double-ended Queue
- Doubly Linked List
- Hash Table
- Heap
- Singly Linked List
- Queue
- Red-Black Tree
- Stack
- Trie

## Installation
### With Nix
```shell
nix build
```

### With Make
```shell
make
make test
make install
```

`make test` builds and runs every test under `test/`. It uses the small
Unicode fixture set in `test/ucd/` so the test suite is self-contained and does
not need to download the full UCD.

## Usage
Include the central header ds.h in your C code:
```C
#include <ds.h> // thread unsafe
#include <ds_safe.h> // thread safe
```

To compile your project with the library:
```shell
gcc -o example.c -lds
```

For specific data structure usage, check out the tests and the headers.

## Contributing

Contributions are welcome!  
Before submitting a pull request please:
- format your code with `clang-format`
- test your code with `valgrind`


## Test helpers

- `make test` builds and runs the normal test suite.
- `make sanitize` runs the test suite with AddressSanitizer and UndefinedBehaviorSanitizer.
- `make test-safe` builds the thread-safe library variant and runs the safe smoke test.
- `make valgrind` runs tests, the safe smoke test, and examples under Valgrind when Valgrind is installed.
- `make examples` builds the example programs.
