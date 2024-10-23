{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation rec {
  pname = "ds";
  version = "0.0.0";

  src = ./.;

  description = "ds";
  #license
  #homepage

  buildInputs = [ pkgs.stdenv.cc ];

  buildPhase = ''
    mkdir -p $out/lib
    mkdir -p $out/include

    gcc -c lib/avl.c -o avl.o
    gcc -c lib/bst.c -o bst.o
    gcc -c lib/btree.c -o btree.o
    gcc -c lib/deque.c -o deque.o
    gcc -c lib/doubly_linked_list.c -o doubly_linked_list.o
    gcc -c lib/hash_table.c -o hash_table.o
    gcc -c lib/heap.c -o heap.o
    gcc -c lib/linked_list.c -o linked_list.o
    gcc -c lib/queue.c -o queue.o
    gcc -c lib/rbt.c -o rbt.o
    gcc -c lib/stack.c -o stack.o
    gcc -c lib/trie.c -o trie.o

    ar rcs $out/lib/libds.a avl.o bst.o btree.o deque.o doubly_linked_list.o hash_table.o heap.o linked_list.o queue.o rbt.o stack.o trie.o

    gcc -shared -o $out/lib/libds.so avl.o bst.o btree.o deque.o doubly_linked_list.o hash_table.o heap.o linked_list.o queue.o rbt.o stack.o trie.o
  '';

  installPhase = ''
    cp lib/*.h $out/include/
    cp ds.h $out/include/
  '';

  meta = with pkgs.lib; {
  #  description
  #  license
     platforms = platforms.unix;
  };
}
