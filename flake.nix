{
  description = "Unisic - Wayland screenshot & screen-recording tool";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # The shared design/foundation kit lives in its own repository and is a git
    # submodule here (external/unisic-kit). A flake ref fetched from GitHub
    # copies only the parent repo's own tracked files, so that directory would
    # arrive empty and the CMake configure would fail on add_subdirectory.
    # Carrying the kit as a flake input instead makes `nix build github:...`
    # work with no extra flags; `?submodules=1` still works and wins, since the
    # copy below only fills the directory when the submodule is absent.
    # KEEP THE REV IN SYNC with `git submodule status external/unisic-kit`.
    unisic-kit = {
      url = "github:unisic/unisic-kit/e52a2f59f6b25df902f1ad36793c68c777c776a6";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, unisic-kit }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in
    {
      # nix build '.#unisic'   |   nix run '.#unisic' -- --region
      packages = forAllSystems (pkgs: {
        unisic = pkgs.callPackage ./nix/package.nix { unisicKitSrc = unisic-kit; };
        default = self.packages.${pkgs.stdenv.hostPlatform.system}.unisic;
      });

      # nix develop   ->   cmake -B build -G Ninja && cmake --build build
      # A dev shell builds from the working tree, where the submodule is a real
      # checkout: clone with --recurse-submodules (or run `git submodule update
      # --init`) before configuring, the kit input above does not apply here.
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.unisic ];
          packages = with pkgs; [ ffmpeg wl-clipboard qt6.qttools ];
        };
      });
    };
}
