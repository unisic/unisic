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
    void start(bool includeCursor, uint sourceTypes = 1);

signals:
    void ready(int pipewireFd, uint nodeId, const QSize &streamSize, const QPoint &streamPos);
    void failed(const QString &error);

private:
    void createSession(bool includeCursor);
    void selectSources(bool includeCursor);
    void startCast();
    void openRemote(uint nodeId, const QSize &size, const QPoint &pos);

    QString m_sessionHandle;
    uint m_sourceTypes = 1;
};
