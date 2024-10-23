{
  description = "ds";

  outputs = { self, nixpkgs }: {
    packages = {
      # Define a package for the "default" system architecture (x86_64-linux in this case)
      default = self.packages.${builtins.currentSystem};

      # Package for different system architectures
      x86_64-linux = let
        pkgs = nixpkgs.legacyPackages.x86_64-linux;
      in
        pkgs.stdenv.mkDerivation {
          pname = "ds";
          version = "1.0.0";

          src = ./.;

          # List of the source files to compile
          sources = [ "avl.c" "bst.c" "btree.c" "deque.c" "doubly_linked_list.c" "hash_table.c" "heap.c" "linked_list.c" "queue.c" "rbt.c" "stack.c" "trie.o" ];

          # Compiler and linker flags
          buildPhase = ''
            gcc -c avl.c bst.c btree.c deque.c doubly_linked_list.c hash_table.c heap.c linked_list.c queue.c rbt.c stack.c trie.o
            ar rcs libds.a *.o
          '';

          installPhase = ''
            mkdir -p $out/lib
            mkdir -p $out/include
            mkdir -p $out/include/lib/
            cp ds.h $out/include
            cp lib/*.h $out/include/lib
            cp libds.a $out/lib
          '';

          meta = with pkgs.lib; {
            description = "ds";
            #license = licenses.mit;
            #maintainers = [ maintainers.yourself ];  # Replace 'yourself' with your nixpkgs username
          };
        };
    };
  };
}

