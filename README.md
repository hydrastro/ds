# Data Structures

A comprehensive collection of intrusive data structures implemented in C.  

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
make install
```

## Usage
Include the central header ds.h in your C code:
```C
#include <ds.h>
```

To compile your project with the library:
```shell
gcc -o example.c -lds
```

For specific data structure usage, check out `test/*`.

## Contributing

Contributions are welcome!  
Before submitting a pull request please:
- format your code with `clang-format`
- test your code with `valgrind`
