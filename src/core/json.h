#pragma once
// JSON as NEO writes it: JSON.stringify(value, null, 2). Same indentation,
// same number spelling (80000, not 80000.0), so a library written by either
// app reads the same in a text editor and a sync tool sees no churn from the
// spelling alone. Qt sorts object keys; NEO keeps insertion order. That is the
// one difference, and it is harmless to every reader.

#include <QByteArray>
#include <QJsonValue>

namespace neosea {

QByteArray toJson(const QJsonValue &v);

} // namespace neosea
