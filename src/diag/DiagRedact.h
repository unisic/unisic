#pragma once
#include <QDir>
#include <QRegularExpression>
#include <QString>

// The privacy choke point for the diagnostic log. EVERY line passes through
// redact() before it is stored in the ring or written to the file, so no call
// site has to remember to be careful and no future qDebug() can leak by
// omission. The log is never uploaded - the user pastes it into an issue by
// hand - but "the user pastes it" is exactly why it must not contain their
// upload tokens: destinations.json holds real Authorization headers and API
// keys, and UploadManager logs request headers on failure.
//
// Header-only and free of any app state so tests/DiagRedactTest.cpp can pin the
// contract without a QGuiApplication, and so CrashHandler is not tempted to
// call it (it must not: this allocates).
namespace DiagRedact {

// Marker left in place of a removed value. Deliberately visible: a redacted
// log should read as redacted, not as a log that never had the field.
inline const char *marker() { return "<redacted>"; }

// `home` and `user` are injected so the test is hermetic. Order matters:
// secrets are masked BEFORE the home path is shortened, otherwise a token that
// happens to contain the home path would be half-rewritten.
inline QString redact(QString line, const QString &home, const QString &user)
{
    // Secret-shaped fields, in three spellings, deliberately masked to
    // different extents. Masking too much costs debug value, masking too
    // little leaks a live credential, so each spelling gets the narrowest cut
    // that still covers the whole value:
    static const QString names =
        QStringLiteral("authorization|proxy-authorization|x-api-key|api[-_]?key|"
                       "access[-_]?token|refresh[-_]?token|client[-_]?secret|"
                       "client[-_]?id|token|secret|password|passwd|pwd");

    //  a) a quoted value  ("client_secret": "xyz")  -> just what is in quotes
    static const QRegularExpression quoted(
        QStringLiteral("(?i)\"?\\b(") + names
        + QStringLiteral(")\\b\"?\\s*[:=]\\s*\"([^\"]*)\""));
    line.replace(quoted, QStringLiteral("\"\\1\": \"") + QLatin1String(marker())
                             + QStringLiteral("\""));

    //  b) an assignment  (handle_token=u123)  -> the token only, so a portal
    //     handle stays recognisable as a handle in the surrounding line
    static const QRegularExpression assigned(
        QStringLiteral("(?i)\\b(") + names
        + QStringLiteral(")\\b(\\s*=\\s*)([^\\s,;&}\"]+)"));
    line.replace(assigned, QStringLiteral("\\1\\2") + QLatin1String(marker()));

    //  c) a header  (Authorization: Client-ID abc123)  -> to end of line, the
    //     HTTP semantic: the value is everything after the colon, spaces and
    //     scheme words included
    static const QRegularExpression headerLine(
        QStringLiteral("(?i)\\b(") + names + QStringLiteral(")(\\s*:\\s*).*$"));
    line.replace(headerLine, QStringLiteral("\\1\\2") + QLatin1String(marker()));

    // Bearer/Basic credentials that appear without a header name.
    static const QRegularExpression bearer(
        QStringLiteral("(?i)\\b(bearer|basic)\\s+([A-Za-z0-9._~+/=-]{8,})"));
    line.replace(bearer, QStringLiteral("\\1 ") + QLatin1String(marker()));

    // URL userinfo (scheme://user:pass@host) and query strings. A destination
    // URL is not a secret, but "?key=..." on one is, and an ftp:// URL carries
    // the password inline.
    static const QRegularExpression userinfo(
        QStringLiteral("([a-zA-Z][a-zA-Z0-9+.-]*://)[^/@\\s]+@"));
    line.replace(userinfo, QStringLiteral("\\1") + QLatin1String(marker()) + QStringLiteral("@"));
    static const QRegularExpression query(
        QStringLiteral("([a-zA-Z][a-zA-Z0-9+.-]*://[^\\s?]+)\\?[^\\s\"']*"));
    line.replace(query, QStringLiteral("\\1?") + QLatin1String(marker()));

    // The home path last: it is not a secret, but it carries the account name
    // into every file path in the log, and "~" reads better in an issue.
    if (!home.isEmpty())
        line.replace(home, QStringLiteral("~"));
    // A bare login name still leaks through /run/user/1000/... and D-Bus names.
    if (!user.isEmpty() && user.size() >= 3)
        line.replace(QStringLiteral("/home/") + user, QStringLiteral("~"));

    return line;
}

inline QString redact(const QString &line)
{
    static const QString home = QDir::homePath();
    static const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    return redact(line, home, user);
}

} // namespace DiagRedact
