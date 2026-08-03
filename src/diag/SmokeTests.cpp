// The diagnostics half of AppContext: every devTest* action behind a button in
// the Settings Developer pane, every *Check() they share with the F8 smoke test,
// and runSmokeTest() itself. They are still AppContext members - this is a split
// of one 9300-line translation unit, not a change of API - so a new self-test is
// declared in AppContext.h next to its neighbours and defined HERE, not back in
// AppContext.cpp.
//
// Helpers used by both halves live in SmokeSupport.h. A helper only the tests use
// stays a file-static in this file.

#include "AppContext.h"
#include "unisic_build_date.h" // generated into the build dir (cmake/BuildDate.cmake)
#include "Settings.h"
#include "capture/CaptureManager.h"
#include "capture/KWinScreenShot2.h"
#include "capture/PortalRequest.h"
#include "overlay/OverlayController.h"
#include "upload/UploadManager.h"
#include "actions/ExternalActionRunner.h"
#include "history/HistoryStore.h"
#include "history/HistoryFilterModel.h"
#include "hotkeys/GlobalHotkeys.h"
#include "hotkeys/ShortcutFormat.h"
#include "diag/CrashHandler.h"
#include "diag/DiagLog.h"

#include <csignal>
#include <QTemporaryFile>
#include "update/UpdateChecker.h"
#include "update/VersionCompare.h"
#include "hotkeys/PortalGlobalShortcuts.h"
#ifdef HAVE_X11_HOTKEYS
#include "hotkeys/X11Hotkeys.h"
#endif
#if defined(HAVE_PIPEWIRE) && defined(HAVE_X11)
#include "record/X11ShmGrabber.h"
#include <QThread>
#endif
#include "record/GifRecorder.h"
#include "record/VideoQuality.h"
#include "media/FfmpegUtil.h"
#include "record/InputPermission.h"
#include "record/CursorOverlayPainter.h"
#include "record/KeystrokeOverlayPainter.h"
#include <linux/input-event-codes.h>
#include "capture/ScreenCastSession.h"
#ifdef HAVE_KWIN_SCREENCAST
#include "capture/KWinScreencasting.h"
#include <QCursor>
#endif
#include "record/RecordBorderController.h"
#include "record/TrimController.h"
#include "editor/EditorSession.h"
#include "editor/ImageEffects.h"
#include "editor/WatermarkPreview.h"
#include "PreviewController.h"
#include "notify/NotifCard.h"
#include "notify/CaptureNotification.h"
#include "notify/DesktopNotifier.h"
#include "notify/NotificationInhibitor.h"
#ifdef HAVE_LAYERSHELL
#include "notify/LayerShellNotifier.h"
#include <LayerShellQt/window.h>
#include <QMargins>
#endif
#include "theme/ThemeController.h"
#include "theme/ThemeJson.h"
#include "editor/AnnotationCanvas.h"
#include "ConfigPath.h"
#include "FilenameTemplate.h"
#include "ImageEncode.h"
#ifdef HAVE_TESSERACT
#include "ocr/OcrEngine.h"
#endif
#ifdef HAVE_ZXING
#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>
#endif
// Clipboard offers (the KDE force-image-copy hint), clipboard reads and drop
// payloads all speak QMimeData, so it is needed with or without KGuiAddons.
#include <QMimeData>
#ifdef HAVE_KGUIADDONS
#include <KSystemClipboard>
#endif
#include <QGuiApplication>
#include <QtMath>
#include <QScreen>
#include <QRegion>
#include <QPointer>
#include <QClipboard>
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QSize>
#include <QColor>
#include <QPainter>
#include <QImageReader>
#include <QStyleHints>
#include <QFileSystemWatcher>
#include <QFileDialog>
#include <QKeySequence>
#include <QDesktopServices>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QBuffer>
#include <QTimer>
#include <QProcess>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUuid>
#include <QMetaProperty>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QtConcurrentRun>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDebug>
#include <memory>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include "diag/SmokeSupport.h"

void AppContext::hideOnCaptureCheck(std::function<void(const QString &)> done)
{
    QPointer<QQuickWindow> win = mainWindow();
    if (!win || !win->isVisible()) {
        done(QStringLiteral("SKIP (main window not open)"));
        return;
    }
    if (!m_settings->hideWindowOnCapture()) {
        // Deliberately does NOT flip the setting to run anyway: this check runs
        // from the smoke test on the user's own configuration, and a check that
        // rewrites config to make itself pass is a worse trade than a SKIP.
        done(QStringLiteral("SKIP (hide while capturing is off)"));
        return;
    }
    const bool wentDown = hideOwnWindowForCapture() && !win->isVisible();
    // Restored a whole event-loop turn later, on the same timer a real capture
    // uses - the failure that matters here is the window not coming BACK, and a
    // same-turn hide/show would not exercise that at all.
    QTimer::singleShot(kSelfHideSettleMs, this, [this, win, wentDown, done = std::move(done)] {
        restoreOwnWindowAfterCapture();
        const bool cameBack = win && win->isVisible();
        done(wentDown && cameBack
                 ? QStringLiteral("PASS")
                 : QStringLiteral("FAIL (hidden=%1, restored=%2)")
                       .arg(wentDown ? 1 : 0).arg(cameBack ? 1 : 0));
    });
}

void AppContext::externalActionTimeoutCheck(std::function<void(const QString &)> done)
{
    // `sleep` is coreutils and needs no input file: the check only has to hang,
    // and the guard has to notice. Five seconds caps the damage if the guard is
    // the thing that is broken - the run ends either way, it just reports FAIL.
    if (QStandardPaths::findExecutable(QStringLiteral("sleep")).isEmpty()) {
        done(QStringLiteral("SKIP (no sleep helper)"));
        return;
    }
    // A runner of its own, so a real after-capture action already in flight
    // cannot push this one into the concurrency limit and turn a PASS into a
    // rejection. Deleted from the callback, which fires exactly once.
    auto *runner = new ExternalActionRunner(this);
    const auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    // What runExternalAction would actually arm, reported alongside the result:
    // the guard being proven to work says nothing about the ceiling the user
    // configured, and a settings file edited by hand is clamped silently.
    const int wanted = m_settings->externalActionTimeoutSec();
    const int used = qBound(ExternalActionRunner::kMinTimeoutSec, wanted,
                            ExternalActionRunner::kMaxTimeoutSec);
    const QString ceiling = used == wanted
        ? QStringLiteral("%1 s").arg(used)
        : QStringLiteral("%1 s, clamped from %2").arg(used).arg(wanted);
    runner->run(QStringLiteral("sleep 5"),
                QDir::temp().filePath(QStringLiteral("unisic-timeout-check.png")),
                false,
                [runner, elapsed, ceiling, done = std::move(done)](const QString &output, const QString &error) {
        const qint64 ms = elapsed->elapsed();
        runner->deleteLater();
        if (error.isEmpty() || !output.isEmpty()) {
            done(QStringLiteral("FAIL (hung action reported success)"));
            return;
        }
        done(ms >= 900 && ms < 3000
                 ? QStringLiteral("PASS (stopped after %1 ms, capture ceiling %2)").arg(ms).arg(ceiling)
                 : QStringLiteral("FAIL (stopped after %1 ms, expected ~1000)").arg(ms));
    }, 1000);
}

void AppContext::smokeLog(const QString &line)
{
    qInfo().noquote() << "[smoke]" << line;
    m_smokeLog += line + QLatin1Char('\n');
    emit smokeTestChanged();
}

// Text-annotation render check: a multi-line, italic, underlined, outlined
// and backgrounded text composited onto a known base must actually change a
// meaningful number of pixels (dev button + smoke step).
static QString textRenderCheck()
{
    AnnotationCanvas c;
    QImage base(320, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.setFontSize(28);
    c.setFontItalic(true);
    c.setFontUnderline(true);
    c.setTextOutline(true);
    c.setTextBackground(true);
    c.commitText(20, 30, QStringLiteral("multi\nline"));
    const QImage out = c.rendered();
    if (out.size() != base.size())
        return QStringLiteral("FAIL (size changed)");
    int diff = 0;
    for (int y = 0; y < out.height(); y += 4)
        for (int x = 0; x < out.width(); x += 4)
            if (out.pixel(x, y) != base.pixel(x, y))
                ++diff;
    return diff > 20 ? QStringLiteral("PASS (%1 sampled pixels changed)").arg(diff)
                     : QStringLiteral("FAIL (render left the base blank)");
}

// Keystroke-badge render check: synthetic evdev events must produce the
// expected chord text AND paint() must land a visible badge in the frame
// (dev button + smoke step). Pure logic — no libinput, no /dev/input.
static QString keystrokeBadgeCheck()
{
    KeystrokeOverlayPainter p;
    constexpr qint64 ms = 1000000LL;
    p.keyEvent(KEY_LEFTCTRL, true, 0);
    p.keyEvent(KEY_LEFTSHIFT, true, 1 * ms);
    p.keyEvent(KEY_T, true, 2 * ms);
    if (p.textAt(3 * ms) != QLatin1String("Ctrl+Shift+T"))
        return QStringLiteral("FAIL (chord text: %1)").arg(p.textAt(3 * ms));
    p.keyEvent(KEY_T, true, 100 * ms);
    // QStringLiteral, NOT QLatin1String: "×" is multi-byte in this UTF-8
    // source and QLatin1String would read those bytes as two Latin-1 chars
    // (same trap as the measure check, commit a8ca485).
    if (p.textAt(101 * ms) != QStringLiteral("Ctrl+Shift+T ×2"))
        return QStringLiteral("FAIL (repeat counter: %1)").arg(p.textAt(101 * ms));
    QImage frame(640, 360, QImage::Format_ARGB32);
    frame.fill(QColor(0x17, 0x15, 0x3B));
    const QImage before = frame;
    {
        QPainter painter(&frame);
        p.paint(painter, 102 * ms);
    }
    if (frame == before)
        return QStringLiteral("FAIL (paint drew nothing)");
    const qint64 gone = 102 * ms + qint64(p.style().badgeMs + p.style().fadeMs + 10) * ms;
    p.keyEvent(KEY_LEFTCTRL, false, 103 * ms);
    p.keyEvent(KEY_LEFTSHIFT, false, 103 * ms);
    if (p.hasContent(gone))
        return QStringLiteral("FAIL (badge never expires)");
    return QStringLiteral("PASS (chord, ×N, paint, expiry)");
}

// Community-theme check: schema validation (accept/reject) + a live
// round-trip through ThemeController — write a theme file, watch it appear in
// customDefs, delete it, watch it disappear (dev button + smoke step). Uses
// the dev build's own config dir, so it never touches a stable install.
static QString customThemeCheck()
{
    QString err;
    const QByteArray good = QByteArrayLiteral(
        "{\"name\": \"Smoke Theme\", \"isDark\": true,"
        " \"primary\": \"#101020\", \"secondary\": \"#202040\", \"tertiary\": \"#303060\","
        " \"accent\": \"#C8ACD6\", \"bg\": \"#0B0B18\", \"surface\": \"#181830\","
        " \"text\": \"#EEEEF8\", \"textOnAccent\": \"#101020\","
        " \"recBadgeBg\": \"#CC101020\", \"keystrokeText\": \"#FFEEEE\"}");
    if (ThemeJson::parse(good, QStringLiteral("smoke"), &err).isEmpty())
        return QStringLiteral("FAIL (valid theme rejected: %1)").arg(err);
    if (!ThemeJson::parse(QByteArrayLiteral("{\"isDark\": true}"),
                          QStringLiteral("smoke"), &err).isEmpty())
        return QStringLiteral("FAIL (incomplete theme accepted)");

    ThemeController *tc = ThemeController::instance();
    if (!tc)
        return QStringLiteral("FAIL (no ThemeController)");
    // The 5 decorative palettes are seeded as REAL files in the themes folder
    // (core system/unisic/dark/light stay hardcoded in Theme.qml). They load
    // through the same custom-theme scan, so they must be present as ids -
    // unless the user deleted one, which is a supported thing to do and stays
    // done. ThemeController's seeded record is what tells the two apart, so
    // this step fails only when a theme was never delivered here at all.
    int userDeleted = 0;
    const QStringList seeded = tc->seededThemes();
    for (const QString &stem : {QStringLiteral("dracula"), QStringLiteral("nord"),
                                QStringLiteral("gruvbox"),
                                QStringLiteral("catppuccin-mocha"),
                                QStringLiteral("catppuccin-latte")}) {
        if (tc->customDefs().contains(QStringLiteral("custom:") + stem))
            continue;
        if (!seeded.contains(stem + QStringLiteral(".json")))
            return QStringLiteral("FAIL (decorative theme %1 not seeded to folder)").arg(stem);
        ++userDeleted;
    }
    const QString dir = tc->themesFolder();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/smoketest-theme.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("FAIL (cannot write %1)").arg(path);
    f.write(good);
    f.close();
    tc->reloadCustomThemes();
    const bool appeared = tc->customDefs().contains(QStringLiteral("custom:smoketest-theme"));
    QFile::remove(path);
    tc->reloadCustomThemes();
    const bool gone = !tc->customDefs().contains(QStringLiteral("custom:smoketest-theme"));
    if (!appeared)
        return QStringLiteral("FAIL (theme file not picked up)");
    if (!gone)
        return QStringLiteral("FAIL (deleted theme still listed)");
    if (userDeleted)
        return QStringLiteral("PASS (schema, %1 of 5 seeded - %2 deleted here, live folder round-trip)")
            .arg(5 - userDeleted).arg(userDeleted);
    return QStringLiteral("PASS (schema, 5 seeded, live folder round-trip)");
}

// The KDE clipboard-history offer MUST carry the x-kde-force-image-copy hint or
// Klipper never records the image (issue #51). Reuses the live builder, so
// dropping the hint fails this. Shared by the developer button and F8 smoke test.
static QString clipboardHistoryHintCheck()
{
#ifdef HAVE_KGUIADDONS
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(Qt::blue);
    QScopedPointer<QMimeData> mime(makeForceImageMime(img));
    const bool ok = mime->hasImage()
                    && mime->hasFormat(QStringLiteral("x-kde-force-image-copy"));
    return ok ? QStringLiteral("PASS (Klipper history hint attached)")
              : QStringLiteral("FAIL (hint missing)");
#else
    return QStringLiteral("SKIP (no KF6GuiAddons; images still copy but skip Klipper history)");
#endif
}

// Clipboard paste must create a real text annotation and retain a pasted image
// for the composited export. Shared by the developer button and F8 smoke test.
static QString clipboardPasteCheck()
{
    AnnotationCanvas canvas;
    QImage base(200, 120, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    auto *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("Unisic paste"));
    if (!canvas.pasteClipboard(30, 30) || canvas.annotCount() != 1)
        return QStringLiteral("FAIL (text was not pasted)");
    QImage stamp(24, 16, QImage::Format_ARGB32_Premultiplied);
    stamp.fill(Qt::black);
    clipboard->setImage(stamp);
    if (!canvas.pasteClipboard(100, 60) || canvas.annotCount() != 2)
        return QStringLiteral("FAIL (image was not pasted)");
    if (canvas.rendered() == base)
        return QStringLiteral("FAIL (paste missing from export)");
    return QStringLiteral("PASS (text + image annotation)");
}

// Watermarking is deliberately export-only: it must change the one image that
// reaches every after-capture action, while leaving its dimensions intact. Every
// pattern preset is exercised here, because a preset that silently paints
// nothing looks exactly like a watermark that was switched off.
static QString watermarkCheck(const Settings *settings)
{
    QImage base(240, 140, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::black);
    QImage logo(80, 40, QImage::Format_ARGB32_Premultiplied);
    logo.fill(Qt::transparent);
    QPainter painter(&logo);
    painter.fillRect(QRect(5, 5, 70, 30), Qt::white);
    painter.end();

    const QStringList patterns{ QStringLiteral("single"), QStringLiteral("tile"),
                                QStringLiteral("diagonal"), QStringLiteral("corners"),
                                QStringLiteral("band") };
    int singlePixels = 0;
    int tilePixels = 0;
    for (const QString &pattern : patterns) {
        const QImage stamped = UnisicImageEffects::watermarkText(
            base, QStringLiteral("Unisic"), 75, QStringLiteral("bottom-right"), pattern);
        const QImage logoStamped = UnisicImageEffects::watermarkImage(
            base, logo, 75, QStringLiteral("top-left"), pattern);
        if (stamped.size() != base.size() || logoStamped.size() != base.size())
            return QStringLiteral("FAIL (%1 resized the image)").arg(pattern);
        if (stamped == base || logoStamped == base)
            return QStringLiteral("FAIL (%1 painted nothing)").arg(pattern);
        int marked = 0;
        for (int y = 0; y < base.height(); ++y)
            for (int x = 0; x < base.width(); ++x)
                if (stamped.pixel(x, y) != base.pixel(x, y))
                    ++marked;
        if (pattern == QLatin1String("single"))
            singlePixels = marked;
        else if (pattern == QLatin1String("tile"))
            tilePixels = marked;
    }
    // The tiled presets exist to make the mark awkward to crop out, so covering
    // no more than one stamp does means the tiling never ran.
    if (tilePixels <= singlePixels)
        return QStringLiteral("FAIL (tiled covers no more than one stamp)");

    // Size is a pure multiplier on whatever the pattern picked, and an absurd
    // value from a hand-edited config still has to produce a usable capture
    // rather than a stamp bigger than the picture.
    const auto marked = [&base](int scale) {
        const QImage out = UnisicImageEffects::watermarkText(
            base, QStringLiteral("Unisic"), 100, QStringLiteral("bottom-right"),
            QStringLiteral("single"), scale);
        if (out.size() != base.size())
            return -1;
        int n = 0;
        for (int y = 0; y < base.height(); ++y)
            for (int x = 0; x < base.width(); ++x)
                if (out.pixel(x, y) != base.pixel(x, y))
                    ++n;
        return n;
    };
    const int small = marked(50);
    const int normal = marked(100);
    const int large = marked(200);
    if (small < 0 || normal < 0 || large < 0 || marked(100000) < 0)
        return QStringLiteral("FAIL (size resized the image)");
    if (!(small < normal && normal < large))
        return QStringLiteral("FAIL (size %1/%2/%3 is not monotonic)")
            .arg(small).arg(normal).arg(large);
    const QString live = settings
        ? QStringLiteral(", live: %1/%2 at %3%")
              .arg(settings->watermarkEnabled() ? settings->watermarkPattern()
                                                : QStringLiteral("off"),
                   settings->watermarkPosition())
              .arg(settings->watermarkScale())
        : QString();
    return QStringLiteral("PASS (5 patterns x 3 sizes, text + logo in image pixels%1)").arg(live);
}

static QString calloutCheck()
{
    class InputCanvas final : public AnnotationCanvas {
    public:
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    };
    InputCanvas canvas;
    canvas.setWidth(220);
    canvas.setHeight(160);
    QImage base(220, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setTool(AnnotationCanvas::Callout);
    canvas.setStrokeColor(Qt::black);
    canvas.setShapeFillColor(Qt::black);
    canvas.setShapeFillEnabled(true);
    const QPointF from(30, 20), to(180, 100);
    QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    canvas.mousePressEvent(&press);
    canvas.mouseMoveEvent(&move);
    canvas.mouseReleaseEvent(&release);
    const QImage out = canvas.rendered();
    return canvas.annotCount() == 1 && out.pixelColor(38, 110).lightness() < 80
               ? QStringLiteral("PASS (bubble + tail)")
               : QStringLiteral("FAIL (bubble did not render)");
}

static QString shiftSnapCheck()
{
    class InputCanvas final : public AnnotationCanvas {
    public:
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    };
    InputCanvas canvas;
    QImage base(100, 100, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setTool(AnnotationCanvas::Line);
    canvas.setStrokeColor(Qt::black);
    canvas.setStrokeWidth(4);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(50, 25), QPointF(50, 25),
                     Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(50, 25), QPointF(50, 25),
                        Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    canvas.mousePressEvent(&press);
    canvas.mouseMoveEvent(&move);
    canvas.mouseReleaseEvent(&release);
    return canvas.rendered().pixelColor(44, 10).lightness() < 80
               ? QStringLiteral("PASS (45° + grid)")
               : QStringLiteral("FAIL (line not constrained)");
}

static QString externalActionCheck()
{
    QString program, output, error;
    QStringList args;
    const QString input = QDir::temp().filePath(QStringLiteral("Unisic input.png"));
    const bool ok = ExternalActionRunner::expandCommand(
        QStringLiteral("true --source $input --dest $output"), input,
        &program, &args, &output, &error);
    if (!ok)
        return QStandardPaths::findExecutable(QStringLiteral("true")).isEmpty()
                   ? QStringLiteral("SKIP (no true helper)")
                   : QStringLiteral("FAIL (%1)").arg(error);
    return args.contains(input) && args.contains(output)
               && output.endsWith(QLatin1String("-action.png"))
               ? QStringLiteral("PASS (direct argv + tokens)")
               : QStringLiteral("FAIL (token expansion)");
}

static QString measureToolsCheck()
{
    class InputCanvas final : public AnnotationCanvas {
    public:
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    } canvas;
    QImage base(260, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setStrokeColor(Qt::black);
    const auto draw = [&canvas](AnnotationCanvas::Tool tool, QPointF a, QPointF b,
                                Qt::KeyboardModifiers mods) {
        canvas.setTool(tool);
        QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton,
                          Qt::LeftButton, mods);
        QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton,
                         Qt::LeftButton, mods);
        QMouseEvent release(QEvent::MouseButtonRelease, b, b, Qt::LeftButton,
                            Qt::NoButton, mods);
        canvas.mousePressEvent(&press);
        canvas.mouseMoveEvent(&move);
        canvas.mouseReleaseEvent(&release);
    };
    // Distance mode: a 200px horizontal line (plain drag).
    canvas.setMeasureMode(0);
    draw(AnnotationCanvas::Measure, {20, 40}, {220, 40}, Qt::NoModifier);
    if (canvas.annotCount() != 1 || canvas.rendered() == base)
        return QStringLiteral("FAIL (distance not placed)");
    if (canvas.measuresText(QStringLiteral("readable")) != QLatin1String("200 px"))
        return QStringLiteral("FAIL (distance text: %1)")
            .arg(canvas.measuresText(QStringLiteral("readable")));
    // Size mode: a 200×60 box; a second retained measurement; formats respected.
    canvas.setMeasureMode(1);
    draw(AnnotationCanvas::Measure, {20, 40}, {220, 100}, Qt::NoModifier);
    // QStringLiteral, NOT QLatin1String: the × is UTF-8 in this source file,
    // and QLatin1String reads those bytes as Latin-1 ("Ã—"), so the comparison
    // could never match — that was a smoke-test FAIL with a correct measuresText.
    if (canvas.measuresText(QStringLiteral("readable")).section('\n', 1, 1)
            != QStringLiteral("200 × 60"))
        return QStringLiteral("FAIL (size text: %1)")
            .arg(canvas.measuresText(QStringLiteral("readable")));
    if (canvas.measuresText(QStringLiteral("plain")).section('\n', 1, 1)
            != QLatin1String("200x60"))
        return QStringLiteral("FAIL (plain format)");
    return QStringLiteral("PASS (ruler: distance + size, retained, formats)");
}

void AppContext::devTestTextRender()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: text render: %1").arg(textRenderCheck()));
}

void AppContext::devTestKeystrokeBadge()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: keystroke badge: %1").arg(keystrokeBadgeCheck()));
}

void AppContext::devTestCustomTheme()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: custom theme: %1").arg(customThemeCheck()));
}

void AppContext::devTestClipboardPaste()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: clipboard paste: %1").arg(clipboardPasteCheck()));
}

void AppContext::devTestCaptureDelay()
{
    if (!devBuild())
        return;
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    setNextCaptureDelayMs(1100);
    withDelay([this, elapsed] {
        showToast(tr("Dev: capture delay: %1")
                  .arg(elapsed->elapsed() >= 1000 ? QStringLiteral("PASS")
                                                   : QStringLiteral("FAIL")));
    });
}

