// Windows build of ClickCapture: there is no libinput. InputPermission_win.cpp
// answers NoPermission, so the recorder never starts this; start() stays a
// no-op for any caller that does, and no buttonEvent is ever emitted.
#include "ClickCapture.h"

ClickCapture::ClickCapture(QObject *parent) : QObject(parent) {}

ClickCapture::~ClickCapture() = default;

void ClickCapture::start() {}

void ClickCapture::stop() {}

void ClickCapture::run() {}
