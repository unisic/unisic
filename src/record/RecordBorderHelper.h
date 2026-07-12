#pragma once

// `unisic --record-border-helper …` — separate-process record-region frame for
// Wayland compositors with neither layer-shell nor KWin (GNOME/mutter). The
// helper boots on the xcb platform (XWayland): an override-redirect X11 window
// is the only surface mutter stacks above every application window, and
// Qt::WindowTransparentForInput gives it an empty input shape (click-through).
// It never touches the single-instance socket — it is a child of the running
// instance, not a peer — and quits on stdin EOF, so a crashed parent can never
// leave an orphaned frame on screen.
int runRecordBorderHelper(int argc, char *argv[]);
