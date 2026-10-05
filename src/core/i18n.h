#pragma once
// Interface translations, read straight from NEO's own locales/<code>.json
// files so a language added upstream works here unchanged. The English text
// is the key: t("Cancel") shows the translation when the language has one and
// the English original otherwise. A plural-sensitive entry maps CLDR plural
// categories ("one", "few", "other"…) to strings, chosen by the {n} variable.

#include <QString>
#include <QVariantMap>
#include <QJsonObject>
#include <QList>

namespace neosea {

inline const QString kAppName = QStringLiteral("NeoSea");

struct Language {
    QString code;
    QString name; // in its own words, from "_meta"
};

class I18n {
public:
    // The locales directory and the language to use; falls back from fr-CA
    // to fr to English.
    static void load(const QString &localesDir, const QString &wanted);
    // For tests: install dictionaries directly.
    static void setLocale(const QString &code, const QJsonObject &dict, const QJsonObject &english);

    static QString t(const QString &key, const QVariantMap &vars = {});
    static QString locale();
    static QString formatNumber(qint64 n);
    static QList<Language> languages(const QString &localesDir);
    // the best language NEO has for a wanted code, or empty
    static QString resolve(const QString &localesDir, const QString &wanted);
    // CLDR plural category of n in the current language ("one", "other", …)
    static QString pluralCategory(double n);
};

inline QString t(const QString &key, const QVariantMap &vars = {}) { return I18n::t(key, vars); }
// Marks a string for translation where it's defined; translated when shown.
inline QString tk(const QString &key) { return key; }

} // namespace neosea