void AppContext::devTestCopyAs()
{
    if (!devBuild())
        return;
    const QString path = QDir::temp().filePath(QStringLiteral("unisic-copy-as-test.png"));
    const QString url = QStringLiteral("https://example.invalid/capture.png");
    copyImageAs({}, path, {}, QStringLiteral("path"));
    const bool pathOk = QGuiApplication::clipboard()->text() == path;
    copyImageAs({}, path, url, QStringLiteral("markdown"));
    const bool markdownOk = QGuiApplication::clipboard()->text() == QStringLiteral("![](%1)").arg(url);
    copyImageAs({}, path, url, QStringLiteral("html"));
    const bool htmlOk = QGuiApplication::clipboard()->text()
                        == QStringLiteral("<img src=\"%1\" alt=\"\">").arg(url);
    showToast(tr("Dev: copy as: %1")
              .arg(pathOk && markdownOk && htmlOk
                       ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
}

void AppContext::devTestWatermark()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: watermark: %1").arg(watermarkCheck(m_settings)));
}

// The Settings preview is not a second renderer: it paints a mock capture and
// hands it to the SAME stampWatermark the after-capture pipeline calls, so this
// verifies the mock exists, keeps its size, and actually carries the mark when
// the watermark is on (and is left untouched when it is off).
QString AppContext::watermarkPreviewCheck() const
{
    const QSize want(440, 260);
    const QImage mock = UnisicImageEffects::mockCapture(want);
    if (mock.isNull() || mock.size() != want)
        return QStringLiteral("FAIL (mock capture missing)");
    // Through the provider itself, not just the pipeline step behind it: the
    // Settings page reaches this only as image://watermark/<rev>, so the id and
    // size handling are part of what has to work.
    WatermarkPreviewProvider provider(this);
    QSize reported;
    const QImage shown = provider.requestImage(QStringLiteral("7"), &reported, want);
    if (shown.size() != want || reported != want)
        return QStringLiteral("FAIL (preview resized the mock)");
    // A QML Image with no sourceSize must still get a picture, not a null one.
    if (provider.requestImage(QString(), nullptr, QSize()).size() != want)
        return QStringLiteral("FAIL (preview has no default size)");
    const bool on = m_settings->watermarkEnabled();
    if (on && shown == mock)
        return QStringLiteral("FAIL (watermark on, preview unmarked)");
    if (!on && shown != mock)
        return QStringLiteral("FAIL (watermark off, preview marked)");
    return QStringLiteral("PASS (%1x%2, watermark %3)")
        .arg(want.width()).arg(want.height())
        .arg(on ? m_settings->watermarkPattern() : QStringLiteral("off"));
}

void AppContext::devTestWatermarkPreview()
{
    if (!devBuild())
        return;
    const QString result = watermarkPreviewCheck();
    // Opened at twice the Settings size so the stamp can actually be judged,
    // which is the whole point of a preview check being clickable.
    const QImage shown = stampWatermark(UnisicImageEffects::mockCapture(QSize(880, 520)));
    if (!shown.isNull())
        openPreview(shown);
    showToast(tr("Dev: watermark preview: %1").arg(result));
}

void AppContext::devTestCallout()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: callout: %1").arg(calloutCheck()));
}

void AppContext::devTestShiftSnap()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: Shift snap: %1").arg(shiftSnapCheck()));
}

void AppContext::devTestQrPreview()
{
    if (!devBuild())
        return;
    showQr(QStringLiteral("https://example.invalid/unisic-qr-test"));
}

void AppContext::devTestDiagnostics()
{
    if (!devBuild())
        return;
    const QString d = systemDiagnostics();
    copyText(d);
    showToast(tr("Dev: diagnostics copied (%1 chars)").arg(d.size()));
}

void AppContext::devTestSystemCheck()
{
    if (!devBuild())
        return;
    const QVariantList rep = dependencyReport();
    int missing = 0, warn = 0;
    for (const QVariant &v : rep) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("ok")).toBool()) {
            ++missing;
            if (m.value(QStringLiteral("warn")).toBool())
                ++warn;
        }
    }
    showToast(tr("Dev: system check: %1 checks, %2 missing (%3 core)")
                  .arg(rep.size()).arg(missing).arg(warn));
}

void AppContext::devTestWelcome()
{
    if (!devBuild())
        return;
    showWelcome();
}

void AppContext::devTestDoNotDisturb()
{
    if (!devBuild())
        return;
    if (!capDoNotDisturb()) {
        showToast(tr("Dev: do not disturb: unsupported on this desktop"), true);
        return;
    }
    m_dnd->acquire();
    const bool active = m_dnd->active();
    QTimer::singleShot(800, this, [this, active] {
        m_dnd->release();
        showToast(tr("Dev: do not disturb: %1")
                      .arg(active ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    });
}

void AppContext::devTestHideOnCapture()
{
    if (!devBuild())
        return;
    hideOnCaptureCheck([this](const QString &r) {
        showToast(tr("Dev: hide while capturing: %1").arg(r));
    });
}

void AppContext::devTestExternalAction()
{
    if (devBuild())
        showToast(tr("Dev: external action: %1").arg(externalActionCheck()));
}

void AppContext::devTestExternalActionTimeout()
{
    if (!devBuild())
        return;
    externalActionTimeoutCheck([this](const QString &r) {
        showToast(tr("Dev: external action timeout: %1").arg(r));
    });
}

void AppContext::devTestTaskPreset()
{
    if (!devBuild())
        return;
    const CaptureTask copy = taskFromId(QStringLiteral("copy"));
    const CaptureTask all = taskFromId(QStringLiteral("all"));
    const CaptureTask normal = taskFromId(QStringLiteral("default"));
    showToast(tr("Dev: task preset: %1")
                  .arg(copy.active && copy.copy && !copy.save && !copy.edit
                               && !copy.upload && !normal.active
                               && all.active && all.copy && all.save && all.edit && all.upload
                           ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
}

void AppContext::devTestCliOutput()
{
    if (!devBuild())
        return;
    QTemporaryDir dir;
    const QString path = dir.isValid()
                             ? saveImageTo(devTestImage(), dir.path(),
                                           QStringLiteral("cli-output.png"))
                             : QString();
    QImageReader reader(path);
    showToast(tr("Dev: CLI output: %1")
                  .arg(!path.isEmpty() && reader.format().toLower() == "png"
                           ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
}

void AppContext::devTestMeasureTools()
{
    if (devBuild())
        showToast(tr("Dev: measure tools: %1").arg(measureToolsCheck()));
}

void AppContext::devTestHardwareEncoder()
{
    if (!devBuild()) return;
    // Verify the WORKING probe, not just the listing: "auto" resolves through
    // it, and a listed-but-broken encoder (seen in the wild with vp9_vaapi)
    // must resolve to software, never be handed out.
    const bool nv = FfmpegUtil::hardwareEncoderWorks(QStringLiteral("nvenc"));
    const bool va = FfmpegUtil::hardwareEncoderWorks(QStringLiteral("vaapi"));
    const bool av1 = FfmpegUtil::hardwareEncoderWorks(QStringLiteral("av1-nvenc"));
    const QString resolved = m_recorder ? m_recorder->resolvedVideoEncoder()
                                        : QStringLiteral("?");
    showToast(tr("Dev: hardware encoder: %1 (auto→%2, nvenc=%3, vaapi=%4, av1-nvenc=%5)")
                  .arg(nv || va || av1 ? QStringLiteral("PASS")
                                       : QStringLiteral("SKIP (software only)"),
                       resolved,
                       nv ? QStringLiteral("works") : QStringLiteral("no"),
                       va ? QStringLiteral("works") : QStringLiteral("no"),
                       av1 ? QStringLiteral("works") : QStringLiteral("no")));
}

void AppContext::devTestFreezeRecorder()
{
    if (!devBuild()) return;
    // SIGSTOP the live recording encoder, then press Stop: the stop-flush
    // watchdog must kill it (~25 s, UNISIC_STOP_STALL_MS to shorten) and the
    // salvage path must still convert the temp into a finished file.
    const bool frozen = m_recorder && m_recorder->devFreezeEncoderForTest();
    showToast(frozen
                  ? tr("Dev: recording encoder frozen (SIGSTOP) - press Stop to exercise the watchdog")
                  : tr("Dev: no live recording encoder to freeze - start a recording first"));
}

void AppContext::devTestPerAppAudio()
{
    if (!devBuild()) return;
    const QVariantList nodes = audioApplicationNodes();
    showToast(tr("Dev: per-app audio: %1")
                  .arg(!perAppAudioAvailable() ? QStringLiteral("SKIP")
                                               : QStringLiteral("PASS (%1 nodes)").arg(nodes.size())));
}

void AppContext::devTestInstantReplay()
{
    if (!devBuild()) return;
    if (!recordingAvailable()) {
        showToast(tr("Dev: instant replay: recording unavailable"), true);
        return;
    }
    if (instantReplayActive())
        saveInstantReplay();
    else if (!recording())
        startInstantReplay();
}

// Encoder for the trim self-test fixtures: the checks exercise trimming, not a
// specific codec, and a mandatory libx264 failed fixture creation outright on
// GPL-less ffmpeg builds. Same fallback order as the recorder — x264, OpenH264,
// then the always-built-in mpeg4 (stream copy and packet keyframe flags are
// codec-agnostic, so every trim path still gets exercised).
static QStringList trimFixtureEncoderArgs()
{
    if (FfmpegUtil::encoderUsable(QStringLiteral("libx264")))
        return {QStringLiteral("-c:v"), QStringLiteral("libx264"),
                QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
    if (FfmpegUtil::encoderUsable(QStringLiteral("libopenh264")))
        return {QStringLiteral("-c:v"), QStringLiteral("libopenh264"),
                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
    return {QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
            QStringLiteral("-q:v"), QStringLiteral("5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
}

void AppContext::devTestTrimRecording()
{
    if (!devBuild()) return;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        showToast(tr("Dev: trim recording: ffmpeg unavailable"), true);
        return;
    }
    const QString path = QDir::temp().filePath(QStringLiteral("unisic-trim-dev.mp4"));
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, path](int code, QProcess::ExitStatus) {
        process->deleteLater();
        if (code == 0)
            openTrimRecording(path);
        else
            showToast(tr("Dev: trim recording: FAIL"), true);
    });
    // FailedToStart never emits finished — reap the QProcess on that path too.
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        process->deleteLater();
        showToast(tr("Dev: trim recording: FAIL"), true);
    });
    // Long enough, moving, and with a keyframe every second: the window has a
    // filmstrip whose tiles differ and real keyframe ticks to snap onto.
    QStringList args{QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("lavfi"),
                     QStringLiteral("-i"), QStringLiteral("testsrc=size=320x180:rate=30:duration=8"),
                     QStringLiteral("-g"), QStringLiteral("30")};
    args << trimFixtureEncoderArgs() << path;
    process->start(ffmpeg, args);
}

void AppContext::devTestTrimCut()
{
    if (!devBuild()) return;
    trimCutCheck([this](const QString &result) {
        showToast(tr("Dev: trim cut: %1").arg(result), result.contains(QLatin1String("FAIL")));
    });
}

void AppContext::trimCutCheck(std::function<void(const QString &)> done)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        done(QStringLiteral("SKIP (ffmpeg/ffprobe missing)"));
        return;
    }
    // A clip with a keyframe every 15 frames: the copy path needs somewhere to
    // snap to, and the moving pattern makes the filmstrip tiles differ.
    const QString source = QDir::temp().filePath(QStringLiteral("unisic-trimcheck.mp4"));
    QFile::remove(source);
    QStringList fixtureArgs{QStringLiteral("-y"), QStringLiteral("-nostats"),
                            QStringLiteral("-loglevel"), QStringLiteral("error"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"),
                            QStringLiteral("-i"),
                            QStringLiteral("testsrc=size=160x90:rate=30:duration=3"),
                            QStringLiteral("-g"), QStringLiteral("15")};
    fixtureArgs << trimFixtureEncoderArgs() << source;
    QProcess::execute(ffmpeg, fixtureArgs);
    if (!QFileInfo::exists(source)) {
        done(QStringLiteral("FAIL (test clip)"));
        return;
    }
    const QString exact = QDir::temp().filePath(QStringLiteral("unisic-trimcheck-trimmed.mp4"));
    const QString copied = QDir::temp().filePath(QStringLiteral("unisic-trimcheck-copy-trimmed.mp4"));
    const QString copySource = QDir::temp().filePath(QStringLiteral("unisic-trimcheck-copy.mp4"));
    QFile::remove(exact);
    QFile::remove(copied);
    QFile::copy(source, copySource);

    // Same window the trim editor builds, on the same file.
    auto *probe = new TrimController(source, 3.0, 1.0 / 30, this);
    probe->buildFilmstrip(8, 48);
    probe->loadKeyframes();

    trimRecording(source, 0.5, 1.5, false);       // exact: re-encode
    trimRecording(copySource, 0.0, 1.0, true);    // lossless: stream copy

    QTimer::singleShot(3000, this, [this, probe, ffprobe, source, copySource,
                                    exact, copied, done] {
        const auto durationOf = [&ffprobe](const QString &path) -> qreal {
            QProcess p;
            p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                              QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                              QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), path});
            if (!p.waitForFinished(2000))
                return -1;
            return QString::fromLatin1(p.readAllStandardOutput().trimmed()).toDouble();
        };
        const qreal exactDuration = durationOf(exact);
        const qreal copyDuration = durationOf(copied);
        const bool exactOk = qAbs(exactDuration - 1.0) < 0.15;
        const bool copyOk = qAbs(copyDuration - 1.0) < 0.25;   // ends on a whole packet
        const bool stripOk = probe->filmstripState() == TrimController::Ready;
        const bool keyframesOk = probe->keyframes().size() >= 2;
        // Snapping may only move the in-point backwards, never past the ask.
        const bool snapOk = keyframesOk && probe->snapStart(1.2) <= 1.2 + 0.001;
        probe->deleteLater();
        // The cuts stay: they went through the real path, so history now points
        // at them. Only the generated sources are scratch. A stale pair from an
        // earlier run is what the removals at the top of this check clear.
        QFile::remove(source);
        QFile::remove(copySource);
        done(QStringLiteral("exact %1 (%2s), lossless %3 (%4s), filmstrip %5, keyframes %6")
                 .arg(exactOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                 .arg(exactDuration, 0, 'f', 2)
                 .arg(copyOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                 .arg(copyDuration, 0, 'f', 2)
                 .arg(stripOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                      keyframesOk && snapOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    });
}

void AppContext::devTestPauseExcise()
{
    if (!devBuild()) return;
    pauseExciseCheck([this](const QString &result) {
        showToast(tr("Dev: pause excise: %1").arg(result),
                  result.contains(QLatin1String("FAIL")));
    });
}

void AppContext::pauseExciseCheck(std::function<void(const QString &)> done)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        done(QStringLiteral("SKIP (ffmpeg/ffprobe missing)"));
        return;
    }
    // A 3 s clip with audio; excising the middle second must yield ~2 s. This is
    // the same filtergraph the recorder runs when the user paused mid-capture.
    const QString source = QDir::temp().filePath(QStringLiteral("unisic-pausecheck-src.mkv"));
    const QString out = QDir::temp().filePath(QStringLiteral("unisic-pausecheck-out.mkv"));
    QFile::remove(source);
    QFile::remove(out);
    QStringList fixture{QStringLiteral("-y"), QStringLiteral("-nostats"),
                        QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("testsrc=size=160x120:rate=15:duration=3"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=48000:duration=3")};
    fixture << trimFixtureEncoderArgs() << QStringLiteral("-c:a") << QStringLiteral("flac")
            << QStringLiteral("-shortest") << source;
    QProcess::execute(ffmpeg, fixture);
    if (!QFileInfo::exists(source)) {
        done(QStringLiteral("FAIL (test clip)"));
        return;
    }
    const QList<QPair<qint64, qint64>> intervals{{1000, 2000}};
    QProcess::execute(ffmpeg, GifRecorder::pauseExciseArgs(source, out, intervals, true));
    if (!QFileInfo::exists(out)) {
        QFile::remove(source);
        done(QStringLiteral("FAIL (excise produced nothing)"));
        return;
    }
    QProcess p;
    p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                      QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                      QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), out});
    p.waitForFinished(2000);
    const qreal dur = QString::fromLatin1(p.readAllStandardOutput().trimmed()).toDouble();
    QFile::remove(source);
    QFile::remove(out);
    const bool ok = qAbs(dur - 2.0) < 0.3;
    done(QStringLiteral("%1 (%2s, expected ~2.0)")
             .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
             .arg(dur, 0, 'f', 2));
}

// The quality percent the user sets and the CRF ffmpeg gets are two units for
// one number. This pins the anchors and the round trip an upgraded config
// takes, so a config written before 0.8.3 keeps encoding exactly as it did.
static QString videoQualityCheck()
{
    if (UnisicVideo::crfFromPercent(0) != UnisicVideo::kCrfWorst
        || UnisicVideo::crfFromPercent(50) != 20
        || UnisicVideo::crfFromPercent(100) != 0)
        return QStringLiteral("FAIL (anchors moved)");
    for (int crf = 0; crf <= UnisicVideo::kCrfWorst; ++crf)
        if (UnisicVideo::crfFromPercent(UnisicVideo::percentFromCrf(crf)) != crf)
            return QStringLiteral("FAIL (crf %1 does not survive the migration)").arg(crf);
    int previous = UnisicVideo::crfFromPercent(0);
    for (int p = 1; p <= 100; ++p) {
        const int crf = UnisicVideo::crfFromPercent(p);
        if (crf > previous)
            return QStringLiteral("FAIL (%1%% is worse than %2%%)").arg(p).arg(p - 1);
        previous = crf;
    }
    return QStringLiteral("PASS (0%%=CRF%1, 50%%=CRF20, 100%%=CRF0, round trip exact)")
        .arg(UnisicVideo::kCrfWorst);
}

void AppContext::devTestVideoQuality()
{
    if (devBuild())
        showToast(tr("Dev: video quality scale: %1").arg(videoQualityCheck()));
}

void AppContext::devTestAudioTracks()
{
    if (!devBuild()) return;
    audioTracksCheck([this](const QString &result) {
        showToast(tr("Dev: separate audio tracks: %1").arg(result),
                  result.contains(QLatin1String("FAIL")));
    });
}

void AppContext::audioTracksCheck(std::function<void(const QString &)> done)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        done(QStringLiteral("SKIP (ffmpeg/ffprobe missing)"));
        return;
    }
    const QStringList titles{tr("Mix"), tr("System audio"), tr("Microphone")};
    const QString source = QDir::temp().filePath(QStringLiteral("unisic-tracks-src.mkv"));
    const QString excised = QDir::temp().filePath(QStringLiteral("unisic-tracks-cut.mkv"));
    const QString out = QDir::temp().filePath(QStringLiteral("unisic-tracks-out.mp4"));
    for (const QString &f : {source, excised, out})
        QFile::remove(f);

    // Two sine tones stand in for the two capture sources, named and muxed the
    // way GifRecorder::start() names and muxes them with separate tracks on:
    // a full amix as track 1 (what a player plays), the stems behind it.
    QStringList fixture{QStringLiteral("-y"), QStringLiteral("-nostats"),
                        QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("testsrc=size=160x120:rate=15:duration=2"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=880:sample_rate=48000:duration=2")};
    fixture << trimFixtureEncoderArgs()
            << QStringLiteral("-filter_complex")
            << QStringLiteral("[1:a][2:a]amix=inputs=2:duration=longest:normalize=0[mix]")
            << QStringLiteral("-map") << QStringLiteral("0:v:0")
            << QStringLiteral("-map") << QStringLiteral("[mix]")
            << QStringLiteral("-map") << QStringLiteral("1:a")
            << QStringLiteral("-map") << QStringLiteral("2:a");
    for (int i = 0; i < titles.size(); ++i)
        fixture << QStringLiteral("-metadata:s:a:%1").arg(i)
                << QStringLiteral("title=%1").arg(titles.at(i));
    fixture << QStringLiteral("-c:a") << QStringLiteral("flac")
            << QStringLiteral("-shortest") << source;
    QProcess::execute(ffmpeg, fixture);
    if (!QFileInfo::exists(source)) {
        done(QStringLiteral("FAIL (test clip)"));
        return;
    }

    // Matroska keeps a stream title; MP4 has no such field and stores the same
    // text as handler_name, so each container is probed for its own tag.
    const auto probe = [&ffprobe](const QString &file, const QString &tag) {
        QProcess p;
        p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-select_streams"), QStringLiteral("a"),
                          QStringLiteral("-show_entries"),
                          QStringLiteral("stream_tags=%1").arg(tag),
                          QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), file});
        p.waitForFinished(4000);
        return QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'),
                                                                 Qt::SkipEmptyParts);
    };

    QProcess::execute(ffmpeg, GifRecorder::pauseExciseArgs(source, excised, {{500, 1000}}, true));
    const QStringList afterExcise = QFileInfo::exists(excised)
                                        ? probe(excised, QStringLiteral("title"))
                                        : QStringList();

    QStringList convert{QStringLiteral("-y"), QStringLiteral("-nostats"),
                        QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-i"), QFileInfo::exists(excised) ? excised : source,
                        QStringLiteral("-c:v"), QStringLiteral("copy"),
                        QStringLiteral("-map"), QStringLiteral("0:v:0")};
    convert << GifRecorder::finalAudioArgs(/*webm=*/false, titles) << out;
    QProcess::execute(ffmpeg, convert);
    const QStringList afterConvert = QFileInfo::exists(out)
                                         ? probe(out, QStringLiteral("handler_name"))
                                         : QStringList();

    for (const QString &f : {source, excised, out})
        QFile::remove(f);
    const bool exciseOk = afterExcise == titles;
    const bool convertOk = afterConvert == titles;
    done(QStringLiteral("excise %1 (%2), MP4 %3 (%4)")
             .arg(exciseOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                  afterExcise.isEmpty() ? QStringLiteral("no tracks") : afterExcise.join(QStringLiteral(" + ")),
                  convertOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                  afterConvert.isEmpty() ? QStringLiteral("no tracks") : afterConvert.join(QStringLiteral(" + "))));
}

void AppContext::devTestAudioInputs()
{
    if (!devBuild()) return;
    if (!audioInputListAvailable()) {
        showToast(tr("Dev: audio inputs: %1").arg(QStringLiteral("SKIP (pw-dump missing)")));
        return;
    }
    const QVariantList devices = audioInputDevices();
    QStringList labels;
    for (const QVariant &device : devices)
        labels << device.toMap().value(QStringLiteral("label")).toString();
    showToast(tr("Dev: audio inputs: %1")
                  .arg(QStringLiteral("PASS (%1: %2)")
                           .arg(devices.size())
                           .arg(labels.join(QStringLiteral(", ")))));
}

void AppContext::devTestTrimAudio()
{
    if (!devBuild()) return;
    trimAudioCheck([this](const QString &result) {
        showToast(tr("Dev: trim audio edit: %1").arg(result),
                  result.contains(QLatin1String("FAIL")));
    });
}

