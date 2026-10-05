#include "core/typing.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

namespace neosea::typing {

namespace {

QRegularExpression re(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::UseUnicodePropertiesOption);
}

QString langBase(const QString &code) { return code.section('-', 0, 0); }

bool isLetter(QChar c) { return c.isLetter(); }

} // namespace

// ---------------------------------------------------------------------------
// quotes

QuoteStyle quoteStyle(const QString &lang)
{
    static const QHash<QString, QuoteStyle> styles{
        {"en", {"“", "”", true}},
        {"nl", {"“", "”", true}},
        {"pt", {"“", "”", true}}, // Brazil
        {"pt-PT", {"«", "»", false}},
        {"fr", {QStringLiteral("«\u202f"), QStringLiteral("\u202f»"), false}}, // narrow no-break spaces inside
        {"es", {"«", "»", false}}, // RAE: « » first
        {"it", {"«", "»", false}},
        {"de", {"„", "“", false}},
        {"pl", {"„", "”", false}},
        {"ro", {"„", "”", false}},
        {"ru", {"«", "»", false}},
        {"el", {"«", "»", false}},
    };
    if (auto it = styles.constFind(lang); it != styles.constEnd()) return *it;
    if (auto it = styles.constFind(langBase(lang)); it != styles.constEnd()) return *it;
    return styles.value("en");
}

QuoteStyle bookQuotes(const QString &lang, const QString &chapterText, const QString &bookText)
{
    const QuoteStyle q = quoteStyle(lang);
    const QString own = q.open.trimmed();
    struct Counts {
        int right = 0, left = 0, own = 0;
    };
    auto opens = [&](const QString &text) {
        Counts c;
        auto count = [&](const QString &pat) {
            int n = 0;
            auto it = re(pat).globalMatch(text);
            while (it.hasNext()) { it.next(); n++; }
            return n;
        };
        c.right = count("»(?=[\\p{L}\\p{N}])");
        c.left = own == "«" ? 0 : count("«(?=[\\p{L}\\p{N}])");
        c.own = count(QRegularExpression::escape(own) + "\\s?(?=[\\p{L}\\p{N}])");
        return c;
    };
    Counts c = opens(chapterText);
    if (!c.right && !c.left && !c.own) c = opens(bookText);
    if (c.right > c.own && c.right >= c.left) return {"»", "«", false};
    if (c.left > c.own && c.left > c.right) return {"«", "»", false};
    return q;
}

bool quoteOpenIn(const QString &text, const QuoteStyle &q, const QString &straight)
{
    const QString open = q.open.trimmed();
    const QString close = q.close.trimmed();
    int depth = 0;
    int straights = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QString c = text.mid(i, 1);
        if (!straight.isEmpty() && c == straight) straights++;
        else if (c == open && open != close) depth++;
        else if (c == close) {
            // an apostrophe between two letters is no quote
            if (close == "’" && i > 0 && i + 1 < text.size() && isLetter(text[i - 1]) && isLetter(text[i + 1])) continue;
            depth = std::max(0, depth - 1);
        }
    }
    return depth > 0 || straights % 2 == 1;
}

QString curlQuote(const QString &before, QChar key, const QuoteStyle &q)
{
    const QString prev = before.right(1);
    static const QRegularExpression opener(QStringLiteral("[\\s\\(\\[\\{‘“«„>]"));
    bool opening = prev.isEmpty() || opener.match(prev).hasMatch();
    // after a dash, a quote usually closes speech that was cut off ("I was
    // just—"); it opens one only when no quotation is open in the paragraph
    if (prev == "—" || prev == "–") {
        if (key == '"') opening = !quoteOpenIn(before, q, "\"");
        else opening = !quoteOpenIn(before, {"‘", "’", false});
    }
    if (key == '\'') return (q.singles && opening) ? QStringLiteral("‘") : QStringLiteral("’");
    return opening ? q.open : q.close;
}

// ---------------------------------------------------------------------------
// dialogue dashes

