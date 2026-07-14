#pragma once

// `unisic --notification-helper …` — separate-process, styled capture card for
// Wayland compositors with neither layer-shell nor KWin (GNOME/mutter). Like
// RecordBorderHelper it boots on the xcb platform (XWayland) and shows an
// override-redirect X11 window — the only surface mutter stacks above every
// application window — but this one ACCEPTS clicks (not input-transparent): it
// hosts the FULL NotificationPopup.qml (every style, every action button), the
// same card the layer-shell path draws. `App`/`notif` are lightweight stubs; the
// action buttons print one-word tokens on stdout for the parent to route onto the
// real CaptureNotification, and the parent pushes url/upload state back over
// stdin ("state:…"). It self-dismisses after a timeout, never touches the
// single-instance socket, and quits on stdin EOF so a crashed parent can never
// leave an orphaned card on screen.
int runNotificationHelper(int argc, char *argv[]);