void AppContext::trimAudioCheck(std::function<void(const QString &)> done)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        done(QStringLiteral("SKIP (ffmpeg/ffprobe missing)"));
        return;
    }
    const QString source = QDir::temp().filePath(QStringLiteral("unisic-trimaudio-src.mkv"));
    const QString out = QDir::temp().filePath(QStringLiteral("unisic-trimaudio-out.mp4"));
    for (const QString &f : {source, out})
        QFile::remove(f);

    QStringList fixture{QStringLiteral("-y"), QStringLiteral("-nostats"),
                        QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("testsrc=size=160x120:rate=15:duration=2"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=880:sample_rate=48000:duration=2")};
    fixture << trimFixtureEncoderArgs()
            << QStringLiteral("-map") << QStringLiteral("0:v:0")
            << QStringLiteral("-map") << QStringLiteral("1:a")
            << QStringLiteral("-map") << QStringLiteral("2:a")
            << QStringLiteral("-c:a") << QStringLiteral("flac")
            << QStringLiteral("-shortest") << source;
    QProcess::execute(ffmpeg, fixture);
    if (!QFileInfo::exists(source)) {
        done(QStringLiteral("FAIL (test clip)"));
        return;
    }

    // The exact shape trimRecording builds for an edited lossless cut: video
    // stream copy, first track dropped, second attenuated to 50%.
    QStringList cut{QStringLiteral("-y"), QStringLiteral("-nostats"),
                    QStringLiteral("-loglevel"), QStringLiteral("error"),
                    QStringLiteral("-ss"), QStringLiteral("0.000"),
                    QStringLiteral("-i"), source,
                    QStringLiteral("-t"), QStringLiteral("1.000"),
                    QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
                    QStringLiteral("-map"), QStringLiteral("0:v:0"),
                    QStringLiteral("-c:v"), QStringLiteral("copy")};
    cut << GifRecorder::audioEditArgs(/*webm=*/false, {-1.0, 0.5}) << out;
    QProcess::execute(ffmpeg, cut);

    const auto countAudio = [&ffprobe](const QString &file) {
        if (!QFileInfo::exists(file))
            return -1;
        QProcess probe;
        probe.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                              QStringLiteral("-select_streams"), QStringLiteral("a"),
                              QStringLiteral("-show_entries"), QStringLiteral("stream=index"),
                              QStringLiteral("-of"), QStringLiteral("csv=p=0"), file});
        probe.waitForFinished(4000);
        return int(QString::fromUtf8(probe.readAllStandardOutput())
                       .split(QLatin1Char('\n'), Qt::SkipEmptyParts).size());
    };
    const int editStreams = countAudio(out);

    // Remix variant: the same cut on a mix+stems recording - the mix (track 0)
    // must come back rebuilt, the muted stem gone: 2 streams survive of 3.
    const QString remixOut = QDir::temp().filePath(QStringLiteral("unisic-trimaudio-remix.mkv"));
    QFile::remove(remixOut);
    QStringList remixCut{QStringLiteral("-y"), QStringLiteral("-nostats"),
                         QStringLiteral("-loglevel"), QStringLiteral("error"),
                         QStringLiteral("-ss"), QStringLiteral("0.000"),
                         QStringLiteral("-i"), source,
                         QStringLiteral("-t"), QStringLiteral("1.000"),
                         QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
                         QStringLiteral("-map"), QStringLiteral("0:v:0"),
                         QStringLiteral("-c:v"), QStringLiteral("copy")};
    // The 2-track fixture stands in for mix+stem: track 0 as "mix", track 1
    // kept at 50% - remix output = rebuilt mix + that stem.
    remixCut << GifRecorder::audioRemixArgs(/*webm=*/false, {1.0, 0.5}, 0,
                                            QStringLiteral("Mix"))
             << remixOut;
    QProcess::execute(ffmpeg, remixCut);
    const int remixStreams = countAudio(remixOut);

    for (const QString &f : {source, out, remixOut})
        QFile::remove(f);
    const bool editOk = editStreams == 1;
    const bool remixOk = remixStreams == 2;
    done(QStringLiteral("edit %1 (%2 streams), remix %3 (%4 streams)")
             .arg(editOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
             .arg(editStreams)
             .arg(remixOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
             .arg(remixStreams));
}

void AppContext::devTestCursorCapability()
{
    if (devBuild())
        showToast(tr("Dev: screenshot cursor: %1")
                      .arg(capScreenshotCursor() ? QStringLiteral("PASS")
                                                 : QStringLiteral("SKIP")));
}

// EditShapes round-trip: place a text shape, select it, restyle + move it,
// assert the item changed and one undo restores it.
static QString shapeEditCheck()
{
    AnnotationCanvas c;
    QImage base(200, 120, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.commitText(40, 40, QStringLiteral("hi"));
    if (c.annotCount() != 1)
        return QStringLiteral("FAIL (text not placed)");
    c.setTool(AnnotationCanvas::EditShapes);
    c.selectAnnotAt(45, 45);
    if (!c.hasAnnotSelection())
        return QStringLiteral("FAIL (hit-test missed the text)");
    const QImage before = c.rendered();
    c.setStrokeColor(QColor(Qt::blue));
    c.nudgeSelectedAnnot(15, 0);
    const QImage after = c.rendered();
    if (before == after)
        return QStringLiteral("FAIL (restyle/move did not change the render)");
    c.undo(); // undoes the nudge...
    c.undo(); // ...and the color change (coalesced separately)
    if (c.annotCount() != 1)
        return QStringLiteral("FAIL (undo lost the shape)");
    return QStringLiteral("PASS (select, restyle, move, undo)");
}

void AppContext::devTestShapeEdit()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: shape edit: %1").arg(shapeEditCheck()));
}

// Capture-on-release: a synthetic selection drag must confirm exactly once on
// release with the toggle on, and never with it off (dev button + smoke step).
static QString captureOnReleaseCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    } c;
    c.setWidth(100);
    c.setHeight(100);
    QImage base(100, 100, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.setSelectionMode(true);
    c.setConfirmOnRelease(true);
    int confirms = 0;
    QObject::connect(&c, &AnnotationCanvas::selectionConfirmed, &c, [&confirms] { ++confirms; });
    const auto drag = [&c](QPointF from, QPointF to) {
        QMouseEvent p(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        c.mousePressEvent(&p);
        QMouseEvent m(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        c.mouseMoveEvent(&m);
        QMouseEvent r(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        c.mouseReleaseEvent(&r);
    };
    drag({10, 10}, {80, 60});
    if (confirms != 1)
        return QStringLiteral("FAIL (drag release confirmed %1x, expected once)").arg(confirms);
    c.setConfirmOnRelease(false);
    drag({85, 80}, {95, 95}); // outside the first selection: a fresh drag
    if (confirms != 1)
        return QStringLiteral("FAIL (confirmed with the toggle off)");
    return QStringLiteral("PASS (release confirms once; off = no confirm)");
}

void AppContext::devTestCaptureOnRelease()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: capture on release: %1").arg(captureOnReleaseCheck()));
}

// Overlay mode identity (issue #98): the overlay must be able to say which
// capture opened it. The name is what crosses into QML as the `overlayPurpose`
// context property, so every purpose needs its own, and every one of them has
// to be a string OverlayWindow.qml actually switches on - a typo on either side
// silently falls back to "screenshot" over a running recording, which is
// exactly the confusion the badge exists to end (dev button + smoke step).
static QString overlayModeCheck(Settings *settings)
{
    using P = OverlayController::Purpose;
    const struct { P purpose; const char *name; } expected[] = {
        {P::Shot, "shot"}, {P::Measure, "measure"}, {P::Ocr, "ocr"},
        {P::Gif, "gif"}, {P::Video, "video"},
    };
    QStringList seen;
    for (const auto &e : expected) {
        const QString got = OverlayController::purposeName(e.purpose);
        if (got != QLatin1String(e.name))
            return QStringLiteral("FAIL (%1 named '%2', expected '%3')")
                .arg(QLatin1String(e.name), got, QLatin1String(e.name));
        if (seen.contains(got))
            return QStringLiteral("FAIL ('%1' used by two modes)").arg(got);
        seen.append(got);
    }
    if (!settings)
        return QStringLiteral("FAIL (no settings)");
    const bool was = settings->overlayModeBadge();
    settings->setOverlayModeBadge(!was);
    const bool flipped = settings->overlayModeBadge() != was;
    settings->setOverlayModeBadge(was);
    if (!flipped || settings->overlayModeBadge() != was)
        return QStringLiteral("FAIL (the preference does not round-trip)");
    return QStringLiteral("PASS (%1; preference round-trips, left %2)")
        .arg(seen.join(QLatin1Char('/')), was ? QStringLiteral("on") : QStringLiteral("off"));
}

void AppContext::devTestOverlayMode()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: overlay mode badge: %1").arg(overlayModeCheck(settings())));
}

// The overlay-settings preview is the only place those options can be SEEN
// without starting a capture, so a preview that has drifted from the overlay is
// worse than none: it teaches the wrong thing. Two ways it can drift, both
// checked here against the real component - a capture purpose it has no entry
// for (the badge would read "Screenshot" over a recording), and a toolbar
// position whose clamp lets the bar leave the mock screen (the drop-down would
// look broken for a position that works).
QString AppContext::overlayPreviewCheck()
{
    if (!m_engine)
        return QStringLiteral("FAIL (no QML engine)");
    Settings *s = settings();
    if (!s)
        return QStringLiteral("FAIL (no settings)");

    QQmlComponent component(m_engine,
                            QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/components/UOverlayPreview.qml")));
    if (component.status() != QQmlComponent::Ready)
        return QStringLiteral("FAIL (preview %1: %2)")
            .arg(component.status() == QQmlComponent::Loading ? QStringLiteral("still loading")
                                                              : QStringLiteral("not ready"),
                 component.errorString().simplified());
    std::unique_ptr<QObject> obj(component.create());
    auto *item = qobject_cast<QQuickItem *>(obj.get());
    if (!item)
        return QStringLiteral("FAIL (preview create: %1)").arg(component.errorString().simplified());

    // A size the mock scene can be laid out in; nothing is shown on screen.
    constexpr qreal kW = 400;
    constexpr qreal kH = 200;
    item->setWidth(kW);
    item->setHeight(kH);

    const QVariantList modes = item->property("modes").toList();
    for (const auto purpose : {OverlayController::Purpose::Shot, OverlayController::Purpose::Measure,
                               OverlayController::Purpose::Ocr, OverlayController::Purpose::Gif,
                               OverlayController::Purpose::Video}) {
        const QString name = OverlayController::purposeName(purpose);
        bool found = false;
        for (const QVariant &m : modes) {
            const QVariantMap entry = m.toMap();
            if (entry.value(QStringLiteral("id")).toString() != name)
                continue;
            found = true;
            if (entry.value(QStringLiteral("label")).toString().isEmpty()
                || entry.value(QStringLiteral("iconName")).toString().isEmpty())
                return QStringLiteral("FAIL ('%1' has no label or icon)").arg(name);
            break;
        }
        if (!found)
            return QStringLiteral("FAIL (no preview entry for '%1')").arg(name);
        item->setProperty("purpose", name);
        if (!item->property("modeColor").value<QColor>().isValid())
            return QStringLiteral("FAIL ('%1' has no mode colour)").arg(name);
    }
    if (modes.size() != 5)
        return QStringLiteral("FAIL (%1 preview entries, expected 5)").arg(modes.size());

    auto *bar = item->findChild<QQuickItem *>(QStringLiteral("overlayPreviewToolbar"));
    if (!bar)
        return QStringLiteral("FAIL (no toolbar in the preview)");
    const QString wasPos = s->overlayToolbarPosition();
    const QStringList positions = {QStringLiteral("follow"),        QStringLiteral("top-left"),
                                   QStringLiteral("top-center"),    QStringLiteral("top-right"),
                                   QStringLiteral("middle-left"),   QStringLiteral("middle-center"),
                                   QStringLiteral("middle-right"),  QStringLiteral("bottom-left"),
                                   QStringLiteral("bottom-center"), QStringLiteral("bottom-right")};
    for (const QString &pos : positions) {
        s->setOverlayToolbarPosition(pos);
        if (bar->x() < 0 || bar->y() < 0 || bar->x() + bar->width() > kW
            || bar->y() + bar->height() > kH) {
            s->setOverlayToolbarPosition(wasPos);
            return QStringLiteral("FAIL ('%1' puts the toolbar at %2,%3 outside the screen)")
                .arg(pos)
                .arg(qRound(bar->x()))
                .arg(qRound(bar->y()));
        }
    }
    s->setOverlayToolbarPosition(wasPos);

    // The two mode-indicator styles: exactly one indicator is drawn for each,
    // and the glyph one has to be BOTH cut and still standing - the selection
    // is a hole in it, so a mock selection that missed it entirely would show
    // an uncut glyph and one that swallowed it whole would show nothing.
    auto *badge = item->findChild<QQuickItem *>(QStringLiteral("overlayPreviewBadge"));
    auto *glyph = item->findChild<QQuickItem *>(QStringLiteral("overlayPreviewModeIcon"));
    auto *scene = item->findChild<QQuickItem *>(QStringLiteral("overlayPreviewScene"));
    if (!badge || !glyph || !scene)
        return QStringLiteral("FAIL (no mode indicator in the preview)");
    const bool wasBadge = s->overlayModeBadge();
    const QString wasStyle = s->overlayModeStyle();
    const auto restore = [&] {
        s->setOverlayModeBadge(wasBadge);
        s->setOverlayModeStyle(wasStyle);
    };
    s->setOverlayModeBadge(true);
    for (const QString &style : {QStringLiteral("badge"), QStringLiteral("icon")}) {
        s->setOverlayModeStyle(style);
        const bool wantIcon = style == QStringLiteral("icon");
        if (badge->isVisible() == wantIcon || glyph->isVisible() != wantIcon) {
            restore();
            return QStringLiteral("FAIL ('%1' draws badge=%2 icon=%3)")
                .arg(style)
                .arg(badge->isVisible())
                .arg(glyph->isVisible());
        }
        if (!wantIcon)
            continue;
        const QRectF sel = scene->property("sel").toRectF();
        if (glyph->property("hole").toRectF() != sel) {
            restore();
            return QStringLiteral("FAIL (the glyph is not cut by the selection)");
        }
        const qreal side = glyph->property("box").toReal();
        const QRectF box(glyph->property("glyphX").toReal(), glyph->property("glyphY").toReal(),
                         side, side);
        if (!sel.intersects(box) || sel.contains(box)) {
            restore();
            return QStringLiteral("FAIL (the mock selection %1 the glyph instead of cutting it)")
                .arg(sel.intersects(box) ? QStringLiteral("swallows") : QStringLiteral("misses"));
        }
    }
    s->setOverlayModeBadge(false);
    if (badge->isVisible() || glyph->isVisible()) {
        restore();
        return QStringLiteral("FAIL (the mode indicator survives its own switch)");
    }
    restore();
    return QStringLiteral("PASS (5 modes, %1 toolbar positions on screen, 2 indicator styles)")
        .arg(positions.size());
}

void AppContext::devTestOverlayPreview()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: overlay preview: %1").arg(overlayPreviewCheck()));
}

// Magnifier round-trip: a synthetic drag over a marked source region must place
// a loupe that shows those pixels enlarged (2x, centred on the source). The
// probe point 6 px off-centre is inside the MAGNIFIED marker but outside the
// source marker, so it distinguishes a real 2x loupe from a 1:1 copy.
static QString magnifyCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    } c;
    c.setWidth(200);
    c.setHeight(200);
    QImage base(200, 200, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    {
        QPainter p(&base);
        p.fillRect(QRect(66, 66, 8, 8), QColor(220, 30, 40)); // marker at the source centre
    }
    c.setImage(base);
    c.setTool(AnnotationCanvas::Magnify);
    const auto drag = [&c](QPointF from, QPointF to) {
        QMouseEvent p(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        c.mousePressEvent(&p);
        QMouseEvent m(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        c.mouseMoveEvent(&m);
        QMouseEvent r(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        c.mouseReleaseEvent(&r);
    };
    drag({50, 50}, {90, 90});   // source = 40x40 centred on (70,70)
    if (c.annotCount() != 1)
        return QStringLiteral("FAIL (loupe not placed)");
    const QImage out = c.rendered();
    const QColor centre = out.pixelColor(70, 70);
    if (centre.red() < 150 || centre.green() > 120)
        return QStringLiteral("FAIL (loupe centre is not the magnified marker)");
    const QColor offCentre = out.pixelColor(76, 70);
    if (offCentre.red() < 150 || offCentre.green() > 120)
        return QStringLiteral("FAIL (loupe does not magnify - 2x expected)");
    return QStringLiteral("PASS (loupe placed, centred, 2x)");
}

void AppContext::devTestMagnify()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: magnifier: %1").arg(magnifyCheck()));
}

// Eyedropper tool: a click adopts the pixel under the cursor as the stroke
// colour (dev button + smoke step).
static QString eyedropperCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::mousePressEvent;
    } c;
    c.setWidth(200);
    c.setHeight(200);
    QImage base(200, 200, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    const QColor target(17, 153, 59);
    {
        QPainter p(&base);
        p.fillRect(QRect(80, 80, 20, 20), target); // a known opaque patch
    }
    c.setImage(base);
    c.setStrokeColor(Qt::white);
    c.setTool(AnnotationCanvas::Eyedropper);
    const QPointF at(90, 90); // renderScale is 1.0 in tests, so item == image
    QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    c.mousePressEvent(&press);
    const QColor got = c.strokeColor();
    if (got.red() != target.red() || got.green() != target.green() || got.blue() != target.blue())
        return QStringLiteral("FAIL (picked %1, expected %2)").arg(got.name(), target.name());
    return QStringLiteral("PASS (stroke colour adopted from pixel)");
}

void AppContext::devTestEyedropper()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: eyedropper: %1").arg(eyedropperCheck()));
}

// Pixel loupe (region overlay): the panel must appear once the pointer hovers
// in selection mode, flip away from the item edges so it never covers the
// pixels being aimed at, zoom one step per scroll notch within its 5–16 range,
// collapse when scrolled out below the minimum (and revive on scroll up), and
// stay out of the exported render (dev button + smoke step).
static QString pixelLoupeCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::hoverMoveEvent;
        using AnnotationCanvas::wheelEvent;
    } c;
    c.setWidth(500);
    c.setHeight(400);
    QImage base(500, 400, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.setSelectionMode(true);
    c.setPixelLoupe(true);
    c.setPixelLoupeZoom(8);
    if (!c.pixelLoupeRect().isEmpty())
        return QStringLiteral("FAIL (loupe visible before any hover)");
    const auto hover = [&c](QPointF at) {
        QHoverEvent e(QEvent::HoverMove, at, at, at);
        c.hoverMoveEvent(&e);
    };
    hover({50, 50});
    const QRectF nearOrigin = c.pixelLoupeRect();
    if (nearOrigin.isEmpty() || nearOrigin.left() <= 50 || nearOrigin.top() <= 50)
        return QStringLiteral("FAIL (loupe not offset below-right of the cursor)");
    hover({490, 390});
    const QRectF nearCorner = c.pixelLoupeRect();
    if (nearCorner.isEmpty() || nearCorner.right() >= 490 || nearCorner.bottom() >= 390)
        return QStringLiteral("FAIL (loupe does not flip away from the item edge)");
    const auto wheel = [&c](int delta) {
        QWheelEvent e(c.hoverPoint(), c.hoverPoint(), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        c.wheelEvent(&e);
    };
    wheel(120);
    if (c.pixelLoupeZoom() != 9)
        return QStringLiteral("FAIL (scroll did not raise the zoom by one)");
    for (int i = 0; i < 20; ++i)
        wheel(-120);
    if (c.pixelLoupeZoom() != 5)
        return QStringLiteral("FAIL (zoom did not clamp at 5)");
    if (!c.pixelLoupeRect().isEmpty())
        return QStringLiteral("FAIL (loupe did not collapse when scrolled out)");
    wheel(120);
    if (c.pixelLoupeRect().isEmpty())
        return QStringLiteral("FAIL (scroll up did not revive the collapsed loupe)");
    if (c.rendered() != base)
        return QStringLiteral("FAIL (loupe leaked into the exported render)");
    return QStringLiteral("PASS (follows hover, edge flip, scroll 5-16, collapse/revive, not exported)");
}

void AppContext::devTestPixelLoupe()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: pixel loupe: %1").arg(pixelLoupeCheck()));
}

// Applies both OCR text-selection actions to real Tesseract glyph geometry.
// The canvas API itself owns the per-line batching; this harness verifies the
// action leaves exportable annotations and still has exactly one undo step.
static QString ocrHighlightCheck(const QVector<OcrWord> &words)
{
    if (words.isEmpty())
        return QStringLiteral("FAIL (no glyphs)");
    AnnotationCanvas canvas;
    const QImage base = ocrBoxTestImage();
    canvas.setImage(base);
    canvas.setOcrMode(true);
    canvas.setOcrWords(words);
    canvas.ocrSelectAll();
    if (!canvas.highlightOcrSelection() || canvas.annotCount() == 0)
        return QStringLiteral("FAIL (highlight not added)");
    const int highlighted = canvas.annotCount();
    canvas.undo();
    if (canvas.annotCount() != 0)
        return QStringLiteral("FAIL (highlight batch needs more than one undo)");
    canvas.redo();
    if (canvas.annotCount() != highlighted)
        return QStringLiteral("FAIL (highlight redo)");
    // highlightOcrSelection deliberately leaves OCR mode (it turns the transient
    // text selection into permanent marks), which clears the words + selection.
    // A second OCR action is a fresh pick, so re-enter and re-select before
    // redacting — exactly what the UI does for a new selection.
    canvas.setOcrMode(true);
    canvas.setOcrWords(words);
    canvas.ocrSelectAll();
    if (!canvas.redactOcrSelection(false) || canvas.annotCount() <= highlighted)
        return QStringLiteral("FAIL (redaction not added)");
    if (canvas.rendered() == base)
        return QStringLiteral("FAIL (annotations missing from export)");
    return QStringLiteral("PASS (%1 highlight bars + redaction, one undo)").arg(highlighted);
}

// Pattern redaction over real Tesseract geometry. The fixture's glyphs are
// digits, so the "long numbers" preset must black them out with no selection
// made at all, while the e-mail preset must leave them alone — a pattern that
// matched everything would be indistinguishable from a broken one here.
static QString ocrRedactPatternCheck(const QVector<OcrWord> &words)
{
    if (words.isEmpty())
        return QStringLiteral("FAIL (no glyphs)");
    AnnotationCanvas canvas;
    const QImage base = ocrBoxTestImage();
    canvas.setImage(base);
    canvas.setOcrMode(true);
    canvas.setOcrWords(words);
    // Deliberately no ocrSelectAll(): redacting WITHOUT a selection is the path.
    if (canvas.redactTextMatching(QStringLiteral("([")) != -1)
        return QStringLiteral("FAIL (invalid pattern not rejected)");
    if (canvas.redactTextMatching(QStringLiteral("[\\w.%+-]+@[\\w.-]+\\.[A-Za-z]{2,}")) != 0)
        return QStringLiteral("FAIL (e-mail pattern matched digits)");
    if (canvas.annotCount() != 0)
        return QStringLiteral("FAIL (non-matching pattern still drew a bar)");
    const int n = canvas.redactTextMatching(QStringLiteral("\\d[\\d -]{5,}\\d"));
    if (n <= 0 || canvas.annotCount() == 0)
        return QStringLiteral("FAIL (long-number pattern found nothing)");
    canvas.undo();
    if (canvas.annotCount() != 0)
        return QStringLiteral("FAIL (pattern batch needs more than one undo)");
    canvas.redo();
    if (canvas.rendered() == base)
        return QStringLiteral("FAIL (redaction missing from export)");
    return QStringLiteral("PASS (%1 match(es) redacted, no selection, one undo)").arg(n);
}