DashStyle dashStyle(const QString &lang)
{
    static const QHash<QString, DashStyle> styles{
        {"pt", {"—", QStringLiteral(" "), "—"}}, // — Olá — diz ela.
        {"ru", {"—", QStringLiteral(" "), "—"}},
        {"es", {"—", QStringLiteral(""), "—"}},  // —Hola —dijo él—.
        {"en", {"—", std::nullopt, "–"}},        // and every other language: word – word
    };
    if (auto it = styles.constFind(lang); it != styles.constEnd()) return *it;
    if (auto it = styles.constFind(langBase(lang)); it != styles.constEnd()) return *it;
    return styles.value("en");
}

QList<Edit> dialogueDashEdits(const QString &text, const DashStyle &style, Edges edges)
{
    static const QRegularExpression onlyBreak = re(QStringLiteral("^[\\s*#•~⁂—–-]*$"));
    static const QRegularExpression dashOpen = re(QStringLiteral("^-(?:(\\s+)(?=[^\\s-])|(?=[^\\s\\d-]))"));
    // (a quote typed right after the hyphen closes whatever comes next)
    static const QRegularExpression dashMid =
        re(QStringLiteral("(?<=\\s)-(?=[\"'“”‘’«»„]*(?:[\\s.,;:!?…)\\]]|$)|[\"'“”‘’«»„]+\\x{E000})"));
    // a scene break, or a paragraph of nothing but dashes
    if (edges.start && edges.end && onlyBreak.match(text).hasMatch()) return {};
    QList<Edit> edits;
    if (edges.start) {
        auto m = dashOpen.match(text);
        if (m.hasMatch()) edits.push_back({0, m.captured(0), style.open + (style.space ? *style.space : m.captured(1))});
    }
    // past the end of a fragment, anything could follow
    const QString lead = (!edges.start && edges.spaced) ? QStringLiteral(" ") : QString();
    const QString probe = lead + text + (edges.end ? QString() : QString(QChar(0xE000)));
    auto it = dashMid.globalMatch(probe);
    while (it.hasNext()) {
        auto m = it.next();
        edits.push_back({m.capturedStart() - lead.size(), "-", style.mid});
    }
    return edits;
}

QString dialogueDashes(const QString &text, const DashStyle &style, Edges edges)
{
    QString s = text;
    const QList<Edit> edits = dialogueDashEdits(text, style, edges);
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) s = s.left(it->at) + it->to + s.mid(it->at + it->from.size());
    return s;
}

void dashRuns(QList<Run> &runs, const DashStyle &style, Edges edges)
{
    QString all;
    for (const Run &r : runs)
        if (!r.mark) all += r.text;
    const QList<Edit> edits = dialogueDashEdits(all, style, edges);
    for (auto e = edits.rbegin(); e != edits.rend(); ++e) {
        qsizetype pos = 0;
        for (Run &r : runs) {
            if (r.mark) continue;
            if (e->at >= pos && e->at + e->from.size() <= pos + r.text.size()) {
                r.text = r.text.left(e->at - pos) + e->to + r.text.mid(e->at - pos + e->from.size());
                break;
            }
            pos += r.text.size();
        }
    }
}

std::optional<Edit> dashForKey(const QString &before, const QString &key, const DashStyle &style, bool manuscript)
{
    if (key.size() > 1) return std::nullopt;
    static const QRegularExpression openOnly(QStringLiteral("^-\\s*$"));
    static const QRegularExpression spacedHyphen(QStringLiteral("\\s-$"));
    qsizetype from;
    if (manuscript && openOnly.match(before).hasMatch()) from = 0;
    else if (spacedHyphen.match(before).hasMatch()) from = before.size() - 2;
    else return std::nullopt;
    const QList<Edit> edits = dialogueDashEdits(before.mid(from) + key, style, {from == 0, key.isEmpty(), false});
    if (edits.isEmpty()) return std::nullopt;
    Edit e = edits.first();
    e.at += from;
    return e;
}

// ---------------------------------------------------------------------------
// Markdown

namespace {

bool balancedRuns(const QString &text, QChar mark)
{
    QHash<qsizetype, int> counts;
    qsizetype i = 0;
    while (i < text.size()) {
        if (text[i] != mark) { ++i; continue; }
        qsizetype j = i;
        while (j < text.size() && text[j] == mark) ++j;
        counts[j - i]++;
        i = j;
    }
    for (int n : counts)
        if (n % 2) return false;
    return true;
}

} // namespace

