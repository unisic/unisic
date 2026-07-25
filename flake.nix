{
  description = "Unisic — Wayland screenshot & screen-recording tool";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in
    {
      # nix build '.?submodules=1#unisic'   |   nix run '.?submodules=1#unisic' -- --region
      # ?submodules=1 is required: external/unisic-kit is a submodule, and a
      # plain flake ref copies only the parent repo's own tracked files.
      packages = forAllSystems (pkgs: {
        unisic = pkgs.callPackage ./nix/package.nix { };
        default = self.packages.${pkgs.system}.unisic;
      });

      # nix develop   ->   cmake -B build -G Ninja && cmake --build build
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.system}.unisic ];
          packages = with pkgs; [ ffmpeg wl-clipboard qt6.qttools ];
        };
      });
    };
}
