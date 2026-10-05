#include "core/json.h"

#include <QJsonArray>
#include <QJsonObject>

#include <charconv>
#include <cmath>

namespace neosea {

namespace {

void quote(QByteArray &out, const QString &s)
{
    out += '"';
    for (QChar c : s) {
        const char16_t u = c.unicode();
        switch (u) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (u < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", u);
                out += buf;
            } else {
                out += QString(c).toUtf8();
            }
        }
    }
    out += '"';
}

void number(QByteArray &out, double d)
{
    if (!std::isfinite(d)) { out += "null"; return; }
    char buf[64];
    // shortest round-trip spelling, like JavaScript's
    auto res = std::to_chars(buf, buf + sizeof buf, d);
    out.append(buf, res.ptr - buf);
}

void write(QByteArray &out, const QJsonValue &v, int depth)
{
    const QByteArray pad(depth * 2, ' ');
    const QByteArray inner((depth + 1) * 2, ' ');
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: out += "null"; break;
    case QJsonValue::Bool: out += v.toBool() ? "true" : "false"; break;
    case QJsonValue::Double: number(out, v.toDouble()); break;
    case QJsonValue::String: quote(out, v.toString()); break;
    case QJsonValue::Array: {
        const QJsonArray a = v.toArray();
        if (a.isEmpty()) { out += "[]"; break; }
        out += "[\n";
        for (qsizetype i = 0; i < a.size(); ++i) {
            out += inner;
            write(out, a.at(i), depth + 1);
            if (i + 1 < a.size()) out += ',';
            out += '\n';
        }
        out += pad + ']';
        break;
    }
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        if (o.isEmpty()) { out += "{}"; break; }
        out += "{\n";
        qsizetype i = 0;
        for (auto it = o.begin(); it != o.end(); ++it, ++i) {
            out += inner;
            quote(out, it.key());
            out += ": ";
            write(out, it.value(), depth + 1);
            if (i + 1 < o.size()) out += ',';
            out += '\n';
        }
        out += pad + '}';
        break;
    }
    }
}

} // namespace

QByteArray toJson(const QJsonValue &v)
{
    QByteArray out;
    write(out, v, 0);
    return out;
}

} // namespace neosea
