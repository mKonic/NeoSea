#include "core/bookmodel.h"

#include "core/i18n.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <unicode/brkiter.h>
#include <unicode/unistr.h>

#include <memory>

namespace neosea {

QStringList chapterOrder(const QJsonObject &book)
{
    QStringList out;
    for (const auto &v : book.value("chapterOrder").toArray()) out << v.toString();
    return out;
}

void setChapterOrder(QJsonObject &book, const QStringList &order)
{
    book.insert("chapterOrder", QJsonArray::fromStringList(order));
}

QString chapterKind(const QString &chId, const QJsonObject &book)
{
    const QString k = book.value("chapterKinds").toObject().value(chId).toString();
    if (!k.isEmpty() && kChapterKinds.contains(k)) return k;
    // NEO 1.0 kept a prologue and an epilogue as roles of the first and last chapters
    const QStringList order = chapterOrder(book);
    if (order.size() >= 2) {
        if (book.value("prologue").toString() == chId && order.first() == chId) return "prologue";
        if (book.value("epilogue").toString() == chId && order.last() == chId) return "epilogue";
    }
    return "chapter";
}

bool isStory(const QString &chId, const QJsonObject &book) { return kStoryKinds.contains(chapterKind(chId, book)); }

QString chapterRole(const QString &chId, const QJsonObject &book)
{
    const QString k = chapterKind(chId, book);
    return (k == "prologue" || k == "epilogue") ? k : QString();
}

int kindCount(const QString &chId, const QString &kind, const QJsonObject &book)
{
    int n = 0;
    const bool restart = book.value("restartNumbering").toBool();
    for (const QString &c : chapterOrder(book)) {
        const QString k = chapterKind(c, book);
        if (k == kind) n++;
        else if (kind == "chapter" && k == "part" && restart) n = 0;
        if (c == chId) break;
    }
    return n;
}

QString chapterTitle(const QString &chId, const QJsonObject &book)
{
    return book.value("chapterTitles").toObject().value(chId).toString().trimmed();
}

QString chapterName(const QString &chId, const QJsonObject &book)
{
    const QString k = chapterKind(chId, book);
    if (k == "chapter") return t("Chapter {n}", {{"n", chapterNumber(chId, book)}});
    if (k == "part") return partLabel(kindCount(chId, "part", book));
    if (k == "unnumbered") {
        const QString title = chapterTitle(chId, book);
        return title.isEmpty() ? t("Untitled") : title;
    }
    return kindName(k);
}

QString chapterHeading(const QString &chId, const QJsonObject &book, bool customTitlesOnly, const QString &sep)
{
    const QString title = chapterTitle(chId, book);
    const QString k = chapterKind(chId, book);
    if (k == "unnumbered") return title;
    if (title.isEmpty()) return chapterName(chId, book);
    return customTitlesOnly ? title : chapterName(chId, book) + sep + title;
}

QString kindName(const QString &kind)
{
    if (kind == "chapter") return t("Chapter");
    if (kind == "unnumbered") return t("Unnumbered Chapter");
    if (kind == "contents") return t("Contents");
    return pageKindName(kind);
}

QString pageKindName(const QString &kind)
{
    if (kind == "cover") return t("Cover");
    if (kind == "copyright") return t("Copyright");
    if (kind == "dedication") return t("Dedication");
    if (kind == "epigraph") return t("Epigraph");
    if (kind == "prologue") return t("Prologue");
    if (kind == "part") return t("Part");
    if (kind == "epilogue") return t("Epilogue");
    if (kind == "acknowledgments") return t("Acknowledgments");
    if (kind == "about") return t("About the Author");
    return kind;
}

QString chapterMark(const QString &chId, const QJsonObject &book)
{
    const QString k = chapterKind(chId, book);
    if (k == "chapter") return QString::number(chapterNumber(chId, book));
    if (k == "part") return roman(kindCount(chId, "part", book));
    return QStringLiteral("❦");
}

int numberedChapters(const QJsonObject &book, const QString &chId)
{
    int n = 0, total = 0;
    bool found = false;
    const bool restart = book.value("restartNumbering").toBool();
    for (const QString &c : chapterOrder(book)) {
        const QString k = chapterKind(c, book);
        if (k == "part" && restart && !chId.isEmpty()) {
            if (found) break;
            n = 0;
        }
        if (c == chId) found = true;
        if (k == "chapter") { n++; total++; }
    }
    return (!chId.isEmpty() && restart) ? n : total;
}

QString soloStory(const QJsonObject &book)
{
    QStringList story;
    for (const QString &c : chapterOrder(book))
        if (isStory(c, book) || chapterKind(c, book) == "part") story << c;
    return (story.size() == 1 && chapterKind(story.first(), book) == "chapter") ? story.first() : QString();
}

bool settleChapterKinds(QJsonObject &book)
{
    bool changed = false;
    QJsonObject kinds = book.value("chapterKinds").toObject();
    for (const QString &role : {QStringLiteral("prologue"), QStringLiteral("epilogue")}) {
        if (!book.contains(role)) continue;
        const QString id = book.value(role).toString();
        if (chapterKind(id, book) == role) kinds.insert(id, role);
        book.remove(role);
        changed = true;
    }
    const QStringList order = chapterOrder(book);
    for (const QString &id : kinds.keys()) {
        const QString k = kinds.value(id).toString();
        if (!order.contains(id) || k == "chapter" || !kChapterKinds.contains(k)) {
            kinds.remove(id);
            changed = true;
        }
    }
    if (changed || book.contains("chapterKinds")) book.insert("chapterKinds", kinds);
    return changed;
}

void setChapterKind(QJsonObject &book, const QString &chId, const QString &kind)
{
    QJsonObject kinds = book.value("chapterKinds").toObject();
    if (kind == "chapter") kinds.remove(chId);
    else kinds.insert(chId, kind);
    book.insert("chapterKinds", kinds);
}

QString roman(int n)
{
    static const std::pair<int, const char *> r[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"},
                                                     {90, "XC"},  {50, "L"},   {40, "XL"},  {10, "X"},   {9, "IX"},
                                                     {5, "V"},    {4, "IV"},   {1, "I"}};
    QString out;
    for (const auto &[v, s] : r)
        while (n >= v) { out += QLatin1String(s); n -= v; }
    return out;
}

QString partLabel(int n) { return t("Part {n}", {{"n", roman(n)}}); }

bool isUntitled(const QString &s) { return s.isEmpty() || s == "Untitled" || s == t("Untitled"); }

bool isScript(const QJsonObject &book) { return book.value("format").toString() == "screenplay"; }

namespace {

struct SegScript {
    const char *lang;
    char16_t from, to;
};
constexpr SegScript kSegmented[] = {
    {"th", 0x0E00, 0x0E7F}, // Thai
    {"lo", 0x0E80, 0x0EFF}, // Lao
    {"my", 0x1000, 0x109F}, // Myanmar
    {"km", 0x1780, 0x17FF}, // Khmer
};

const SegScript *segmentedScript(const QString &s)
{
    for (QChar c : s)
        for (const auto &sc : kSegmented)
            if (c.unicode() >= sc.from && c.unicode() <= sc.to) return &sc;
    return nullptr;
}

bool hasLetterOrDigit(QStringView w)
{
    for (QChar c : w)
        if (c.isLetterOrNumber()) return true;
    // a surrogate pair (letters outside the BMP)
    for (qsizetype i = 0; i + 1 < w.size(); ++i)
        if (w[i].isHighSurrogate()) {
            const char32_t u = QChar::surrogateToUcs4(w[i], w[i + 1]);
            if (QChar::isLetterOrNumber(u)) return true;
        }
    return false;
}

} // namespace

int countWords(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return 0;
    if (const SegScript *sc = segmentedScript(trimmed)) {
        UErrorCode err = U_ZERO_ERROR;
        std::unique_ptr<icu::BreakIterator> it(icu::BreakIterator::createWordInstance(icu::Locale(sc->lang), err));
        if (U_SUCCESS(err) && it) {
            const icu::UnicodeString us(reinterpret_cast<const UChar *>(trimmed.utf16()), int32_t(trimmed.size()));
            it->setText(us);
            int words = 0;
            for (int32_t p = it->next(); p != icu::BreakIterator::DONE; p = it->next())
                if (it->getRuleStatus() >= UBRK_WORD_LETTER) words++;
            return words;
        }
    }
    int n = 0;
    qsizetype i = 0;
    const qsizetype len = trimmed.size();
    while (i < len) {
        while (i < len && trimmed[i].isSpace()) i++;
        const qsizetype start = i;
        while (i < len && !trimmed[i].isSpace()) i++;
        if (i > start && hasLetterOrDigit(QStringView(trimmed).mid(start, i - start))) n++;
    }
    return n;
}

} // namespace neosea
