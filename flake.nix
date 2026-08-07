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
    # THE SAME KIT COMMIT IS PINNED IN THREE PLACES AND THEY MOVE TOGETHER, IN
    # ONE COMMIT:
    #   1. the git submodule pointer  - `git submodule status external/unisic-kit`
    #   2. the rev in the url below
    #   3. flake.lock's nodes."unisic-kit".locked.rev (and its narHash), which
    #      only `nix flake update unisic-kit` rewrites
    # Nothing derives one from another. Bumping 1 and 2 while forgetting 3
    # leaves every `nix build` fetching the OLD kit while the submodule
    # checkout builds the new one, and the two disagree silently: the flake
    # input is only consulted when the submodule content is absent (see
    # nix/package.nix postUnpack), so the mismatch surfaces as "works from a
    # clone, broken from github:unisic/unisic" and nothing else.
    # This matters more than it used to: the kit's gates (HAVE_X11,
    # HAVE_X11_HOTKEYS, HAVE_KWIN_SCREENCAST) are hard build requirements now,
    # so a stale kit is a configure error rather than a quiet feature loss.
    unisic-kit = {
      url = "github:unisic/unisic-kit/a7eeb9406082bc7b26d0a7d23f38c2539313a6b7";
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
          # inputsFrom brings every library the gates need; these are the
          # helpers the app shells out to at runtime, which the packaged build
          # gets through qtWrapperArgs and a `cmake --build` binary does not.
          # Same list as nix/package.nix - a dev shell that cannot record,
          # copy, play a cue, upload, zip or grim is a dev shell where half the
          # features look broken for no reason.
          packages = with pkgs; [
            ffmpeg
            wl-clipboard
            pipewire
            curl
            zip
            grim
            qt6.qttools
          ];
        };
      });
    };
}
