#pragma once
#include <QDate>
#include <QString>

class QJsonObject;

// Screenshots per local day, for the activity chart and the year review at
// unisic.app/stats. The file is a small JSON object the page parses in the
// browser, nothing is uploaded:
//   {"format": "unisic.stats", "version": 1, "id": "<random uuid>",
//    "screenshots": {"2026-10-28": 14, ...}, "sig": "<hex>"}
// Dates and counts only - no file names, paths or images. One key per day
// with a capture, ~25 bytes each: ten years of daily use stay under 100 KB.
// The metric is its own key so recordings can be counted later beside it
// without a format bump. `id` is random, made once with the file and kept on
// every write: the page takes the higher count per day for copies of one
// counter (a backup, the same file dropped twice) and adds separate counters
// up (another computer, the install before a reinstall). `sig` covers id and
// counts, so a hand edit is refused here and on the page alike.
namespace CaptureStats {

struct Counts {
    int today = 0;
    int month = 0;
    int year = 0;
    int total = 0;
    bool readable = true; // false: the file exists but is not a stats file
};

// <AppDataLocation>/unisic.stats, beside the capture history, so a dev build
// counts into unisic-dev's directory and never into the stable app's.
QString filePath();

// Counts one screenshot on `day` (read, add, QSaveFile). A file that exists
// but does not parse or whose sig does not match is preserved as unisic.stats.broken-<ts>, the way
// HistoryStore keeps a broken history.json, and a new count starts.
// False when the file could not be written.
bool record(const QString &path, QDate day);

// Totals for the day, month and year `day` falls in, plus all time.
Counts read(const QString &path, QDate day);

// The hex `sig` for `id` and `screenshots`. Public for the test vector that
// the website's scripts/stats-check.ts checks too.
QByteArray signature(const QString &id, const QJsonObject &screenshots);

}
