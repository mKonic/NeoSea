#include "core/screenplay.h"

#include <QHash>
#include <QRegularExpression>

namespace neosea::sp {

namespace {

QRegularExpression rx(const QString &p, bool caseless = false)
{
    QRegularExpression::PatternOptions o = QRegularExpression::UseUnicodePropertiesOption;
    if (caseless) o |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(p, o);
}

const QRegularExpression &headRe()
{
    static const QRegularExpression r = rx("^(?:INT\\.?/EXT|INT/EXT|I/E|INT|EXT|EST)(?:\\.|\\s)", true);
    return r;
}

const QStringList kTimes{"DAY", "NIGHT", "CONTINUOUS", "LATER", "MORNING", "EVENING", "DAWN", "DUSK", "MOMENTS LATER", "SAME TIME"};
const QStringList kTransitions{"CUT TO:", "DISSOLVE TO:", "SMASH CUT TO:", "MATCH CUT TO:", "JUMP CUT TO:", "FADE OUT.", "FADE TO BLACK.", "INTERCUT WITH:"};
const QStringList kExtensions{"V.O.)", "O.S.)", "O.C.)", "CONT'D)"};

bool hasUpper(const QString &s)
{
    for (QChar c : s)
        if (c.isUpper()) return true;
    return false;
}

bool hasLower(const QString &s)
{
    for (QChar c : s)
        if (c.isLower()) return true;
    return false;
}

QString trimStart(const QString &s)
{
    qsizetype i = 0;
    while (i < s.size() && s[i].isSpace()) ++i;
    return s.mid(i);
}

// the rest of the first word in the pool that starts with what's typed
QString complete(const QString &partial, const QStringList &pool)
{
    if (partial.isEmpty()) return {};
    const QString p = partial.toUpper();
    // a name or place already used just as typed is what's meant (KIM, not KIMBERLY)
    if (pool.contains(p)) return {};
    for (const QString &w : pool)
        if (w.startsWith(p) && w.size() > p.size()) return w.mid(p.size());
    return {};
}

QStringList names(const QList<Line> &lines, qsizetype skip)
{
    struct E {
        QString name;
        int count = 0;
        qsizetype last = 0;
    };
    QList<E> seen;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (lines[i].type != "character" || i == skip) continue;
        const QString n = bareName(lines[i].text);
        if (n.isEmpty()) continue;
        auto it = std::find_if(seen.begin(), seen.end(), [&](const E &e) { return e.name == n; });
        if (it == seen.end()) { seen << E{n, 1, i}; continue; }
        it->count++;
        it->last = i;
    }
    std::stable_sort(seen.begin(), seen.end(), [](const E &a, const E &b) {
        return a.count != b.count ? a.count > b.count : a.last > b.last;
    });
    QStringList out;
    for (const E &e : seen) out << e.name;
    return out;
}

QStringList locations(const QList<Line> &lines, qsizetype skip)
{
    QStringList out;
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        if (lines[i].type != "heading" || i == skip) continue;
        const auto h = parseHeading(lines[i].text);
        const QString loc = h ? h->loc.trimmed().toUpper() : QString();
        if (!loc.isEmpty() && !out.contains(loc)) out << loc;
    }
    return out;
}

QStringList timesUsed(const QList<Line> &lines, qsizetype skip)
{
    QStringList out;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (lines[i].type != "heading" || i == skip) continue;
        const auto h = parseHeading(lines[i].text);
        const QString time = (h && h->time) ? h->time->trimmed().toUpper() : QString();
        if (!time.isEmpty() && !out.contains(time)) out << time;
    }
    return out;
}

// the one being answered: the speaker before the last one, in this scene
QString partner(const QList<Line> &lines, qsizetype i)
{
    QStringList order;
    for (qsizetype j = i - 1; j >= 0; --j) {
        const Line &l = lines[j];
        if (l.type == "heading") break;
        if (l.type != "character") continue;
        const QString n = bareName(l.text);
        if (!n.isEmpty() && !order.contains(n)) order << n;
        if (order.size() == 2) break;
    }
    return order.size() == 2 ? order[1] : QString();
}

