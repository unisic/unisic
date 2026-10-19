// Windows build of KeyCapture: there is no libinput. InputPermission_win.cpp
// answers NoPermission, so the recorder never starts this; start() stays a
// no-op for any caller that does, and no keyEvent is ever emitted.
#include "KeyCapture.h"

KeyCapture::KeyCapture(QObject *parent) : QObject(parent) {}

KeyCapture::~KeyCapture() = default;

void KeyCapture::start() {}

void KeyCapture::stop() {}

void KeyCapture::run() {}
