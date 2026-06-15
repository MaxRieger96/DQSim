{
  description = "DQSim dev environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems =
        f:
        builtins.listToAttrs (
          map (system: {
            name = system;
            value = f system;
          }) systems
        );
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            packages =
              (with pkgs; [
                zsh
                gcc15
                gnumake
                cmake
                ninja
                boost
                glpk

                # for trino experiments
                docker
                docker-compose
                trino-cli

                # for LSPs:
                clang
                clang-tools
                nil
                nixd
              ])
              # binutils is a sane “build-essential-ish” add-on on Linux,
              # but is often problematic/unnecessary on macOS (Darwin toolchain differs).
              ++ pkgs.lib.optionals pkgs.stdenv.isLinux (with pkgs; [ binutils ]);
            shellHook = ''
              export CC=${pkgs.gcc15}/bin/gcc
              export CXX=${pkgs.gcc15}/bin/g++
              export NIX_ENFORCE_NO_NATIVE=0
            '';
          };
        }
      );
    };
}
