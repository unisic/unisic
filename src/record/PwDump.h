#pragma once
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVariantList>

// The PipeWire graph as `pw-dump` prints it, and the two lists Unisic reads
// out of it: the applications playing audio right now and the capture inputs.
// Both the Settings/Record pickers and the recorder go through here, so what
// the user ticks and what gets recorded are matched by the same rule.
namespace PwDump {

// Runs pw-dump (bounded to 2.5 s, killed past that) and returns its node
// array, or an empty one when it is missing or fails. Blocking, touches no
// GUI state: safe on a worker thread. Measured at ~10 ms on a desktop graph.
QJsonArray nodes();

// One playback stream (media.class Stream/Output/Audio).
struct AppStream {
    QString serial;  // object.serial, what pw-record --target takes
    QString app;     // the stable key a selection is stored by
    QString label;   // what the user reads
    QString icon;    // freedesktop icon name, may be empty
};

// `app` is application.process.binary, then application.name, then
// node.name: the binary survives the app restarting, and a serial does not -
// a stored serial outlived its stream on every relaunch, and pw-record given
// a target that no longer exists falls back to the default SOURCE, which
// recorded the microphone under "Application audio only".
QList<AppStream> appStreams(const QJsonArray &nodes);

// The picker model: one entry per application, however many streams it has
// open (a browser opens one per tab), as {id, label, icon, streams} maps.
QVariantList applications(const QList<AppStream> &streams);

// Capture-capable inputs as {id, label} maps; id is node.name.
QVariantList inputDevices(const QJsonArray &nodes);

} // namespace PwDump