QString dropContd(const QString &t)
{
    QString s = t;
    s.remove(rx("\\s*\\(\\s*cont(?:['’]?d|inued)\\s*\\)\\s*$", true));
    return s.trimmed();
}

QString xmlText(const QString &s)
{
    QString out;
    qsizetype i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            const qsizetype semi = s.indexOf(';', i);
            if (semi > i) {
                const QString e = s.mid(i + 1, semi - i - 1);
                bool ok = false;
                uint cp = 0;
                if (e.startsWith("#x") || e.startsWith("#X")) cp = e.mid(2).toUInt(&ok, 16);
                else if (e.startsWith('#')) cp = e.mid(1).toUInt(&ok, 10);
                if (ok) { out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1); i = semi + 1; continue; }
                static const QHash<QString, QString> named{{"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"amp", "&"}};
                if (named.contains(e)) { out += named.value(e); i = semi + 1; continue; }
            }
        }
        out += s[i++];
    }
    return out;
}

QString xmlAttr(const QString &tag, const QString &name)
{
    auto m = rx("\\b" + name + "=\"([^\"]*)\"").match(tag);
    return m.hasMatch() ? xmlText(m.captured(1)) : QString();
}

QString xmlEsc(const QString &s)
{
    QString o = s;
    o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
    return o;
}

struct FdxPara {
    QString type, align;
    QList<Run> runs;
};

QList<FdxPara> fdxParas(QString xml)
{
    xml.replace(QRegularExpression("<Paragraph\\b[^>]*>\\s*<DualDialogue>(.*?)</DualDialogue>\\s*</Paragraph>",
                                   QRegularExpression::DotMatchesEverythingOption),
                "\\1");
    QList<FdxPara> out;
    auto it = QRegularExpression("<Paragraph\\b([^>]*)>(.*?)</Paragraph>", QRegularExpression::DotMatchesEverythingOption).globalMatch(xml);
    while (it.hasNext()) {
        auto m = it.next();
        FdxPara p;
        auto rt = QRegularExpression("<Text\\b([^>]*?)(?:/>|>(.*?)</Text>)", QRegularExpression::DotMatchesEverythingOption).globalMatch(m.captured(2));
        while (rt.hasNext()) {
            auto r = rt.next();
            QString text = xmlText(r.captured(2));
            text.replace(QRegularExpression("\\s*\\n\\s*"), " ");
            if (text.isEmpty()) continue;
            const QStringList style = xmlAttr(r.captured(1), "Style").toLower().split('+');
            Run run;
            run.text = text;
            run.b = style.contains("bold");
            run.i = style.contains("italic");
            run.u = style.contains("underline");
            run.s = style.contains("strikeout");
            p.runs << run;
        }
        p.type = xmlAttr(m.captured(1), "Type");
        p.align = xmlAttr(m.captured(1), "Alignment").toLower();
        out << p;
    }
    return out;
}

QString runsText(const QList<Run> &runs)
{
    QString s;
    for (const Run &r : runs) s += r.text;
    return s;
}

const QRegularExpression &titleKey()
{
    static const QRegularExpression r = rx("^(title|credit|author|authors|source|draft date|date|contact|copyright|notes|revision)\\s*:", true);
    return r;
}

} // namespace

int linesBefore(const QString &type)
{
    static const QHash<QString, int> b{{"heading", 2}, {"action", 1}, {"character", 1}, {"paren", 0},
                                       {"dialogue", 0}, {"transition", 1}, {"shot", 1}};
    return b.value(type, 0);
}

QString after(const QString &type)
{
    static const QHash<QString, QString> a{{"heading", "action"},   {"action", "action"},     {"character", "dialogue"},
                                           {"paren", "dialogue"},   {"dialogue", "action"},   {"transition", "heading"},
                                           {"shot", "action"}};
    return a.value(type, "action");
}

QString onEmpty(const QString &type)
{
    static const QHash<QString, QString> e{{"action", "character"}, {"character", "action"}, {"dialogue", "action"},
                                           {"paren", "dialogue"},   {"heading", "action"},   {"transition", "action"},
                                           {"shot", "action"}};
    return e.value(type, "action");
}

bool isHeading(const QString &text) { return headRe().match(text).hasMatch(); }

