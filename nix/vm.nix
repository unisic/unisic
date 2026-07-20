{ pkgs, lib, unisic, ... }:

# A throwaway NixOS VM for hand-testing Unisic on a real KWin Wayland session.
# Plasma 6 + SDDM (Wayland), PipeWire, xdg-desktop-portal-kde, autologin.
# Built and booted by `scripts/vm-test.sh nix` (or `nix run .#vm`).
{
  system.stateVersion = "24.11";
  networking.hostName = "unisic-vm";

  # Plasma 6 on Wayland, autologin straight to the desktop.
  services.displayManager = {
    sddm.enable = true;
    sddm.wayland.enable = true;
    defaultSession = "plasma";
    autoLogin.enable = true;
    autoLogin.user = "tester";
  };
  services.desktopManager.plasma6.enable = true;

  users.users.tester = {
    isNormalUser = true;
    password = "test";
    description = "Unisic tester";
    # `input`  -> libinput click/keystroke capture (badge in recordings)
    # `video`  -> GPU nodes for KWin
    extraGroups = [ "wheel" "input" "video" ];
  };

  # Recording backend + screencast/screenshot portal (KWin ScreenShot2 lives in
  # the Plasma session; the portal is the cross-desktop fallback).
  security.rtkit.enable = true;
  services.pipewire = {
    enable = true;
    alsa.enable = true;
    pulse.enable = true;
  };
  xdg.portal = {
    enable = true;
    extraPortals = [ pkgs.kdePackages.xdg-desktop-portal-kde ];
    config.common.default = "*";
  };

  hardware.graphics.enable = true;

  environment.systemPackages = [
    unisic
    pkgs.ffmpeg
    pkgs.wl-clipboard
    pkgs.kdePackages.konsole
  ];

  # VM resources. The host /nix/store is mounted read-only, so the qcow2 only
  # holds mutable state — a few GB is plenty.
  virtualisation = {
    memorySize = 4096;
    cores = 4;
    diskSize = 8192;

    # Software rendering (llvmpipe) is the portable default — KWin Wayland runs
    # fine on it, just not fast. For GPU acceleration on a host with virgl,
    # UNCOMMENT the next block (needs qemu built with OpenGL + a GL-capable host):
    # qemu.options = [
    #   "-vga none"
    #   "-device virtio-vga-gl"
    #   "-display gtk,gl=on,grab-on-hover=on,zoom-to-fit=on"
    # ];
  };
}
