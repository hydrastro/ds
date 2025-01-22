{
  description = "ds";

  inputs = { nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable"; };

  outputs = { self, nixpkgs }: {
    packages = nixpkgs.lib.genAttrs [ "x86_64-linux" ] (system:
      let pkgs = import nixpkgs { inherit system; };
      in rec {
        ds = pkgs.stdenv.mkDerivation {
          pname = "ds";
          version = "0.0.0";

          src = ./.;

          buildInputs = [ pkgs.stdenv.cc ];

          buildPhase = ''
            make PREFIX=$out
          '';

          installPhase = ''
            make install PREFIX=$out
          '';

          meta = with pkgs.lib; { description = "ds"; };
        };
      });

    defaultPackage = { x86_64-linux = self.packages.x86_64-linux.ds; };
  };
}
