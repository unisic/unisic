// Windows sibling of CardSurface.cpp. There is no layer-shell, and none is
// needed: Windows keeps a topmost window above the others by itself and lets a
// client put its own window anywhere, so the card is an ordinary frameless
// window that moves itself into the corner.
#include "LayerShellNotifier.h"

#include <QQuickWindow>
#include <QRect>
#include <QScreen>
#include <QString>

bool LayerShellNotifier::compositorSupportsLayerShell()
{
    return false;
}

bool LayerShellNotifier::placesCardItself()
{
    return true;
}

void LayerShellNotifier::place(QQuickWindow *win, QScreen *screen, const QString &position, int edge)
{
    // Tool: no taskbar button. DoesNotAcceptFocus: shown without activation,
    // so the card never takes the keyboard from the window the user is typing
    // in, the same promise as layer-shell's KeyboardInteractivityNone.
    win->setFlags(win->flags() | Qt::Tool | Qt::WindowStaysOnTopHint
                  | Qt::WindowDoesNotAcceptFocus);
    if (!screen)
        return;

    // availableGeometry leaves the taskbar out, the clearance layer-shell gets
    // from the panels' exclusive zones. `edge` is measured from the window, pad
    // included, exactly like the layer-shell margin.
    const QRect area = screen->availableGeometry();
    const QSize size = win->size();
    const bool top = position.startsWith(QLatin1String("top"));
    const bool left = position.endsWith(QLatin1String("left"));
    const bool right = position.endsWith(QLatin1String("right"));
    const int x = left    ? area.left() + edge
                  : right ? area.left() + area.width() - edge - size.width()
                          : area.left() + (area.width() - size.width()) / 2;
    const int y = top ? area.top() + edge
                      : area.top() + area.height() - edge - size.height();
    win->setPosition(x, y);
}
