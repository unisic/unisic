{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  qt6,
  kdePackages,
  pipewire,
  tesseract,
  leptonica,
  libinput,
  udev,
  zxing-cpp,
  wayland,
  ffmpeg,
  wl-clipboard,
  # The unisic-kit checkout, passed by the flake. Only used when the source
  # copy has no submodule content of its own (see postUnpack).
  unisicKitSrc ? null,
}:

# Unisic - Wayland screenshot & screen-recording tool. Plain qt6 CMake build.
# The interesting bits: ffmpeg + wl-clipboard are shelled out at RUNTIME, so
# they are put on the wrapped app's PATH (not buildInputs); the QtTest suite is
# compositor-free and runs headless under QT_QPA_PLATFORM=offscreen.
stdenv.mkDerivation (finalAttrs: {
  pname = "unisic";
  version = "0.8";

  # cleanSource here resolves to the flake's store copy (git-tracked files only),
  # so build/ dist/ and .git never enter the derivation. That copy carries the
  # external/unisic-kit submodule only when the flake ref asked for it
  # (?submodules=1); otherwise postUnpack fills the directory from the kit input.
  src = lib.cleanSource ../.;

  # A GitHub flake ref without ?submodules=1 leaves external/unisic-kit empty,
  # and CMakeLists.txt add_subdirectory()s it. Drop the kit input in when the
  # submodule did not come along, so `nix build github:unisic/unisic` works
  # unflagged; a real submodule checkout is left untouched.
  postUnpack = lib.optionalString (unisicKitSrc != null) ''
    kit="$sourceRoot/external/unisic-kit"
    if [ ! -e "$kit/CMakeLists.txt" ]; then
      rm -rf "$kit"
      mkdir -p "$kit"
      cp -r --no-preserve=mode,ownership ${unisicKitSrc}/. "$kit/"
    fi
  '';

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
    qt6.qttools # lrelease/lupdate for the baked-in translations (LinguistTools)
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative # Quick, Qml, QuickControls2
    qt6.qtsvg
    qt6.qtwayland
    kdePackages.layer-shell-qt # LayerShellQt (notification/preview surfaces)
    kdePackages.kguiaddons # KF6::GuiAddons - KSystemClipboard (Klipper history)
    pipewire # HAVE_PIPEWIRE - GIF/video recording
    (tesseract.override { enableLanguages = [ "eng" "pol" ]; }) # HAVE_TESSERACT (OCR)
    leptonica
    zxing-cpp # HAVE_ZXING - QR/barcode decode inside the OCR path
    libinput # HAVE_LIBINPUT - click/keystroke capture (needs the `input` group)
    udev
    wayland # wayland-client
  ];

  cmakeFlags = [
    (lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
    (lib.cmakeBool "UNISIC_DEV_BUILD" false) # stable app id, not unisic-dev
    (lib.cmakeBool "BUILD_TESTING" true)
  ];

  # Recording pipes frames to ffmpeg; the clipboard is mirrored through wl-copy.
  # Both are looked up on PATH at runtime - wrap them in so capture/record work.
  qtWrapperArgs = [
    "--prefix PATH : ${lib.makeBinPath [ ffmpeg wl-clipboard ]}"
  ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    # The build sandbox has HOME=/homeless-shelter (read-only); the settings
    # and history tests persist into ~/.config / the data dir, so give them a
    # writable HOME. offscreen: the tests use QGuiApplication with no display.
    export HOME=$(mktemp -d)
    export XDG_RUNTIME_DIR=$(mktemp -d)
    QT_QPA_PLATFORM=offscreen ctest --output-on-failure
    runHook postCheck
  '';

  enableParallelBuilding = true;

  meta = {
    description = "Capture, annotate, record and share your screen on Linux Wayland";
    homepage = "https://github.com/unisic/unisic";
    license = lib.licenses.gpl3Plus;
    mainProgram = "unisic";
    platforms = lib.platforms.linux;
  };
})
