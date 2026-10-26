#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// What the first-run system check expects this platform's install to ship.
// RuntimeTools.cpp lists the command-line tools the Linux packages depend on;
// RuntimeTools_win.cpp lists the one the Windows build still shells out to, so
// a Windows user is never told to install wl-clipboard or PipeWire.
namespace RuntimeTools {

struct Group {
    QString label;           // shown as is, a tool name needs no translation
    QStringList executables; // every one must be on PATH for the group to pass
};

QList<Group> required();

// True where the build plays recordings back through QtMultimedia. The Windows
// build records nothing, so it has no recording to play.
bool needsVideoPlayback();

} // namespace RuntimeTools
