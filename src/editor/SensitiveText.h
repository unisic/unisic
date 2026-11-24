#pragma once

#include <QString>
#include <QVector>

// What Auto-redact covers in the recognized text of a screenshot. Pure string
// work, so it is unit-tested without Tesseract (tests/AnnotationCanvasTest.cpp).
struct SensitiveMatch {
    int start = 0;
    int length = 0;
    // "secret", "email", "iban", "card", "mac", "ip" or "number"; the editor
    // names each one in the user's language (qml/components/RedactReview.qml).
    QString kind;
};

// Every match in `text`, ascending by start, never overlapping: where two
// detectors claim the same characters the more specific one wins (a card
// number is not also a long number).
QVector<SensitiveMatch> findSensitiveText(const QString &text);
