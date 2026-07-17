#pragma once
#include <QObject>
#include <QRect>

// Drives one org.freedesktop.portal.ScreenCast session:
// CreateSession -> SelectSources(monitor) -> Start -> OpenPipeWireRemote.
// Emits ready(fd, nodeId, streamSize) when frames can be consumed,
// or failed(error). Close by deleting the object (closes the session).
class ScreenCastSession : public QObject
{
    Q_OBJECT
public:
    explicit ScreenCastSession(QObject *parent = nullptr);
    ~ScreenCastSession() override;

    // Portal cursor_mode values (org.freedesktop.portal.ScreenCast). These are
    // the wire values, not an app-side enum — do not renumber.
    enum CursorMode : uint {
        CursorHidden = 1,    // no cursor in the stream at all
        CursorEmbedded = 2,  // compositor paints the cursor into the frames
        CursorMetadata = 4,  // cursor delivered as per-buffer metadata instead:
                             // the frames carry NO cursor, so a consumer that
                             // asks for this MUST draw the pointer itself.
    };

    // Which cursor modes this portal supports (bitmask of CursorMode). Blocking
    // Properties.Get, cached after the first successful answer. Metadata is
    // optional in the spec, so a caller that needs it must check first and fall
    // back — an unsupported mode makes SelectSources fail outright.
    static uint availableCursorModes();

    // sourceTypes: bitmask MONITOR=1, WINDOW=2, VIRTUAL=4 (the portal shows a
    // matching picker). Default MONITOR for screen/region capture.
    void start(CursorMode cursor, uint sourceTypes = 1, const QString &restoreToken = {});

signals:
    // streamPos: the stream's logical position in the compositor workspace, or
    // (INT_MIN, INT_MIN) when the portal did not report one — (0,0) alone is
    // ambiguous, it is also a legit primary-monitor origin.
    void ready(int pipewireFd, uint nodeId, const QSize &streamSize, const QPoint &streamPos);
    void failed(const QString &error);
    void restoreTokenChanged(const QString &token);
    // The user stopped sharing from the system UI (portal Session Closed).
    void sessionClosed();

private:
    void createSession(CursorMode cursor);
    void selectSources(CursorMode cursor);
    void startCast();
    void openRemote(uint nodeId, const QSize &size, const QPoint &pos);

    QString m_sessionHandle;
    QString m_restoreToken;
    uint m_sourceTypes = 1;
    bool m_restoreTokensSupported = false;
};