// Style presets are stored as one JSON string in a settings key. The QML side
// owns the schema, but the STORAGE is the part that can fail silently: INI
// treats an unquoted comma-bearing value as a QStringList, and a preset object
// is nothing but commas. Round-trip a realistic payload through the real
// Settings object and compare byte for byte.
static QString stylePresetsCheck(Settings *s)
{
    if (!s)
        return QStringLiteral("FAIL (no settings)");
    const QString saved = s->editorStylePresets();
    const QString payload = QStringLiteral(
        "[{\"stroke\":\"#ff4757\",\"width\":4,\"fontFamily\":\"Noto Sans, Bold\","
        "\"fillOn\":false,\"hlMode\":2}]");
    s->setEditorStylePresets(payload);
    const QString back = s->editorStylePresets();
    s->setEditorStylePresets(saved); // never clobber the user's real presets
    if (back != payload)
        return QStringLiteral("FAIL (round-trip mangled: %1)").arg(back);
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(back.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray() || doc.array().size() != 1)
        return QStringLiteral("FAIL (not parseable back into one preset)");
    return QStringLiteral("PASS (JSON survives the INI round-trip)");
}

// The cursor overlay, end to end minus the compositor: paint a pointer, a halo
// and a click ripple into a frame-shaped buffer and prove the pixels changed.
// This is the part that silently produces a cursor-less recording if it breaks
// — in metadata cursor mode nothing else draws the pointer.
static QString cursorOverlayCheck()
{
    const int w = 200, h = 120;
    QImage frame(w, h, QImage::Format_RGB32);   // same wrap the recorder uses for "bgr0"
    frame.fill(Qt::black);
    const QImage clean = frame;

    CursorOverlayPainter painter;
    CursorOverlayPainter::Style style;
    style.highlight = true;
    style.ripple = true;
    painter.setStyle(style);

    const qint64 now = 1000000000LL;
    // No position yet: painting must be a no-op rather than stamp the origin.
    {
        QPainter p(&frame);
        painter.paint(p, now);
    }
    if (frame != clean)
        return QStringLiteral("FAIL (drew a cursor before one was known)");

    painter.setCursor(QPointF(w / 2, h / 2), /*visible=*/true, /*shapeId=*/0);
    {
        QPainter p(&frame);
        painter.paint(p, now);
    }
    if (frame == clean)
        return QStringLiteral("FAIL (pointer + halo drew nothing)");
    // The fallback pointer must appear even with no bitmap from the compositor.
    if (frame.pixelColor(w / 2 + 1, h / 2 + 4) == QColor(Qt::black))
        return QStringLiteral("FAIL (no pointer where the cursor is)");

    // A click ripple must expire on its own, or a long recording accumulates them.
    painter.addClick(now);
    if (!painter.hasContent(now))
        return QStringLiteral("FAIL (ripple not live at t=0)");
    const qint64 afterMs = now + qint64(style.rippleMs + 50) * 1000000LL;
    QImage expired(w, h, QImage::Format_RGB32);
    expired.fill(Qt::black);
    {
        // Cursor hidden + halo/pointer off: the only thing left that could draw
        // is the ripple, so an unchanged frame proves it really expired.
        CursorOverlayPainter rippleOnly;
        CursorOverlayPainter::Style s;
        s.highlight = false;
        s.drawCursor = false;
        rippleOnly.setStyle(s);
        rippleOnly.setCursor(QPointF(w / 2, h / 2), true, 0);
        rippleOnly.addClick(now);
        if (rippleOnly.hasContent(afterMs))
            return QStringLiteral("FAIL (ripple outlived its lifetime)");
        QPainter p(&expired);
        rippleOnly.paint(p, afterMs);
    }
    if (expired != clean)
        return QStringLiteral("FAIL (expired ripple still painted)");

    // Premultiplied-alpha fidelity: cursor bitmaps arrive premultiplied (Wayland
    // ARGB8888 and the XCursor themes both store them that way). A half-opaque
    // WHITE edge pixel is therefore (128,128,128,128). If any stage treats it as
    // straight alpha it gets premultiplied a second time — the classic soft/dark
    // cursor edge. Composite it over black and check the visible result did not
    // darken past what a single, correct premultiply gives (~128, i.e. white*0.5).
    {
        QImage frame2(w, h, QImage::Format_RGB32);
        frame2.fill(Qt::black);
        QImage cur(4, 4, QImage::Format_ARGB32_Premultiplied);
        cur.fill(qRgba(128, 128, 128, 128)); // premultiplied half-opaque white
        CursorOverlayPainter cp;
        CursorOverlayPainter::Style cs;
        cs.highlight = false;
        cs.ripple = false;
        cp.setStyle(cs);
        cp.setShape(1, cur, QPoint(0, 0));
        cp.setCursor(QPointF(w / 2, h / 2), true, 1);
        {
            QPainter p(&frame2);
            cp.paint(p, now);
        }
        const int v = frame2.pixelColor(w / 2, h / 2).red();
        // Correct: ~128. Double-premultiplied would land near 64.
        if (v < 110)
            return QStringLiteral("FAIL (cursor alpha double-premultiplied: %1, want ~128)").arg(v);
    }

    const bool meta = ScreenCastSession::availableCursorModes()
                      & uint(ScreenCastSession::CursorMode::Metadata);
    return QStringLiteral("PASS (portal metadata cursor: %1, clicks: %2)")
        .arg(meta ? QStringLiteral("yes") : QStringLiteral("NO - would fall back to embedded"),
             InputPermission::probe() == InputPermission::Available
                 ? QStringLiteral("yes") : QStringLiteral("no (needs the input group)"));
}

// Loads every bundled non-English .qm and checks a known string translates.
static QString languageCheck()
{
#ifdef HAVE_TRANSLATIONS
    const QStringList codes = {QStringLiteral("pl"), QStringLiteral("es"), QStringLiteral("it"),
                                QStringLiteral("fr"), QStringLiteral("ru"),
                                QStringLiteral("de")};
    QStringList parts;
    for (const QString &c : codes) {
        QTranslator tr;
        if (!tr.load(QStringLiteral(":/i18n/unisic_%1.qm").arg(c)))
            return QStringLiteral("FAIL (unisic_%1.qm not loadable)").arg(c);
        const QString q = tr.translate("AppContext", "Quit");
        if (q.isEmpty() || q == QLatin1String("Quit"))
            return QStringLiteral("FAIL ('Quit' not translated in %1)").arg(c);
        parts << QStringLiteral("%1: '%2'").arg(c, q);
    }
    return QStringLiteral("PASS (Quit → %1)").arg(parts.join(QStringLiteral(", ")));
#else
    return QStringLiteral("SKIP (built without Qt LinguistTools)");
#endif
}

void AppContext::devTestLanguage()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: language: %1").arg(languageCheck()));
}

void AppContext::devTestUpdateCheck()
{
    if (!devBuild())
        return;
    // manual=true: visible errors, no toast/once-per-version bookkeeping.
    m_updater->check(true, [this](const UpdateChecker::Result &r) {
        showToast(tr("Dev: update check: %1")
                      .arg(r.ok ? QStringLiteral("PASS (latest %1 - %2)")
                                      .arg(r.latestVersion.isEmpty() ? QStringLiteral("none")
                                                                     : r.latestVersion,
                                           r.updateAvailable ? QStringLiteral("update available")
                                                             : QStringLiteral("up to date"))
                                : QStringLiteral("FAIL (%1)").arg(r.error)),
                  !r.ok);
    });
}

void AppContext::devTestUpdateAvailable()
{
    if (!devBuild())
        return;
    // Fake "update available" state: exercises the toast, the tray entry and
    // the Settings → General card without a newer release existing. "Update
    // now" then fails gracefully (no asset) unless UNISIC_UPDATE_FEED_URL
    // points at a fake feed.
    m_updater->simulateAvailable(QStringLiteral("99.0"));
}

void AppContext::devTestAutoRestart()
{
    if (!devBuild())
        return;
    const QString b = autoRestartBlockers();
    showToast(b.isEmpty() ? tr("Dev: auto-restart gate: idle - an installed update would restart now")
                          : tr("Dev: auto-restart gate: deferred (%1)").arg(b));
}

void AppContext::devTestInstallerUpdate()
{
    if (!devBuild())
        return;
    // Dry-run only: fetches + validates install.sh and detects a terminal, but
    // never spawns one or installs anything (a dev build can't self-install).
    // installKind() is "system" && dev, so canInstallViaScript is false here —
    // the dry-run deliberately ignores that gate to exercise the machinery.
    showToast(tr("Dev: installer update: checking…"));
    m_updater->verifyInstallerReady([this](bool ok, const QString &detail) {
        showToast(ok ? tr("Dev: installer update: PASS (%1)").arg(detail)
                     : tr("Dev: installer update: FAIL (%1)").arg(detail),
                  !ok);
    });
}

void AppContext::devTestOcrBoxes()
{
    if (!devBuild())
        return;
#ifdef HAVE_TESSERACT
    ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
        if (!err.isEmpty())
            showToast(tr("Dev: OCR boxes: FAIL (%1)").arg(err), true);
        else
            showToast(tr("Dev: OCR boxes: %1 (%2 glyphs)")
                          .arg(words.size() >= 4 ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                          .arg(words.size()));
    });
#else
    showToast(tr("Dev: OCR boxes: SKIP (built without tesseract)"));
#endif
}

void AppContext::devTestOcrHighlight()
{
    if (!devBuild())
        return;
#ifdef HAVE_TESSERACT
    ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
        showToast(!err.isEmpty() ? tr("Dev: OCR highlight + redact: FAIL (%1)").arg(err)
                                 : tr("Dev: OCR highlight + redact: %1").arg(ocrHighlightCheck(words)),
                  !err.isEmpty());
    });
#else
    showToast(tr("Dev: OCR highlight + redact: SKIP (built without tesseract)"));
#endif
}

void AppContext::devTestCursorOverlay()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: cursor overlay: %1").arg(cursorOverlayCheck()));
}

void AppContext::devTestStylePresets()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: style presets: %1").arg(stylePresetsCheck(m_settings)));
}

void AppContext::devTestOcrRedactPattern()
{
    if (!devBuild())
        return;
#ifdef HAVE_TESSERACT
    ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
        showToast(!err.isEmpty() ? tr("Dev: auto-redact pattern: FAIL (%1)").arg(err)
                                 : tr("Dev: auto-redact pattern: %1").arg(ocrRedactPatternCheck(words)),
                  !err.isEmpty());
    });
#else
    showToast(tr("Dev: auto-redact pattern: SKIP (built without tesseract)"));
#endif
}

void AppContext::devTestOcrAutoLang()
{
    if (!devBuild())
        return;
#ifdef HAVE_TESSERACT
    const QString detected = OcrEngine::detectedLanguages();
    // Pin the script→langpack mapping deterministically (no OSD traineddata
    // needed): a distinct script narrows to its pack + eng, Latin/Cyrillic keep
    // the full set. Uses a synthetic install list so the result is stable.
    const QString avail = QStringLiteral("eng+pol+ara+jpn+heb+rus");
    struct { const char *script; const char *want; } cases[] = {
        {"Arabic", "eng+ara"}, {"Japanese", "eng+jpn"}, {"Hebrew", "eng+heb"},
        {"Han", "eng+jpn"} /* no chi installed → nothing; falls to "" */,
        {"Latin", ""}, {"Cyrillic", ""},
    };
    QString mapErr;
    for (const auto &c : cases) {
        const QString got = OcrEngine::languagesForScript(QLatin1String(c.script), avail);
        // Han with no chi_* installed → "" (nothing to narrow to); accept that.
        const QString want = (QLatin1String(c.script) == QLatin1String("Han"))
                                 ? QString() : QString::fromLatin1(c.want);
        if (got != want) {
            mapErr = QStringLiteral("%1→'%2' (want '%3')")
                         .arg(QLatin1String(c.script), got, want);
            break;
        }
    }
    const QString osd = OcrEngine::scriptDetectionAvailable()
                            ? QStringLiteral("OSD ready") : QStringLiteral("no osd.traineddata");
    const bool ok = !detected.isEmpty() && mapErr.isEmpty();
    showToast(tr("Dev: OCR auto language: %1 (installed: %2; %3; map: %4)")
                  .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                       detected.isEmpty() ? QStringLiteral("none") : detected, osd,
                       mapErr.isEmpty() ? QStringLiteral("ok") : mapErr),
              !ok);
#else
    showToast(tr("Dev: OCR auto language: SKIP (built without tesseract)"));
#endif
}

void AppContext::devTestZipExport()
{
    if (!devBuild())
        return;
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
        showToast(tr("Dev: ZIP export: SKIP (zip not installed)"));
        return;
    }
    const QString dir = QDir::tempPath();
    QStringList files;
    for (int i = 1; i <= 2; ++i) {
        QImage im(64, 64, QImage::Format_ARGB32);
        im.fill(i == 1 ? Qt::red : Qt::blue);
        const QString p = dir + QStringLiteral("/unisic-zip-test-%1.png").arg(i);
        im.save(p, "PNG");
        files << p;
    }
    const QString dest = dir + QStringLiteral("/unisic-zip-test.zip");
    exportFilesToZip(files, dest, [this, files, dest](bool ok, const QString &msg) {
        showToast(tr("Dev: ZIP export: %1")
                      .arg(ok ? tr("PASS (%1)").arg(msg) : tr("FAIL (%1)").arg(msg)),
                  !ok);
        for (const QString &f : files)
            QFile::remove(f);
        QFile::remove(dest);
    });
}

void AppContext::devTestCaptureSound()
{
    if (!devBuild())
        return;
    playCaptureSound();
    showToast(tr("Dev: played capture sound '%1'").arg(m_settings->captureSound()));
}

void AppContext::devTestRecordingSound()
{
    if (!devBuild())
        return;
    playRecordingSound();
    showToast(tr("Dev: played recording sound '%1'").arg(m_settings->recordingSound()));
}

void AppContext::devTestRecordStartSound()
{
    if (!devBuild())
        return;
    playRecordStartSound();
    showToast(tr("Dev: played record-start sound '%1'").arg(m_settings->recordStartSound()));
}

void AppContext::devTestTrashSound()
{
    if (!devBuild())
        return;
    playTrashSound();
    showToast(tr("Dev: played the fixed trash sound"));
}

void AppContext::devTestCountdown()
{
    if (!devBuild())
        return;
    if (m_settings->recordCountdownSec() <= 0) {
        showToast(tr("Dev: countdown is 0s (off) - set it in Recording settings"));
        return;
    }
    // Exercise the real in-frame countdown: set a centered test region (same as
    // the record-border test) so startRecorderCountdown pops the frame with the
    // number ticking inside it, then tears it down at the end.
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen && capRecordBorder()) {
        const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        const int pw = qRound(screen->geometry().width() * dpr);
        const int ph = qRound(screen->geometry().height() * dpr);
        m_pendingRecordRegion = QRect(pw * 3 / 10, ph * 3 / 10, pw * 2 / 5, ph * 2 / 5);
        m_pendingRecordScreen = screen;
    }
    // Dev: no real recorder/portal, so armed() never fires — drive the countdown
    // visuals directly (commit() no-ops with no recording), then tear the demo
    // frame down a moment after the countdown + start-cue tail.
    const int secs = qBound(1, m_settings->recordCountdownSec(), 10);
    m_recordHoldActive = true; // commitRecordingAfterCue clears it
    runRecordCountdownVisuals(secs);
    QTimer::singleShot(secs * 1000 + 1200, this, [this]() {
        if (!recording())
            hideRecordBorder();
        showToast(tr("Dev: countdown finished - recording would start now"));
    });
}

void AppContext::devTestFullscreenCountdown()
{
    if (!devBuild())
        return;
    if (m_settings->recordCountdownSec() <= 0) {
        showToast(tr("Dev: countdown is 0s (off) - set it in Recording settings"));
        return;
    }
    // The full-screen / window path: NO pending region, so runRecordCountdownVisuals
    // takes the countdownOnly branch (big number centered on the screen, torn down
    // when it hits 0). Mirrors what startVideoScreen/startGifFullScreen produce.
    m_pendingRecordRegion = QRect();
    m_pendingRecordScreen = nullptr;
    const int secs = qBound(1, m_settings->recordCountdownSec(), 10);
    m_recordHoldActive = true;
    runRecordCountdownVisuals(secs);
    const bool overlay = (m_recordBorderWindow != nullptr || m_recordBorderHelper != nullptr);
    QTimer::singleShot(secs * 1000 + 1200, this, [this, overlay]() {
        if (!recording())
            hideRecordBorder();
        showToast(overlay
                      ? tr("Dev: full-screen countdown finished - recording would start now")
                      : tr("Dev: full-screen countdown fell back to a toast (no record-border support)"),
                  !overlay);
    });
}

void AppContext::devTestSaveDialog()
{
    if (!devBuild())
        return;
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(0x2E, 0x23, 0x6C));
    const QString chosen = QFileDialog::getSaveFileName(
        nullptr, tr("Save capture (dev test)"),
        m_settings->saveDirectory() + QLatin1Char('/') + makeFileName(),
        tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (chosen.isEmpty()) {
        showToast(tr("Dev: save dialog cancelled"));
        return;
    }
    const QFileInfo fi(chosen);
    const QString path = saveImageTo(img, fi.absolutePath(), fi.fileName());
    showToast(path.isEmpty() ? tr("Dev: save FAILED")
                             : tr("Dev: saved to %1").arg(path),
              path.isEmpty());
}

void AppContext::devTestFilename()
{
    if (!devBuild())
        return;
    QString dir = m_settings->saveDirectory();
    if (m_settings->dateSubfolders())
        dir += QLatin1Char('/')
             + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM"));
    showToast(tr("Dev: next file = %1/%2 (counter=%3, subfolders=%4, stripMeta=%5)")
                  .arg(dir, makeFileName())
                  .arg(m_settings->filenameCounter())
                  .arg(m_settings->dateSubfolders() ? tr("on") : tr("off"),
                       m_settings->stripMetadata() ? tr("on") : tr("off")));
}

void AppContext::devTestCardPreview()
{
    if (!devBuild())
        return;
    if (!m_settings->showCapturePopup() || !m_settings->showNotifications()) {
        showToast(tr("Dev: card preview needs the stylized card enabled "
                     "(Preferences → Show notifications / capture card)"), true);
        return;
    }
    previewCapturePopup();
    if (!m_previewNotif) {
        showToast(tr("Dev: card preview FAILED (no card was created)"), true);
        return;
    }
    showToast(tr("Dev: card preview - withdrawing in 3 s"));
    QTimer::singleShot(3000, this, [this] { hideCapturePopupPreview(); });
}

void AppContext::devTestNotification()
{
    if (!devBuild())
        return;
    // Full parity with a real capture — including the gates. A dev test that
    // bypassed them "worked" while every real capture card was suppressed,
    // which made the suppression look like a notification bug. Explain instead.
    if (!m_settings->showNotifications()) {
        showToast(tr("Dev: ALL notifications are disabled in Settings "
                     "(Preferences → Show notifications)"), true);
        return;
    }
    if (!m_settings->showCapturePopup())
        showToast(tr("Dev: stylized cards are off - falling back to a native "
                     "desktop notification"));
    const bool inhibited = nowInhibited();
    if (inhibited && m_settings->muteOnFullscreen())
        showToast(tr("Dev: cards are currently muted (fullscreen / Do Not Disturb "
                     "inhibition is active)"), true);
    showCaptureNotification(devTestImage(), QString(), QStringLiteral("image"), inhibited);
}

void AppContext::devTestNotificationOrder()
{
    if (!devBuild())
        return;
    if (!m_settings->showCapturePopup() || !m_settings->showNotifications()) {
        devTestCardPreview();
        return;
    }

    // The preview override exercises the exact settings snapshot and host path
    // a real card uses, but never dirties the user's persisted order.
    previewCapturePopup({
        {QStringLiteral("notificationActionOrder"),
         QStringLiteral("folder,upload,copy,edit,link,qr,ocr,trim,delete")},
        {QStringLiteral("hiddenNotifActions"), QString()},
        {QStringLiteral("capturePopupDurationSec"), 6},
    });
    QTimer::singleShot(6000, this, [this] { hideCapturePopupPreview(); });
}

void AppContext::devTestEditor()
{
    if (!devBuild())
        return;
    openEditor(devTestImage());
}

void AppContext::devTestHistory()
{
    if (!devBuild())
        return;
    m_history->addEntry(QString(), devTestImage(), QStringLiteral("image"));
    showToast(tr("Dev: added a test history entry"));
}

void AppContext::devTestFavoriteHistory()
{
    if (!devBuild())
        return;
    m_history->addEntry(QString(), devTestImage(), QStringLiteral("image"));
    m_history->setFavorite(0, true);
    showToast(tr("Dev: added a STARRED history entry; try Clear all / delete on it"));
}

void AppContext::devTestEditFromHistory()
{
    if (!devBuild())
        return;
    // Persist a throwaway image, register it in history, then open it in the
    // overwrite editor — the exact path the History "Edit" button drives.
    const QString p = saveImageAuto(devTestImage(), QStringLiteral("devtest-edit.png"));
    if (p.isEmpty()) {
        showToast(tr("Dev: couldn't save the test image"), true);
        return;
    }
    m_history->addEntry(p, devTestImage(), QStringLiteral("image"));
    editFromHistory(p);
}

