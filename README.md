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

## Compilation

```shell
gcc -c lib/*.c
```

## Installation
Building the Static Library

Archive the object files into a static library:

```shell
ar rcs build/lib/libds.a *.o
gcc -shared -o build/lib/libds.so *.o
```

## Usage
Include the central header ds.h in your C code:
```C
#include <ds.h>
```

To compile your project with the library:
```shell
gcc -o my_program my_program.c -lds
```

For specific data structure usage, check out `test/*`

##Contributing

Contributions are welcome!