QString bareName(const QString &t)
{
    QString s = t;
    s.remove(rx("\\s*\\^\\s*$"));
    s.remove(rx("\\s*\\([^)]*\\)?\\s*$"));
    return s.trimmed().toUpper();
}

bool looksLikeTransition(const QString &t)
{
    const QString s = t.trimmed();
    if (kTransitions.contains(s.toUpper())) return true;
    return !s.isEmpty() && s == s.toUpper() && hasUpper(s)
        && (s.endsWith("TO:") || s == "FADE OUT." || s == "FADE TO BLACK.");
}

bool looksLikeCharacter(const QString &t)
{
    const QString s = t.trimmed();
    if (s.isEmpty() || s.size() > 38 || s != s.toUpper() || !hasUpper(s)) return false;
    QString name = s;
    name.remove(rx("\\s*\\^\\s*$"));
    name.remove(rx("\\s*\\([^)]*\\)\\s*$"));
    name = name.trimmed();
    if (name.isEmpty() || !hasUpper(name)) return false;
    const QStringList words = name.split(rx("\\s+"));
    if (rx("[.!?,;:—–-]$").match(name).hasMatch() && !rx("^(MR|MRS|MS|DR|ST|JR|SR)\\.$").match(words.last()).hasMatch())
        return false;
    return words.size() <= 4;
}

std::optional<Heading> parseHeading(const QString &t)
{
    static const QRegularExpression parse = rx("^(INT\\.?/EXT\\.?|INT/EXT\\.?|I/E\\.?|INT\\.?|EXT\\.?|EST\\.?)\\s+(.*)$", true);
    auto m = parse.match(t);
    if (!m.hasMatch()) return std::nullopt;
    const QString rest = m.captured(2);
    static const QRegularExpression dash = rx("\\s+[-–—]\\s*");
    auto d = dash.match(rest);
    if (!d.hasMatch()) return Heading{m.captured(1), rest, std::nullopt};
    return Heading{m.captured(1), rest.left(d.capturedStart()).trimmed(), rest.mid(d.capturedEnd())};
}

QString ghost(const QList<Line> &lines, qsizetype i)
{
    const Line &l = lines[i];
    const QString &text = l.text;
    if (l.type == "character") {
        const qsizetype open = text.lastIndexOf('(');
        if (open >= 0 && text.indexOf(')', open) < 0) return complete(text.mid(open + 1), kExtensions);
        if (text.trimmed().isEmpty()) return partner(lines, i);
        return complete(trimStart(text), names(lines, i));
    }
    if (l.type == "heading") {
        const auto h = parseHeading(text);
        if (!h) return {};
        if (!h->time) return (!text.isEmpty() && text.back().isSpace()) ? QString() : complete(h->loc, locations(lines, i));
        return complete(*h->time, timesUsed(lines, i) + kTimes);
    }
    if (l.type == "transition") {
        QStringList used;
        for (qsizetype j = 0; j < lines.size(); ++j)
            if (lines[j].type == "transition" && j != i && !lines[j].text.trimmed().isEmpty())
                used << lines[j].text.trimmed().toUpper();
        return complete(trimStart(text), used + kTransitions);
    }
    return {};
}

bool contd(const QList<Line> &lines, qsizetype i)
{
    const QString me = bareName(lines[i].text);
    if (me.isEmpty() || lines[i].text.contains('(')) return false;
    bool between = false;
    for (qsizetype j = i - 1; j >= 0; --j) {
        const Line &l = lines[j];
        if (l.type == "heading" || l.type == "transition") return false;
        if (l.type == "character") return between && bareName(l.text) == me;
        if ((l.type == "action" || l.type == "shot") && !l.text.trimmed().isEmpty()) between = true;
    }
    return false;
}