void AppContext::devTestHistoryDrag()
{
    if (!devBuild())
        return;
    // The drag payload is built entirely by fileDragUri(); the QML drag gesture
    // itself can't be driven headlessly, so assert the uri-list string a drop
    // target would receive (spaces percent-encoded, empty path → empty).
    const QString uri = fileDragUri(QStringLiteral("/tmp/unisic drag test.png"));
    const bool ok = uri == QStringLiteral("file:///tmp/unisic%20drag%20test.png")
                    && fileDragUri(QString()).isEmpty();
    showToast(tr("Dev: history drag payload: %1")
                  .arg(ok ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL (%1)").arg(uri)));
}

void AppContext::devTestNotificationDrag()
{
    if (!devBuild())
        return;
    // What the notification thumbnail hands a drop target: a saved capture drags
    // its real file (spaces percent-encoded); an unsaved one materializes a
    // private temp PNG on demand. The QML drag gesture can't run headlessly, so
    // assert only the payload. Stack notifications clean up their files on scope
    // exit (thumb + any temp drag file), so this leaves nothing behind.
    CaptureNotification saved(this, devTestImage(),
                              QStringLiteral("/tmp/unisic drag test.png"),
                              QStringLiteral("image"));
    const bool savedOk =
        saved.dragUri() == QStringLiteral("file:///tmp/unisic%20drag%20test.png");

    CaptureNotification unsaved(this, devTestImage(), QString(), QStringLiteral("image"));
    const QUrl du(unsaved.dragUri());
    const bool unsavedOk = du.isLocalFile() && QFile::exists(du.toLocalFile());

    const bool ok = savedOk && unsavedOk;
    showToast(tr("Dev: notification drag payload: %1")
                  .arg(ok ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL (saved=%1 unsaved=%2)")
                                .arg(savedOk).arg(unsavedOk)),
              !ok);
}

void AppContext::devTestCopyLast()
{
    if (!devBuild())
        return;
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    devTestImage().save(&buf, "PNG");
    m_lastCaptureData = png;
    copyLastCapture();
    const bool ok = !QGuiApplication::clipboard()->image().isNull();
    showToast(tr("Dev: copy last capture: %1")
                  .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL (clipboard empty)")), !ok);
}

void AppContext::devTestClipboardHistory()
{
    if (!devBuild())
        return;
    // Live side effect first: on Plasma the image should now appear in the
    // clipboard applet's history (issue #51). Hand-checkable in Klipper.
    copyImageToClipboard(devTestImage());
    const QString status = clipboardHistoryHintCheck();
    showToast(tr("Dev: Klipper clipboard history: %1").arg(status),
              status.startsWith(QLatin1String("FAIL")));
}

void AppContext::devTestDiagLog()
{
    if (!devBuild())
        return;
    // Writes through the real handler, so this exercises redaction, the ring,
    // the file and the child-tag path exactly as a live run would.
    qWarning() << "Dev: diagnostic log check, Authorization: Bearer devtoken123 in"
               << QDir::homePath();
    DiagLog::appendRaw(QStringLiteral("dev"), QStringLiteral("synthetic helper line"));
    const QString tail = DiagLog::recentLines(4);
    const bool leaked = tail.contains(QStringLiteral("devtoken123"))
                        || tail.contains(QDir::homePath());
    const QString where = DiagLog::logFilePath().isEmpty()
                              ? tr("memory only")
                              : DiagLog::logFilePath();
    showToast(leaked ? tr("Dev: log FAILED to redact a secret")
                     : tr("Dev: log OK (%1 lines) - %2")
                           .arg(DiagLog::bufferedLineCount())
                           .arg(where),
              leaked);
}

void AppContext::devTestCrashReport()
{
    if (!devBuild())
        return;
    // Renders the REAL report through the same writer the signal handler uses,
    // into a temp file, without raising anything: the point is to check the
    // shape of what a user would paste, not to kill the app to get one.
    QTemporaryFile f;
    f.setAutoRemove(false);
    if (!f.open()) {
        showToast(tr("Dev: crash report: could not open a temp file"), true);
        return;
    }
    CrashHandler::devWriteSyntheticReport(f.handle(), SIGSEGV);
    f.flush();
    f.seek(0);
    const QString text = QString::fromUtf8(f.readAll());
    const bool ok = text.contains(QLatin1String("=== unisic crash report ==="))
                    && text.contains(QLatin1String("SIGSEGV"))
                    && text.contains(QLatin1String("backtrace"))
                    && text.count(QLatin1Char('\n')) > 6;
    showInFileManager(f.fileName());
    showToast(ok ? tr("Dev: crash report renders - opened it in the file manager")
                 : tr("Dev: crash report is malformed"),
              !ok);
}

void AppContext::devTestPreview()
{
    if (!devBuild())
        return;
    openPreview(devTestImage());
}

void AppContext::devTestShowInFolder()
{
    if (!devBuild())
        return;
    // Hand-checkable: the file manager should open the cache folder with this
    // file SELECTED (FileManager1), not merely show the folder (the fallback).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/unisic-dev-show-in-folder.png");
    devTestImage().save(path);
    showInFileManager(path);
    showToast(tr("Dev: show in folder: check the file is selected in the file manager"));
}

void AppContext::devTestKWinRecord()
{
    if (!devBuild())
        return;
#ifdef HAVE_KWIN_SCREENCAST
    if (!capKWinRecord()) {
        showToast(tr("Dev: KWin record: interface not granted (desktop file / not KWin)"), true);
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    auto *stream = kwinScreencastProbe()->createOutputStream(
        screen, KWinScreencasting::Embedded);
    if (!stream) {
        showToast(tr("Dev: KWin record: stream request failed"), true);
        return;
    }
    connect(stream, &KWinScreencastStream::created, this, [this, stream](quint32 node) {
        showToast(tr("Dev: KWin record OK - PipeWire node %1, no portal dialog").arg(node));
        stream->deleteLater(); // closing the object ends the cast
    });
    connect(stream, &KWinScreencastStream::failed, this, [this, stream](const QString &e) {
        showToast(tr("Dev: KWin record failed: %1").arg(e), true);
        stream->deleteLater();
    });
#else
    showToast(tr("Dev: KWin record: not built (needs qt6-qtwayland-devel + plasma-wayland-protocols)"), true);
#endif
}

void AppContext::devTestRecordBorder()
{
    if (!devBuild())
        return;
    if (!capRecordBorder()) {
        showToast(tr("Dev: record border: unsupported on this compositor"), true);
        return;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    // Centered region ≈ 40% of the primary screen, in physical pixels — the
    // same unit a real region recording hands to showRecordBorder().
    const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
    const int pw = qRound(screen->geometry().width() * dpr);
    const int ph = qRound(screen->geometry().height() * dpr);
    showRecordBorder(QRect(pw * 3 / 10, ph * 3 / 10, pw * 2 / 5, ph * 2 / 5), screen);
    const bool up = m_recordBorderWindow || m_recordBorderHelper;
    showToast(up ? tr("Dev: record border shown for 4 s")
                 : tr("Dev: record border FAILED to show"), !up);
    QTimer::singleShot(4000, this, [this] {
        if (!recording()) // a real region recording may own the frame by now
            hideRecordBorder();
    });
}

void AppContext::devTestPreviewFromHistory()
{
    if (!devBuild())
        return;
    // Same path the History pin button drives: file on disk -> preview.
    const QString p = saveImageAuto(devTestImage(), QStringLiteral("devtest-preview.png"));
    if (p.isEmpty()) {
        showToast(tr("Dev: couldn't save the test image"), true);
        return;
    }
    m_history->addEntry(p, devTestImage(), QStringLiteral("image"));
    previewFromHistory(p);
}

QString AppContext::settingsRoundTripCheck()
{
    // Export -> parse -> verify every writable Settings property serialized ->
    // import the file back. Importing the just-exported effective config is a
    // no-op for values while exercising the whole read path.
    // QTemporaryFile: unique name + 0600 — the export embeds destination
    // secrets (API keys), which must not sit world-readable in shared /tmp.
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/unisic-smoketest-XXXXXX.json"));
    if (!tmp.open())
        return QStringLiteral("FAIL (cannot create a temp file)");
    const QString path = tmp.fileName();
    tmp.close(); // exportSettings rewrites it in place; 0600 perms survive
    const QString exportErr = exportSettings(QUrl::fromLocalFile(path));
    if (!exportErr.isEmpty())
        return QStringLiteral("FAIL (export: %1)").arg(exportErr);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("FAIL (cannot re-read %1)").arg(path);
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonObject s = root.value(QStringLiteral("settings")).toObject();
    QStringList missing;
    const QMetaObject *mo = m_settings->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (p.isWritable() && !s.contains(QString::fromLatin1(p.name())))
            missing << QString::fromLatin1(p.name());
    }
    if (!missing.isEmpty())
        return QStringLiteral("FAIL (not serialized: %1)").arg(missing.join(QLatin1String(", ")));

    const QString importErr = importSettings(QUrl::fromLocalFile(path));
    if (!importErr.isEmpty())
        return QStringLiteral("FAIL (import: %1)").arg(importErr);
    return QStringLiteral("PASS (%1 settings + %2 destinations)")
        .arg(s.size())
        .arg(root.value(QStringLiteral("destinations")).toArray().size());
}

QString AppContext::toolShortcutsCheck() const
{
    if (!m_engine)
        return QStringLiteral("FAIL (no QML engine)");

    // Load the real singleton instead of duplicating the table in C++. The
    // expected ids below are a test oracle; both window key handlers resolve
    // through ToolCatalog.toolForShortcut(), so a missing/duplicate mapping is
    // caught before either UI is opened.
    QQmlComponent probeComponent(m_engine);
    probeComponent.setData(QByteArray(R"qml(
import QtQuick
import Unisic
QtObject {
    readonly property var expected: [
        { key: Qt.Key_V, id: "edit" },
        { key: Qt.Key_P, id: "pen" },
        { key: Qt.Key_L, id: "line" },
        { key: Qt.Key_A, id: "arrow" },
        { key: Qt.Key_M, id: "measure" },
        { key: Qt.Key_R, id: "rect" },
        { key: Qt.Key_O, id: "ellipse" },
        { key: Qt.Key_D, id: "callout" },
        { key: Qt.Key_T, id: "text" },
        { key: Qt.Key_H, id: "highlight" },
        { key: Qt.Key_B, id: "blur" },
        { key: Qt.Key_X, id: "pixelate" },
        { key: Qt.Key_E, id: "smarterase" },
        { key: Qt.Key_N, id: "step" },
        { key: Qt.Key_C, id: "crop" }
    ]
    readonly property bool valid: check()
    function check() {
        for (let i = 0; i < expected.length; ++i) {
            const editorTool = ToolCatalog.toolForShortcut(expected[i].key, "editor")
            if (!editorTool || editorTool.id !== expected[i].id)
                return false
            const overlayTool = ToolCatalog.toolForShortcut(expected[i].key, "overlay")
            if (expected[i].id === "crop") {
                if (overlayTool)
                    return false
            } else if (!overlayTool || overlayTool.id !== expected[i].id) {
                return false
            }
        }
        return true
    }
}
)qml"),
                          // A qrc base url keeps the compile synchronous: a custom
                          // scheme makes the implicit-import qmldir lookup go through
                          // the network loader, so the component never leaves Loading
                          // and create() returns null with an empty errorString.
                          QUrl(QStringLiteral("qrc:/ToolShortcutProbe.qml")));
    if (probeComponent.status() != QQmlComponent::Ready)
        return QStringLiteral("FAIL (probe %1: %2)")
            .arg(probeComponent.status() == QQmlComponent::Loading ? QStringLiteral("still loading")
                                                                   : QStringLiteral("not ready"),
                 probeComponent.errorString().simplified());
    std::unique_ptr<QObject> probe(probeComponent.create());
    if (!probe)
        return QStringLiteral("FAIL (probe create: %1)").arg(probeComponent.errorString().simplified());
    return probe->property("valid").toBool()
        ? QStringLiteral("PASS (15 editor, 14 overlay mappings)")
        : QStringLiteral("FAIL (catalog mapping mismatch)");
}

QString AppContext::historyFilterCheck()
{
    HistoryFilterModel f;
    f.setSourceModel(m_history);

    // Three scratch entries, one per filter dimension. Pathless (never saved),
    // so nothing reaches the trash when they are removed again below.
    const quint64 idImage = m_history->addEntry({}, devTestImage(), QStringLiteral("image"));
    const quint64 idRec = m_history->addEntry({}, devTestImage(), QStringLiteral("gif"));
    // A saved instant replay: same media kind as a recording, own category.
    const quint64 idReplay = m_history->addEntry({}, devTestImage(), QStringLiteral("video"), {}, {},
                                                 QStringLiteral("replay"));
    const quint64 idUploaded = m_history->addEntry({}, devTestImage(), QStringLiteral("image"),
                                                   QStringLiteral("https://example.invalid/smoke-xyzzy.png"));
    auto visible = [&f] {
        QSet<quint64> s;
        const QVariantList ids = f.entryIds();
        for (const QVariant &v : ids)
            s.insert(v.toULongLong());
        return s;
    };
    const QSet<quint64> seeded{idImage, idRec, idUploaded, idReplay};
    QStringList fails;
    auto expect = [&](const QString &what, const QSet<quint64> &want) {
        // Only the seeded ids are asserted: the user's real history is in the
        // same model and legitimately matches the same filters.
        if ((visible() & seeded) != want)
            fails << what;
    };

    f.setKindFilter(QStringLiteral("gif"));
    expect(QStringLiteral("kind=gif"), {idRec});
    f.setKindFilter(QStringLiteral("replay"));
    expect(QStringLiteral("kind=replay"), {idReplay});
    f.setKindFilter(QStringLiteral("image"));
    expect(QStringLiteral("kind=image"), {idImage, idUploaded});
    f.setKindFilter({});

    f.setUploadedOnly(true);
    expect(QStringLiteral("uploadedOnly"), {idUploaded});
    f.setUploadedOnly(false);

    m_history->setFavoriteByIds({QVariant(idImage)}, true);
    f.setFavoritesOnly(true);
    expect(QStringLiteral("favoritesOnly"), {idImage});
    f.setFavoritesOnly(false);
    m_history->setFavoriteByIds({QVariant(idImage)}, false);

    f.setSearchText(QStringLiteral("xyzzy"));   // matches the upload URL only
    expect(QStringLiteral("search"), {idUploaded});
    f.setSearchText({});
    expect(QStringLiteral("no filter"), seeded);

    // Batch delete, the History page's selection action.
    m_history->removeByIds({QVariant(idImage), QVariant(idRec), QVariant(idUploaded),
                            QVariant(idReplay)});
    if (!(visible() & seeded).isEmpty())
        fails << QStringLiteral("removeByIds");
    if (!m_history->entryById(idImage).isEmpty())
        fails << QStringLiteral("entryById after delete");

    return fails.isEmpty() ? QStringLiteral("PASS (kind, replay, uploaded, starred, search, batch delete)")
                           : QStringLiteral("FAIL (%1)").arg(fails.join(QStringLiteral(", ")));
}

QString AppContext::imgurSetupCheck()
{
    // 1) No stored destination may still carry the placeholder Client-ID that
    // shipped up to 0.7 — ensureBuiltins() repairs those on load.
    const QJsonArray dests = m_uploads->destinationsJson();
    for (const QJsonValue &v : dests) {
        const QString auth = v.toObject().value(QStringLiteral("headers")).toObject()
                              .value(QStringLiteral("Authorization")).toString();
        if (auth.contains(QStringLiteral("REPLACE_WITH_YOUR_IMGUR_CLIENT_ID")))
            return QStringLiteral("FAIL (placeholder Client-ID survives in '%1')")
                .arg(v.toObject().value(QStringLiteral("name")).toString());
    }

    // 2) The guard: an Imgur destination with no Client-ID must fail before any
    // request goes out, naming the fix. Scratch destination, removed below —
    // the user's own Imgur destination may legitimately have an ID by now.
    const QString scratch = QStringLiteral("unisic-smoke-imgur");
    m_uploads->saveDestination(QVariantMap{
        {QStringLiteral("name"), scratch},
        {QStringLiteral("type"), QStringLiteral("http")},
        {QStringLiteral("requestUrl"), QStringLiteral("https://api.imgur.com/3/image")},
        {QStringLiteral("fileFormName"), QStringLiteral("image")},
        {QStringLiteral("urlPath"), QStringLiteral("$json:data.link$")},
    });
    QString error;
    bool called = false;
    m_uploads->uploadDataTo(scratch, QByteArray("not a real upload"),
                            QStringLiteral("smoke.png"), QStringLiteral("image/png"),
                            [&](const QString &, const QString &, const QString &err) {
        called = true;
        error = err;
    });
    m_uploads->removeDestination(scratch);
    if (!called)
        return QStringLiteral("FAIL (no Client-ID: upload was attempted, not refused)");
    if (!error.contains(QStringLiteral("Client-ID")))
        return QStringLiteral("FAIL (unhelpful error: %1)").arg(error.left(80));
    return QStringLiteral("PASS (placeholder purged, missing Client-ID refused early)");
}

QString AppContext::curlDestinationCheck() const
{
    QStringList fails;
    auto target = [&](const QString &url, const QString &name, const QString &want) {
        const QString got = UploadManager::curlTargetUrl(url, name);
        if (got != want)
            fails << QStringLiteral("target '%1' + '%2' -> %3 (want %4)").arg(url, name, got, want);
    };
    // No token: the name is appended, with or without the trailing slash, which
    // is every FTP/SFTP destination written before the token existed.
    target(QStringLiteral("sftp://host/dir"), QStringLiteral("a b.png"),
           QStringLiteral("sftp://host/dir/a%20b.png"));
    target(QStringLiteral("sftp://host/dir/"), QStringLiteral("a b.png"),
           QStringLiteral("sftp://host/dir/a%20b.png"));
    // Token: the name lands where it was put, and the query survives it.
    target(QStringLiteral("https://host/up/%file%?to=inbox"), QStringLiteral("a b.png"),
           QStringLiteral("https://host/up/a%20b.png?to=inbox"));
    // A name can never climb out of the target directory, in either form.
    target(QStringLiteral("https://host/up/%file%"), QStringLiteral("../../etc/x.png"),
           QStringLiteral("https://host/up/.._.._etc_x.png"));
    target(QStringLiteral("sftp://host/dir/"), QStringLiteral("../../etc/x.png"),
           QStringLiteral("sftp://host/dir/.._.._etc_x.png"));

    // curl's stdout, resolved the same way an http response is.
    const QJsonObject dest{{QStringLiteral("urlPath"),
                            QStringLiteral("https://host/$json:data.id$")}};
    const QString url = UploadManager::extractUrl(dest, QStringLiteral("urlPath"),
                                                  QByteArray(R"({"data":{"id":"abc123"}})"));
    if (url != QStringLiteral("https://host/abc123"))
        fails << QStringLiteral("response '%1' (want https://host/abc123)").arg(url);

    return fails.isEmpty()
        ? QStringLiteral("PASS (%file% token, append fallback, name cannot escape, response parsed)")
        : QStringLiteral("FAIL (%1)").arg(fails.join(QStringLiteral("; ")));
}

void AppContext::devTestCurlDestination()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: curl destination: %1").arg(curlDestinationCheck()));
}

QString AppContext::templateVarsCheck() const
{
    QStringList fails;
    // A chip is a promise: click it and the server gets your file name, not the
    // six characters "%file%". So every token the editor offers is put through
    // the code that has to consume it, and the answer has to differ from the
    // token. Advertising one the sender leaves alone would be worse than no
    // chip, because it looks like it worked right up until the upload lands.
    auto check = [&](const QString &field, const QString &type,
                     const std::function<QString(const QString &)> &substitute) {
        const QVariantMap help = m_uploads->templateHelp(field, type);
        const QVariantList vars = help.value(QStringLiteral("vars")).toList();
        const QString pattern = help.value(QStringLiteral("pattern")).toString();
        if (vars.isEmpty())
            return; // legitimately token-free (the http request URL)
        const QRegularExpression re(pattern);
        if (!re.isValid()) {
            fails << QStringLiteral("%1/%2: pattern does not compile").arg(field, type);
            return;
        }
        for (const QVariant &v : vars) {
            const QString token = v.toMap().value(QStringLiteral("token")).toString();
            // The pills are painted from `pattern`, so a token the chip inserts
            // and the pattern does not match would go in and then refuse to be
            // drawn as a variable. Filled in first: a chip with caretBack > 0
            // types a token the user still has to finish ($json:$ needs its
            // path), and an unfinished one is MEANT to stay unpilled, since it
            // is not yet something extractUrl resolves.
            const int caretBack = v.toMap().value(QStringLiteral("caretBack")).toInt();
            QString filled = token;
            if (caretBack > 0)
                filled.insert(filled.size() - caretBack, QLatin1Char('x'));
            const QRegularExpressionMatch m = re.match(filled);
            if (!m.hasMatch() || m.captured(0) != filled)
                fails << QStringLiteral("%1/%2: pattern misses %3").arg(field, type, filled);
            const QString got = substitute(filled);
            if (got.contains(filled))
                fails << QStringLiteral("%1/%2: %3 not substituted").arg(field, type, filled);
        }
    };

    check(QStringLiteral("requestUrl"), QStringLiteral("curl"), [](const QString &token) {
        return UploadManager::curlTargetUrl(QStringLiteral("https://host/up/") + token,
                                            QStringLiteral("shot.png"));
    });
    check(QStringLiteral("requestUrl"), QStringLiteral("http"), [](const QString &token) {
        return UploadManager::requestUrlWithFileName(QStringLiteral("https://host/up/") + token,
                                                     QStringLiteral("shot.png"));
    });
    // And the http URL must still be sent as typed when the token is NOT there:
    // appending the name (curl's rule for FTP folders) would post every capture
    // to a path the API does not have.
    if (UploadManager::requestUrlWithFileName(QStringLiteral("https://api.host/v1/upload"),
                                              QStringLiteral("shot.png"))
        != QLatin1String("https://api.host/v1/upload"))
        fails << QStringLiteral("requestUrl/http: a URL without the token was rewritten");
    // The answer is shaped so that the filled-in "x" resolves as a JSON key and
    // as a pattern alike; extractToken() hands back an unrecognised token
    // unchanged, which is exactly the drift this is looking for.
    check(QStringLiteral("urlPath"), QStringLiteral("http"), [](const QString &token) {
        const QJsonObject dest{{QStringLiteral("urlPath"), token}};
        return UploadManager::extractUrl(dest, QStringLiteral("urlPath"),
                                         QByteArray(R"({"x":"https://host/shot.png"})"));
    });
    // The JSON body's three tokens are substituted inside sendHttp(), which needs
    // a network stack, so they are checked against the one destination the whole
    // path is driven from instead: what matters is that the editor and the sender
    // agree on the spelling.
    const QVariantList bodyVars = m_uploads->templateHelp(QStringLiteral("data"),
                                                          QStringLiteral("http"))
                                      .value(QStringLiteral("vars")).toList();
    QStringList bodyTokens;
    for (const QVariant &v : bodyVars)
        bodyTokens << v.toMap().value(QStringLiteral("token")).toString();
    bodyTokens.sort();
    const QStringList wantBody{QStringLiteral("$base64$"), QStringLiteral("$filename$"),
                               QStringLiteral("$mime$")};
    if (bodyTokens != wantBody)
        fails << QStringLiteral("data: offers %1 (sendHttp substitutes %2)")
                     .arg(bodyTokens.join(QLatin1Char(',')), wantBody.join(QLatin1Char(',')));

    // Same promise on the other field that takes variables: the filename
    // template. An unexpanded token here does not fail an upload, it names the
    // file "Unisic_%date%" and every capture after it collides on that name.
    const QVariantMap fileHelp = FilenameTemplate::help();
    const QRegularExpression fileRe(fileHelp.value(QStringLiteral("pattern")).toString());
    if (!fileRe.isValid())
        fails << QStringLiteral("filename: pattern does not compile");
    const QDateTime when = QDateTime::fromSecsSinceEpoch(1754236417);
    const QVariantList fileVars = fileHelp.value(QStringLiteral("vars")).toList();
    if (fileVars.isEmpty())
        fails << QStringLiteral("filename: offers no variables at all");
    for (const QVariant &v : fileVars) {
        const QString token = v.toMap().value(QStringLiteral("token")).toString();
        const QRegularExpressionMatch m = fileRe.match(token);
        if (!m.hasMatch() || m.captured(0) != token)
            fails << QStringLiteral("filename: pattern misses %1").arg(token);
        // Wrapped in a name expand() cannot strip, so an empty expansion still
        // leaves something to compare - and %i% legitimately expands to "0".
        if (FilenameTemplate::expand(QStringLiteral("shot_") + token, 7, when).contains(token))
            fails << QStringLiteral("filename: %1 not substituted").arg(token);
    }

    return fails.isEmpty()
        ? QStringLiteral("PASS (every offered variable is one the sender resolves)")
        : QStringLiteral("FAIL (%1)").arg(fails.join(QStringLiteral("; ")));
}

void AppContext::devTestTemplateVars()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: template variables: %1").arg(templateVarsCheck()));
}

QString AppContext::staticGifCheck() const
{
    QStringList fails;
    // The name has to be able to say gif at all, or the format setting can
    // never reach saveImageTo's encoder pick.
    if (FilenameTemplate::extensionFor(QStringLiteral("gif")) != QLatin1String("gif"))
        fails << QStringLiteral("extensionFor(gif) is not gif");

    if (!ffmpegAvailable())
        return QStringLiteral("SKIP (no ffmpeg: GIF saves fall back to PNG with a toast)");

    // A real encode of a tiny image, checked down to the bytes: GIF89a in the
    // header is what a file manager, a browser and every upload target read to
    // decide what this file is.
    QImage src(64, 48, QImage::Format_ARGB32_Premultiplied);
    src.fill(Qt::transparent);
    QPainter p(&src);
    p.fillRect(0, 0, 32, 48, QColor(0xC8, 0xAC, 0xD6));
    p.fillRect(32, 0, 32, 48, QColor(0x17, 0x15, 0x3B));
    p.end();
    const QByteArray gif = FfmpegUtil::encodeStillGif(src, 90);
    if (gif.isEmpty())
        return QStringLiteral("FAIL (ffmpeg is present but the encode produced nothing)");
    if (!gif.startsWith("GIF89a") && !gif.startsWith("GIF87a"))
        fails << QStringLiteral("no GIF header (first bytes: %1)")
                     .arg(QString::fromLatin1(gif.left(6).toHex()));
    // And it has to come back as ONE frame the image editor can open, not as
    // something the trim window would claim.
    QBuffer buf;
    buf.setData(gif);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, "gif");
    if (reader.imageCount() != 1)
        fails << QStringLiteral("frame count is %1, not 1").arg(reader.imageCount());
    if (reader.size() != src.size())
        fails << QStringLiteral("size came back %1x%2")
                     .arg(reader.size().width()).arg(reader.size().height());

    return fails.isEmpty()
        ? QStringLiteral("PASS (%1 bytes, GIF89a, one frame, %2x%3)")
              .arg(gif.size()).arg(src.width()).arg(src.height())
        : QStringLiteral("FAIL (%1)").arg(fails.join(QStringLiteral("; ")));
}

void AppContext::devTestStaticGif()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: still GIF: %1").arg(staticGifCheck()));
}

QString AppContext::imageConvertCheck() const
{
    QStringList fails;

    // Every conversion in the app - incoming files, the over-size rule, the
    // history entries, the upload format - ends in this one call, so the check
    // is on the bytes it produces, not on the callers. Magic numbers, because
    // that is what the receiving end reads: a name is not evidence.
    QImage opaque(32, 24, QImage::Format_ARGB32_Premultiplied);
    opaque.fill(QColor(0x43, 0x3D, 0x8B));
    struct { const char *fmt; const char *magic; int at; } kinds[] = {
        { "png",  "\x89PNG", 0 },
        { "jpg",  "\xFF\xD8\xFF", 0 },
        { "webp", "WEBP", 8 }, // RIFF<4-byte size>WEBP
    };
    for (const auto &k : kinds) {
        const ImageEncode::Result r =
            ImageEncode::encode(opaque, QString::fromLatin1(k.fmt), 80);
        if (!r.ok()) {
            fails << QStringLiteral("%1 produced nothing").arg(QLatin1String(k.fmt));
            continue;
        }
        if (r.format != QLatin1String(k.fmt))
            fails << QStringLiteral("%1 came back as %2").arg(QLatin1String(k.fmt), r.format);
        if (!r.bytes.mid(k.at).startsWith(QByteArray(k.magic)))
            fails << QStringLiteral("%1 header is %2").arg(QLatin1String(k.fmt),
                        QString::fromLatin1(r.bytes.left(12).toHex()));
    }

    // The one substitution a user can actually be surprised by: JPEG has no
    // alpha channel, so a transparent capture must come back a PNG and SAY why,
    // rather than a .jpg with a black background where the transparency was.
    QImage alpha(16, 16, QImage::Format_ARGB32_Premultiplied);
    alpha.fill(Qt::transparent);
    const ImageEncode::Result a = ImageEncode::encode(alpha, QStringLiteral("jpg"), 80);
    if (a.format != QLatin1String("png") || a.fallbackReason != QLatin1String("alpha"))
        fails << QStringLiteral("transparent JPEG gave %1/%2, not png/alpha")
                     .arg(a.format, a.fallbackReason);
    if (ImageEncode::hasTransparency(opaque))
        fails << QStringLiteral("an opaque image reads as transparent");

    // Renaming is the other half of the same rule: whatever the encoder decided
    // has to reach the file name, or the extension lies about the bytes.
    if (FilenameTemplate::withExtension(QStringLiteral("shot.png"), QStringLiteral("webp"))
        != QLatin1String("shot.webp"))
        fails << QStringLiteral("withExtension did not replace the extension");
    if (FilenameTemplate::withExtension(QStringLiteral("shot"), QStringLiteral("gif"))
        != QLatin1String("shot.gif"))
        fails << QStringLiteral("withExtension did not add a missing extension");
    if (FilenameTemplate::withExtension(QStringLiteral("2026-01-30_12.30.11"),
                                        QStringLiteral("png"))
        != QLatin1String("2026-01-30_12.30.png"))
        fails << QStringLiteral("withExtension took the wrong dot");
    // An upload names its file after the encode, from the mime that came back.
    if (FilenameTemplate::extensionForMime(QStringLiteral("image/jpeg")) != QLatin1String("jpg")
        || FilenameTemplate::extensionForMime(QStringLiteral("image/webp")) != QLatin1String("webp")
        || FilenameTemplate::extensionForMime(QString()) != QLatin1String("png"))
        fails << QStringLiteral("extensionForMime does not map the encoder's answer");

    // Settings side: what the upload path will actually do right now.
    const QString up = uploadImageFormat();
    const QString upSays = up.isEmpty() ? QStringLiteral("as saved") : up;
    const QString large = m_settings->autoConvertLarge()
        ? QStringLiteral("over %1 MB to %2").arg(m_settings->autoConvertOverMb())
              .arg(FilenameTemplate::extensionFor(m_settings->autoConvertFormat()))
        : QStringLiteral("off");

    return fails.isEmpty()
        ? QStringLiteral("PASS (upload %1, incoming %2, size rule %3)")
              .arg(upSays,
                   m_settings->convertIncoming() ? QStringLiteral("converted")
                                                 : QStringLiteral("kept"),
                   large)
        : QStringLiteral("FAIL (%1)").arg(fails.join(QStringLiteral("; ")));
}

void AppContext::devTestImageConvert()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: image conversion: %1").arg(imageConvertCheck()));
}

void AppContext::devTestHistoryFilter()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: history search + filters: %1").arg(historyFilterCheck()));
}

