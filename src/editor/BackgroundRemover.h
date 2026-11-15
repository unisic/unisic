#pragma once
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QNetworkReply>
#include <QSysInfo>
#include <QTimer>
#include <qqmlregistration.h>

#include <functional>
#include <memory>

#include "ocr/PaddleOcr.h"

class QNetworkAccessManager;
class QNetworkReply;

// Cuts the subject out of a picture with BiRefNet lite (MIT) on ONNX Runtime.
//
// Nothing of it is linked, so it is the one feature of Unisic that is allowed
// to be missing (docs/dev/packaging.md "Background removal"): the runtime is
// dlopen()ed when first asked for - bundled in the Flatpak, a distro package
// anywhere else, or a one-time download into the user's data folder on x86_64
// Linux - and the 181 MB model is always a one-time download there too. No
// root, no compile gate: every build has the code.
//
// It also hosts the "Better OCR model" (PaddleOcr.h): the same runtime, the
// same pinned downloads and the same Linux-only rule, so its download, state
// and removal live here too. One download runs at a time, for either feature.
class BackgroundRemover : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppContext")

    // Probing loads the runtime, so it happens on the first read of
    // runtimeFound, not at startup.
    Q_PROPERTY(bool runtimeFound READ runtimeFound NOTIFY stateChanged)
    Q_PROPERTY(QString runtimeInfo READ runtimeInfo NOTIFY stateChanged)
    Q_PROPERTY(bool modelInstalled READ modelInstalled NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    // Linux only: Windows has no package to install and no download, so the
    // editor and Settings leave the feature out there.
    Q_PROPERTY(bool available READ available CONSTANT)
    // x86_64 Linux outside the Flatpak: a missing runtime is fetched with the model.
    Q_PROPERTY(bool canDownloadRuntime READ canDownloadRuntime CONSTANT)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY stateChanged)
    Q_PROPERTY(QString downloadSizeText READ downloadSizeText NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    // Any download, for either feature: the other feature's button waits.
    Q_PROPERTY(bool fetching READ fetching NOTIFY stateChanged)
    // The installed OCR model, "tiny" or "small", or "" when none is.
    Q_PROPERTY(QString ocrModel READ ocrModel NOTIFY stateChanged)
    // The size picked in Settings for the next downloadOcr(). Not persisted:
    // which model is installed is what the files on disk say (ocrModel).
    Q_PROPERTY(QString ocrChoice READ ocrChoice WRITE setOcrChoice NOTIFY stateChanged)
    Q_PROPERTY(QString ocrDownloadSizeText READ ocrDownloadSizeText NOTIFY stateChanged)
    Q_PROPERTY(bool ocrDownloading READ ocrDownloading NOTIFY stateChanged)
    Q_PROPERTY(QString ocrError READ ocrError NOTIFY stateChanged)

public:
    using Done = std::function<void(const QImage &cutOut, const QString &error)>;

    explicit BackgroundRemover(QObject *parent = nullptr);
    ~BackgroundRemover() override;

    bool runtimeFound();
    QString runtimeInfo();
    bool modelInstalled() const;
    bool ready() { return runtimeFound() && modelInstalled(); }
    static bool available() { return QSysInfo::kernelType() == QLatin1String("linux"); }
    bool canDownloadRuntime() const;
    bool hasDownloads() const;
    QString downloadSizeText();
    bool fetching() const { return m_reply != nullptr || m_extracting; }
    bool downloading() const { return fetching() && m_job == BackgroundJob; }
    qreal progress() const { return m_progress; }
    bool busy() const { return m_busy; }
    QString error() const { return m_errorJob == BackgroundJob ? m_error : QString(); }

    QString ocrModel() const;
    QString ocrChoice() const { return m_ocrChoice; }
    void setOcrChoice(const QString &model);
    QString ocrDownloadSizeText();
    // One size's model files alone, for the comparison Settings shows.
    Q_INVOKABLE QString ocrModelSizeText(const QString &model) const;
    bool ocrDownloading() const { return fetching() && m_job == OcrJob; }
    QString ocrError() const { return m_errorJob == OcrJob ? m_error : QString(); }
    // Fetches the chosen OCR model (and the runtime when it can), then removes
    // the other size: one model on disk at a time.
    Q_INVOKABLE void downloadOcr();
    // Removes the OCR model, and the downloaded runtime unless background
    // removal still needs it.
    Q_INVOKABLE void deleteOcrModel();
    // What a run of the installed OCR model needs. Loads the runtime on first
    // use; `api` is null without one, `det`/`rec` are empty without a model.
    PaddleOcr::Setup ocrSetup();

    // Fetches what is missing (the runtime when it can, then the model),
    // each checked against its pinned size and SHA-256 before it is kept.
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    // Removes the downloaded model, and the runtime unless the OCR model still
    // needs it. A runtime already loaded stays mapped until Unisic quits; the
    // files go now.
    Q_INVOKABLE void deleteDownloads();

    // The picture with everything but the subject made transparent, off the
    // GUI thread. `done` runs on the GUI thread, and only while `context` is
    // alive; destroying `context` stops the run. One run at a time. The loaded
    // model stays in memory for a minute after a run, so the next one skips
    // the load.
    void remove(const QImage &img, QObject *context, Done done);

    static QString modelPath();

signals:
    void stateChanged();
    void progressChanged();
    // An OCR model was downloaded or removed (not on a failed or stopped
    // download): AppContext switches OCR to it, or back to the default.
    void ocrModelChanged();

private:
    struct Runtime;
    enum Job { BackgroundJob, OcrJob };
    void probe();
    void start(Job job, QList<int> pins);
    void fetchNext();
    void fetch(int pin);
    void installRuntime(const QString &archive);
    void fail(const QString &message);
    QNetworkAccessManager *nam();

    std::shared_ptr<Runtime> m_rt;
    bool m_probed = false;
    QString m_runtimeInfo;
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QList<int> m_queue;              // pins still to fetch, in order
    bool m_extracting = false;
    Job m_job = BackgroundJob;       // what the running (or last) download was for
    Job m_errorJob = BackgroundJob;  // whose row shows m_error
    QString m_ocrChoice = QStringLiteral("tiny");
    bool m_cancelled = false;
    qint64 m_doneBytes = 0;
    qint64 m_totalBytes = 0;
    qreal m_progress = 0;
    bool m_busy = false;
    QString m_error;
    QTimer m_idle;                   // drops the loaded model a minute after the last run
};