std::optional<Emphasis> mdEmphasisMatch(const QString &before, QChar mark)
{
    const QString m = QRegularExpression::escape(QString(mark));
    const QString edge = QStringLiteral("(^|[^\\p{L}\\p{N}%1\\\\])").arg(m);
    const QString inner = QStringLiteral("(?!\\s|%1)(.*?[^\\s\\\\])").arg(m);
    struct Try {
        int open, part;
        bool bold, italic;
    };
    static const Try tries[] = {{3, 2, true, true}, {2, 1, true, false}, {1, 0, false, true}};
    for (const Try &t : tries) {
        const QRegularExpression rx = re(edge + m.repeated(t.open) + inner + m.repeated(t.part) + "$");
        auto r = rx.match(before);
        if (!r.hasMatch()) continue;
        const QString in = r.captured(2);
        // the inner text must not end on the mark itself (a longer mark still
        // being typed), nor hold an emphasis opened but not yet closed
        if (in.endsWith(mark)) continue;
        if (t.part == 0 && before.endsWith(mark)) continue;
        if (!balancedRuns(in, mark)) continue;
        Emphasis e;
        e.open = t.open;
        e.part = t.part;
        e.bold = t.bold;
        e.italic = t.italic;
        e.inner = in;
        e.start = before.size() - (r.captured(0).size() - r.captured(1).size());
        return e;
    }
    return std::nullopt;
}

std::optional<qsizetype> mdStrikeMatch(const QString &before)
{
    static const QRegularExpression rx = re(QStringLiteral("(^|[^~\\\\])~~(?![\\s~])(.*?[^\\s\\\\~])~$"));
    auto m = rx.match(before);
    if (!m.hasMatch()) return std::nullopt;
    return before.size() - m.captured(0).size() + m.captured(1).size();
}

std::optional<QList<Run>> markdownInline(const QString &line)
{
    const QString edge = QStringLiteral("(^|[^\\p{L}\\p{N}*_\\\\])");
    const QString tail = QStringLiteral("(?![\\p{L}\\p{N}])");
    QString h = escHtml(line);
    const QString before = h;
    h.replace(re(edge + "(\\*\\*\\*|___)(?!\\s)(.+?)(?<![\\s\\\\])\\2" + tail), "\\1<b><i>\\3</i></b>");
    h.replace(re(edge + "(\\*\\*|__)(?!\\s)(.+?)(?<![\\s\\\\])\\2" + tail), "\\1<b>\\3</b>");
    h.replace(re(edge + "(\\*|_)(?![\\s*_])(.+?)(?<![\\s\\\\*_])\\2" + tail), "\\1<i>\\3</i>");
    h.replace(re(QStringLiteral("(^|[^~\\\\])~~(?![\\s~])(.+?)(?<![\\s\\\\~])~~(?!~)")), "\\1<s>\\2</s>");
    if (h == before) return std::nullopt;
    const QList<Para> p = parseChapter("<p>" + h + "</p>");
    return p.isEmpty() ? QList<Run>{} : p.first().runs;
}

// ---------------------------------------------------------------------------
// French, capitals

QString frenchTypography(const QString &writingLanguage, const QString &uiLocale)
{
    if (!writingLanguage.startsWith("fr")) return {};
    return uiLocale.compare("fr-CA", Qt::CaseInsensitive) == 0 ? QStringLiteral("ca") : QStringLiteral("fr");
}

bool isCapsAbbrev(const QString &word)
{
    static const QSet<QString> abbrev{"mr",  "mrs", "ms",  "dr",   "st",   "jr",  "sr",  "vs",  "etc", "e.g",
                                      "i.e", "cf",  "approx", "no", "vol",  "pp",  "p",   "fig", "ca",  "mt",
                                      "ft",  "lt",  "sgt", "capt", "col",  "gen", "prof", "rev", "hon", "inc",
                                      "ltd", "co",  "ave", "a.m",  "p.m",  "sra", "srta", "dra", "av",  "ex",
                                      "z.b", "bzw", "ggf", "usw",  "m",    "mme", "mlle"};
    return abbrev.contains(word);
}

