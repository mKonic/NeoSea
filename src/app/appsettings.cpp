#include "app/appsettings.h"

#include "core/json.h"
#include "core/storage.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace neosea {

QString AppSettings::path()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("settings.json");
}

QJsonObject AppSettings::read() { return readJsonFile(path()).value_or(QJsonObject{}).toObject(); }

void AppSettings::write(const QJsonObject &o)
{
    QDir().mkpath(QFileInfo(path()).absolutePath());
    writeDurable(path(), toJson(o));
}

void AppSettings::set(const QString &key, const QJsonValue &v)
{
    QJsonObject o = read();
    if (v.isNull() || v.isUndefined()) o.remove(key);
    else o.insert(key, v);
    write(o);
}

QString AppSettings::defaultLibraryDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("NEO Library");
}

QString AppSettings::libraryDir()
{
    const QString chosen = read().value("libraryDir").toString();
    if (!chosen.isEmpty() && QFileInfo(chosen).isDir()) return chosen;
    return defaultLibraryDir();
}

} // namespace neosea