Pages paginate(const QList<Item> &items, int perPage)
{
    const qsizetype n = items.size();
    QList<QPair<qsizetype, qsizetype>> blocks;
    for (qsizetype i = 0; i < n;) {
        qsizetype j = i + 1;
        if (items[i].type == "character")
            while (j < n && (items[j].type == "dialogue" || items[j].type == "paren")) ++j;
        blocks << QPair{i, j};
        i = j;
    }
    Pages out;
    out.at.resize(n);
    int page = 1, used = 0;
    auto above = [&](qsizetype x, bool top) { return (top || x == 0) ? 0 : linesBefore(items[x].type); };
    auto height = [&](const QPair<qsizetype, qsizetype> &b, bool top) {
        int h = 0;
        for (qsizetype x = b.first; x < b.second; ++x) h += items[x].lines + above(x, top && x == b.first);
        return h;
    };
    for (qsizetype bi = 0; bi < blocks.size(); ++bi) {
        const auto &b = blocks[bi];
        int need = height(b, used == 0);
        if (items[b.first].type == "heading" && bi + 1 < blocks.size()) {
            const qsizetype nx = blocks[bi + 1].first;
            need += items[nx].lines + above(nx, false);
        }
        if (used > 0 && used + need > perPage) {
            out.at[b.first].brk = true;
            out.at[b.first].fill = std::max(0, perPage - used);
            page++;
            used = 0;
        }
        for (qsizetype x = b.first; x < b.second; ++x) {
            out.at[x].before = above(x, used == 0 && x == b.first);
            out.at[x].page = page;
            used += out.at[x].before + items[x].lines;
            // longer than a page: it runs on over the next one
            while (used > perPage) { used -= perPage; page++; }
        }
    }
    out.pages = page;
    out.used = used;
    return out;
}

int eighths(int lines, int perPage) { return std::max(1, int(std::lround(double(lines) / perPage * 8))); }

