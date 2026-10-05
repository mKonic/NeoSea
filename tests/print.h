#pragma once
// gtest prints a QString that fails a comparison as text, not as code units.
#include <QString>
#include <ostream>

QT_BEGIN_NAMESPACE
inline void PrintTo(const QString &s, std::ostream *os) { *os << '"' << s.toStdString() << '"'; }
QT_END_NAMESPACE