void AppContext::devTestImgurSetup()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: Imgur Client-ID guard: %1").arg(imgurSetupCheck()));
}

void AppContext::devTestSettingsRoundTrip()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: settings round-trip: %1").arg(settingsRoundTripCheck()));
}

QString AppContext::installChannelCheck() const
{
    const QString kind = m_updater->installKind();
    const bool external = m_updater->updatesManagedExternally();
    const bool selfUpdate = m_updater->canSelfUpdate();
    const bool viaScript = m_updater->canInstallViaScript();

    // Exactly one of the three has to be true for a shipped install, and an
    // externally managed channel must offer NEITHER button: "Install now" runs
    // install.sh, which on an AUR box would pacman -U the GitHub package over
    // the helper's and register the OBS repo on top.
    QString verdict = QStringLiteral("PASS");
    if (external && (selfUpdate || viaScript))
        verdict = QStringLiteral("FAIL (externally managed but still offers an install button)");
    else if (QLatin1String(UNISIC_BUILD) == QLatin1String("dev"))
        verdict = QStringLiteral("SKIP (dev build: every update path is off by design)");
    else if (!external && !selfUpdate && !viaScript)
        verdict = QStringLiteral("FAIL (no update path at all)");

    return QStringLiteral("%1 - channel '%2'%3, self-update %4, install-via-script %5")
        .arg(verdict,
             kind,
             external ? QStringLiteral(" (owns updates)") : QString(),
             selfUpdate ? QStringLiteral("yes") : QStringLiteral("no"),
             viaScript ? QStringLiteral("yes") : QStringLiteral("no"));
}

void AppContext::devTestInstallChannel()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: install channel: %1").arg(installChannelCheck()));
}

void AppContext::devTestUpload()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: uploading a test image to '%1'…").arg(m_settings->activeDestination()));
    uploadImage(devTestImage(), [this](const QString &url, const QString &err) {
        if (err.isEmpty())
            showToast(tr("Dev: upload OK: %1").arg(url));
        else
            showToast(tr("Dev: upload failed: %1").arg(err), true);
    });
}

void AppContext::destinationTestCheck(std::function<void(const QString &)> done)
{
    // The server editor's "Test upload" button: it hands UploadManager the
    // UNSAVED form state, which then pushes a generated PNG through the very
    // same http/curl path a real capture takes. Two promises can rot silently
    // here, so both are asserted: (a) an unusable form is refused before any
    // request goes out, and (b) a test never persists anything.
    //
    // Phase one is that guard. Its verdict arrives on the check's own callback
    // channel (the overload, never the shared testFinished signal - see
    // UploadManager.h) and the transport phase is chained off it, never read
    // off the stack right after the call. testDestination() does answer
    // synchronously for an empty form today, but a check that quietly depends
    // on that starts lying the day the guard grows any asynchronous branch (a
    // reachability probe, a name lookup), and it would lie by reporting PASS.
    const int destsBefore = m_uploads->destinationsJson().size();

    auto guarded = std::make_shared<bool>(false);
    QPointer<AppContext> self(this);
    m_uploads->testDestination(QVariantMap{}, // no requestUrl: nothing may leave the app
                               [self, guarded, destsBefore, done]
                               (bool ok, const QString &, const QString &err) {
        if (!self || *guarded)
            return;
        *guarded = true;
        self->destinationTestTransport((!ok && !err.isEmpty())
                                           ? QStringLiteral("PASS")
                                           : QStringLiteral("FAIL (an empty form was not refused)"),
                                       destsBefore, done);
    });
    // No answer at all is a failure of the guard, not of the transport: report
    // it and still run the transport half, which is the more interesting one.
    QTimer::singleShot(5000, this, [this, guarded, destsBefore, done] {
        if (*guarded)
            return;
        *guarded = true;
        destinationTestTransport(QStringLiteral("FAIL (no answer)"), destsBefore, done);
    });
}

void AppContext::devTestDestinationTest()
{
    if (!devBuild())
        return;
    destinationTestCheck([this](const QString &result) {
        // contains(), not the startsWith() the other dev buttons use: this
        // check reports three verdicts in one line ("guard …, transport …,
        // no side effects …"), so a failure can sit anywhere in it.
        showToast(tr("Dev: server test upload: %1").arg(result),
                  result.contains(QLatin1String("FAIL")));
    });
}

QString AppContext::importDropCheck()
{
    // Everything the main window's DropArea can hand over, routed through the
    // one router (openPath) the file dialog and Ctrl+V also use. The drag
    // itself cannot be synthesized headlessly, so what is asserted is the part
    // that decides where a payload lands - and that nothing lands silently.
    QTemporaryDir dir;
    if (!dir.isValid())
        return QStringLiteral("FAIL (no scratch dir)");
    const QString png = dir.filePath(QStringLiteral("unisic-drop.png"));
    const QString png2 = dir.filePath(QStringLiteral("unisic-drop-2.png"));
    if (!devTestImage().save(png, "PNG") || !devTestImage().save(png2, "PNG"))
        return QStringLiteral("FAIL (couldn't write the fixture)");
    const QString txt = dir.filePath(QStringLiteral("unisic-drop.txt"));
    {
        QFile f(txt);
        if (!f.open(QIODevice::WriteOnly))
            return QStringLiteral("FAIL (couldn't write the fixture)");
        f.write("not an image\n");
    }
    // Every window this opens is real (it is the assertion), so collect and
    // close them again instead of leaving a stack of editors on the desktop.
    CheckWindowCollector collect(this);

    int before = m_editorWindows;
    openDroppedUrls({QUrl::fromLocalFile(png)});
    const bool imageOk = m_editorWindows > before;

    before = m_editorWindows;
    openDroppedUrls({QUrl(QStringLiteral("https://example.invalid/remote.png"))});
    const bool remoteRefused = m_editorWindows == before;

    before = m_editorWindows;
    openDroppedUrls({QUrl::fromLocalFile(txt)});
    const bool unsupportedRefused = m_editorWindows == before;

    before = m_editorWindows;
    openPath(dir.filePath(QStringLiteral("gone.png")));
    const bool missingRefused = m_editorWindows == before;

    // A whole FOLDER is a legal drop payload (drag one out of a file manager)
    // and gets its own answer, both through the drop router and through the
    // shared openPath() a paste lands in.
    before = m_editorWindows;
    openDroppedUrls({QUrl::fromLocalFile(dir.path())});
    openPath(dir.path());
    const bool folderRefused = m_editorWindows == before;

    // Multi-file drop: the first payload Unisic can open wins, the rest is
    // reported rather than dumped onto the desktop as extra windows. Both
    // shapes of "the rest" run, because they get different notes: one entry
    // that could never have opened (the text file), and one that could (the
    // second image).
    before = m_editorWindows;
    openDroppedUrls({QUrl::fromLocalFile(txt), QUrl::fromLocalFile(png)});
    openDroppedUrls({QUrl::fromLocalFile(png), QUrl::fromLocalFile(png2)});
    const bool multiOk = m_editorWindows == before + 2;

    // A drag out of a browser carries PIXELS, not a path.
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    devTestImage().save(&buf, "PNG");
    buf.close();
    before = m_editorWindows;
    const bool dataOk = openImageData(bytes) && m_editorWindows > before;
    const bool junkRefused = !openImageData(QByteArrayLiteral("definitely not a PNG"));

    if (!imageOk)
        return QStringLiteral("FAIL (a dropped image did not open the editor)");
    if (!remoteRefused)
        return QStringLiteral("FAIL (a remote url was not refused)");
    if (!unsupportedRefused)
        return QStringLiteral("FAIL (an unsupported file was not refused)");
    if (!missingRefused)
        return QStringLiteral("FAIL (a missing file was not refused)");
    if (!folderRefused)
        return QStringLiteral("FAIL (a dropped folder was not refused)");
    if (!multiOk)
        return QStringLiteral("FAIL (a multi-file drop opened the wrong number of windows)");
    if (!dataOk)
        return QStringLiteral("FAIL (dropped image data did not open the editor)");
    if (!junkRefused)
        return QStringLiteral("FAIL (junk bytes were accepted as an image)");
    return QStringLiteral("PASS (file, pixels, multi-drop; "
                          "remote/unsupported/missing/folder refused)");
}

void AppContext::devTestImportDrop()
{
    if (!devBuild())
        return;
    const QString result = importDropCheck();
    showToast(tr("Dev: drop import: %1").arg(result), result.startsWith(QLatin1String("FAIL")));
}

QString AppContext::clipboardImportCheck()
{
    // Ctrl+V in the main window. Both branches matter: pixels open a NEW
    // editor document (no overwrite path), a copied FILE goes through the same
    // router a drop uses.
    //
    // The payloads are handed straight to the paste ROUTER; the system
    // clipboard is never read and never written. Not touching it at all beats
    // the snapshot/restore the smoke run has to do for the steps that really
    // must copy (snapshotClipboardForSmoke): that one is best-effort by nature
    // - formats are served on demand by the source application, and on Plasma
    // putting the selection back adds another Klipper history entry. The only
    // line this check skips is clipboard()->mimeData() itself; everything that
    // DECIDES where a payload lands is below.
    QTemporaryDir dir;
    if (!dir.isValid())
        return QStringLiteral("FAIL (no scratch dir)");
    CheckWindowCollector collect(this);

    QMimeData pixels;
    pixels.setImageData(devTestImage());
    int before = m_editorWindows;
    pasteMimeData(&pixels);
    const bool imageOk = m_editorWindows > before;

    const QString png = dir.filePath(QStringLiteral("unisic-paste.png"));
    if (!devTestImage().save(png, "PNG"))
        return QStringLiteral("FAIL (couldn't write the fixture)");
    QMimeData copiedFile;
    copiedFile.setUrls({QUrl::fromLocalFile(png)});
    before = m_editorWindows;
    pasteMimeData(&copiedFile);
    const bool fileOk = m_editorWindows > before;

    // Copying SEVERAL files takes the same rule a multi-file drop does: the
    // first entry Unisic can open wins, not simply the first entry.
    const QString txt = dir.filePath(QStringLiteral("unisic-paste.txt"));
    {
        QFile f(txt);
        if (!f.open(QIODevice::WriteOnly))
            return QStringLiteral("FAIL (couldn't write the fixture)");
        f.write("not an image\n");
    }
    QMimeData copiedPair;
    copiedPair.setUrls({QUrl::fromLocalFile(txt), QUrl::fromLocalFile(png)});
    before = m_editorWindows;
    pasteMimeData(&copiedPair);
    const bool pairOk = m_editorWindows > before;

    // Plain text is not a capture: it must be refused out loud, not opened.
    QMimeData plainText;
    plainText.setText(QStringLiteral("Unisic paste import check"));
    before = m_editorWindows;
    pasteMimeData(&plainText);
    const bool textRefused = m_editorWindows == before;

    // An empty clipboard hands over no QMimeData at all: its own branch.
    before = m_editorWindows;
    pasteMimeData(nullptr);
    const bool emptyRefused = m_editorWindows == before;

    if (!imageOk)
        return QStringLiteral("FAIL (a clipboard image did not open the editor)");
    if (!fileOk)
        return QStringLiteral("FAIL (a copied file did not open the editor)");
    if (!pairOk)
        return QStringLiteral("FAIL (a multi-file paste skipped the openable file)");
    if (!textRefused)
        return QStringLiteral("FAIL (plain text was opened as a capture)");
    if (!emptyRefused)
        return QStringLiteral("FAIL (an empty clipboard opened something)");
    return QStringLiteral("PASS (image, copied file, multi-file copy; "
                          "plain text and an empty clipboard refused)");
}

void AppContext::devTestClipboardImport()
{
    if (!devBuild())
        return;
    const QString result = clipboardImportCheck();
    showToast(tr("Dev: paste import: %1").arg(result), result.startsWith(QLatin1String("FAIL")));
}

QString AppContext::recordPageModeCheck()
{
    // The Record page hosts Video and GIF behind one mode segment, and the page
    // Loader is destroyed on every navigation - so the chosen mode only
    // survives because it is a persisted setting, not page state.
    const int original = m_settings->recordPageMode();
    m_settings->setRecordPageMode(1);
    const bool gif = m_settings->recordPageMode() == 1;
    m_settings->setRecordPageMode(0);
    const bool video = m_settings->recordPageMode() == 0;
    m_settings->setRecordPageMode(original);
    const bool restored = m_settings->recordPageMode() == original;
    if (!gif || !video || !restored)
        return QStringLiteral("FAIL (the mode does not persist)");
    return QStringLiteral("PASS (mode round-trips; currently %1)")
        .arg(original == 1 ? QStringLiteral("GIF") : QStringLiteral("video"));
}

void AppContext::devTestRecordPageMode()
{
    if (!devBuild())
        return;
    const QString result = recordPageModeCheck();
    showToast(tr("Dev: record page mode: %1").arg(result), result.startsWith(QLatin1String("FAIL")));
}

QString AppContext::altHotkeysCheck()
{
    // Round-trip a MULTI-binding through the real daemon on a scratch action:
    // push "F9, Meta+F9", read the active keys back, expect BOTH to be live
    // and the portable form to collapse to the same string; then release.
    // Exercises keysFor (multi-chord parse), the daemon's alternate-key list
    // and portableFromKeys — the plumbing the alternative-hotkeys UI rides on.
    if (!m_hotkeys->available())
        return QStringLiteral("SKIP (no KGlobalAccel)");
    const QString id = QStringLiteral("alt-hotkey-test");
    const QString name = tr("Alternate hotkey test");
    const QString wanted = QStringLiteral("F9, Meta+F9");
    QString result;
    if (!m_hotkeys->setShortcut(id, name, wanted)) {
        result = QStringLiteral("FAIL (daemon refused the multi-binding - keys taken?)");
    } else {
        bool ok = false;
        const QString actual = m_hotkeys->activeKeysPortable(id, &ok);
        if (!ok)
            result = QStringLiteral("FAIL (readback query failed)");
        else if (!GlobalHotkeys::sameBinding(actual, wanted))
            result = QStringLiteral("FAIL (round-trip returned '%1')").arg(actual);
        else
            result = QStringLiteral("PASS (both alternates live)");
    }
    m_hotkeys->releaseShortcut(id, name);
    // Fully remove the scratch action — unbinding alone (NoAutoloading) leaves a
    // phantom "Alternate hotkey test" row in the Shortcuts KCM forever.
    m_hotkeys->unregisterAction(id);
    return result;
}

void AppContext::devTestAltHotkeys()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: alternate hotkeys - %1").arg(altHotkeysCheck()));
}

void AppContext::devTestHotkeyBinds()
{
    if (!devBuild())
        return;
    if (!m_hotkeys->available()) {
        showToast(tr("Dev: KGlobalAccel not available (backend: %1)")
                      .arg(m_hotkeyBackend.isEmpty() ? tr("none") : m_hotkeyBackend));
        return;
    }
    int bad = 0;
    QStringList conflicts;
    const QStringList lines = hotkeyBindStatus(&bad, true, &conflicts);
    qInfo().noquote() << "[dev] hotkey binds:\n" + lines.join(QLatin1Char('\n'));
    if (!conflicts.isEmpty())
        showToast(tr("Hotkey taken by another app: %1. Pick a different key in "
                     "Settings → Hotkeys, or free it in System Settings → Shortcuts.")
                      .arg(conflicts.join(QStringLiteral("; "))), true);
    else if (bad == 0)
        showToast(tr("Hotkeys: all %1 bound in the daemon").arg(lines.size()));
    else
        showToast(tr("Hotkeys: %1 of %2 were unbound and have been re-asserted (details in the log)")
                      .arg(bad).arg(lines.size()), true);
}

// showInFileManager's select-the-file path needs a FileManager1 host on the
// bus; without one the action still works via the plain open-folder fallback.
static QString fileManager1Check()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    const QString svc = QStringLiteral("org.freedesktop.FileManager1");
    if (bus->isServiceRegistered(svc))
        return QStringLiteral("PASS (service live)");
    const QDBusReply<QStringList> act = bus->activatableServiceNames();
    if (act.isValid() && act.value().contains(svc))
        return QStringLiteral("PASS (activatable)");
    return QStringLiteral("SKIP (no FileManager1 host - falls back to opening the folder)");
}

// Dev/smoke: exercise the X11 XShm grabber directly - construct it on the
// primary monitor, grab one frame and verify the tight-packed size. No ffmpeg,
// so it isolates the new frame source. English status (like the other *Check()).
static QString x11RecordCheck()
{
#if defined(HAVE_PIPEWIRE) && defined(HAVE_X11)
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return QStringLiteral("SKIP (not an X11 session)");
    QScreen *scr = QGuiApplication::primaryScreen();
    if (!scr)
        return QStringLiteral("FAIL (no screen)");
    const qreal dpr = scr->devicePixelRatio();
    const QRect g = scr->geometry();
    const QRect rootRect(qRound(g.x() * dpr), qRound(g.y() * dpr),
                         qRound(g.width() * dpr), qRound(g.height() * dpr));
    X11ShmGrabber grab;
    if (!grab.start(rootRect, 10, false))
        return QStringLiteral("FAIL (grabber start)");
    QByteArray frame;
    const qsizetype expected = qsizetype(rootRect.width()) * rootRect.height() * 4;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 1000 && !grab.latestFrame(frame))
        QThread::msleep(20);
    const QString fmt = grab.pixelFormat();
    grab.stop();
    if (frame.isEmpty())
        return QStringLiteral("FAIL (no frame in 1s)");
    if (frame.size() != expected)
        return QStringLiteral("FAIL (size %1 != %2)").arg(frame.size()).arg(expected);
    return QStringLiteral("PASS (%1x%2 %3)").arg(rootRect.width()).arg(rootRect.height()).arg(fmt);
#else
    return QStringLiteral("SKIP (built without X11 capture)");
#endif
}

// Dev/smoke: prove XGrabKey works end-to-end without disturbing the live binds -
// grab an unlikely scratch combo through a throwaway backend (its destructor
// ungrabs). Reports the active hotkey backend too.
static QString x11HotkeysCheck(const QString &backend)
{
#ifdef HAVE_X11_HOTKEYS
    if (!X11Hotkeys::isAvailable())
        return QStringLiteral("SKIP (not an X11 session)");
    X11Hotkeys probe;
    const QVector<X11Hotkeys::Shortcut> one{
        {QStringLiteral("dev-x11-probe"), QStringLiteral("Ctrl+Alt+Shift+F12")}};
    probe.bind(one); // conflict on the scratch combo is not our failure
    return backend == QLatin1String("x11")
               ? QStringLiteral("PASS (active backend)")
               : QStringLiteral("PASS (available; active backend: %1)")
                     .arg(backend.isEmpty() ? QStringLiteral("none") : backend);
#else
    Q_UNUSED(backend)
    return QStringLiteral("SKIP (built without X11 hotkeys)");
#endif
}

void AppContext::devTestX11Record()
{
    if (!devBuild())
        return;
    const QString r = x11RecordCheck();
    showToast(tr("Dev: X11 record grab: %1").arg(r), r.startsWith(QLatin1String("FAIL")));
}