QString toFountain(const QList<Line> &lines, const TitlePage &title)
{
    QStringList out;
    const QList<QPair<QString, QString>> keys{{"Title", title.title}, {"Credit", title.credit}, {"Author", title.author},
                                              {"Draft date", title.draft}, {"Contact", title.contact}};
    for (const auto &[k, v] : keys) {
        const QString s = v.trimmed();
        if (s.isEmpty()) continue;
        QStringList rows;
        for (const QString &r : s.split('\n'))
            if (!r.trimmed().isEmpty()) rows << r.trimmed();
        if (rows.size() == 1) out << k + ": " + rows.first();
        else {
            out << k + ":";
            for (const QString &r : rows) out << "    " + r;
        }
    }
    if (!out.isEmpty()) out << "";
    bool inSpeech = false;
    auto gap = [&] {
        if (!out.isEmpty() && !out.last().isEmpty()) out << "";
    };
    static const QRegularExpression mark = rx("^[.!@~>#=\\[]");
    for (const Line &l : lines) {
        const QString text = l.text.trimmed();
        if (text.isEmpty()) { inSpeech = false; continue; }
        const QString caps = text.toUpper();
        if ((l.type == "dialogue" || l.type == "paren") && inSpeech) {
            out << ((l.type == "paren" && !text.startsWith('(')) ? "(" + text + ")" : text);
            continue;
        }
        inSpeech = false;
        gap();
        if (l.type == "heading") out << (isHeading(caps) ? caps : "." + caps);
        else if (l.type == "character") {
            out << (hasLower(caps) ? "@" + text : caps);
            inSpeech = true;
        } else if (l.type == "transition") out << (caps.endsWith("TO:") ? caps : ">" + caps);
        else if (l.type == "shot") out << "!" + caps;
        else {
            // forced when it would read as a heading, a name, a transition or a mark
            const bool misread = isHeading(text) || (text == caps && hasUpper(text)) || mark.match(text).hasMatch();
            out << (misread ? "!" + text : text);
        }
    }
    while (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out.join('\n') + '\n';
}

QList<Line> fromFountain(const QString &src)
{
    QString text = src;
    text.replace(QRegularExpression("\\r\\n?"), "\n").replace('\t', "    ");
    text.remove(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression("\\[\\[.*?\\]\\]", QRegularExpression::DotMatchesEverythingOption));
    QStringList rows = text.split('\n');
    if (!rows.isEmpty() && titleKey().match(rows.first()).hasMatch()) {
        qsizetype k = 0;
        while (k < rows.size() && !rows[k].trimmed().isEmpty()) ++k;
        rows = rows.mid(k);
    }
    auto blank = [&](qsizetype k) { return k < 0 || k >= rows.size() || rows[k].trimmed().isEmpty(); };
    static const QRegularExpression pdfNoise = rx("^(?:\\d{1,3}[A-Z]?\\.|\\(MORE\\)|\\(?CONTINUED\\)?:?|CONTINUED:)$", true);
    static const QRegularExpression sceneNum = rx("\\s*#[^#\\s]+#$");
    QList<Line> out;
    bool inSpeech = false, joinable = false;
    auto push = [&](const QString &type, const QString &t, bool join = false) {
        if (join && joinable && !out.isEmpty() && out.last().type == type) out.last().text += " " + t;
        else out << Line{type, t};
        joinable = join;
    };
    for (qsizetype k = 0; k < rows.size(); ++k) {
        const QString s = rows[k].trimmed();
        if (s.isEmpty()) { inSpeech = false; joinable = false; continue; }
        if (rx("^={3,}$").match(s).hasMatch() || s.startsWith('#') || rx("^=[^=]").match(s).hasMatch() || s == "="
            || pdfNoise.match(s).hasMatch())
            continue;
        if (inSpeech) {
            if (rx("^\\(.*\\)$").match(s).hasMatch()) push("paren", s);
            else {
                QString d = s;
                d.remove(rx("^~\\s*"));
                push("dialogue", d, true);
            }
            continue;
        }
        if (s.startsWith('!')) { push("action", s.mid(1).trimmed(), true); continue; }
        if (rx("^\\.[^.\\s]").match(s).hasMatch()) {
            QString h = s.mid(1).trimmed();
            h.remove(sceneNum);
            push("heading", h);
            continue;
        }
        if (s.startsWith('>') && s.endsWith('<')) { push("action", s.mid(1, s.size() - 2).trimmed()); continue; }
        if (s.startsWith('>')) { push("transition", s.mid(1).trimmed()); continue; }
        if (s.startsWith('~')) { push("action", s.mid(1).trimmed()); continue; }
        if (s.startsWith('@')) {
            QString n = s.mid(1).trimmed();
            n.remove(rx("\\s*\\^$"));
            push("character", dropContd(n));
            inSpeech = true;
            continue;
        }
        if (isHeading(s) && blank(k - 1)) {
            QString h = s;
            h.remove(sceneNum);
            push("heading", h);
            continue;
        }
        if (looksLikeTransition(s) && blank(k - 1) && blank(k + 1)) { push("transition", s); continue; }
        QString noCaret = s;
        noCaret.remove(rx("\\s*\\^$"));
        if (blank(k - 1) && !blank(k + 1) && looksLikeCharacter(noCaret)) {
            push("character", dropContd(noCaret));
            inSpeech = true;
            continue;
        }
        push("action", s, true);
    }
    return out;
}

QList<Run> runsFromFountain(const QString &t)
{
    QList<Run> runs;
    bool b = false, i = false, u = false;
    QString buf;
    auto flush = [&] {
        if (buf.isEmpty()) return;
        Run r;
        r.text = buf;
        r.b = b;
        r.i = i;
        r.u = u;
        runs << r;
        buf.clear();
    };
    // a mark only counts where a closing one follows on the line
    auto closes = [&](qsizetype from, const QString &mark) { return t.indexOf(mark, from) > -1; };
    for (qsizetype k = 0; k < t.size(); ++k) {
        const QChar c = t[k];
        if (c == '\\' && k + 1 < t.size()) { buf += t[++k]; continue; }
        if (c == '*') {
            int n = 1;
            while (k + n < t.size() && t[k + n] == '*' && n < 3) n++;
            const bool on = n == 3 ? (b && i) : n == 2 ? b : i;
            if (on || closes(k + n, QString(n, '*'))) {
                flush();
                if (n == 3) { b = !on; i = !on; }
                else if (n == 2) b = !b;
                else i = !i;
                k += n - 1;
                continue;
            }
        }
        if (c == '_' && (u || closes(k + 1, "_"))) {
            flush();
            u = !u;
            continue;
        }
        buf += c;
    }
    flush();
    return runs;
}

QString fountainOfRuns(const QList<Run> &runs)
{
    QString out;
    static const QRegularExpression escape("([\\\\*_])");
    for (const Run &r : runs) {
        if (r.mark) continue;
        QString s = r.text;
        s.replace(escape, "\\\\1");
        qsizetype lead = 0;
        while (lead < s.size() && s[lead].isSpace()) ++lead;
        qsizetype trail = 0;
        while (trail < s.size() - lead && s[s.size() - 1 - trail].isSpace()) ++trail;
        QString core = s.mid(lead, s.size() - lead - trail);
        if (core.isEmpty()) { out += s; continue; }
        const QString mark = (r.b && r.i) ? "***" : r.b ? "**" : r.i ? "*" : "";
        if (r.u) core = "_" + core + "_";
        out += s.left(lead) + mark + core + mark + s.right(trail);
    }
    return out;
}

TitlePage fountainTitle(const QString &src)
{
    QString text = src;
    text.replace(QRegularExpression("\\r\\n?"), "\n").replace('\t', "    ");
    const QStringList rows = text.split('\n');
    TitlePage out;
    if (rows.isEmpty() || !titleKey().match(rows.first()).hasMatch()) return out;
    QHash<QString, QStringList> vals;
    QString key;
    static const QRegularExpression kv("^([^:]+):\\s*(.*)$");
    for (const QString &row : rows) {
        if (row.trimmed().isEmpty()) break;
        auto m = kv.match(row);
        if (!(row.size() && row[0].isSpace()) && m.hasMatch()) {
            key = m.captured(1).trimmed().toLower();
            vals[key] = m.captured(2).trimmed().isEmpty() ? QStringList{} : QStringList{m.captured(2).trimmed()};
        } else if (!key.isEmpty()) {
            vals[key] << row.trimmed();
        }
    }
    auto plain = [](const QStringList &a) {
        QStringList out;
        for (QString r : a) {
            r.remove(QRegularExpression("^>\\s*|\\s*<$"));
            const QString s = runsText(runsFromFountain(r)).trimmed();
            if (!s.isEmpty()) out << s;
        }
        return out;
    };
    if (vals.contains("title")) out.title = plain(vals.value("title")).join(' ');
    if (vals.contains("credit")) out.credit = plain(vals.value("credit")).join(' ');
    if (vals.contains("author") || vals.contains("authors"))
        out.author = plain(vals.contains("author") ? vals.value("author") : vals.value("authors")).join(" & ");
    if (vals.contains("draft date") || vals.contains("date"))
        out.draft = plain(vals.contains("draft date") ? vals.value("draft date") : vals.value("date")).join('\n');
    if (vals.contains("contact")) out.contact = plain(vals.value("contact")).join('\n');
    return out;
}

Fdx fromFdx(const QString &xmlIn)
{
    static const QHash<QString, QString> types{{"scene heading", "heading"}, {"action", "action"},
                                               {"character", "character"},   {"parenthetical", "paren"},
                                               {"dialogue", "dialogue"},     {"transition", "transition"},
                                               {"shot", "shot"},             {"lyrics", "dialogue"},
                                               {"general", "action"}};
    const auto dot = QRegularExpression::DotMatchesEverythingOption;
    const QString body = QRegularExpression("<Content>(.*?)</Content>", dot).match(xmlIn).captured(1);
    Fdx out;
    for (const FdxPara &p : fdxParas(body)) {
        const QString text = runsText(p.runs).trimmed();
        if (text.isEmpty()) continue;
        const QString type = types.value(p.type.toLower(), "action");
        QList<Run> runs = p.runs;
        if (type == "character") {
            Run r;
            r.text = dropContd(text);
            runs = {r};
        }
        if (type == "paren" && !text.startsWith('(')) {
            Run r;
            r.text = "(" + text + ")";
            runs = {r};
        }
        out.lines << FdxLine{type, runs};
    }
    // the title page: centered lines are the title, the credit and the writer;
    // lines set left are the contact; set right, the draft
    const QString page = QRegularExpression("<TitlePage>.*?<Content>(.*?)</Content>", dot).match(xmlIn).captured(1);
    QList<QPair<QString, QString>> tp; // align, text
    for (const FdxPara &p : fdxParas(page)) {
        const QString t = runsText(p.runs).trimmed();
        if (!t.isEmpty()) tp << QPair{p.align, t};
    }
    QStringList centered, left, right;
    for (const auto &[align, text] : tp) {
        if (align == "center") centered << text;
        else if (align == "right") right << text;
        else left << text;
    }
    if (!centered.isEmpty()) {
        out.title.title = centered.first();
        static const QRegularExpression credit = rx("^(?:written by|screenplay by|teleplay by|story by|by)$", true);
        qsizetype c = -1;
        for (qsizetype k = 1; k < centered.size(); ++k)
            if (credit.match(centered[k]).hasMatch()) { c = k; break; }
        if (c > 0) {
            out.title.credit = centered[c];
            if (c + 1 < centered.size()) out.title.author = centered[c + 1];
        } else if (centered.size() > 1) {
            out.title.author = centered[1];
        }
    }
    if (!left.isEmpty()) out.title.contact = left.join('\n');
    if (!right.isEmpty()) out.title.draft = right.join('\n');
    return out;
}

QString toFdx(const QList<FdxLine> &lines, const TitlePage &title)
{
    static const QHash<QString, QString> names{{"heading", "Scene Heading"}, {"action", "Action"},
                                               {"character", "Character"},   {"paren", "Parenthetical"},
                                               {"dialogue", "Dialogue"},     {"transition", "Transition"},
                                               {"shot", "Shot"}};
    static const QStringList caps{"heading", "character", "transition", "shot"};
    auto textEl = [](const Run &r, bool upper) {
        QStringList style;
        if (r.b) style << "Bold";
        if (r.i) style << "Italic";
        if (r.u) style << "Underline";
        if (r.s) style << "Strikeout";
        return QStringLiteral("      <Text%1>%2</Text>\n")
            .arg(style.isEmpty() ? QString() : " Style=\"" + style.join('+') + "\"", xmlEsc(upper ? r.text.toUpper() : r.text));
    };
    QString out = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n<FinalDraft DocumentType=\"Script\" Template=\"No\" Version=\"1\">\n\n  <Content>\n";
    for (const FdxLine &l : lines) {
        QList<Run> runs;
        for (const Run &r : l.runs)
            if (!r.text.isEmpty() && !r.mark) runs << r;
        if (runs.isEmpty()) continue;
        out += "    <Paragraph Type=\"" + names.value(l.type, "Action") + "\">\n";
        for (const Run &r : runs) out += textEl(r, caps.contains(l.type));
        out += "    </Paragraph>\n";
    }
    out += "  </Content>\n";
    auto para = [](const QString &text, const QString &align) {
        return "    <Paragraph Alignment=\"" + align + "\">\n      <Text>" + xmlEsc(text) + "</Text>\n    </Paragraph>\n";
    };
    auto gap = [](int n) {
        return QString("    <Paragraph Alignment=\"Center\">\n      <Text></Text>\n    </Paragraph>\n").repeated(n);
    };
    auto rows = [](const QString &s) {
        QStringList r;
        for (const QString &x : s.split('\n'))
            if (!x.trimmed().isEmpty()) r << x.trimmed();
        return r;
    };
    QString tp;
    if (!title.title.isEmpty()) tp += gap(18) + para(title.title.toUpper(), "Center");
    if (!title.credit.isEmpty()) tp += gap(1) + para(title.credit, "Center");
    if (!title.author.isEmpty()) tp += gap(1) + para(title.author, "Center");
    if (!rows(title.draft).isEmpty() || !rows(title.contact).isEmpty()) tp += gap(16);
    for (const QString &r : rows(title.draft)) tp += para(r, "Right");
    for (const QString &r : rows(title.contact)) tp += para(r, "Left");
    if (!tp.isEmpty()) {
        tp.replace(QRegularExpression("^ {4}", QRegularExpression::MultilineOption), "      ");
        out += "  <TitlePage>\n    <Content>\n" + tp + "    </Content>\n  </TitlePage>\n";
    }
    return out + "</FinalDraft>\n";
}

QString typeOf(const Para &p)
{
    for (const QString &c : p.classes())
        if (c.startsWith("sp-") && kTypes.contains(c.mid(3))) return c.mid(3);
    return "action";
}

void setType(Para &p, const QString &type)
{
    for (const QString &t : kTypes) p.removeClass("sp-" + t);
    if (type != "action" && kTypes.contains(type)) p.addClass("sp-" + type);
}

} // namespace neosea::sp
