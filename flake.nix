{
  description = "Data structures and algorithms in C";

  inputs = { nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable"; };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      packages = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in rec {
          dsa = pkgs.stdenv.mkDerivation {
            pname = "dsa";
            version = "0.0.0";
            src = ./.;

            nativeBuildInputs = [ pkgs.gnumake ];

            buildPhase = ''
              make
            '';

            installPhase = ''
              make install PREFIX=$out
            '';

            meta = with pkgs.lib; {
              description = "Data structures and algorithms in C";
              license = {
                shortName = "dw";
                fullName = "Do Whatever License";
                url = "https://github.com/hydrastro/dw";
                free = true;
                redistributable = true;
              };
              platforms = platforms.unix;
            };
          };

          default = dsa;
        });

      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            packages = [ pkgs.gcc pkgs.gnumake pkgs.gdb pkgs.valgrind ];
          };
        });

    };
}
