#include <QtTest>
#include <QTemporaryDir>
#include "capture/KWinForceBlur.h"

// Better Blur's kwinrc group, read the way both forks read it (#121): the
// blacklist mode blurs Unisic unless it is listed, the whitelist mode only
// when it is, a dock (our layer-shell frames) only with BlurDocks on, and
// KConfig's "\n" between classes must split.
class KWinForceBlurTest : public QObject
{
    Q_OBJECT
private slots:
    void readsTheEffectsConfig()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dx = QStringLiteral("Effect-better-blur-dx");
        const QString old = QStringLiteral("Effect-blurplus");
        // A layer surface goes by the executable only, an X11 window also by its class.
        const QStringList layer{QStringLiteral("unisic")};
        const QStringList toplevel{QStringLiteral("unisic"), QStringLiteral("app.unisic.Unisic")};
        const auto write = [&dir](const QString &name, const QByteArray &body) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QString();
            f.write(body);
            return f.fileName();
        };

        // Default config as shipped: whitelist with placeholder classes.
        const QString defaults = write(QStringLiteral("kwinrc.default"),
            "[Effect-better-blur-dx]\nBlurStrength=15\nWindowClasses=class1\\nclass2\\nclass3\n");
        QVERIFY(KWinForceBlur::verdict({defaults}, dx, toplevel, false).isEmpty());

        // Blacklist ("blur all except") without Unisic: a toplevel is blurred,
        // a dock only once BlurDocks is on.
        const QString blacklist = write(QStringLiteral("kwinrc.blacklist"),
            "[Effect-better-blur-dx]\nBlurMatching=false\nWindowClasses=konsole\\ndolphin\n");
        QVERIFY(!KWinForceBlur::verdict({blacklist}, dx, toplevel, false).isEmpty());
        QVERIFY(KWinForceBlur::verdict({blacklist}, dx, layer, true).isEmpty());
        const QString docks = write(QStringLiteral("kwinrc.docks"),
            "[Effect-better-blur-dx]\nBlurMatching=false\nBlurDocks=true\nWindowClasses=konsole\n");
        QVERIFY(!KWinForceBlur::verdict({docks}, dx, layer, true).isEmpty());

        // The other fork's group is not this one's.
        QVERIFY(KWinForceBlur::verdict({blacklist}, old, toplevel, false).isEmpty());
        const QString archived = write(QStringLiteral("kwinrc.archived"),
            "[Effect-blurplus]\nBlurMatching=false\nWindowClasses=konsole\n");
        QVERIFY(!KWinForceBlur::verdict({archived}, old, toplevel, false).isEmpty());

        // Blacklist with Unisic excluded by executable name, or by a regex: left alone.
        const QString excluded = write(QStringLiteral("kwinrc.excluded"),
            "[Effect-better-blur-dx]\nBlurMatching=false\nBlurDocks=true\nWindowClasses=konsole\\nunisic\n");
        QVERIFY(KWinForceBlur::verdict({excluded}, dx, toplevel, false).isEmpty());
        QVERIFY(KWinForceBlur::verdict({excluded}, dx, layer, true).isEmpty());
        const QString regex = write(QStringLiteral("kwinrc.regex"),
            "[Effect-better-blur-dx]\nBlurMatching=false\nBlurDocks=true\nWindowClasses=/^uni/\n");
        QVERIFY(KWinForceBlur::verdict({regex}, dx, layer, true).isEmpty());

        // Excluded by app id: that spares a toplevel, not a layer surface.
        const QString excludedId = write(QStringLiteral("kwinrc.excluded-id"),
            "[Effect-better-blur-dx]\nBlurMatching=false\nBlurDocks=true\nWindowClasses=app.unisic.Unisic\n");
        QVERIFY(KWinForceBlur::verdict({excludedId}, dx, toplevel, false).isEmpty());
        QVERIFY(!KWinForceBlur::verdict({excludedId}, dx, layer, true).isEmpty());

        // Whitelist naming Unisic: blurred.
        const QString whitelist = write(QStringLiteral("kwinrc.whitelist"),
            "[Effect-better-blur-dx]\nWindowClasses=unisic\n");
        QVERIFY(!KWinForceBlur::verdict({whitelist}, dx, toplevel, false).isEmpty());

        // Cascade: the user's file (first) overrides the distro's.
        QVERIFY(KWinForceBlur::verdict({excluded, blacklist}, dx, toplevel, false).isEmpty());
        QVERIFY(!KWinForceBlur::verdict({blacklist, excluded}, dx, toplevel, false).isEmpty());

        // No group at all, or no files: nothing to blur with.
        const QString plain = write(QStringLiteral("kwinrc.plain"), "[Compositing]\nBackend=OpenGL\n");
        QVERIFY(KWinForceBlur::verdict({plain}, dx, toplevel, false).isEmpty());
        QVERIFY(KWinForceBlur::verdict({}, dx, toplevel, false).isEmpty());
    }
};

QTEST_GUILESS_MAIN(KWinForceBlurTest)
#include "KWinForceBlurTest.moc"