std::optional<QString> autoCapital(const QString &before, const QString &key, const QString &lang)
{
    if (key.size() != 1) return std::nullopt;
    const QLocale loc(lang);
    const QString upper = loc.toUpper(key);
    if (upper == key || !key[0].isLetter()) return std::nullopt;
    static const QRegularExpression atStart = re(QStringLiteral("^[\\s\"'“‘„«»(\\[¿¡—–-]*$"));
    static const QRegularExpression afterStop = re(QStringLiteral("(?<!\\.)\\.\\s+[\"'“‘„«(\\[]?$"));
    static const QRegularExpression stopTail = re(QStringLiteral("\\.\\s+[\"'“‘„«(\\[]?$"));
    static const QRegularExpression lastWord = re(QStringLiteral("([\\p{L}.]+)\\.$"));
    static const QRegularExpression oneLetter = re(QStringLiteral("^\\p{L}$"));
    bool starts = atStart.match(before).hasMatch();
    if (!starts && afterStop.match(before).hasMatch()) {
        QString b = before;
        b.replace(stopTail, ".");
        const QString word = lastWord.match(b).captured(1);
        starts = !isCapsAbbrev(word.toLower()) && !oneLetter.match(word).hasMatch();
    }
    if (!starts) return std::nullopt;
    return upper;
}

std::optional<qsizetype> englishI(const QString &before, const QString &key, const QString &lang)
{
    static const QRegularExpression en(QStringLiteral("^en\\b"));
    static const QRegularExpression keyRx = re(QStringLiteral("^[\\s,;:!?'’\")”\\]—–-]$"));
    static const QRegularExpression aloneI = re(QStringLiteral("(?:^|[^\\p{L}\\p{M}\\d'’.(-])i$"));
    if (!en.match(lang).hasMatch() || !keyRx.match(key).hasMatch() || !aloneI.match(before).hasMatch())
        return std::nullopt;
    return before.size() - 1;
}

QList<qsizetype> capitalSlips(const QString &text, bool english)
{
    QList<qsizetype> out;
    if (text.trimmed().isEmpty()) return out;
    auto mark = [&](qsizetype i) {
        if (!out.contains(i)) out << i;
    };
    static const QRegularExpression leadRx = re(QStringLiteral("^[\\s\"'“‘„«»(\\[¿¡—–-]*"));
    static const QRegularExpression lower = re(QStringLiteral("^\\p{Ll}"));
    static const QRegularExpression initial = re(QStringLiteral("^\\p{Ll}\\."));
    const qsizetype lead = leadRx.match(text).capturedLength();
    if (lower.match(text.mid(lead, 1)).hasMatch() && !initial.match(text.mid(lead, 2)).hasMatch()) mark(lead);
    // a letter after a full stop (not an ellipsis or an abbreviation)
    static const QRegularExpression stop = re(QStringLiteral("(?<!\\.)\\.\\s+[\"'“‘„«(\\[]?(\\p{Ll})"));
    static const QRegularExpression wordBefore = re(QStringLiteral("([\\p{L}.]+)$"));
    static const QRegularExpression oneLetter = re(QStringLiteral("^\\p{L}$"));
    auto it = stop.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        const QString word = wordBefore.match(text.left(m.capturedStart())).captured(1).toLower();
        if (isCapsAbbrev(word) || oneLetter.match(word).hasMatch()) continue; // Mr. smith, J. r. r.
        mark(m.capturedStart(1));
    }
    // English "i", "i'm", "i'd" … standing alone (not "i.e." or "(i)")
    if (english) {
        static const QRegularExpression eye =
            re(QStringLiteral("(?<![\\p{L}\\p{M}\\d'’.(-])i(?![\\p{L}\\p{M}\\d.)-])(?!['’](?![mdv]|ll|re))"));
        auto e = eye.globalMatch(text);
        while (e.hasNext()) mark(e.next().capturedStart());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool isAttribution(const QString &text)
{
    static const QRegularExpression rx(QStringLiteral("^(?:[—–]|--?\\s)"));
    return rx.match(text).hasMatch();
}

bool opensWithDash(const QString &text)
{
    static const QRegularExpression rx(QStringLiteral("^\\s*[-‐‑‒–—―]"));
    return rx.match(text).hasMatch();
}

} // namespace neosea::typing
