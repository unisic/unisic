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
}:

# Unisic — Wayland screenshot & screen-recording tool. Plain qt6 CMake build.
# The interesting bits: ffmpeg + wl-clipboard are shelled out at RUNTIME, so
# they are put on the wrapped app's PATH (not buildInputs); the QtTest suite is
# compositor-free and runs headless under QT_QPA_PLATFORM=offscreen.
stdenv.mkDerivation (finalAttrs: {
  pname = "unisic";
  version = "0.7.4";

  # cleanSource here resolves to the flake's store copy (git-tracked files only),
  # so build/ dist/ and .git never enter the derivation.
  src = lib.cleanSource ../.;

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
    kdePackages.kguiaddons # KF6::GuiAddons — KSystemClipboard (Klipper history)
    pipewire # HAVE_PIPEWIRE — GIF/video recording
    (tesseract.override { enableLanguages = [ "eng" "pol" ]; }) # HAVE_TESSERACT (OCR)
    leptonica
    zxing-cpp # HAVE_ZXING — QR/barcode decode inside the OCR path
    libinput # HAVE_LIBINPUT — click/keystroke capture (needs the `input` group)
    udev
    wayland # wayland-client
  ];

  cmakeFlags = [
    (lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
    (lib.cmakeBool "UNISIC_DEV_BUILD" false) # stable app id, not unisic-dev
    (lib.cmakeBool "BUILD_TESTING" true)
  ];

  # Recording pipes frames to ffmpeg; the clipboard is mirrored through wl-copy.
  # Both are looked up on PATH at runtime — wrap them in so capture/record work.
  qtWrapperArgs = [
    "--prefix PATH : ${lib.makeBinPath [ ffmpeg wl-clipboard ]}"
  ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
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
