#include "core/spell.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QStringEncoder>

#include <hunspell/hunspell.h>

namespace neosea::spell {

QString norm(const QString &word)
{
    QString w = word;
    w.replace(QChar(0x2019), '\'');
    qsizetype a = 0, b = w.size();
    while (a < b && w[a] == '\'') a++;
    while (b > a && w[b - 1] == '\'') b--;
    return w.mid(a, b - a);
}

namespace {

// acronyms and shouting are legal
bool legal(const QString &raw)
{
    static const QRegularExpression re(QStringLiteral("^[\\p{Lu}'’]+$"));
    return re.match(raw).hasMatch();
}

QString bare(const QString &s)
{
    QString d = s.normalized(QString::NormalizationForm_D);
    static const QRegularExpression marks(QStringLiteral("\\p{M}"));
    d.remove(marks);
    return d.toLower();
}

// a stammer (E-eu, N-não, Wh-what): each short piece starts the next
bool stammers(const QStringList &bits)
{
    if (bits.size() < 2) return false;
    for (qsizetype i = 0; i + 1 < bits.size(); ++i)
        if (bits[i].size() > 3 || !bare(bits[i + 1]).startsWith(bare(bits[i]))) return false;
    return true;
}

} // namespace

QList<Occurrence> occurrences(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("[\\p{L}\\p{M}'’]+(?:-[\\p{L}\\p{M}'’]+)*"));
    static const QRegularExpression piece(QStringLiteral("[\\p{L}\\p{M}'’]+"));
    QList<Occurrence> out;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString run = m.captured();
        const bool stammer = stammers(run.split('-'));
        Occurrence o{int(m.capturedStart()), int(m.capturedEnd()), {}, {}};
        for (auto pit = piece.globalMatch(run); pit.hasNext();) {
            const auto q = pit.next();
            const QString word = norm(q.captured());
            if (word.size() < 2 || legal(q.captured())) continue;
            if (stammer && q.capturedEnd() < run.size()) continue; // only the word it lands on
            o.parts << Piece{int(m.capturedStart() + q.capturedStart()), int(m.capturedStart() + q.capturedEnd()), word};
        }
        if (o.parts.isEmpty()) continue;
        QString joined = run;
        if (run.contains('-') && !stammer && !legal(joined.remove('-'))) o.whole = norm(run);
        out << o;
    }
    return out;
}

QList<QPair<int, int>> wrong(const QList<Occurrence> &found, const std::function<bool(const QString &)> &correct)
{
    QList<QPair<int, int>> out;
    for (const Occurrence &o : found) {
        if (!o.whole.isEmpty() && correct(o.whole)) continue;
        bool any = false;
        for (const Piece &p : o.parts)
            if (!correct(p.word)) {
                out << qMakePair(p.start, p.end);
                any = true;
            }
        if (!any && !o.whole.isEmpty()) out << qMakePair(o.start, o.end); // every piece fine, the whole not
    }
    return out;
}

const QList<Language> &languages()
{
    static const QList<Language> all{
        {"en-US", "English (US)"}, {"en-GB", "English (UK)"}, {"en-CA", "English (Canada)"}, {"en-AU", "English (Australia)"},
        {"fr", "Français"},        {"es", "Español"},         {"de", "Deutsch"},          {"nl", "Nederlands"},
        {"pl", "Polski"},          {"pt-BR", "Português (Brasil)"}, {"ro", "Română"},     {"ru", "Русский"},
        {"el", "Ελληνικά"},
    };
    return all;
}

QString defaultLanguage(const QString &ui)
{
    auto known = [](const QString &c) {
        for (const Language &l : languages())
            if (l.code == c) return true;
        return false;
    };
    const QString u = ui.isEmpty() ? QStringLiteral("en") : ui;
    if (known(u)) return u;
    // NEO's Portuguese interface is Brazilian; the dictionary is too
    if (u == "pt" || u == "pt-BR") return QStringLiteral("pt-BR");
    const QString base = u.section('-', 0, 0);
    return known(base) ? base : QStringLiteral("en-US");
}

