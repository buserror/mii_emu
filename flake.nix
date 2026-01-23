{
  description = "MII Apple //e Emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "mii-emu";
          version = "dev";
          src = self;

          nativeBuildInputs = [
            pkgs.pkg-config
            pkgs.tinycc
          ];

          buildInputs = [
            pkgs.alsa-lib
            pkgs.libGL
            pkgs.libGLU
            pkgs.xorg.libX11
            pkgs.pixman
          ];

          makeFlags = [
            "CC=cc"
            "SHELL=${pkgs.bash}/bin/bash"
          ];

          installPhase = ''
            runHook preInstall
            make SHELL=${pkgs.bash}/bin/bash DESTDIR=$out install
            runHook postInstall
          '';
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.pkg-config
            pkgs.tinycc
          ];

          buildInputs = [
            pkgs.alsa-lib
            pkgs.libGL
            pkgs.libGLU
            pkgs.xorg.libX11
            pkgs.pixman
          ];
        };
      }
    );
}
