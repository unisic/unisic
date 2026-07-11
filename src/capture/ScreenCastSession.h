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

    // sourceTypes: bitmask MONITOR=1, WINDOW=2, VIRTUAL=4 (the portal shows a
    // matching picker). Default MONITOR for screen/region capture.
    void start(bool includeCursor, uint sourceTypes = 1, const QString &restoreToken = {});

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
    void createSession(bool includeCursor);
    void selectSources(bool includeCursor);
    void startCast();
    void openRemote(uint nodeId, const QSize &size, const QPoint &pos);

    QString m_sessionHandle;
    QString m_restoreToken;
    uint m_sourceTypes = 1;
    bool m_restoreTokensSupported = false;
};
