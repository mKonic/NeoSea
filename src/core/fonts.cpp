#include "core/fonts.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QImage>

namespace neosea {

QString resourcesDir()
{
    const QString env = qEnvironmentVariable("NEOSEA_RESOURCES");
    if (!env.isEmpty() && QFileInfo(env).isDir()) return env;
    if (QCoreApplication::instance()) {
        const QString installed = QDir(QCoreApplication::applicationDirPath()).filePath("../share/neosea");
        if (QFileInfo(installed + "/locales").isDir()) return QDir(installed).canonicalPath();
    }
    return QStringLiteral(NEOSEA_SOURCE_RESOURCES);
}

int registerBundledFonts()
{
    static int loaded = -1;
    if (loaded >= 0) return loaded;
    loaded = 0;
    const QDir d(resourcesDir() + "/fonts");
    for (const QString &f : d.entryList({"*.woff2", "*.woff", "*.otf", "*.ttf"}, QDir::Files)) {
        // Cyrillic-only cuts registered under the same family as the Latin
        // ones would win the match for Latin text too (CSS keeps them apart
        // with unicode-range, which Qt has no notion of)
        if (f.startsWith("oswald-cyrillic") || f.startsWith("playfair-display-cyrillic")) continue;
        if (QFontDatabase::addApplicationFont(d.filePath(f)) >= 0) loaded++;
    }
    return loaded;
}

QPaintDevice *pointDevice()
{
    static QImage *img = [] {
        auto *i = new QImage(1, 1, QImage::Format_ARGB32);
        const int dpm = int(72 / 0.0254 + 0.5);
        i->setDotsPerMeterX(dpm);
        i->setDotsPerMeterY(dpm);
        return i;
    }();
    return img;
}

QStringList bodyFontChoices()
{
    return {"Gelasio", "TeX Gyre Pagella", "Libre Baskerville", "Alegreya", "Source Serif Pro", "Jost", "iA Writer Quattro"};
}

QString bodyFontFamily(const QString &choice)
{
    static const QHash<QString, QString> legacy{
        {"Georgia", "Gelasio"},
        {"Palatino", "TeX Gyre Pagella"},
        {"Baskerville", "Libre Baskerville"},
        {"Hoefler Text", "Alegreya"},
        {"Iowan Old Style", "Source Serif Pro"},
        {"Cambria", "Source Serif Pro"},
        {"Constantia", "Libre Baskerville"},
        // the face registers under its own name
        {"iA Writer Quattro", "iA Writer Quattro S"},
        {"Jost", "Jost*"},
    };
    if (choice.isEmpty()) return "Gelasio";
    if (legacy.contains(choice)) return legacy.value(choice);
    return choice;
}

QString dropCapFamily(const QString &style)
{
    if (style == "none") return {};
    if (style == "fantasy") return "TeX Gyre Chorus";
    if (style == "scifi") return "Jost*";
    return "Libre Bodoni";
}

} // namespace neosea
