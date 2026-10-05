#pragma once
// The few settings that belong to this computer rather than the library: where
// the library lives, the interface language, the window's place. They live in
// the system's per-app config folder, since they must exist before the library
// is found. Everything about the writing stays in the library itself.

#include <QJsonObject>
#include <QString>

namespace neosea {

class AppSettings {
public:
    static QString path();
    static QJsonObject read();
    static void write(const QJsonObject &o);
    static void set(const QString &key, const QJsonValue &v);

    // NEO's own default, so both apps find the same library
    static QString defaultLibraryDir();
    static QString libraryDir();
};

} // namespace neosea
