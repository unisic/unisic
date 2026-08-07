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
  libarchive,
  curl,
  libinput,
  udev,
  zxing-cpp,
  wayland,
  wayland-scanner,
  xorg,
  ffmpeg,
  wl-clipboard,
  zip,
  grim,
  # The unisic-kit checkout, passed by the flake. Only used when the source
  # copy has no submodule content of its own (see postUnpack).
  unisicKitSrc ? null,
}:

# Unisic - Wayland screenshot & screen-recording tool. Plain qt6 CMake build.
# The interesting bits: ffmpeg, wl-clipboard, pw-play, curl, zip and grim are
# shelled out at RUNTIME, so they are put on the wrapped app's PATH (not
# buildInputs); the QtTest suite is compositor-free and runs headless under
# QT_QPA_PLATFORM=offscreen.
#
# Unisic has no optional dependencies: every compile-time gate is a hard build
# requirement and CMake stops at configure time naming the missing package. So
# buildInputs below is not a wish list - a package dropped from it turns this
# derivation from "builds without the feature" into "does not build", which is
# the whole point of the rule. Three gates were silently OFF in every nix build
# before that landed: HAVE_X11 (only libXfixes was missing, and
# pkg_check_modules is all-or-nothing, so X11-session recording vanished),
# HAVE_KWIN_SCREENCAST (no plasma-wayland-protocols, so KDE users got the
# portal share dialog instead of the native path) and HAVE_X11_HOTKEYS (it
# happened to work, purely because qtbase propagates libX11 and libxcb - not
# something to keep resting on).
let
  # ONE derivation, referenced twice on purpose: buildInputs links against it
  # and TESSDATA_PREFIX below points into it. Two separate `tesseract.override`
  # calls would be two store paths, and the wrapper would then advertise
  # traineddata files to a libtesseract that was linked against the other one.
  # eng + pol are the OCR defaults; osd is what the script auto-detection that
  # Settings enables BY DEFAULT loads. The other four UI languages are
  # deliberately NOT here: they would add tens of MB of fixed-output downloads
  # to every closure for a language nobody selected.
  tesseractWithData = tesseract.override {
    enableLanguages = [
      "eng"
      "pol"
      "osd"
    ];
  };
in
stdenv.mkDerivation (finalAttrs: {
  pname = "unisic";
  version = "0.8.4";

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
    # HAVE_KWIN_SCREENCAST codegen: qt6_generate_wayland_protocol_client_sources
    # shells out to upstream `wayland-scanner` as well as Qt's own scanner.
    # qtbase propagates it when built withWayland, but that is a transitive
    # accident and this gate is now a hard requirement, so name it.
    wayland-scanner
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative # Quick, Qml, QuickControls2
    qt6.qtsvg
    qt6.qtwayland
    kdePackages.layer-shell-qt # LayerShellQt (notification/preview surfaces)
    kdePackages.kguiaddons # KF6::GuiAddons - KSystemClipboard (Klipper history)
    # HAVE_KWIN_SCREENCAST - carries zkde-screencast-unstable-v1.xml, which the
    # kit generates its KWin-native recording client from (no portal share
    # dialog on KDE). There is no top-level alias; kdePackages is the only path.
    kdePackages.plasma-wayland-protocols
    pipewire # HAVE_PIPEWIRE - GIF/video recording
    tesseractWithData # HAVE_TESSERACT (OCR) - see the let block above
    leptonica
    # nixpkgs' tesseract.pc puts `-larchive -lcurl` in the PUBLIC Libs:, so
    # pkg_check_modules(TESSERACT) drops both onto our link line and the binary
    # ends up NEEDING them. Without them here they get no RPATH entry: on a
    # distro with /usr/lib the loader papers over it, on NixOS the app dies with
    # "libarchive.so.13: cannot open shared object file".
    libarchive
    curl
    zxing-cpp # HAVE_ZXING - QR/barcode decode inside the OCR path
    libinput # HAVE_LIBINPUT - click/keystroke capture (needs the `input` group)
    udev # libudev.pc, the second half of the HAVE_LIBINPUT check
    wayland # wayland-client
    # HAVE_X11 (x11 + xext + xfixes, XShm/XFixes frame grabbing on an X11
    # session) and HAVE_X11_HOTKEYS (x11 + xcb, XGrabKey where KGlobalAccel is
    # absent). qtbase propagates libX11, libXext and libxcb, but NOT libXfixes,
    # and pkg_check_modules takes all three modules in one call - so the single
    # missing .pc used to report the whole gate as not found. All four are
    # listed explicitly rather than left to Qt's closure.
    xorg.libX11
    xorg.libXext
    xorg.libXfixes
    xorg.libxcb
  ];

  cmakeFlags = [
    (lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
    (lib.cmakeBool "UNISIC_DEV_BUILD" false) # stable app id, not unisic-dev
    (lib.cmakeBool "BUILD_TESTING" true)
  ];

  # Everything Unisic shells out to, looked up on PATH at runtime with
  # QStandardPaths::findExecutable. None of it is visible to the linker, so
  # none of it fails the build - each one just turns a working feature into an
  # error toast, which is the same class of silent hole the compile gates now
  # refuse to produce:
  #   ffmpeg        (+ ffprobe, same package) recording, GIF conversion, trim
  #   wl-clipboard  wl-copy / wl-paste
  #   pipewire      pw-play, the first choice for the capture/record sound cues
  #   curl          the only transport for ftp/ftps/sftp upload destinations
  #   zip           Info-ZIP, for the ZIP export and the diagnostics bundle
  #   grim          the wlroots screenshot fallback (sway, Hyprland). Note it
  #                 also flips the default cursor-capture setting on non-KDE,
  #                 non-GNOME sessions, because `grim -c` does capture it.
  # TESSDATA_PREFIX is --set-default, not --set, so a user pointing at their
  # own traineddata collection still wins. It names the tessdata dir itself:
  # src/ocr/OcrEngine.cpp accepts both that and its parent, and tesseract 5.x
  # wants the directory.
  qtWrapperArgs = [
    "--prefix PATH : ${
      lib.makeBinPath [
        ffmpeg
        wl-clipboard
        pipewire
        curl
        zip
        grim
      ]
    }"
    "--set-default TESSDATA_PREFIX ${tesseractWithData}/share/tessdata"
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
