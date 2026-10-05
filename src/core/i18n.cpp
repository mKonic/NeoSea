#include "core/i18n.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>

#include <unicode/plurrule.h>
#include <unicode/locid.h>

#include <memory>

namespace neosea {

namespace {

struct State {
    QString locale = QStringLiteral("en");
    QJsonObject dict;
    QJsonObject base;
    std::unique_ptr<icu::PluralRules> plural;
    std::unique_ptr<icu::PluralRules> englishPlural;
};

State &state()
{
    static State s;
    return s;
}

std::unique_ptr<icu::PluralRules> rulesFor(const QString &code)
{
    UErrorCode err = U_ZERO_ERROR;
    icu::Locale loc(QString(code).replace('-', '_').toUtf8().constData());
    std::unique_ptr<icu::PluralRules> r(icu::PluralRules::forLocale(loc, err));
    if (U_FAILURE(err)) r.reset();
    return r;
}

QString category(icu::PluralRules *rules, double n)
{
    if (!rules) return n == 1 ? QStringLiteral("one") : QStringLiteral("other");
    icu::UnicodeString kw = rules->select(n);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(kw.getBuffer()), kw.length());
}

bool validCode(const QString &code)
{
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z]{2,3}(-[a-zA-Z0-9]{2,8})*$"));
    return re.match(code).hasMatch();
}

QJsonObject readLocaleFile(const QString &dir, const QString &code)
{
    if (!validCode(code)) return {};
    QFile f(QDir(dir).filePath(code + QStringLiteral(".json")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString pick(const QJsonValue &entry, const QVariantMap &vars, icu::PluralRules *rules)
{
    if (entry.isObject()) {
        const QJsonObject o = entry.toObject();
        double n = 0;
        const QVariant v = vars.value(QStringLiteral("n"));
        if (v.isValid() && (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong
                            || v.typeId() == QMetaType::Double || v.typeId() == QMetaType::UInt))
            n = v.toDouble();
        QString s = o.value(category(rules, n)).toString();
        if (s.isEmpty()) s = o.value(QStringLiteral("other")).toString();
        return s.isEmpty() ? QString() : s;
    }
    if (entry.isString() && !entry.toString().isEmpty()) return entry.toString();
    return {};
}

QString fill(const QString &str, const QVariantMap &vars)
{
    if (vars.isEmpty()) return str;
    static const QRegularExpression re(QStringLiteral("\\{(\\w+)\\}"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(str);
    while (it.hasNext()) {
        auto m = it.next();
        out += str.mid(last, m.capturedStart() - last);
        const QString k = m.captured(1);
        if (!vars.contains(k)) {
            out += m.captured(0);
        } else {
            const QVariant v = vars.value(k);
            const auto id = v.typeId();
            if (id == QMetaType::Int || id == QMetaType::LongLong || id == QMetaType::UInt
                || id == QMetaType::ULongLong)
                out += I18n::formatNumber(v.toLongLong());
            else if (id == QMetaType::Double)
                out += QLocale(state().locale).toString(v.toDouble(), 'g', 12);
            else
                out += v.toString();
        }
        last = m.capturedEnd();
    }
    out += str.mid(last);
    return out;
}

} // namespace

void I18n::setLocale(const QString &code, const QJsonObject &dict, const QJsonObject &english)
{
    State &s = state();
    s.locale = code.isEmpty() ? QStringLiteral("en") : code;
    s.dict = dict;
    s.base = english;
    s.plural = rulesFor(s.locale);
    if (!s.englishPlural) s.englishPlural = rulesFor(QStringLiteral("en"));
}

QString I18n::resolve(const QString &dir, const QString &wantedIn)
{
    QString wanted = wantedIn;
    wanted.replace('_', '-');
    const auto langs = languages(dir);
    QStringList tries;
    if (!wanted.isEmpty()) tries << wanted << wanted.section('-', 0, 0);
    for (const QString &c : tries)
        for (const auto &l : langs)
            if (l.code.compare(c, Qt::CaseInsensitive) == 0) return l.code;
    return {};
}

void I18n::load(const QString &dir, const QString &wanted)
{
    QString code = resolve(dir, wanted);
    if (code.isEmpty()) code = QStringLiteral("en");
    const QJsonObject english = readLocaleFile(dir, QStringLiteral("en"));
    QJsonObject dict;
    if (code == QLatin1String("en")) {
        dict = english;
    } else {
        // a regional file holds only what differs from its base language
        const QString baseCode = code.section('-', 0, 0);
        if (baseCode != code) dict = readLocaleFile(dir, baseCode);
        const QJsonObject own = readLocaleFile(dir, code);
        for (auto it = own.begin(); it != own.end(); ++it) dict.insert(it.key(), it.value());
    }
    setLocale(code, dict, english);
}

QString I18n::t(const QString &key, const QVariantMap &vars)
{
    State &s = state();
    QString str = pick(s.dict.value(key), vars, s.plural.get());
    if (str.isNull()) {
        if (!s.englishPlural) s.englishPlural = rulesFor(QStringLiteral("en"));
        str = pick(s.base.value(key), vars, s.englishPlural.get());
    }
    if (str.isNull()) str = key;
    return fill(str, vars);
}

QString I18n::locale() { return state().locale; }

QString I18n::formatNumber(qint64 n)
{
    QLocale loc(state().locale);
    loc.setNumberOptions(QLocale::DefaultNumberOptions);
    return loc.toString(n);
}

QString I18n::pluralCategory(double n) { return category(state().plural.get(), n); }

QList<Language> I18n::languages(const QString &dir)
{
    QList<Language> out;
    static const QRegularExpression re(QStringLiteral("^([a-zA-Z]{2,3}(?:-[a-zA-Z0-9]{2,8})*)\\.json$"));
    for (const QString &f : QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files)) {
        auto m = re.match(f);
        if (!m.hasMatch()) continue;
        const QJsonObject data = readLocaleFile(dir, m.captured(1));
        if (data.isEmpty()) continue;
        QString name = data.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("name")).toString();
        out.push_back({m.captured(1), name.isEmpty() ? m.captured(1) : name});
    }
    bool hasEn = false;
    for (const auto &l : out) hasEn |= l.code == QLatin1String("en");
    if (!hasEn) out.push_back({QStringLiteral("en"), QStringLiteral("English")});
    std::sort(out.begin(), out.end(), [](const Language &a, const Language &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

} // namespace neosea
