#pragma once
#include <QString>
#include <QVersionNumber>

// Release-tag comparison for the update checker. Header-only and free of
// QObject/network so the semantics are unit-testable (VersionCompareTest).
namespace UpdateVersion {

// "v0.5.1b" -> QVersionNumber(0,5,1) with suffix "b". A leading v/V (the
// release-tag convention) is stripped; whatever follows the numeric-dot part
// is the pre-release suffix.
inline QVersionNumber numericPart(const QString &s, QString *suffix = nullptr)
{
    QString t = s.trimmed();
    if (t.startsWith(QLatin1Char('v')) || t.startsWith(QLatin1Char('V')))
        t.remove(0, 1);
    qsizetype idx = 0;
    const QVersionNumber num = QVersionNumber::fromString(t, &idx);
    if (suffix)
        *suffix = num.isNull() ? QString() : t.mid(idx);
    return num;
}

// True when `remote` (a published release tag — GitHub's releases/latest
// never returns prereleases) supersedes the running `local` version. Equal
// numerics count only when the RUNNING build carries a prerelease suffix
// ("0.5.1b" upgrades to the final "0.5.1"). An unparsable remote is never an
// update.
inline bool isNewer(const QString &remote, const QString &local)
{
    QString remoteSuffix, localSuffix;
    const QVersionNumber r = numericPart(remote, &remoteSuffix);
    const QVersionNumber l = numericPart(local, &localSuffix);
    if (r.isNull())
        return false;
    if (r != l)
        return r > l;
    return !localSuffix.isEmpty() && remoteSuffix.isEmpty();
}

} // namespace UpdateVersion