void AppContext::devTestX11Hotkeys()
{
    if (!devBuild())
        return;
    const QString r = x11HotkeysCheck(m_hotkeyBackend);
    showToast(tr("Dev: X11 hotkeys: %1").arg(r), r.startsWith(QLatin1String("FAIL")));
}

void AppContext::runSmokeTest()
{
    // Dev-only, defense in depth: the F8 dispatch and the QML pane already
    // check, but the invokable itself must not be reachable in a release.
    if (!devBuild() || m_smokeRunning)
        return;
    m_smokeRunning = true;
    m_smokeLog.clear();
    m_smokeIdx = 0;
    m_smokeSteps.clear();
    m_smokeWindows.clear();
    smokeLog(QStringLiteral("=== Unisic smoke test ==="));
    // Several steps copy on purpose, so take the selection away first and hand
    // it back in the cleanup step. Said out loud right here, and hedged on
    // whether the snapshot actually succeeded, because the one part of the cost
    // that CANNOT be undone is the clipboard history.
    snapshotClipboardForSmoke();
    smokeLog(QStringLiteral("note: this run copies to the clipboard several times. What is on it "
                            "now %1. Either way, every entry the run copies stays in the "
                            "clipboard history (Klipper on Plasma).")
                 .arg(m_smokeClipboard
                          ? QStringLiteral("was saved and the last step puts it back")
                          : QStringLiteral("could not be saved and is lost")));
    emit smokeTestChanged();

    // 1) capability / availability snapshot (synchronous)
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("build: ") + (devBuild() ? QStringLiteral("dev") : QStringLiteral("release")));
        smokeLog(QStringLiteral("identity: app=%1 desktop=%2 hotkeys=%3 config=%4")
                     .arg(QCoreApplication::applicationName(),
                          QGuiApplication::desktopFileName(),
                          GlobalHotkeys::componentPrefix().chopped(1),
                          m_settings->configPath()));
        smokeLog(QStringLiteral("capture backend: ") + (m_capture ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("screenshot cursor: ")
                 + (capScreenshotCursor() ? QStringLiteral("PASS")
                                          : QStringLiteral("SKIP (portal screenshot has no cursor mode)")));
        smokeLog(QStringLiteral("recording: ")
                 + (recordingAvailable()
                        ? QStringLiteral("PASS (%1)").arg(m_screenCastPortalPresent
                              ? QStringLiteral("ScreenCast portal")
                              : QStringLiteral("X11 XShm - no portal needed"))
                    : capPipeWireBuild() ? QStringLiteral("SKIP (no ScreenCast portal backend on this desktop)")
                                         : QStringLiteral("SKIP (built without PipeWire)")));
        smokeLog(QStringLiteral("window record source: ")
                 + (capRecordWindowSource() ? QStringLiteral("PASS")
                                            : QStringLiteral("SKIP (no window picker - X11 grabs a monitor)")));
        smokeLog(QStringLiteral("X11 record grab: ") + x11RecordCheck());
#ifdef HAVE_KWIN_SCREENCAST
        smokeLog(QStringLiteral("KWin native record: ")
                 + (capKWinRecord() ? QStringLiteral("PASS (zkde_screencast bound - no portal dialog)")
                                    : QStringLiteral("SKIP (not KWin, or desktop file lacks the grant)")));
#else
        smokeLog(QStringLiteral("KWin native record: SKIP (built without qtwayland/plasma-wayland-protocols)"));
#endif
        const QString cardWhy = customNotificationReason();
        smokeLog(QStringLiteral("notifications: native=%1 custom=%2%3 -> %4")
                 .arg(capNativeNotification() ? "y" : "n", capCustomNotification() ? "y" : "n",
                      cardWhy.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(cardWhy),
                      (capNativeNotification() || capCustomNotification()) ? "PASS" : "FAIL"));
        // Settings hover preview: the same show/withdraw pair the pointer drives.
        // Only the creation is asserted — withdrawal tears the card down through
        // deleteLater / the helper's exit, so nothing observable has happened yet
        // by the next line. The dev button ("Card preview (3 s)") is where the
        // withdrawal gets checked, by eye.
        if (!m_settings->showNotifications() || !m_settings->showCapturePopup()) {
            smokeLog(QStringLiteral("card preview: SKIP (stylized card disabled)"));
            smokeLog(QStringLiteral("notification action order: SKIP (stylized card disabled)"));
        } else {
            const QString testOrder =
                QStringLiteral("folder,upload,copy,edit,link,qr,ocr,trim,delete");
            const QVariantMap orderOverrides{
                {QStringLiteral("notificationActionOrder"), testOrder},
                {QStringLiteral("hiddenNotifActions"), QString()},
            };
            previewCapturePopup(orderOverrides);
            const bool shown = !m_previewNotif.isNull();
            const bool orderForwarded =
                NotifCard::effectiveSettings(m_settings, orderOverrides)
                    .value(QStringLiteral("notificationActionOrder")).toString() == testOrder;
            hideCapturePopupPreview();
            // "Open a file": the dialog cannot run headless, so assert the routing
        // table it feeds — the part that decides which window a file lands in.
        {
            const bool ok = editableKindFor(QStringLiteral("/tmp/a.PNG")) == QLatin1String("image")
                            && editableKindFor(QStringLiteral("/tmp/b.mp4")) == QLatin1String("video")
                            && editableKindFor(QStringLiteral("/tmp/c.gif")) == QLatin1String("video")
                            && editableKindFor(QStringLiteral("/tmp/d.txt")).isEmpty();
            smokeLog(QStringLiteral("open own file: ")
                     + (ok ? QStringLiteral("PASS (image -> editor, recording -> trim, other -> refused)")
                           : QStringLiteral("FAIL (wrong routing)")));
        }
        smokeLog(QStringLiteral("card preview: ")
                     + (shown ? QStringLiteral("PASS") : QStringLiteral("FAIL (no card created)")));
        smokeLog(QStringLiteral("notification action order: ")
                     + (shown && orderForwarded
                            ? QStringLiteral("PASS (override reached card host)")
                            : QStringLiteral("FAIL")));
        }
        smokeLog(QStringLiteral("show in folder (FileManager1): ") + fileManager1Check());
        smokeLog(QStringLiteral("tray: ") + (trayAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no tray host)")));
        smokeLog(QStringLiteral("hotkeys: %1 (%2)").arg(hotkeysAvailable() ? "PASS" : "SKIP", hotkeyBackend()));
        if (m_hotkeys->available()) {
            // Live daemon check: every action's active binding (heals unbound
            // ones — same repair defineHotkeys runs at startup).
            int bad = 0;
            QStringList conflicts;
            const QStringList lines = hotkeyBindStatus(&bad, true, &conflicts);
            for (const QString &l : lines)
                smokeLog(QStringLiteral("  bind ") + l);
            smokeLog(QStringLiteral("  hotkey binds: ")
                     + (!conflicts.isEmpty()
                            ? QStringLiteral("CONFLICT %1 (key owned by another component)")
                                  .arg(conflicts.size())
                            : bad == 0 ? QStringLiteral("PASS")
                                       : QStringLiteral("HEALED %1 (re-run to confirm)").arg(bad)));
        }
        smokeLog(QStringLiteral("X11 hotkeys: ") + x11HotkeysCheck(hotkeyBackend()));
        smokeLog(QStringLiteral("desktop shortcuts: ") + desktopShortcutsCheck());
        smokeLog(QStringLiteral("OCR: %1, QR: %2").arg(
                 ocrAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no tesseract)"),
                 qrAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no zxing-cpp)")));
        smokeLog(QStringLiteral("tool letter shortcuts: ") + toolShortcutsCheck());
        smokeLog(QStringLiteral("history drag payload: ")
                 + (fileDragUri(QStringLiteral("/tmp/a b.png"))
                            == QStringLiteral("file:///tmp/a%20b.png")
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("history search + filters: ") + historyFilterCheck());
        smokeLog(QStringLiteral("Imgur Client-ID guard: ") + imgurSetupCheck());
        smokeLog(QStringLiteral("curl destination: ") + curlDestinationCheck());
        smokeLog(QStringLiteral("template variables: ") + templateVarsCheck());
        smokeLog(QStringLiteral("still GIF: ") + staticGifCheck());
        smokeLog(QStringLiteral("image conversion: ") + imageConvertCheck());
        {
            // Notification thumbnail drag: an unsaved image must materialize a
            // real temp file for the drop target (the new dragUri() branch).
            CaptureNotification nd(this, devTestImage(), QString(), QStringLiteral("image"));
            const QUrl du(nd.dragUri());
            smokeLog(QStringLiteral("notification drag payload: ")
                     + (du.isLocalFile() && QFile::exists(du.toLocalFile())
                            ? QStringLiteral("PASS (temp payload for unsaved capture)")
                            : QStringLiteral("FAIL")));
        }
        smokeNext();
    });

    // Diagnostics dump + optional-dependency report (the "Copy diagnostics" and
    // first-run "system check" paths). A missing optional dep on the dev box is
    // reported, never failed — the check itself running is the pass.
    m_smokeSteps.append([this] {
        const QString diag = systemDiagnostics();
        smokeLog(QStringLiteral("diagnostics: %1")
                     .arg(diag.size() > 40 ? QStringLiteral("PASS (%1 chars)").arg(diag.size())
                                           : QStringLiteral("FAIL (empty)")));
        const QVariantList rep = dependencyReport();
        int warn = 0;
        QStringList missing;
        for (const QVariant &v : rep) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("ok")).toBool() && m.value(QStringLiteral("warn")).toBool()) {
                ++warn;
                missing << m.value(QStringLiteral("label")).toString();
            }
        }
        smokeLog(QStringLiteral("dependency report: %1")
                     .arg(rep.isEmpty()
                              ? QStringLiteral("FAIL (no entries)")
                              : warn == 0
                                    ? QStringLiteral("PASS (%1 checks, all core deps present)").arg(rep.size())
                                    : QStringLiteral("PASS (%1 checks; %2 core dep(s) missing: %3)")
                                          .arg(rep.size())
                                          .arg(warn)
                                          .arg(missing.join(QStringLiteral(", ")))));

        // Diagnostic log: four assertions, because each one fails on its own.
        // (1) the handler is installed and the ring receives, (2) redaction
        // actually removed a secret and the home path, (3) a log file exists
        // or the reason it does not is legitimate, (4) the crash report still
        // renders in the shape a user would paste.
        const int before = DiagLog::bufferedLineCount();
        qWarning() << "smoke: log probe, token=smoketoken987 under" << QDir::homePath();
        const QString tail = DiagLog::recentLines(3);
        const bool grew = DiagLog::bufferedLineCount() > before;
        const bool clean = !tail.contains(QStringLiteral("smoketoken987"))
                           && !tail.contains(QDir::homePath());
        smokeLog(QStringLiteral("diagnostic log: %1")
                     .arg(!grew    ? QStringLiteral("FAIL (message handler is not recording)")
                          : !clean ? QStringLiteral("FAIL (a secret or the home path survived redaction)")
                                   : QStringLiteral("PASS (%1 lines buffered)")
                                         .arg(DiagLog::bufferedLineCount())));
        const QString lf = DiagLog::logFilePath();
        smokeLog(QStringLiteral("log file: %1")
                     .arg(lf.isEmpty()
                              ? QStringLiteral("SKIP (memory only - UNISIC_LOG=0 or the file could not be opened)")
                              : QStringLiteral("PASS (%1, %2 bytes)").arg(lf).arg(DiagLog::logFileSize())));
        {
            QTemporaryFile cf;
            bool ok = cf.open();
            if (ok) {
                CrashHandler::devWriteSyntheticReport(cf.handle(), SIGSEGV);
                cf.flush();
                cf.seek(0);
                const QString rep = QString::fromUtf8(cf.readAll());
                ok = rep.contains(QLatin1String("=== unisic crash report ==="))
                     && rep.contains(QLatin1String("SIGSEGV"))
                     && rep.contains(QLatin1String("backtrace"));
            }
            smokeLog(QStringLiteral("crash report: %1")
                         .arg(ok ? QStringLiteral("PASS (renders with signal and frames)")
                                 : QStringLiteral("FAIL (missing header, signal or backtrace)")));
        }

        // First-run welcome. The card itself is QML (the dev button shows it by
        // eye); what can silently break here is the one-shot LATCH — a settings
        // key that fails to persist would either re-show the card every launch
        // or hide it on a fresh install. Round-trip it through the real setter
        // and restore the user's value.
        {
            const bool original = m_settings->showWelcome();
            m_settings->setShowWelcome(false);
            const bool readFalse = !m_settings->showWelcome();
            m_settings->setShowWelcome(true);
            const bool readTrue = m_settings->showWelcome();
            m_settings->setShowWelcome(original);
            smokeLog(QStringLiteral("welcome screen: %1")
                         .arg(readFalse && readTrue
                                  ? QStringLiteral("PASS (one-shot latch round-trips; currently %1)")
                                        .arg(original ? QStringLiteral("pending") : QStringLiteral("seen"))
                                  : QStringLiteral("FAIL (showWelcome does not persist)")));
        }
        smokeNext();
    });

    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("hardware encoder: %1 (VAAPI=%2 NVENC=%3)")
                     .arg((m_vaapiAvailable || m_nvencAvailable) ? "PASS" : "SKIP")
                     .arg(m_vaapiAvailable ? "y" : "n", m_nvencAvailable ? "y" : "n"));
        smokeLog(QStringLiteral("encoder auto→%1 (nvenc works=%2, vaapi works=%3)")
                     .arg(m_recorder ? m_recorder->resolvedVideoEncoder() : QStringLiteral("?"),
                          FfmpegUtil::hardwareEncoderWorks(QStringLiteral("nvenc")) ? "y" : "n",
                          FfmpegUtil::hardwareEncoderWorks(QStringLiteral("vaapi")) ? "y" : "n"));
        if (!perAppAudioAvailable())
            smokeLog(QStringLiteral("per-app audio: SKIP (pw-dump/pw-record missing)"));
        else
            smokeLog(QStringLiteral("per-app audio: PASS (%1 active nodes)")
                         .arg(audioApplicationNodes().size()));
        if (!audioInputListAvailable())
            smokeLog(QStringLiteral("audio input devices: SKIP (pw-dump missing)"));
        else
            smokeLog(QStringLiteral("audio input devices: PASS (%1 sources)")
                         .arg(audioInputDevices().size()));
        const int segments = GifRecorder::replaySegmentCount(m_settings->instantReplaySeconds());
        smokeLog(QStringLiteral("instant replay ring: ")
                 + (segments >= 3 && segments <= 302
                        ? QStringLiteral("PASS (%1 bounded segments)").arg(segments)
                        : QStringLiteral("FAIL")));
        const bool trimTools = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
                               && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
        smokeLog(QStringLiteral("trim recording: %1, preview: %2").arg(
                 trimTools ? QStringLiteral("PASS (helpers found)")
                           : QStringLiteral("SKIP (ffmpeg/ffprobe missing)"),
                 capVideoPlayback() ? QStringLiteral("PASS (QtMultimedia)")
                                    : QStringLiteral("SKIP (no qt6-qtmultimedia)")));
        smokeNext();
    });

    // 1a2) trim cut: the saved file must hold the selection the window showed —
    // both ways of cutting — and the timeline must get its filmstrip/keyframes.
    m_smokeSteps.append([this] {
        trimCutCheck([this](const QString &result) {
            smokeLog(QStringLiteral("trim cut: ") + result);
            smokeNext();
        });
    });

    // 1a3) recording pause: excising a known pause span from a clip must drop its
    // duration by that span (the real filtergraph a paused recording runs).
    m_smokeSteps.append([this] {
        pauseExciseCheck([this](const QString &result) {
            smokeLog(QStringLiteral("recording pause excise: ") + result);
            smokeNext();
        });
    });

    // 1a4) recording quality + separate audio tracks: the percent the user sets
    // must reach ffmpeg as the same CRF an old config encoded at, and both named
    // audio tracks must survive the excise and the final MP4 conversion.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("video quality scale: ") + videoQualityCheck());
        audioTracksCheck([this](const QString &result) {
            smokeLog(QStringLiteral("separate audio tracks: ") + result);
            smokeNext();
        });
    });

    // 1a5) trimmer audio edit: a per-track mute/volume export must keep exactly
    // the surviving track next to the stream-copied video.
    m_smokeSteps.append([this] {
        trimAudioCheck([this](const QString &result) {
            smokeLog(QStringLiteral("trim audio edit: ") + result);
            smokeNext();
        });
    });

    // 1b) record border: flash the region frame on whichever host this
    // compositor uses (layer-shell / KWin fullscreen fallback / X11 /
    // XWayland helper) and take it down again.
    m_smokeSteps.append([this] {
        if (!capRecordBorder()) {
            smokeLog(QStringLiteral("record border: SKIP (no layer-shell/KWin/XWayland)"));
            smokeNext();
            return;
        }
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) {
            smokeLog(QStringLiteral("record border: FAIL (no screen)"));
            smokeNext();
            return;
        }
        const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        const int pw = qRound(screen->geometry().width() * dpr);
        const int ph = qRound(screen->geometry().height() * dpr);
        showRecordBorder(QRect(pw * 3 / 10, ph * 3 / 10, pw * 2 / 5, ph * 2 / 5), screen);
        const bool up = m_recordBorderWindow || m_recordBorderHelper;
        const bool helper = m_recordBorderHelper != nullptr;
        QTimer::singleShot(1200, this, [this, up, helper] {
            if (!recording())
                hideRecordBorder();
            smokeLog(QStringLiteral("record border (%1): %2")
                         .arg(helper ? QStringLiteral("xwayland helper")
                              : m_layerShellAvailable ? QStringLiteral("layer-shell")
                                                      : QStringLiteral("fullscreen window"),
                              up ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
            smokeNext();
        });
    });

    // 1c) KDE notification inhibition is an async D-Bus capability. Exercise
    // the real acquire/release pair, then continue without keeping DND active.
    m_smokeSteps.append([this] {
        if (!capDoNotDisturb()) {
            smokeLog(QStringLiteral("do not disturb: SKIP (not KDE)"));
            smokeNext();
            return;
        }
        m_dnd->acquire();
        QTimer::singleShot(500, this, [this] {
            const bool active = m_dnd->active();
            m_dnd->release();
            smokeLog(QStringLiteral("do not disturb: ")
                     + (active ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
            smokeNext();
        });
    });

    // 2) real fullscreen capture -> save -> history
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("capture (fullscreen)…"));
        m_capture->captureWorkspace([this](const QImage &img, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("  capture: FAIL (%1)").arg(err));
            else if (img.isNull())
                smokeLog(QStringLiteral("  capture: FAIL (null image)"));
            else {
                smokeLog(QStringLiteral("  capture: PASS (%1x%2)").arg(img.width()).arg(img.height()));
                const QString p = saveImageAuto(img, QStringLiteral("smoketest.png"));
                if (!p.isEmpty())
                    m_history->addEntry(p, img, QStringLiteral("image"));
                smokeLog(QStringLiteral("  save + history: ") + (p.isEmpty() ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
            }
            smokeNext();
        });
    });

    // 2b) single screen under the cursor (KWin CaptureActiveScreen / portal
    // crop fallback). Asserts it returns ONE screen, not the workspace.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("capture (screen under cursor)…"));
        QScreen *hint = QGuiApplication::screenAt(QCursor::pos());
        if (!hint)
            hint = QGuiApplication::primaryScreen();
        m_capture->captureActiveScreen(hint, [this](const QImage &img, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("  screen capture: FAIL (%1)").arg(err));
            else if (img.isNull())
                smokeLog(QStringLiteral("  screen capture: FAIL (null image)"));
            else {
                // One screen must not be wider than every screen combined
                // (only meaningful on a multi-monitor layout).
                int wsWidth = 0;
                for (QScreen *s : QGuiApplication::screens())
                    wsWidth = qMax(wsWidth, s->geometry().x() + s->geometry().width());
                const bool single = QGuiApplication::screens().size() < 2
                    || img.width() / qMax(1.0, img.devicePixelRatio()) < wsWidth;
                smokeLog(QStringLiteral("  screen capture: %1 (%2x%3)")
                             .arg(single ? QStringLiteral("PASS") : QStringLiteral("FAIL (workspace-sized)"))
                             .arg(img.width()).arg(img.height()));
            }
            smokeNext();
        });
    });

    // 2c) re-capture last region: parse the stored rect, capture its screen and
    // crop — the same math recaptureLastRegion() runs, without the after-capture
    // pipeline (no save/notification side effects in a smoke run).
    m_smokeSteps.append([this] {
        const QString stored = m_settings->lastCaptureRegion();
        const int bar = stored.indexOf(QLatin1Char('|'));
        const QStringList parts = stored.mid(bar + 1).split(QLatin1Char(','));
        if (bar <= 0 || parts.size() != 4) {
            smokeLog(QStringLiteral("re-capture region: SKIP (no region stored - take a region shot first)"));
            smokeNext();
            return;
        }
        QScreen *target = nullptr;
        for (QScreen *s : QGuiApplication::screens())
            if (s->name() == stored.left(bar)) { target = s; break; }
        if (!target) {
            smokeLog(QStringLiteral("re-capture region: SKIP (screen \"%1\" not connected)").arg(stored.left(bar)));
            smokeNext();
            return;
        }
        const QRect rect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
        smokeLog(QStringLiteral("re-capture region…"));
        m_capture->captureScreen(target, [this, target, rect](const QImage &img, const QString &err) {
            if (!err.isEmpty() || img.isNull()) {
                smokeLog(QStringLiteral("  re-capture: FAIL (%1)").arg(err.isEmpty() ? QStringLiteral("null image") : err));
            } else {
                const double s = double(img.width()) / target->geometry().width();
                const QRect crop = QRectF(rect.x() * s, rect.y() * s, rect.width() * s, rect.height() * s)
                                       .toAlignedRect().intersected(img.rect());
                smokeLog(QStringLiteral("  re-capture: %1 (stored %2x%3 -> crop %4x%5)")
                             .arg(crop.width() > 1 && crop.height() > 1 ? QStringLiteral("PASS")
                                                                       : QStringLiteral("FAIL (rect off-screen)"))
                             .arg(rect.width()).arg(rect.height())
                             .arg(crop.width()).arg(crop.height()));
            }
            smokeNext();
        });
    });

    // 2d) keystroke badge (pure render check; libinput access is reported
    // separately as a capability, not failed here).
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("keystroke badge: ") + keystrokeBadgeCheck());
        smokeLog(QStringLiteral("keystroke capture access: ")
                 + (keystrokeCaptureBlockedReason().isEmpty()
                        ? QStringLiteral("PASS (/dev/input readable)")
                        : QStringLiteral("SKIP (%1)").arg(keystrokeCaptureBlockedReason())));
        smokeNext();
    });

    // 2e) community themes: schema + live themes-folder round-trip.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("custom themes: ") + customThemeCheck());
        smokeNext();
    });

    // 3) post-capture editor open
    m_smokeSteps.append([this] {
        const int before = m_editorWindows;
        QImage t(64, 64, QImage::Format_ARGB32);
        t.fill(QColor(0x2E, 0x23, 0x6C));
        openEditor(t);
        smokeLog(QStringLiteral("editor open: ") + (m_editorWindows > before
                 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeNext();
    });

    // 3b) edit an existing capture from history (overwrite editor path)
    m_smokeSteps.append([this] {
        const QString p = saveImageAuto(devTestImage(), QStringLiteral("smoketest-edit.png"));
        if (p.isEmpty()) {
            smokeLog(QStringLiteral("edit from history: FAIL (couldn't save source)"));
            smokeNext();
            return;
        }
        m_history->addEntry(p, devTestImage(), QStringLiteral("image"));
        const int before = m_editorWindows;
        editFromHistory(p);
        smokeLog(QStringLiteral("edit from history: ") + (m_editorWindows > before
                 ? QStringLiteral("PASS (overwrite editor)") : QStringLiteral("FAIL")));
        smokeNext();
    });

    // 3b2) drag and drop import: the router behind the main window's DropArea.
    // The drag itself cannot be synthesized here, so what is asserted is where
    // each payload LANDS - and that nothing is refused silently.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("drop import: ") + importDropCheck());
        smokeNext();
    });

    // 3b3) Ctrl+V import in the main window (pixels -> a new editor document,
    // a copied file -> the same router, anything else -> refused out loud).
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("paste import: ") + clipboardImportCheck());
        smokeNext();
    });

    // 3c) copy last capture — seed a known image, invoke, clipboard must fill.
    m_smokeSteps.append([this] {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        devTestImage().save(&buf, "PNG");
        m_lastCaptureData = png;
        copyLastCapture();
        smokeLog(QStringLiteral("copy last capture: ")
                 + (QGuiApplication::clipboard()->image().isNull()
                        ? QStringLiteral("FAIL (clipboard empty)")
                        : QStringLiteral("PASS")));
        smokeNext();
    });

    // 3c2) KDE clipboard-history hint — the offer must carry
    // x-kde-force-image-copy or Klipper never records the image (issue #51).
    m_smokeSteps.append([this] {
        copyImageToClipboard(devTestImage());
        smokeLog(QStringLiteral("clipboard history hint: ") + clipboardHistoryHintCheck());
        smokeNext();
    });

    // 3d) floating preview window (pin/opacity/drag)
    m_smokeSteps.append([this] {
        const bool ok = openPreview(devTestImage());
        smokeLog(QStringLiteral("preview window: ") + (ok
                 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeNext();
    });

    // 3e) history favorite round-trip (star -> role reads back -> unstar)
    m_smokeSteps.append([this] {
        m_history->addEntry(QString(), devTestImage(), QStringLiteral("image"));
        m_history->setFavorite(0, true);
        const bool fav = m_history->data(m_history->index(0), HistoryStore::FavoriteRole).toBool();
        smokeLog(QStringLiteral("history favorite: ") + (fav ? QStringLiteral("PASS")
                                                             : QStringLiteral("FAIL")));
        m_history->setFavorite(0, false);
        smokeNext();
    });


    // 3e3) alternate hotkeys: multi-binding round-trip on a scratch action.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("alternate hotkeys: ") + altHotkeysCheck());
        smokeNext();
    });

    // 3e3b) text annotations: multi-line + styling must render into the composite.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("text render: ") + textRenderCheck());
        smokeNext();
    });

    // 3e3d) Ctrl+V text/image annotations — retained in the exported composite.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("clipboard paste: ") + clipboardPasteCheck());
        smokeNext();
    });

    // 3e3e) capture delay uses the real one-shot timer without opening a
    // portal dialog. The lower bound leaves scheduling jitter room while still
    // proving that a CLI-style override was not executed immediately.
    m_smokeSteps.append([this] {
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        setNextCaptureDelayMs(1100);
        withDelay([this, elapsed] {
            smokeLog(QStringLiteral("capture delay: ")
                     + (elapsed->elapsed() >= 1000 ? QStringLiteral("PASS")
                                                    : QStringLiteral("FAIL (fired early)")));
            smokeNext();
        });
    });

    // 3e3ef) The window we hide so it stays out of the shot has to come back.
    // Real hide/show against the compositor, not a flag check - the whole point
    // is the round trip, and a window left down is worse than one in the frame.
    m_smokeSteps.append([this] {
        hideOnCaptureCheck([this](const QString &r) {
            smokeLog(QStringLiteral("hide while capturing: ") + r);
            smokeNext();
        });
    });

    // 3e3ga) Text watermark is a one-shot image-pixel export pass; it must
    // retain dimensions so every independent after-capture consumer agrees.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("watermark: ") + watermarkCheck(m_settings));
        smokeLog(QStringLiteral("watermark preview: ") + watermarkPreviewCheck());
        smokeNext();
    });

    // 3e3gb) Callout stays an ordinary vector annotation: no extra canvas
    // buffer and its tail must survive the image-space composite.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("callout: ") + calloutCheck());
        smokeNext();
    });

    // 3e3h) Shift snaps geometry to a grid and constrains line angles/ratios.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("shift snap: ") + shiftSnapCheck());
        smokeNext();
    });

    // 3e3i) QR generation reuses the optional zxing-cpp already present for
    // decoding. Keep the smoke path offline and bounded to a tiny matrix.
    m_smokeSteps.append([this] {
        if (!qrAvailable()) {
            smokeLog(QStringLiteral("QR preview: SKIP (no zxing-cpp)"));
        } else {
            const QImage qr = qrPreviewImage(QStringLiteral("https://example.invalid/unisic-smoke"));
            smokeLog(QStringLiteral("QR preview: ")
                     + (!qr.isNull() && qr.size() == QSize(360, 360)
                            ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        }
        smokeNext();
    });

    // 3e3j) A helper program that never exits must not hold the after-capture
    // pipeline open forever. Real child process, real ceiling, real kill.
    m_smokeSteps.append([this] {
        externalActionTimeoutCheck([this](const QString &r) {
            smokeLog(QStringLiteral("external action timeout: ") + r);
            smokeNext();
        });
    });

    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("external action: ") + externalActionCheck());
        const CaptureTask task = taskFromId(QStringLiteral("all"));
        smokeLog(QStringLiteral("task preset: ")
                 + (task.active && task.upload && task.copy && task.save && task.edit
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        QTemporaryDir dir;
        const QString cliPath = dir.isValid()
                                    ? saveImageTo(devTestImage(), dir.path(),
                                                  QStringLiteral("cli-smoke.png"))
                                    : QString();
        smokeLog(QStringLiteral("CLI output format: ")
                 + (!cliPath.isEmpty() && QImageReader(cliPath).format().toLower() == "png"
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("measure: ") + measureToolsCheck());
        smokeNext();
    });

    // 3e3c) editable shapes: select / restyle / move / undo round-trip.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("shape edit: ") + shapeEditCheck());
        smokeNext();
    });

    // 3e3d) magnifier: a synthetic drag places a 2x loupe centred on the source.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("magnifier: ") + magnifyCheck());
        smokeNext();
    });

    // 3e3d2) eyedropper: a click adopts the pixel colour under the cursor.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("eyedropper: ") + eyedropperCheck());
        smokeNext();
    });

    // 3e3d3) ZIP export: archive two fixture PNGs and confirm the zip lands.
    m_smokeSteps.append([this] {
        if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
            smokeLog(QStringLiteral("zip export: SKIP (zip not installed)"));
            smokeNext();
            return;
        }
        const QString dir = QDir::tempPath();
        QStringList files;
        for (int i = 1; i <= 2; ++i) {
            QImage im(48, 48, QImage::Format_ARGB32);
            im.fill(i == 1 ? Qt::green : Qt::magenta);
            const QString p = dir + QStringLiteral("/unisic-smoke-zip-%1.png").arg(i);
            im.save(p, "PNG");
            files << p;
        }
        const QString dest = dir + QStringLiteral("/unisic-smoke-zip.zip");
        exportFilesToZip(files, dest, [this, files, dest](bool ok, const QString &msg) {
            smokeLog(QStringLiteral("zip export: ")
                     + (ok ? QStringLiteral("PASS (%1)").arg(msg)
                           : QStringLiteral("FAIL (%1)").arg(msg)));
            for (const QString &f : files)
                QFile::remove(f);
            QFile::remove(dest);
            smokeNext();
        });
    });

    // 3e3e) pixel loupe: hover placement, edge flip, scroll zoom + collapse.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("pixel loupe: ") + pixelLoupeCheck());
        smokeNext();
    });

    // 3e4) capture sound: a player must exist and the WAV must extract.
    m_smokeSteps.append([this] {
        const bool player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty();
        if (!player) {
            smokeLog(QStringLiteral("capture sound: SKIP (no pw-play/paplay/aplay)"));
        } else {
            const QString id = m_settings->captureSound();
            const QString source = (id == QLatin1String("off") || bundledSoundIds().contains(id))
                                       ? QStringLiteral("bundled")
                                       : QStringLiteral("custom");
            playCaptureSound();
            smokeLog(QStringLiteral("capture sound: PASS (played '%1', %2)").arg(id, source));
        }
        smokeNext();
    });

    // 3e5) recording sound: same player requirement, separate setting/cue.
    m_smokeSteps.append([this] {
        const bool player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty();
        if (!player) {
            smokeLog(QStringLiteral("recording sound: SKIP (no pw-play/paplay/aplay)"));
        } else {
            const QString id = m_settings->recordingSound();
            const QString source = (id == QLatin1String("off") || bundledSoundIds().contains(id))
                                       ? QStringLiteral("bundled")
                                       : QStringLiteral("custom");
            playRecordingSound();
            smokeLog(QStringLiteral("recording sound: PASS (played '%1', %2)").arg(id, source));
        }
        smokeNext();
    });

    // 3e5b) trash sound: fixed cue — the qrc WAV must extract and play.
    m_smokeSteps.append([this] {
        const bool player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty();
        if (!player) {
            smokeLog(QStringLiteral("trash sound: SKIP (no pw-play/paplay/aplay)"));
        } else {
            playTrashSound();
            smokeLog(QStringLiteral("trash sound: PASS (played fixed 'trash' cue)"));
        }
        smokeNext();
    });

    // 3e6) capture-on-release: synthetic drag confirms once; toggle off = never.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("capture on release: ") + captureOnReleaseCheck());
        smokeNext();
    });

    // 3e7) overlay mode identity: every capture purpose has its own name for
    // the overlay to show, and the badge preference round-trips.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("overlay mode badge: ") + overlayModeCheck(settings()));
        smokeNext();
    });

    // 3e8) the overlay preview in Settings: an entry per capture purpose and a
    // toolbar that survives the clamp in every configured position.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("overlay preview: ") + overlayPreviewCheck());
        smokeNext();
    });

    // 3f0) OCR auto language: the tessdata scan enumerates installed langpacks
    // and the effective spec is non-empty (auto-detected list or manual spec).
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        const QString detected = OcrEngine::detectedLanguages();
        const QString effective = effectiveOcrLanguages();
        // Also pin the script→langpack narrowing (deterministic, no OSD data):
        // a distinct script → its pack + eng; Latin/Cyrillic → keep the full set.
        const QString av = QStringLiteral("eng+pol+ara+jpn");
        const bool mapOk =
            OcrEngine::languagesForScript(QStringLiteral("Arabic"), av) == QLatin1String("eng+ara")
            && OcrEngine::languagesForScript(QStringLiteral("Japanese"), av) == QLatin1String("eng+jpn")
            && OcrEngine::languagesForScript(QStringLiteral("Latin"), av).isEmpty()
            && OcrEngine::languagesForScript(QStringLiteral("Cyrillic"), av).isEmpty();
        if (effective.isEmpty() || !mapOk)
            smokeLog(QStringLiteral("ocr auto language: FAIL (%1)")
                         .arg(effective.isEmpty() ? QStringLiteral("empty spec")
                                                  : QStringLiteral("script map")));
        else
            smokeLog(QStringLiteral("ocr auto language: PASS (installed: %1; using: %2; script detect: %3)")
                         .arg(detected.isEmpty() ? QStringLiteral("none") : detected, effective,
                              OcrEngine::scriptDetectionAvailable() ? QStringLiteral("OSD")
                                                                    : QStringLiteral("load-all (no osd pack)")));