QString dictionaryBase(const QString &resources, const QString &code)
{
    const QString bundled = resources + "/dictionaries/" + code + "/index";
    if (QFileInfo::exists(bundled + ".aff") && QFileInfo::exists(bundled + ".dic")) return bundled;
    // a distribution's hunspell packages: en-US → en_US, fr → fr_FR
    QStringList names{QString(code).replace('-', '_')};
    if (!code.contains('-')) names << code + "_" + code.toUpper();
    for (const QString &dir : {QStringLiteral("/usr/share/hunspell"), QStringLiteral("/usr/share/myspell/dicts"), QStringLiteral("/usr/share/myspell")})
        for (const QString &n : names)
            if (QFileInfo::exists(dir + "/" + n + ".aff") && QFileInfo::exists(dir + "/" + n + ".dic")) return dir + "/" + n;
    return {};
}

struct Speller::Engine {
    Hunhandle *h = nullptr;
    QString code;
    QStringConverter::Encoding enc = QStringConverter::Utf8;
    QByteArray encName;
    ~Engine()
    {
        if (h) Hunspell_destroy(h);
    }
    QByteArray encode(const QString &w) const
    {
        QString s = w.normalized(QString::NormalizationForm_C);
        // Romanian: the legacy cedillas are the comma-below letters
        if (code == "ro") s.replace(QChar(0x015F), QChar(0x0219)).replace(QChar(0x0163), QChar(0x021B)).replace(QChar(0x015E), QChar(0x0218)).replace(QChar(0x0162), QChar(0x021A));
        QStringEncoder e(encName.constData());
        return e.isValid() ? QByteArray(e(s)) : s.toUtf8();
    }
    QString decode(const char *b) const
    {
        QStringDecoder d(encName.constData());
        return d.isValid() ? QString(d(QByteArray(b))) : QString::fromUtf8(b);
    }
};

Speller::Speller() = default;
Speller::~Speller() = default;

bool Speller::load(const QString &base, const QString &code, const QStringList &custom)
{
    if (base.isEmpty()) return false;
    auto e = std::make_unique<Engine>();
    e->h = Hunspell_create(QFile::encodeName(base + ".aff").constData(), QFile::encodeName(base + ".dic").constData());
    if (!e->h) return false;
    e->code = code;
    e->encName = QByteArray(Hunspell_get_dic_encoding(e->h)).trimmed();
    if (e->encName.isEmpty()) e->encName = "UTF-8";
    for (const QString &w : custom)
        if (!w.isEmpty()) Hunspell_add(e->h, e->encode(w).constData());
    // only now let go of the previous language: a failed load keeps it
    QMutexLocker lock(&m_lock);
    m_engine = std::move(e);
    return true;
}

bool Speller::loaded() const
{
    QMutexLocker lock(&m_lock);
    return bool(m_engine);
}

QString Speller::language() const
{
    QMutexLocker lock(&m_lock);
    return m_engine ? m_engine->code : QString();
}

bool Speller::check(const QString &word) const
{
    QMutexLocker lock(&m_lock);
    if (!m_engine || word.isEmpty()) return true;
    return Hunspell_spell(m_engine->h, m_engine->encode(word).constData()) != 0;
}

QStringList Speller::suggest(const QString &word) const
{
    QMutexLocker lock(&m_lock);
    QStringList out;
    if (!m_engine || word.isEmpty()) return out;
    char **list = nullptr;
    const int n = Hunspell_suggest(m_engine->h, &list, m_engine->encode(word).constData());
    for (int i = 0; i < n && out.size() < 6; ++i) out << m_engine->decode(list[i]);
    if (list) Hunspell_free_list(m_engine->h, &list, n);
    return out;
}

void Speller::add(const QString &word)
{
    QMutexLocker lock(&m_lock);
    if (m_engine && !word.isEmpty()) Hunspell_add(m_engine->h, m_engine->encode(word).constData());
}

} // namespace neosea::spell
