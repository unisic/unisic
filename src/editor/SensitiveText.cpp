#include "SensitiveText.h"

#include <QHostAddress>
#include <QRegularExpression>

#include <algorithm>

namespace {

bool luhnValid(const QString &digits)
{
    int sum = 0;
    bool dbl = false;
    for (int i = digits.size() - 1; i >= 0; --i) {
        int d = digits.at(i).digitValue();
        if (dbl && (d *= 2) > 9)
            d -= 9;
        sum += d;
        dbl = !dbl;
    }
    return sum % 10 == 0;
}

// ISO 13616: the first four characters move to the end, letters become 10..35,
// and the whole number leaves 1 modulo 97.
bool ibanValid(const QString &compact)
{
    if (compact.size() < 15 || compact.size() > 34)
        return false;
    const QString r = compact.mid(4) + compact.left(4);
    int rem = 0;
    for (const QChar c : r) {
        const int v = c.isDigit() ? c.digitValue() : c.toLatin1() - 'A' + 10;
        rem = (v > 9 ? rem * 100 : rem * 10) + v;
        rem %= 97;
    }
    return rem == 1;
}

QString onlyDigits(const QString &s)
{
    QString out;
    for (const QChar c : s)
        if (c.isDigit())
            out += c;
    return out;
}

bool mixedCaseAndDigits(const QString &s)
{
    bool up = false, low = false, dig = false;
    for (const QChar c : s) {
        up |= c.isUpper();
        low |= c.isLower();
        dig |= c.isDigit();
    }
    return up && low && dig;
}

} // namespace

QVector<SensitiveMatch> findSensitiveText(const QString &text)
{
    QVector<SensitiveMatch> out;
    auto overlaps = [&](int s, int len) {
        for (const SensitiveMatch &m : std::as_const(out))
            if (s < m.start + m.length && m.start < s + len)
                return true;
        return false;
    };
    // `group` picks the part to cover: the password of a URL, the value after
    // "token:", the whole match otherwise. `valid` vets what the pattern alone
    // cannot (a checksum, a date).
    auto scan = [&](const QRegularExpression &re, const QString &kind, int group,
                    bool (*valid)(const QString &) = nullptr) {
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int s = m.capturedStart(group);
            const int len = m.capturedLength(group);
            if (s < 0 || len <= 0 || overlaps(s, len) || (valid && !valid(m.captured(group))))
                continue;
            out.append({s, len, kind});
        }
    };
    using RE = QRegularExpression;

    // Secrets first: an e-mail-looking "pass@host" inside a URL is a password.
    static const RE urlCreds(QStringLiteral("[A-Za-z][A-Za-z0-9+.-]*://[^\\s:/@]+:([^\\s/@]+)@"));
    static const RE keyValue(QStringLiteral(
        "\\b(?:pass(?:word|wd)?|pwd|secret|token|api[_-]?key|access[_-]?key|client[_-]?secret)"
        "[\"']?\\s*[:=]\\s*[\"']?([^\\s\"',;]{3,})"), RE::CaseInsensitiveOption);
    static const RE bearer(QStringLiteral("\\bbearer\\s+([A-Za-z0-9._~+/=-]{8,})"),
                           RE::CaseInsensitiveOption);
    static const RE knownToken(QStringLiteral(
        "\\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|glpat-[A-Za-z0-9_-]{20,}"
        "|sk-[A-Za-z0-9_-]{20,}|AKIA[0-9A-Z]{16}|xox[abposr]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{35})"));
    static const RE jwt(QStringLiteral(
        "\\beyJ[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}"));
    // Any other key-shaped run: 32+ characters mixing upper case, lower case
    // and digits. Lower-case hex (git hashes, UUIDs) stays visible.
    static const RE longKey(QStringLiteral("(?<![A-Za-z0-9+/=_-])[A-Za-z0-9+/=_-]{32,}(?![A-Za-z0-9+/=_-])"));
    scan(urlCreds, QStringLiteral("secret"), 1);
    scan(keyValue, QStringLiteral("secret"), 1);
    scan(bearer, QStringLiteral("secret"), 1);
    scan(knownToken, QStringLiteral("secret"), 0);
    scan(jwt, QStringLiteral("secret"), 0);
    scan(longKey, QStringLiteral("secret"), 0, mixedCaseAndDigits);

    static const RE email(QStringLiteral(
        "[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,}"));
    scan(email, QStringLiteral("email"), 0);

    static const RE iban(QStringLiteral("\\b[A-Z]{2}\\d{2}(?: ?[A-Z0-9]{4}){2,7}(?: ?[A-Z0-9]{1,3})?\\b"));
    scan(iban, QStringLiteral("iban"), 0, [](const QString &s) {
        return ibanValid(QString(s).remove(QLatin1Char(' ')));
    });

    // The whole grouped run or nothing: a 26-digit account number must not
    // pass as the card hiding in its first 16 digits.
    static const RE card(QStringLiteral("(?<!\\d[ -]|[\\d-])\\d(?:[ -]?\\d){12,18}(?![\\d-]|[ -]\\d)"));
    scan(card, QStringLiteral("card"), 0, [](const QString &s) { return luhnValid(onlyDigits(s)); });

    static const RE mac(QStringLiteral(
        "\\b[0-9A-Fa-f]{2}([:-])[0-9A-Fa-f]{2}(?:\\1[0-9A-Fa-f]{2}){4}\\b"));
    scan(mac, QStringLiteral("mac"), 0);

    // No octet range check and a space allowed beside a dot: OCR reads a 0 as
    // 6 or 8 and splits "166.66" into two words, and an address left visible
    // over one misread digit costs more than a version number covered.
    static const RE ipv4(QStringLiteral("(?<![\\d.])\\d{1,3}(?: ?\\. ?\\d{1,3}){3}(?:/\\d{1,2})?(?!\\d|\\.\\d)"));
    scan(ipv4, QStringLiteral("ip"), 0);
    // A clock (12:30:05) fits the pattern too; QHostAddress turns it away.
    static const RE ipv6(QStringLiteral(
        "(?<![0-9A-Fa-f:.])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![0-9A-Fa-f:])"));
    scan(ipv6, QStringLiteral("ip"), 0, [](const QString &s) {
        return s.split(QLatin1Char(':'), Qt::SkipEmptyParts).size() >= 2
               && QHostAddress(s).protocol() == QAbstractSocket::IPv6Protocol;
    });

    // Phone and account numbers: nine or more digits, grouped by spaces,
    // dashes or brackets, an optional leading +. A date and time
    // ("2026-11-09 01") reaches nine digits too and is left alone.
    // ponytail: a row of short table numbers ("100 200 300") reads as one;
    // the review list is the way out until a layout-aware rule earns its keep.
    static const RE number(QStringLiteral("(?<![\\w.,+])\\+?\\(?\\d(?:[ ()-]{0,2}\\d){8,}(?!\\w|[.,]\\d)"));
    scan(number, QStringLiteral("number"), 0, [](const QString &s) {
        static const QRegularExpression date(QStringLiteral("\\d{4}-\\d{2}-\\d{2}"));
        return !date.match(s).hasMatch();
    });

    std::sort(out.begin(), out.end(),
              [](const SensitiveMatch &a, const SensitiveMatch &b) { return a.start < b.start; });
    return out;
}