#else
        smokeLog(QStringLiteral("ocr auto language: SKIP (built without tesseract)"));
#endif
        smokeNext();
    });

    // 3f) OCR recognition — a real tesseract run on a rendered known token
    // (digits: language-neutral, works with any installed traineddata).
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        QImage t(320, 120, QImage::Format_ARGB32);
        t.fill(Qt::white);
        {
            QPainter p(&t);
            p.setPen(Qt::black);
            QFont f;
            f.setPixelSize(64);
            f.setBold(true);
            p.setFont(f);
            p.drawText(t.rect(), Qt::AlignCenter, QStringLiteral("1234"));
        }
        m_ocr->recognize(t, effectiveOcrLanguages(), m_settings->ocrAutoLanguage(), [this](const QString &text, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("ocr recognize: FAIL (%1)").arg(err));
            else if (text.contains(QLatin1String("1234")))
                smokeLog(QStringLiteral("ocr recognize: PASS"));
            else
                smokeLog(QStringLiteral("ocr recognize: FAIL (got '%1')").arg(text.simplified()));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("ocr recognize: SKIP (built without tesseract)"));
        smokeNext();
#endif
    });

    // 3f1a) UI translations: the bundled .qm loads and a known string translates.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("language: ") + languageCheck());
        smokeNext();
    });

    // 3f2) OCR word boxes — the selectable-text overlay's data source.
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("ocr boxes: FAIL (%1)").arg(err));
            else if (words.size() >= 4)
                smokeLog(QStringLiteral("ocr boxes: PASS (%1 glyphs)").arg(words.size()));
            else
                smokeLog(QStringLiteral("ocr boxes: FAIL (%1 glyphs)").arg(words.size()));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("ocr boxes: SKIP (built without tesseract)"));
        smokeNext();
#endif
    });

    // 3f3) OCR selected text → permanent highlight/redaction annotations.
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
            smokeLog(!err.isEmpty() ? QStringLiteral("ocr highlight + redact: FAIL (%1)").arg(err)
                                     : QStringLiteral("ocr highlight + redact: ") + ocrHighlightCheck(words));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("ocr highlight + redact: SKIP (built without tesseract)"));
        smokeNext();
#endif
    });

    // 3f4) Auto-redact: pattern → redaction bars with no selection made.
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
            smokeLog(!err.isEmpty() ? QStringLiteral("auto-redact pattern: FAIL (%1)").arg(err)
                                     : QStringLiteral("auto-redact pattern: ") + ocrRedactPatternCheck(words));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("auto-redact pattern: SKIP (built without tesseract)"));
        smokeNext();
#endif
    });

    // 3f6) Recording cursor overlay — pointer/halo/ripple compositing.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("cursor overlay: ") + cursorOverlayCheck());
        smokeNext();
    });

    // 3f5) Annotation style presets — the JSON-in-a-settings-key storage.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("style presets: ") + stylePresetsCheck(m_settings));
        smokeNext();
    });

    // 4) short GIF recording (fullscreen, ~3s, auto-stop)
    m_smokeSteps.append([this] {
        if (!recordingAvailable()) {
            smokeLog(QStringLiteral("recording: SKIP"));
            smokeNext();
            return;
        }
        smokeLog(QStringLiteral("recording (GIF fullscreen, ~3s)…"));
        // `live` dies with THIS smoke recording: without it the 3s auto-stop
        // outlives an early failure (e.g. portal dialog cancelled) and would
        // kill an unrelated recording the user starts right after.
        auto live = std::make_shared<bool>(true);
        auto done = std::make_shared<QMetaObject::Connection>();
        auto fail = std::make_shared<QMetaObject::Connection>();
        auto begun = std::make_shared<QMetaObject::Connection>();
        *done = connect(m_recorder, &GifRecorder::finished, this, [this, live, done, fail, begun](const QString &f) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  recording: PASS (%1)").arg(f));
            smokeNext();
        });
        *fail = connect(m_recorder, &GifRecorder::failed, this, [this, live, done, fail, begun](const QString &e) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  recording: FAIL (%1)").arg(e));
            smokeNext();
        });
        // Start the 3s clock only once actually RECORDING: a cold-start ScreenCast
        // portal negotiation (the session's first recording) can exceed 3s, and
        // stopping while still Starting cancels it ("cancelled") instead of
        // testing it. started() marks the Starting→Recording edge.
        *begun = connect(m_recorder, &GifRecorder::started, this, [this, live] {
            QTimer::singleShot(3000, this, [this, live] { if (*live && recording()) stopRecording(); });
        });
        startGifFullScreen();
    });

    // 4b) short video recording (MP4 fullscreen, ~3s, auto-stop) — the video
    // pipeline (format selection, convertVideo, poster extraction) is distinct
    // from the GIF path and needs its own pass/fail line.
    m_smokeSteps.append([this] {
        if (!recordingAvailable()) {
            smokeLog(QStringLiteral("video recording: SKIP"));
            smokeNext();
            return;
        }
        smokeLog(QStringLiteral("recording (video fullscreen, ~3s)…"));
        auto live = std::make_shared<bool>(true);
        auto done = std::make_shared<QMetaObject::Connection>();
        auto fail = std::make_shared<QMetaObject::Connection>();
        auto begun = std::make_shared<QMetaObject::Connection>();
        *done = connect(m_recorder, &GifRecorder::finished, this, [this, live, done, fail, begun](const QString &f) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  video recording: PASS (%1)").arg(f));
            smokeNext();
        });
        *fail = connect(m_recorder, &GifRecorder::failed, this, [this, live, done, fail, begun](const QString &e) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  video recording: FAIL (%1)").arg(e));
            smokeNext();
        });
        // Same as the GIF step: begin the 3s clock only once RECORDING, so a slow
        // cold-start negotiation doesn't get stopped mid-Starting.
        *begun = connect(m_recorder, &GifRecorder::started, this, [this, live] {
            QTimer::singleShot(3000, this, [this, live] { if (*live && recording()) stopRecording(); });
        });
        startVideoScreen();
    });

    // 5) capture notification
    m_smokeSteps.append([this] {
        QImage t(48, 48, QImage::Format_ARGB32);
        t.fill(QColor(0xC8, 0xAC, 0xD6));
        auto *n = showCaptureNotification(t, QString(), QStringLiteral("image"), false);
        smokeLog(QStringLiteral("notification: ") + (n ? QStringLiteral("PASS (shown)")
                 : QStringLiteral("SKIP (disabled or no server/layer-shell)")));
        smokeNext();
    });

    // 5b) settings export/import round-trip (metaobject serialization has
    // regression history — the [%General] key folding).
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("settings round-trip: ") + settingsRoundTripCheck());
        smokeLog(QStringLiteral("record page mode: ") + recordPageModeCheck());
        smokeNext();
    });

    // 5c) update check: comparator semantics (synchronous, always runs), then
    // a live feed query — manual mode so the run never toasts or burns the
    // once-per-version notification.
    m_smokeSteps.append([this] {
        const bool cmpOk = UpdateVersion::isNewer(QStringLiteral("0.5.2"), QStringLiteral("0.5.1"))
                        && !UpdateVersion::isNewer(QStringLiteral("v0.5.1"), QStringLiteral("0.5.1"))
                        && UpdateVersion::isNewer(QStringLiteral("0.5.1"), QStringLiteral("0.5.1b"))
                        && !UpdateVersion::isNewer(QStringLiteral("0.5.0"), QStringLiteral("0.5.1"));
        smokeLog(QStringLiteral("version compare: ")
                 + (cmpOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        const QString gate = autoRestartBlockers();
        smokeLog(QStringLiteral("auto-restart gate: ")
                 + (gate.isEmpty() ? QStringLiteral("idle")
                                   : QStringLiteral("deferred (%1)").arg(gate)));
        m_updater->check(true, [this](const UpdateChecker::Result &r) {
            // Offline is a SKIP, not a FAIL — dev machines must keep a green run.
            smokeLog(QStringLiteral("update check: ")
                     + (r.ok ? QStringLiteral("PASS (latest %1 - %2)")
                                   .arg(r.latestVersion.isEmpty() ? QStringLiteral("none")
                                                                  : r.latestVersion,
                                        r.updateAvailable ? QStringLiteral("update available")
                                                          : QStringLiteral("up to date"))
                             : QStringLiteral("SKIP (network: %1)").arg(r.error)));
            smokeNext();
        });
    });

    // 5c2) which packaging channel owns updates, and whether the buttons agree.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("install channel: ") + installChannelCheck());
        smokeNext();
    });

    // 5d) native "Install now" via install.sh: dry-run the fetch + terminal
    // detection (never spawns a terminal or installs). Offline is a SKIP.
    m_smokeSteps.append([this] {
        m_updater->verifyInstallerReady([this](bool ok, const QString &detail) {
            smokeLog(QStringLiteral("installer update: ")
                     + (ok ? QStringLiteral("PASS (%1)").arg(detail)
                           : QStringLiteral("SKIP (%1)").arg(detail)));
            smokeNext();
        });
    });

    // 6) upload (needs a real destination + a public target — left manual)
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("upload: SKIP (active destination '%1'); run a real upload manually")
                 .arg(m_settings->activeDestination()));
        smokeNext();
    });

    // 6b) the server editor's "Test upload" button. Unlike the step above this
    // one CAN run unattended: it targets a scratch file:// directory through
    // the real curl transport, so it costs no quota and touches no server.
    m_smokeSteps.append([this] {
        destinationTestCheck([this](const QString &result) {
            smokeLog(QStringLiteral("server test upload: ") + result);
            smokeNext();
        });
    });

    // Essentials: filename tokens, save routing, countdown, volume, channel.
    m_smokeSteps.append([this] {
        const QString name = makeFileName();
        smokeLog(QStringLiteral("filename: %1 (counter=%2, dateSubfolders=%3, stripMeta=%4) - %5")
                     .arg(name)
                     .arg(m_settings->filenameCounter())
                     .arg(m_settings->dateSubfolders() ? QStringLiteral("on") : QStringLiteral("off"),
                          m_settings->stripMetadata() ? QStringLiteral("on") : QStringLiteral("off"),
                          name.isEmpty() ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
        smokeLog(QStringLiteral("record countdown: %1s; start sound: %2; sound volume: %3 %; ask-where-to-save: %4")
                     .arg(m_settings->recordCountdownSec())
                     .arg(m_settings->recordStartSound())
                     .arg(m_settings->soundVolume())
                     .arg(m_settings->askWhereToSave() ? QStringLiteral("on") : QStringLiteral("off")));
        smokeLog(QStringLiteral("update channel: %1; autostart: %2")
                     .arg(m_settings->updateChannel(),
                          autostartEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled")));
        smokeNext();
    });

    // 7) cleanup: close every editor/preview window the run opened and give the
    // clipboard back - F8 must verify and leave the desktop as it found it.
    m_smokeSteps.append([this] {
        int closed = 0;
        for (const QPointer<QQuickWindow> &w : std::as_const(m_smokeWindows)) {
            if (w) {
                w->close();
                ++closed;
            }
        }
        m_smokeWindows.clear();
        smokeLog(QStringLiteral("cleanup: closed %1 test window(s)").arg(closed));
        smokeLog(QStringLiteral("clipboard: ") + restoreClipboardAfterSmoke());
        smokeNext();
    });

    smokeNext();
}

QString AppContext::desktopShortcutsCheck()
{
    const ShortcutBinder::Backend b = ShortcutBinder::detect();
    if (hotkeysAvailable())
        return QStringLiteral("SKIP (native hotkey backend active)");
    if (!ShortcutBinder::autoInstallable(b))
        return QStringLiteral("SKIP (no writable store here; copy-paste only)");
    // Only ever touches Unisic's own entries, so the round-trip leaves the
    // user's other custom shortcuts untouched.
    const ShortcutBinder::Result ins = ShortcutBinder::install(b, desktopShortcutBindings());
    const ShortcutBinder::Result rem = ShortcutBinder::remove(b);
    if (ins.ok && ins.written > 0 && rem.ok)
        return QStringLiteral("PASS (%1 install+remove round-trip, %2 entries)")
            .arg(ShortcutBinder::desktopName(b)).arg(ins.written);
    return QStringLiteral("FAIL (%1: install ok=%2 n=%3, remove ok=%4)")
        .arg(ShortcutBinder::desktopName(b))
        .arg(ins.ok ? QStringLiteral("y") : QStringLiteral("n")).arg(ins.written)
        .arg(rem.ok ? QStringLiteral("y") : QStringLiteral("n"));
}

void AppContext::devTestDesktopShortcuts()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: desktop shortcuts: %1").arg(desktopShortcutsCheck()));
}
