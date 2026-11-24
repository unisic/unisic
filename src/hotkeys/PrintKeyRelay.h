#pragma once
#include <QAbstractNativeEventFilter>

// Hands the Settings shortcut recorder a Print Screen it would never see.
//
// Windows posts no key-down for Print Screen (the system keeps that half for
// its own snapshot), only the key-up, and Qt drops a key-up whose key-down it
// never saw. So on Windows a QML recorder got no event at all for any Print
// chord, and every default capture key there is one: the field sat on "Press
// shortcut..." and nothing was saved. While a recorder listens
// (AppContext::setShortcutRecording), the Windows build (PrintKeyRelay_win.cpp)
// watches for that key-up and posts Print, with the modifiers held, to the
// focused window as an ordinary key press. On Linux the press arrives like
// any other key, so PrintKeyRelay.cpp does nothing.
class PrintKeyRelay : public QAbstractNativeEventFilter
{
public:
    ~PrintKeyRelay() override;

    void setActive(bool active);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    bool m_active = false;
};
