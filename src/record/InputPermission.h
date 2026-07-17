#pragma once
#include <QString>

// Cheap, on-demand probe of whether libinput can observe pointer devices for
// click capture. It is deliberately NOT run at startup: the recorder calls
// probe() the moment it needs clicks, and the Settings UI calls it to decide
// whether to show the fix hint. This layer returns a raw enum + the raw command
// string; user-visible wrapping/tr() happens at the UI layer, not here.
class InputPermission
{
public:
    enum Status {
        Available,     // opened a libinput udev context and saw >=1 pointer device
        NoPermission,  // context/seat failed, or zero devices (user not in `input`)
        NotBuilt,      // compiled without libinput support
    };

    // Create a throwaway libinput udev context, assign seat0, dispatch the initial
    // device-enumeration burst and count the pointer-capable devices. Zero devices
    // or any failure → NoPermission. Without HAVE_LIBINPUT → NotBuilt. The context
    // is torn down before returning; no fds or threads survive the call.
    static Status probe();

    // The command that grants access (adds the user to the `input` group). Plain,
    // copyable, NOT translated — the Settings UI supplies its own explanatory,
    // translated text around it.
    static QString fixHint() { return QStringLiteral("sudo usermod -aG input $USER"); }
};
