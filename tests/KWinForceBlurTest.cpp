#include <QtTest>
#include <QTemporaryDir>
#include "capture/KWinForceBlur.h"

// Better Blur's kwinrc group, read the way the effect reads it (#121): the
// blacklist mode blurs Unisic unless it is listed, the whitelist mode only
// when it is, and KConfig's "\n" between classes must split.
class KWinForceBlurTest : public QObject
{
    Q_OBJECT
private slots:
    void readsTheEffectsConfig()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString appId = QStringLiteral("app.unisic.Unisic");
        const auto write = [&dir](const QString &name, const QByteArray &body) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QString();
            f.write(body);
            return f.fileName();
        };

        // Default config as shipped: whitelist with placeholder classes.
        const QString defaults = write(QStringLiteral("kwinrc.default"),
            "[Effect-blurplus]\nBlurStrength=15\nWindowClasses=class1\\nclass2\\nclass3\n");
        QVERIFY(KWinForceBlur::verdict({defaults}, appId).isEmpty());

        // Blacklist ("blur all except") without Unisic: blurred.
        const QString blacklist = write(QStringLiteral("kwinrc.blacklist"),
            "[Effect-blurplus]\nBlurMatching=false\nWindowClasses=konsole\\ndolphin\n");
        QVERIFY(!KWinForceBlur::verdict({blacklist}, appId).isEmpty());

        // Blacklist with Unisic excluded by executable name, or by app id: left alone.
        const QString excluded = write(QStringLiteral("kwinrc.excluded"),
            "[Effect-blurplus]\nBlurMatching=false\nWindowClasses=konsole\\nunisic\n");
        QVERIFY(KWinForceBlur::verdict({excluded}, appId).isEmpty());
        const QString excludedId = write(QStringLiteral("kwinrc.excluded-id"),
            "[Effect-blurplus]\nBlurMatching=false\nWindowClasses=app.unisic.Unisic\n");
        QVERIFY(KWinForceBlur::verdict({excludedId}, appId).isEmpty());

        // Whitelist naming Unisic: blurred.
        const QString whitelist = write(QStringLiteral("kwinrc.whitelist"),
            "[Effect-blurplus]\nWindowClasses=unisic\n");
        QVERIFY(!KWinForceBlur::verdict({whitelist}, appId).isEmpty());

        // Cascade: the user's file (first) overrides the distro's.
        QVERIFY(KWinForceBlur::verdict({excluded, blacklist}, appId).isEmpty());
        QVERIFY(!KWinForceBlur::verdict({blacklist, excluded}, appId).isEmpty());

        // No group at all, or no files: nothing to blur with.
        const QString plain = write(QStringLiteral("kwinrc.plain"), "[Compositing]\nBackend=OpenGL\n");
        QVERIFY(KWinForceBlur::verdict({plain}, appId).isEmpty());
        QVERIFY(KWinForceBlur::verdict({}, appId).isEmpty());
    }
};

QTEST_GUILESS_MAIN(KWinForceBlurTest)
#include "KWinForceBlurTest.moc"
