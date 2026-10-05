#include "core/exporter.h"

#include "core/bookmodel.h"
#include "core/htmldom.h"
#include "core/i18n.h"
#include "core/storage.h"
#include "core/typing.h"

#include <QDateTime>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QUuid>

#include <zip.h>

namespace neosea {

using html::Node;

namespace {

QString esc(const QString &s) { return html::escHtml(s); }

QString escXml(const QString &s)
{
    QString o = s;
    o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;").replace('\'', "&apos;");
    return o;
}

// A paragraph's runs for an export: an italic inside an italic is emphasis in
// a poetry paragraph (itself one italic), set upright, the typesetter's way
void exportRuns(const Node &n, bool b, bool i, bool u, bool s, bool flip, QList<Run> &out)
{
    for (const auto &c : n.children) {
        if (c->type == Node::Text) {
            if (c->text.isEmpty()) continue;
            Run r;
            r.text = c->text;
            r.text.replace(QChar(0xa0), ' ');
            r.b = b;
            r.i = i;
            r.u = u;
            r.s = s;
            out << r;
            continue;
        }
        if (c->hasClass("ph-mark") || c->hasClass("darling-anchor")) continue;
        if (c->tag == "br") continue;
        const QString tag = c->tag;
        const QString fw = c->style("font-weight");
        const bool it = tag == "i" || tag == "em" || c->style("font-style") == "italic";
        const bool bo = tag == "b" || tag == "strong" || fw == "bold" || fw.toInt() >= 600;
        QString deco = c->style("text-decoration");
        if (deco.isEmpty()) deco = c->style("text-decoration-line");
        const bool un = tag == "u" || tag == "ins" || deco.contains("underline");
        const bool st = tag == "s" || tag == "strike" || tag == "del" || deco.contains("line-through");
        exportRuns(*c, b || bo, it ? (flip ? !i : true) : i, u || un, s || st, flip, out);
    }
}

QList<Run> mergeRuns(const QList<Run> &runs)
{
    QList<Run> out;
    for (const Run &r : runs) {
        if (r.text.isEmpty()) continue;
        if (!out.isEmpty() && out.last().sameStyle(r)) out.last().text += r.text;
        else out << r;
    }
    return out;
}

QString runsHtml(const QList<Run> &runs)
{
    QString out;
    for (const Run &r : runs) {
        QString t = esc(r.text);
        if (r.s) t = "<s>" + t + "</s>";
        if (r.u) t = "<u>" + t + "</u>";
        if (r.i) t = "<i>" + t + "</i>";
        if (r.b) t = "<b>" + t + "</b>";
        out += t;
    }
    return out;
}

QString runsXhtml(const QList<Run> &runs, bool flipItalic = false)
{
    QString out;
    for (const Run &r : runs) {
        QString t = escXml(r.text);
        const bool it = flipItalic ? !r.i : r.i;
        if (r.s) t = "<s>" + t + "</s>";
        if (r.u) t = "<u>" + t + "</u>";
        if (it) t = "<em>" + t + "</em>";
        if (r.b) t = "<strong>" + t + "</strong>";
        out += t;
    }
    return out;
}

bool isAttr(const ExportPara &p) { return typing::isAttribution(p.text); }

// the page's first line is a part's title; what follows, a quote or a verse
void partTitleOf(QList<ExportPara> paras, QString &title, QList<ExportPara> &rest)
{
    const bool titled = !paras.isEmpty() && !paras.first().sceneBreak && !isAttr(paras.first());
    title = titled ? paras.first().text : QString();
    if (titled) paras.removeFirst();
    rest = paras;
}

const QStringList kFrontPages{"copyright", "dedication", "epigraph"};

} // namespace

QList<ExportPara> parasFromHtml(const QString &htmlText)
{
    auto root = html::parse(htmlText);
    // an unwritten outline section is a ghost plus the scene break NEO planted for it
    QSet<QString> ghostIds;
    for (Node *n : root->descendants("p"))
        if (n->hasClass("ghost") && n->hasAttr("data-sec-id")) ghostIds.insert(n->attr("data-sec-id"));
    QList<ExportPara> out;
    for (Node *p : root->descendants("p")) {
        if (p->hasClass("ghost")) continue;
        if (p->hasClass("scene-break") && ghostIds.contains(p->attr("data-sec-brk"))) continue;
        ExportPara e;
        e.sceneBreak = p->hasClass("scene-break");
        e.poetry = p->hasClass("poetry");
        e.flush = !e.poetry && p->hasClass("flush");
        e.align = p->style("text-align");
        QList<Run> runs;
        exportRuns(*p, false, false, false, false, true, runs);
        e.runs = mergeRuns(runs);
        QString text;
        for (const Run &r : e.runs) text += r.text;
        e.text = text.trimmed();
        if (e.sceneBreak || !e.text.isEmpty()) out << e;
    }
    return out;
}

QString plainHeading(const Section &s)
{
    return s.partTitle.isEmpty() ? s.heading : s.heading + ": " + s.partTitle;
}

QString safeName(const QString &s)
{
    auto clean = [](const QString &x) {
        QString o;
        for (QChar c : x)
            if (c.isLetterOrNumber() || c.isMark() || c == '_' || c == '-' || c.isSpace()) o += c;
        o = o.trimmed();
        o.replace(QRegularExpression("\\s+"), "-");
        return o;
    };
    const QString a = clean(s);
    return a.isEmpty() ? clean(t("Untitled")) : a;
}

bool bookExportData(QJsonObject &book, const QHash<QString, QString> &chapters, bool customTitlesOnly,
                    const QString &language, ExportData &d)
{
    bool changed = false;
    if (book.value("uuid").toString().isEmpty()) {
        book.insert("uuid", QUuid::createUuid().toString(QUuid::WithoutBraces));
        changed = true;
    }
    const QString solo = soloStory(book);
    int parts = 0, contentsAt = -1;
    bool inPart = false;
    auto push = [&](Section s) {
        s.num = int(d.sections.size()) + 1;
        d.sections << s;
        return s.num;
    };
    for (const QString &chId : chapterOrder(book)) {
        const QString kind = chapterKind(chId, book);
        if (kind == "part") { parts++; inPart = true; }
        else if (kBackKinds.contains(kind)) inPart = false;
        if (kind == "contents") {
            if (contentsAt < 0) contentsAt = int(d.sections.size());
            continue;
        }
        const QList<ExportPara> paras = parasFromHtml(chapters.value(chId));
        if (kFrontPages.contains(kind)) {
            if (!paras.isEmpty()) {
                Section s;
                s.kind = kind;
                s.label = kindName(kind);
                s.paras = paras;
                push(s);
            }
            continue;
        }
        if (kind == "acknowledgments" || kind == "about") {
            if (paras.isEmpty()) continue;
            Section s;
            s.kind = kind;
            s.heading = kindName(kind);
            s.paras = paras;
            const int n = push(s);
            d.toc << TocEntry{s.heading, n, 0, "page"};
            continue;
        }
        if (kind == "part") {
            Section s;
            s.kind = "part";
            s.heading = partLabel(parts);
            partTitleOf(paras, s.partTitle, s.paras);
            const int n = push(s);
            d.toc << TocEntry{s.partTitle.isEmpty() ? s.heading : s.heading + ": " + s.partTitle, n, 0, "part"};
            continue;
        }
        // the story: chapterless stories export as continuous text
        Section s;
        s.heading = chId == solo ? QString() : chapterHeading(chId, book, customTitlesOnly);
        s.paras = paras;
        s.role = chapterRole(chId, book);
        s.level = inPart ? 1 : 0;
        s.chId = chId;
        const int n = push(s);
        d.toc << TocEntry{s.heading.isEmpty() ? book.value("title").toString() : s.heading, n, s.level, "chapter"};
    }
    if (contentsAt >= 0) {
        for (qsizetype i = 0; i < d.sections.size(); ++i) d.sections[i].front = i < contentsAt;
    } else {
        for (Section &s : d.sections) {
            if (!kFrontPages.contains(s.kind)) break;
            s.front = true;
        }
    }
    d.id = book.value("id").toString();
    d.uuid = book.value("uuid").toString();
    d.title = book.value("title").toString();
    d.subtitle = book.value("subtitle").toString();
    d.author = book.value("author").toString();
    if (d.author.isEmpty()) d.author = t("Anonymous");
    d.language = language;
    d.coverSeed = book.value("coverSeed").toString();
    d.coverImage = book.value("coverImage").toString();
    d.contents = contentsAt >= 0;
    d.contentsChapters = true;
    return changed;
}

std::optional<ExportData> chapterExportData(const ExportData &book, const QString &chId)
{
    for (const Section &s : book.sections) {
        if (s.kind != "chapter" || s.chId != chId) continue;
        ExportData d = book;
        Section one = s;
        one.num = 1;
        one.level = 0;
        one.front = false;
        d.sections = {one};
        d.toc = {TocEntry{one.heading.isEmpty() ? d.title : one.heading, 1, 0, "chapter"}};
        d.contents = false;
        d.chapterOnly = one.heading.isEmpty() ? t("Chapter") : one.heading;
        return d;
    }
    return std::nullopt;
}

ExportData shelfBookData(const Library &lib, QJsonObject &shelf, const QJsonObject &library, bool bound,
                         const QString &titleIn, const QString &language)
{
    QJsonObject binding = shelf.value("binding").toObject();
    const bool through = bound && binding.value("numbering").toString("through") != "restart";
    const bool customTitles = library.value("exportCustomChapterTitles").toBool();
    QList<QJsonObject> metas;
    for (const auto &idv : shelf.value("bookIds").toArray()) {
        auto m = lib.readBookMeta(idv.toString());
        if (m && (bound || !kPageKinds.contains(m->value("kind").toString()))) metas << *m;
    }
    QJsonObject cover;
    for (const auto &m : metas)
        if (m.value("kind").toString() == "cover") { cover = m; break; }
    QString author = cover.value("author").toString();
    if (author.isEmpty()) {
        // the pen name that owns the shelf
        const QJsonArray authors = library.value("authors").toArray();
        for (const auto &a : authors)
            if (a.toObject().value("id") == shelf.value("authorId")) author = a.toObject().value("name").toString();
        if (author.isEmpty() && !authors.isEmpty()) author = authors.first().toObject().value("name").toString();
        if (author.isEmpty()) author = t("Anonymous");
    }
    QList<QJsonObject> titles;
    for (const auto &m : metas)
        if (!kPageKinds.contains(m.value("kind").toString())) titles << m;
    const bool single = titles.size() == 1;
    auto firstChapter = [&](const QJsonObject &m) {
        const QStringList order = chapterOrder(m);
        return order.isEmpty() ? QString() : lib.readChapter(m.value("id").toString(), order.first());
    };
    ExportData d;
    auto push = [&](Section s) {
        s.num = int(d.sections.size()) + 1;
        d.sections << s;
        return s.num;
    };
    int n = 0, parts = 0;
    bool inPart = false;
    for (const QJsonObject &m : metas) {
        const QString kind = m.value("kind").toString();
        if (kind == "cover") continue;
        if (kPageFront.contains(kind) || kPageBack.contains(kind)) {
            const auto paras = parasFromHtml(firstChapter(m));
            if (paras.isEmpty()) continue; // a page left blank stays out of the book
            const bool back = kPageBack.contains(kind);
            Section s;
            s.kind = kind;
            s.front = !back;
            s.heading = back ? pageKindName(kind) : QString();
            s.label = pageKindName(kind);
            s.paras = paras;
            const int num = push(s);
            if (back) {
                d.toc << TocEntry{s.heading, num, 0, "page"};
                inPart = false;
            }
            continue;
        }
        if (kind == "part") {
            parts++;
            Section s;
            s.kind = "part";
            s.heading = partLabel(parts);
            partTitleOf(parasFromHtml(firstChapter(m)), s.partTitle, s.paras);
            const int num = push(s);
            d.toc << TocEntry{s.partTitle.isEmpty() ? s.heading : s.heading + ": " + s.partTitle, num, 0, "part"};
            inPart = true;
            continue;
        }
        if (kPageWritten.contains(kind)) {
            // the book's own prologue or epilogue: one unnumbered section
            QList<ExportPara> paras;
            for (const QString &chId : chapterOrder(m)) {
                const auto p = parasFromHtml(lib.readChapter(m.value("id").toString(), chId));
                if (p.isEmpty()) continue;
                if (!paras.isEmpty()) {
                    ExportPara brk;
                    brk.sceneBreak = true;
                    paras << brk;
                }
                paras << p;
            }
            if (paras.isEmpty()) continue;
            Section s;
            s.role = kind;
            s.heading = isUntitled(m.value("title").toString()) ? pageKindName(kind) : m.value("title").toString().trimmed();
            s.paras = paras;
            const int num = push(s);
            d.toc << TocEntry{s.heading, num, 0, "title"};
            inPart = false;
            continue;
        }
        if (!through) n = 0;
        const int level = inPart ? 1 : 0;
        struct Ch {
            QString chId, kind;
            QList<ExportPara> paras;
        };
        QList<Ch> chapters;
        for (const QString &chId : chapterOrder(m)) {
            const QString k = chapterKind(chId, m);
            if (!kStoryKinds.contains(k) && k != "part") continue;
            const auto paras = parasFromHtml(lib.readChapter(m.value("id").toString(), chId));
            if (!paras.isEmpty() || k == "part") chapters << Ch{chId, k, paras};
        }
        while (!chapters.isEmpty() && chapters.last().kind == "part") chapters.removeLast();
        if (chapters.isEmpty()) continue;
        const QString mAuthor = m.value("author").toString();
        const QString byline = (!mAuthor.isEmpty() && mAuthor != author) ? mAuthor : QString();
        const QString mTitle = m.value("title").toString();
        if (single && chapters.size() == 1) {
            Section s;
            s.paras = chapters.first().paras;
            push(s);
            continue;
        }
        if (chapters.size() == 1) {
            // one chapter is one section, headed by the title's own name
            Section s;
            s.heading = mTitle;
            s.byline = byline;
            s.level = level;
            s.paras = chapters.first().paras;
            const int num = push(s);
            d.toc << TocEntry{mTitle, num, level, "title"};
            continue;
        }
        int chLevel = level;
        if (!single) {
            Section s;
            s.kind = "opener";
            s.heading = mTitle;
            s.subtitle = m.value("subtitle").toString();
            s.byline = byline;
            s.level = level;
            const int num = push(s);
            d.toc << TocEntry{mTitle, num, level, "title"};
            chLevel = level + 1;
        }
        int titleParts = 0;
        bool underPart = false;
        for (const Ch &c : chapters) {
            if (c.kind == "part") {
                titleParts++;
                if (m.value("restartNumbering").toBool()) n = 0;
                Section s;
                s.kind = "part";
                s.heading = partLabel(titleParts);
                partTitleOf(c.paras, s.partTitle, s.paras);
                s.level = chLevel;
                const int num = push(s);
                d.toc << TocEntry{s.partTitle.isEmpty() ? s.heading : s.heading + ": " + s.partTitle, num, chLevel, "part"};
                underPart = true;
                continue;
            }
            const QString role = chapterRole(c.chId, m);
            if (role == "epilogue") underPart = false;
            const QString chTitle = m.value("chapterTitles").toObject().value(c.chId).toString();
            QString heading;
            if (c.kind == "unnumbered") heading = chTitle;
            else {
                if (!role.isEmpty()) heading = role == "prologue" ? t("Prologue") : t("Epilogue");
                else heading = t("Chapter {n}", {{"n", ++n}});
                if (!chTitle.isEmpty()) heading = customTitles ? chTitle : heading + " — " + chTitle;
            }
            const int lv = underPart ? chLevel + 1 : chLevel;
            Section s;
            s.heading = heading;
            s.level = lv;
            s.paras = c.paras;
            s.role = role;
            const int num = push(s);
            d.toc << TocEntry{heading, num, lv, "chapter"};
        }
    }
    if (bound) {
        if (binding.value("uuid").toString().isEmpty()) {
            binding.insert("uuid", QUuid::createUuid().toString(QUuid::WithoutBraces));
            shelf.insert("binding", binding);
        }
        d.uuid = binding.value("uuid").toString();
    }
    d.title = titleIn.isEmpty() ? shelf.value("name").toString() : titleIn;
    d.id = !cover.value("coverImage").toString().isEmpty() ? cover.value("id").toString()
                                                            : "shelf-" + shelf.value("id").toString();
    d.subtitle = cover.value("subtitle").toString();
    d.author = author;
    d.language = language;
    d.coverSeed = !cover.isEmpty() ? cover.value("coverSeed").toString()
                                   : shelf.value("id").toString() + ":" + d.title;
    d.coverImage = cover.value("coverImage").toString();
    bool ownContents = false;
    if (single)
        for (const QString &c : chapterOrder(titles.first()))
            ownContents |= chapterKind(c, titles.first()) == "contents";
    d.contents = titles.size() > 1 || (single && ownContents);
    d.contentsChapters = single;
    return d;
}

QString buildTxt(const ExportData &d)
{
    QString out = d.title.toUpper() + "\n";
    if (!d.subtitle.isEmpty()) out += d.subtitle + "\n";
    out += t("by {author}", {{"author", d.author}}) + "\n\n\n";
    for (const Section &ch : d.sections) {
        if (!ch.heading.isEmpty()) out += plainHeading(ch).toUpper() + "\n\n";
        for (const ExportPara &p : ch.paras)
            out += p.sceneBreak ? QStringLiteral("\n***\n\n") : (p.poetry ? QStringLiteral("    ") : QString()) + p.text + "\n\n";
        out += "\n";
    }
    return out;
}

QString buildMd(const ExportData &d)
{
    static const QRegularExpression metaRe("([\\\\`*_\\[\\]#<>])");
    static const QRegularExpression runRe("([\\\\*_`~])");
    auto mdMeta = [](QString s) { return s.replace(metaRe, "\\\\1"); };
    auto mdRun = [](const Run &r) {
        QString t = r.text;
        t.replace(runRe, "\\\\1");
        const QString mark = (r.b && r.i) ? "***" : r.b ? "**" : r.i ? "*" : "";
        if (mark.isEmpty() && !r.s && !r.u) return t;
        qsizetype lead = 0;
        while (lead < t.size() && t[lead].isSpace()) ++lead;
        qsizetype trail = 0;
        while (trail < t.size() - lead && t[t.size() - 1 - trail].isSpace()) ++trail;
        QString core = t.mid(lead, t.size() - lead - trail);
        if (core.isEmpty()) return t;
        if (r.s) core = "~~" + core + "~~";
        if (r.u) core = "<u>" + core + "</u>";
        return t.left(lead) + mark + core + mark + t.right(trail);
    };
    QString out = "# " + mdMeta(d.title) + "\n\n";
    if (!d.subtitle.isEmpty()) out += "*" + mdMeta(d.subtitle) + "*\n\n";
    out += "**" + t("by {author}", {{"author", mdMeta(d.author)}}) + "**\n\n";
    for (const Section &ch : d.sections) {
        if (!ch.heading.isEmpty()) out += "\n## " + mdMeta(plainHeading(ch)) + "\n\n";
        for (const ExportPara &p : ch.paras) {
            if (p.sceneBreak) { out += "\n***\n\n"; continue; }
            QString line = p.poetry ? QStringLiteral("> ") : QString();
            for (const Run &r : p.runs) line += mdRun(r);
            out += line + "\n\n";
        }
    }
    return out;
}

QString buildHtml(const ExportData &d, const HtmlOptions &o)
{
    int total = 0;
    for (const Section &ch : d.sections)
        if (ch.kind == "chapter")
            for (const ExportPara &p : ch.paras) total += countWords(p.text);
    auto paraHtml = [](const ExportPara &p, const QStringList &extra) {
        QStringList cls = extra;
        if (p.poetry) cls.prepend("poetry");
        else if (p.flush) cls.prepend("flush");
        return "<p" + (cls.isEmpty() ? QString() : " class=\"" + cls.join(' ') + "\"")
            + (p.align.isEmpty() ? QString() : " style=\"text-align:" + p.align + "\"") + ">" + runsHtml(p.runs) + "</p>";
    };
    // a chapter's text: only its opening paragraph gets the enlarged initial
    auto prose = [&](const QList<ExportPara> &paras, bool initial) {
        bool first = initial, afterBreak = false;
        QStringList out;
        for (const ExportPara &p : paras) {
            if (p.sceneBreak) { afterBreak = initial; out << "<p class=\"brk\">***</p>"; continue; }
            if (p.poetry) { afterBreak = false; out << paraHtml(p, {}); continue; }
            QStringList extra;
            if (first || afterBreak) {
                if (first) extra << "first";
                if (typing::opensWithDash(p.text)) extra << "dialogue";
            }
            first = false;
            afterBreak = false;
            out << paraHtml(p, extra);
        }
        return out.join('\n');
    };
    auto lines = [&](const QList<ExportPara> &paras, bool attrs) {
        QStringList out;
        for (const ExportPara &p : paras) {
            if (p.sceneBreak) { out << "<p class=\"brk\">***</p>"; continue; }
            QStringList cls;
            if (attrs && isAttr(p)) cls << "attr";
            if (p.poetry) cls << "poetry";
            out << "<p" + (cls.isEmpty() ? QString() : " class=\"" + cls.join(' ') + "\"")
                       + (p.align.isEmpty() ? QString() : " style=\"text-align:" + p.align + "\"") + ">" + runsHtml(p.runs) + "</p>";
        }
        return out.join('\n');
    };
    auto sectionHtml = [&](const Section &ch) {
        const QString id = "s" + QString::number(ch.num);
        const QString h = "h" + QString::number(std::min(6, 2 + ch.level));
        if (kFrontPages.contains(ch.kind))
            return QStringLiteral("\n    <section class=\"page %1\" id=\"%2\"><div class=\"pg-in\">%3</div></section>")
                .arg(ch.kind, id, lines(ch.paras, ch.kind != "copyright"));
        if (ch.kind == "part")
            return QStringLiteral("\n    <section class=\"page part\" id=\"%1\">\n      <%2 class=\"hd\"><span class=\"pl\">%3</span>%4</%2>\n      %5\n    </section>")
                .arg(id, h, esc(ch.heading),
                     ch.partTitle.isEmpty() ? QString() : "<span class=\"sep\">: </span><span class=\"pt\">" + esc(ch.partTitle) + "</span>",
                     lines(ch.paras, true));
        if (ch.kind == "opener")
            return QStringLiteral("\n    <section class=\"page opener\" id=\"%1\">\n      <%2 class=\"hd\">%3</%2>\n      %4\n      %5\n    </section>")
                .arg(id, h, esc(ch.heading),
                     ch.subtitle.isEmpty() ? QString() : "<p class=\"sub\">" + esc(ch.subtitle) + "</p>",
                     ch.byline.isEmpty() ? QString() : "<p class=\"byline\">" + esc(ch.byline) + "</p>");
        const bool back = ch.kind == "acknowledgments" || ch.kind == "about";
        return QStringLiteral("\n    <section class=\"chapter%1\" id=\"%2\">\n      %3\n      %4\n      %5\n    </section>")
            .arg(back ? " backpage" : "", id,
                 ch.heading.isEmpty() ? QString() : "<" + h + " class=\"hd\">" + esc(ch.heading) + "</" + h + ">",
                 ch.byline.isEmpty() ? QString() : "<p class=\"byline\">" + esc(ch.byline) + "</p>", prose(ch.paras, !back));
    };
    QString contents;
    if (d.contents && !d.toc.isEmpty()) {
        contents = "\n    <nav class=\"contents\">\n      <h2 class=\"hd\">" + esc(t("Contents")) + "</h2>\n      <ol>";
        for (const TocEntry &e : d.toc) {
            if (!d.contentsChapters && e.type == "chapter") continue;
            contents += QStringLiteral("\n        <li class=\"lv%1 t-%2\"><a href=\"#s%3\"><span class=\"toc-t\">%4</span><span class=\"toc-pg\" data-for=\"s%3\"></span></a></li>")
                            .arg(e.level).arg(e.type).arg(e.num).arg(esc(e.label));
        }
        contents += "\n      </ol>\n    </nav>";
    }
    QString body;
    bool placed = contents.isEmpty();
    for (const Section &ch : d.sections) {
        if (!placed && !ch.front) { body += contents; placed = true; }
        body += sectionHtml(ch);
    }
    if (!placed) body += contents;
    const QString font = "'" + o.bodyFont + "', Georgia, serif";
    QString dropCss;
    if (!o.dropCapFont.isEmpty())
        dropCss = ".chapter p.first:not(.dialogue)::first-letter { -webkit-initial-letter: 2; initial-letter: 2; padding-right: 4px; font-family: '"
            + o.dropCapFont + "', Georgia, serif; }\n  @supports not ((initial-letter: 2) or (-webkit-initial-letter: 2)) { .chapter p.first:not(.dialogue)::first-letter { font-size: 1.8em; line-height: 1; padding-right: 0; } }";
    const QString stamp = QLocale(I18n::locale()).toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
    QString out = "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + esc(d.title) + "</title>\n<style>\n  " + o.fontsCss
        + "\n  body { font-family: " + font + "; color: #1c1c1c; max-width: 620px; margin: 40px auto; line-height: 1.7; font-size: 13pt; }\n"
        R"(  .coverpage { text-align: center; margin: 0 0 40px; page-break-after: always; }
  .coverpage img { display: block; margin: 0 auto; width: 100%; max-width: 620px; max-height: 95vh; object-fit: contain; }
  .titlepage { text-align: center; margin: 30vh 0 20vh; page-break-after: always; }
  .titlepage h1 { font-size: 30pt; margin: 0; }
  .titlepage .sub { font-style: italic; color: #555; }
  .titlepage .auth { margin-top: 40px; letter-spacing: 3px; text-transform: uppercase; font-size: 11pt; }
  .chapter { page-break-before: always; }
  .chapter .hd, .contents .hd { text-align: center; letter-spacing: 4px; font-variant-caps: all-small-caps; font-variant-numeric: oldstyle-nums; font-size: 17pt; font-weight: normal; color: #555; margin: 54px 0 36px; }
  .chapter p { text-indent: 2em; margin: 0; }
  .chapter .hd + p, .chapter .byline + p, .brk + p, .chapter p.first { text-indent: 0; }
  .chapter p.dialogue { text-indent: 2em; }
  )" + dropCss + R"(
  .brk { text-align: center; text-indent: 0 !important; letter-spacing: 8px; color: #888; margin: 2.5em 0; }
  .chapter p.poetry { text-indent: 0; margin: 0 2.5em; }
  .chapter p.flush { text-indent: 0 !important; }
  .chapter p:not(.poetry) + p.poetry, .chapter .hd + p.poetry { margin-top: 0.9em; }
  .chapter p.poetry + p:not(.poetry) { margin-top: 0.9em; }
  .chapter p.byline { text-align: center; margin: -24px 0 40px; letter-spacing: 3px; text-transform: uppercase; font-size: 10pt; color: #555; }
  .page { page-break-before: always; text-align: center; }
  .page p { margin: 0 0 0.5em; }
  .page p.poetry { margin: 0 0 0.2em; }
  .page p.attr { font-style: normal; font-size: 10pt; letter-spacing: 1px; margin-top: 1.2em; }
  .page .brk { margin: 1.2em 0; }
  .copyright { min-height: 98vh; display: flex; flex-direction: column; justify-content: flex-end; text-align: left; font-size: 9pt; line-height: 1.6; color: #333; }
  .copyright p { margin: 0 0 0.9em; }
  .dedication { padding-top: 26vh; font-style: italic; }
  .epigraph { padding-top: 24vh; margin: 0 3em; font-style: italic; }
  .part { padding-top: 28vh; }
  .part .hd { font-weight: normal; margin: 0 0 2.4em; }
  .part .pl { display: block; font-size: 15.5pt; letter-spacing: 5px; font-variant-caps: all-small-caps; color: #555; }
  .part .sep { color: transparent; font-size: 1px; line-height: 0; white-space: pre; }
  .part .pt { display: block; font-size: 24pt; line-height: 1.25; margin-top: 14px; }
  .part p { font-style: italic; margin-left: 3em; margin-right: 3em; }
  .dedication i, .epigraph i, .part p i { font-style: normal; }
  .opener { padding-top: 28vh; }
  .opener .hd { font-size: 26pt; font-weight: normal; line-height: 1.25; margin: 0; }
  .opener .sub { font-style: italic; color: #555; margin-top: 12px; }
  .opener .byline { margin-top: 40px; letter-spacing: 3px; text-transform: uppercase; font-size: 10pt; }
  .contents { page-break-before: always; }
  .contents ol { list-style: none; margin: 0 1.5em; padding: 0; }
  .contents li { margin: 0.3em 0; }
  .contents a { display: flex; align-items: baseline; color: inherit; text-decoration: none; }
  .contents .toc-t { flex: 1; }
  .contents .toc-pg { flex: none; width: 3em; text-align: right; font-size: 13pt; font-variant-numeric: lining-nums tabular-nums; }
  .contents li.lv1 { margin-left: 1.6em; }
  .contents li.lv2 { margin-left: 3.2em; }
  .contents li.t-part { margin-top: 1.1em; font-size: 15pt; line-height: 1.5; letter-spacing: 2px; font-variant-caps: all-small-caps; }
  .contents li:not(.t-page) + li.t-page { margin-top: 1.2em; }
  .prov { margin-top: 80px; text-align: center; color: #999; font-size: 9pt; }
</style></head><body>
)";
    if (!o.coverBytes.isEmpty())
        out += "<div class=\"coverpage\"><img src=\"data:" + o.coverMime + ";base64," + QString::fromLatin1(o.coverBytes.toBase64())
            + "\" alt=\"" + t("Cover") + "\"/></div>\n";
    out += "<div class=\"titlepage\"><h1>" + esc(d.title) + "</h1>\n"
        + (d.subtitle.isEmpty() ? QString() : "<p class=\"sub\">" + esc(d.subtitle) + "</p>") + "\n<p class=\"auth\">"
        + esc(d.author) + "</p></div>\n" + body + "\n";
    if (o.stamp)
        out += "<p class=\"prov\">" + t("{n} words · exported from NEO on {date}", {{"n", total}, {"date", stamp}}) + "</p>\n";
    out += "</body></html>";
    return out;
}

QByteArray buildZip(const QList<ZipEntry> &entries)
{
    zip_error_t err;
    zip_error_init(&err);
    zip_source_t *mem = zip_source_buffer_create(nullptr, 0, 0, &err);
    if (!mem) return {};
    zip_t *z = zip_open_from_source(mem, ZIP_TRUNCATE, &err);
    if (!z) {
        zip_source_free(mem);
        return {};
    }
    zip_source_keep(mem);
    for (const ZipEntry &e : entries) {
        void *copy = malloc(std::max<qsizetype>(1, e.content.size()));
        memcpy(copy, e.content.constData(), e.content.size());
        zip_source_t *src = zip_source_buffer(z, copy, e.content.size(), 1);
        const zip_int64_t idx = zip_file_add(z, e.path.toUtf8().constData(), src, ZIP_FL_ENC_UTF_8);
        if (idx < 0) {
            zip_source_free(src);
            continue;
        }
        zip_set_file_compression(z, zip_uint64_t(idx), e.store ? ZIP_CM_STORE : ZIP_CM_DEFLATE, 0);
    }
    if (zip_close(z) != 0) {
        zip_discard(z);
        zip_source_free(mem);
        return {};
    }
    QByteArray out;
    zip_stat_t st;
    if (zip_source_stat(mem, &st) == 0 && zip_source_open(mem) == 0) {
        out.resize(qsizetype(st.size));
        zip_source_read(mem, out.data(), st.size);
        zip_source_close(mem);
    }
    zip_source_free(mem);
    return out;
}

// ---------------------------------------------------------------------------
// DOCX

namespace {

struct DocxRun {
    QString text;
    bool b = false, i = false, u = false, s = false, br = false;
    int caps = -1; // -1 unset, 0 off, 1 on
    int size = 0;
};

struct DocxOpts {
    QString style, align;
    bool keepNext = false, pageBreak = false, poetry = false, indent = false, flip = false;
    int spaceBefore = 0, spaceAfter = 0, indentLeft = 0, size = 0, tracking = 0;
    int caps = -1;
};

QString docxP(const QList<DocxRun> &runs, const DocxOpts &o)
{
    QString pPr;
    if (!o.style.isEmpty()) pPr += "<w:pStyle w:val=\"" + o.style + "\"/>";
    if (o.keepNext) pPr += "<w:keepNext/>";
    if (o.pageBreak) pPr += "<w:pageBreakBefore/>";
    if (o.spaceBefore || o.spaceAfter)
        pPr += "<w:spacing" + (o.spaceBefore ? QStringLiteral(" w:before=\"%1\"").arg(o.spaceBefore) : QString())
            + (o.spaceAfter ? QStringLiteral(" w:after=\"%1\"").arg(o.spaceAfter) : QString()) + " w:line=\"360\" w:lineRule=\"auto\"/>";
    if (o.poetry) pPr += "<w:ind w:left=\"720\" w:right=\"720\"/>";
    else if (o.indentLeft) pPr += QStringLiteral("<w:ind w:left=\"%1\"/>").arg(o.indentLeft);
    else if (o.indent) pPr += "<w:ind w:firstLine=\"480\"/>";
    if (!o.align.isEmpty()) pPr += "<w:jc w:val=\"" + o.align + "\"/>";
    QString rXml;
    for (const DocxRun &r : runs) {
        if (r.br) { rXml += "<w:r><w:br/></w:r>"; continue; }
        const bool it = o.flip ? !r.i : r.i;
        const int caps = r.caps >= 0 ? r.caps : o.caps;
        const int size = r.size ? r.size : o.size;
        QString rPr = QString(r.b ? "<w:b/>" : "") + (it ? "<w:i/>" : "") + (r.s ? "<w:strike/>" : "") + (r.u ? "<w:u w:val=\"single\"/>" : "")
            + (caps == 1 ? "<w:caps/>" : caps == 0 ? "<w:caps w:val=\"0\"/>" : "")
            + (o.tracking ? QStringLiteral("<w:spacing w:val=\"%1\"/>").arg(o.tracking) : QString())
            + (size ? QStringLiteral("<w:sz w:val=\"%1\"/>").arg(size) : QString());
        rXml += "<w:r>" + (rPr.isEmpty() ? QString() : "<w:rPr>" + rPr + "</w:rPr>") + "<w:t xml:space=\"preserve\">" + escXml(r.text) + "</w:t></w:r>";
    }
    return "<w:p><w:pPr>" + pPr + "</w:pPr>" + rXml + "</w:p>";
}

QList<DocxRun> docxRuns(const QList<Run> &runs)
{
    QList<DocxRun> out;
    for (const Run &r : runs) {
        DocxRun d;
        d.text = r.text;
        d.b = r.b;
        d.i = r.i;
        d.u = r.u;
        d.s = r.s;
        out << d;
    }
    return out;
}

DocxRun textRun(const QString &text)
{
    DocxRun r;
    r.text = text;
    return r;
}

} // namespace

QByteArray buildDocx(const ExportData &d)
{
    QStringList body;
    auto heading = [](const Section &ch) { return "Heading" + QString::number(std::min(3, 1 + ch.level)); };
    auto pageLines = [&](const QList<ExportPara> &paras, bool italic, DocxOpts first) {
        for (qsizetype i = 0; i < paras.size(); ++i) {
            const ExportPara &p = paras[i];
            DocxOpts o = i == 0 ? first : DocxOpts{};
            o.align = "center";
            if (p.sceneBreak) { body << docxP({textRun("***")}, o); continue; }
            const bool attr = isAttr(p);
            o.flip = italic && !attr;
            if (attr) { o.size = 20; o.spaceBefore = 240; }
            body << docxP(docxRuns(p.runs), o);
        }
    };
    {
        DocxRun t1 = textRun(d.title);
        t1.b = true;
        DocxOpts o;
        o.align = "center";
        o.spaceBefore = 3000;
        o.size = 56;
        body << docxP({t1}, o);
        if (!d.subtitle.isEmpty()) {
            DocxRun s = textRun(d.subtitle);
            s.i = true;
            DocxOpts so;
            so.align = "center";
            so.size = 32;
            body << docxP({s}, so);
        }
        DocxOpts ao;
        ao.align = "center";
        ao.spaceBefore = 800;
        body << docxP({textRun(d.author)}, ao);
    }
    auto contents = [&] {
        DocxOpts h;
        h.align = "center";
        h.pageBreak = true;
        h.spaceBefore = 1200;
        h.size = 28;
        h.caps = 1;
        body << docxP({textRun(t("Contents"))}, h);
        body << docxP({}, {});
        for (const TocEntry &e : d.toc) {
            if (!d.contentsChapters && e.type == "chapter") continue;
            DocxOpts o;
            o.indentLeft = 480 * e.level;
            o.spaceBefore = e.type == "part" ? 240 : 0;
            o.caps = e.type == "part" ? 1 : -1;
            body << docxP({textRun(e.label)}, o);
        }
    };
    bool placed = !(d.contents && !d.toc.isEmpty());
    for (const Section &ch : d.sections) {
        if (!placed && !ch.front) { contents(); placed = true; }
        if (ch.kind == "copyright") {
            for (qsizetype i = 0; i < ch.paras.size(); ++i) {
                DocxOpts o;
                o.pageBreak = i == 0;
                o.spaceBefore = i == 0 ? 6000 : 0;
                o.spaceAfter = 120;
                o.size = 18;
                body << docxP(ch.paras[i].sceneBreak ? QList<DocxRun>{} : docxRuns(ch.paras[i].runs), o);
            }
            continue;
        }
        if (ch.kind == "dedication" || ch.kind == "epigraph") {
            DocxOpts f;
            f.pageBreak = true;
            f.spaceBefore = ch.kind == "dedication" ? 3600 : 3200;
            pageLines(ch.paras, true, f);
            continue;
        }
        if (ch.kind == "part") {
            QList<DocxRun> runs{textRun(ch.heading)};
            if (!ch.partTitle.isEmpty()) {
                DocxRun br;
                br.br = true;
                DocxRun pt = textRun(ch.partTitle);
                pt.caps = 0;
                pt.size = 48;
                runs << br << br << pt;
            }
            DocxOpts o;
            o.style = heading(ch);
            o.spaceBefore = 3600;
            o.spaceAfter = 480;
            body << docxP(runs, o);
            pageLines(ch.paras, true, {});
            continue;
        }
        if (ch.kind == "opener") {
            DocxRun h = textRun(ch.heading);
            h.caps = 0;
            h.size = 44;
            DocxOpts o;
            o.style = heading(ch);
            o.spaceBefore = 3600;
            body << docxP({h}, o);
            if (!ch.subtitle.isEmpty()) {
                DocxRun s = textRun(ch.subtitle);
                s.i = true;
                DocxOpts so;
                so.align = "center";
                so.size = 28;
                body << docxP({s}, so);
            }
            if (!ch.byline.isEmpty()) {
                DocxOpts bo;
                bo.align = "center";
                bo.spaceBefore = 600;
                bo.size = 20;
                bo.caps = 1;
                body << docxP({textRun(ch.byline)}, bo);
            }
            continue;
        }
        if (!ch.heading.isEmpty()) {
            DocxOpts o;
            o.style = heading(ch);
            body << docxP({textRun(ch.heading)}, o);
            if (!ch.byline.isEmpty()) {
                DocxOpts bo;
                bo.align = "center";
                bo.size = 20;
                bo.caps = 1;
                body << docxP({textRun(ch.byline)}, bo);
            }
            body << docxP({}, {});
        } else {
            DocxOpts o;
            o.pageBreak = true; // a headingless story still starts fresh
            body << docxP({}, o);
        }
        for (const ExportPara &p : ch.paras) {
            DocxOpts o;
            if (p.sceneBreak) {
                o.align = "center";
                o.spaceBefore = 240;
                body << docxP({textRun("***")}, o);
            } else if (p.poetry) {
                o.poetry = true;
                if (p.align == "center" || p.align == "right") o.align = p.align;
                body << docxP(docxRuns(p.runs), o);
            } else if (p.align == "center" || p.align == "right") {
                o.align = p.align;
                body << docxP(docxRuns(p.runs), o);
            } else {
                o.indent = !p.flush;
                body << docxP(docxRuns(p.runs), o);
            }
        }
    }
    if (!placed) contents();
    const QString documentXml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        + body.join("") + "\n<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr>\n</w:body></w:document>";
    auto headingStyle = [](int n) {
        return QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"Heading%1\"><w:name w:val=\"heading %1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:uiPriority w:val=\"9\"/><w:qFormat/>\n"
                              "<w:pPr><w:keepNext/><w:pageBreakBefore/><w:spacing w:before=\"1200\"/><w:jc w:val=\"center\"/><w:outlineLvl w:val=\"%2\"/></w:pPr><w:rPr><w:caps/><w:sz w:val=\"28\"/></w:rPr></w:style>")
            .arg(n)
            .arg(n - 1);
    };
    const QString stylesXml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                              "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
                              "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Georgia\" w:hAnsi=\"Georgia\"/><w:sz w:val=\"24\"/></w:rPr></w:rPrDefault>\n"
                              "<w:pPrDefault><w:pPr><w:spacing w:line=\"360\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>\n"
                              "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>\n"
        + headingStyle(1) + "\n" + headingStyle(2) + "\n" + headingStyle(3) + "\n</w:styles>";
    return buildZip({
        {"[Content_Types].xml", R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>)"},
        {"_rels/.rels", R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>)"},
        {"word/_rels/document.xml.rels", R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>)"},
        {"word/document.xml", documentXml.toUtf8()},
        {"word/styles.xml", stylesXml.toUtf8()},
    });
}

// ---------------------------------------------------------------------------
// EPUB

namespace {

struct TocNode {
    TocEntry e;
    QList<TocNode> children;
};

QList<TocNode> tocTree(const QList<TocEntry> &entries)
{
    // build with indices: a stack of paths into the tree
    QList<TocNode> root;
    QList<QList<TocNode> *> stack{&root};
    for (const TocEntry &e : entries) {
        const qsizetype level = std::max<qsizetype>(0, std::min<qsizetype>(e.level, stack.size() - 1));
        stack.resize(level + 1);
        stack[level]->append(TocNode{e, {}});
        stack << &stack[level]->last().children;
    }
    return root;
}

int entryCount(const QList<TocNode> &nodes)
{
    int n = 0;
    for (const auto &x : nodes) n += 1 + entryCount(x.children);
    return n;
}

QString chapterXhtml(const Section &ch, const ExportData &d)
{
    static const QHash<QString, QString> epubTypes{{"copyright", "copyright-page"}, {"dedication", "dedication"},
                                                   {"epigraph", "epigraph"},        {"part", "part"},
                                                   {"opener", "volume"},            {"acknowledgments", "acknowledgments"},
                                                   {"about", "backmatter"}};
    auto lines = [&] {
        QStringList out;
        for (const ExportPara &p : ch.paras) {
            if (p.sceneBreak) { out << "<p class=\"brk\">* * *</p>"; continue; }
            QStringList cls;
            if (ch.kind != "copyright" && isAttr(p)) cls << "attr";
            if (p.poetry) cls << "poetry";
            if (p.align == "center" || p.align == "right") cls << p.align;
            out << "<p" + (cls.isEmpty() ? QString() : " class=\"" + cls.join(' ') + "\"") + ">" + runsXhtml(p.runs) + "</p>";
        }
        return out.join('\n');
    };
    QString inner;
    if (kFrontPages.contains(ch.kind)) {
        inner = "<section epub:type=\"" + epubTypes.value(ch.kind) + "\" class=\"" + ch.kind + "\">\n" + lines() + "\n</section>";
    } else if (ch.kind == "part") {
        inner = "<section epub:type=\"part\" class=\"part\"><h1><span class=\"pl\">" + escXml(ch.heading) + "</span>"
            + (ch.partTitle.isEmpty() ? QString() : "<span class=\"pt\">" + escXml(ch.partTitle) + "</span>") + "</h1>\n"
            + lines() + "\n</section>";
    } else if (ch.kind == "opener") {
        inner = "<section epub:type=\"volume\" class=\"opener\"><h1>" + escXml(ch.heading) + "</h1>\n"
            + (ch.subtitle.isEmpty() ? QString() : "<p class=\"sub\">" + escXml(ch.subtitle) + "</p>")
            + (ch.byline.isEmpty() ? QString() : "<p class=\"byline\">" + escXml(ch.byline) + "</p>") + "\n</section>";
    } else {
        bool first = true;
        QStringList paras;
        for (const ExportPara &p : ch.paras) {
            if (p.sceneBreak) { first = true; paras << "<p class=\"brk\">* * *</p>"; continue; }
            QStringList cls;
            if (p.poetry) cls << "poetry";
            else if (p.flush) cls << "flush";
            else if (first) {
                cls << "first";
                if (typing::opensWithDash(p.text)) cls << "dialogue";
            }
            if (p.align == "center" || p.align == "right") cls << p.align;
            if (!p.poetry) first = false;
            paras << "<p" + (cls.isEmpty() ? QString() : " class=\"" + cls.join(' ') + "\"") + ">" + runsXhtml(p.runs) + "</p>";
        }
        const QString type = epubTypes.value(ch.kind, ch.role.isEmpty() ? "chapter" : ch.role);
        inner = "<section epub:type=\"" + type + "\">" + (ch.heading.isEmpty() ? QString() : "<h1>" + escXml(ch.heading) + "</h1>")
            + (ch.byline.isEmpty() ? QString() : "<p class=\"byline\">" + escXml(ch.byline) + "</p>") + "\n" + paras.join('\n') + "\n</section>";
    }
    const QString title = !ch.heading.isEmpty() ? ch.heading : !ch.label.isEmpty() ? ch.label : d.title;
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n<head><title>"
        + escXml(title) + "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>\n<body>" + inner + "</body></html>";
}

} // namespace

QByteArray buildEpub(const ExportData &d, const CoverImage &cover)
{
    const QString uuid = "urn:uuid:" + (d.uuid.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : d.uuid);
    const QString modified = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ");
    const QString coverName = "cover." + cover.ext;
    QList<const Section *> front, rest;
    for (const Section &ch : d.sections) (ch.front ? front : rest) << &ch;
    const Section *start = !rest.isEmpty() ? rest.first() : (d.sections.isEmpty() ? nullptr : &d.sections.first());
    const QString startHref = start ? QStringLiteral("ch%1.xhtml").arg(start->num) : QStringLiteral("title.xhtml");
    QString chItems;
    for (const Section &ch : d.sections)
        chItems += QStringLiteral("<item id=\"ch%1\" href=\"ch%1.xhtml\" media-type=\"application/xhtml+xml\"/>\n").arg(ch.num);
    auto spineOf = [](const QList<const Section *> &list) {
        QString s;
        for (const Section *ch : list) s += QStringLiteral("<itemref idref=\"ch%1\"/>\n").arg(ch->num);
        return s;
    };
    QList<TocEntry> tocEntries = d.toc;
    if (tocEntries.isEmpty())
        for (const Section &ch : d.sections) tocEntries << TocEntry{ch.heading.isEmpty() ? d.title : ch.heading, ch.num, 0, "chapter"};
    const QList<TocNode> toc = tocTree(tocEntries);
    std::function<QString(const QList<TocNode> &)> navList = [&](const QList<TocNode> &nodes) {
        QStringList out;
        for (const TocNode &n : nodes)
            out << QStringLiteral("<li><a href=\"ch%1.xhtml\">%2</a>%3</li>")
                       .arg(n.e.num)
                       .arg(escXml(n.e.label), n.children.isEmpty() ? QString() : "\n<ol>\n" + navList(n.children) + "\n</ol>");
        return out.join('\n');
    };
    int playOrder = 1;
    std::function<QString(const QList<TocNode> &)> ncxPoints = [&](const QList<TocNode> &nodes) {
        QString out;
        for (const TocNode &n : nodes) {
            playOrder++;
            out += QStringLiteral("\n<navPoint id=\"ch%1\" playOrder=\"%2\"><navLabel><text>%3</text></navLabel><content src=\"ch%1.xhtml\"/>")
                       .arg(n.e.num)
                       .arg(playOrder)
                       .arg(escXml(n.e.label))
                + ncxPoints(n.children) + "</navPoint>";
        }
        return out;
    };
    const QString lang = escXml(d.language.isEmpty() ? I18n::locale() : d.language);
    const QString opf = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\">\n<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
                        "<dc:identifier id=\"bookid\">" + uuid + "</dc:identifier>\n<dc:title>" + escXml(d.title) + "</dc:title>\n<dc:creator>"
        + escXml(d.author) + "</dc:creator>\n<dc:language>" + lang + "</dc:language>\n<meta property=\"dcterms:modified\">" + modified
        + "</meta>\n<meta name=\"cover\" content=\"cover-image\"/>\n</metadata>\n<manifest>\n<item id=\"cover-image\" href=\"" + coverName
        + "\" media-type=\"" + cover.mime + "\" properties=\"cover-image\"/>\n"
          "<item id=\"cover\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
          "<item id=\"titlepage\" href=\"title.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
          "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n"
          "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
          "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n"
        + chItems + "</manifest>\n<spine toc=\"ncx\">\n<itemref idref=\"cover\" linear=\"no\"/>\n<itemref idref=\"titlepage\"/>\n"
        + spineOf(front) + "<itemref idref=\"nav\"" + (entryCount(toc) <= 1 ? " linear=\"no\"" : "") + "/>\n" + spineOf(rest)
        + "</spine>\n<guide>\n<reference type=\"cover\" title=\"" + escXml(t("Cover")) + "\" href=\"cover.xhtml\"/>\n"
          "<reference type=\"toc\" title=\"" + escXml(t("Table of Contents")) + "\" href=\"nav.xhtml\"/>\n"
          "<reference type=\"text\" title=\"" + escXml(t("Beginning")) + "\" href=\"" + startHref + "\"/>\n</guide>\n</package>";
    const QString nav = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n<head><title>"
        + escXml(t("Table of Contents")) + "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>\n<body><nav epub:type=\"toc\" id=\"toc\"><h1>"
        + escXml(t("Contents")) + "</h1>\n<ol>\n<li><a href=\"title.xhtml\">" + escXml(t("Title Page")) + "</a></li>\n" + navList(toc)
        + "\n</ol></nav>\n<nav epub:type=\"landmarks\" hidden=\"\"><ol>\n<li><a epub:type=\"cover\" href=\"cover.xhtml\">" + escXml(t("Cover"))
        + "</a></li>\n<li><a epub:type=\"toc\" href=\"nav.xhtml\">" + escXml(t("Table of Contents"))
        + "</a></li>\n<li><a epub:type=\"bodymatter\" href=\"" + startHref + "\">" + escXml(t("Beginning")) + "</a></li>\n</ol></nav>\n</body></html>";
    const QString ncx = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n<head><meta name=\"dtb:uid\" content=\""
        + uuid + "\"/></head>\n<docTitle><text>" + escXml(d.title) + "</text></docTitle>\n<navMap>\n<navPoint id=\"titlepage\" playOrder=\"1\"><navLabel><text>"
        + escXml(t("Title Page")) + "</text></navLabel><content src=\"title.xhtml\"/></navPoint>" + ncxPoints(toc) + "\n</navMap></ncx>";
    const char *css = R"(body { font-family: serif; line-height: 1.5; margin: 1em; }
h1 { text-align: center; font-weight: normal; letter-spacing: 0.2em; text-transform: uppercase; font-size: 1.2em; margin: 3em 0 2em; }
p { text-indent: 1.2em; margin: 0; }
p.first, p.brk + p, p.byline + p { text-indent: 0; }
p.first.dialogue:not(.center):not(.right) { text-indent: 1.2em; }
p.center { text-align: center; text-indent: 0; }
p.right { text-align: right; text-indent: 0; }
p.brk { text-align: center; text-indent: 0; margin: 2.5em 0; letter-spacing: 0.5em; }
p.poetry { text-indent: 0; margin: 0 2em; }
p.flush { text-indent: 0 !important; }
p:not(.poetry) + p.poetry, h1 + p.poetry { margin-top: 0.9em; }
p.poetry + p:not(.poetry) { margin-top: 0.9em; }
p.byline { text-align: center; text-indent: 0; letter-spacing: 0.2em; text-transform: uppercase; font-size: 0.8em; margin: -1em 0 2em; }
.copyright { margin-top: 40%; font-size: 0.8em; line-height: 1.5; }
.copyright p { text-indent: 0; margin: 0 0 0.9em; }
.dedication, .epigraph, .part, .opener { text-align: center; margin-top: 30%; }
.epigraph { margin-left: 2em; margin-right: 2em; }
.dedication p, .epigraph p, .part p { text-indent: 0; margin: 0 0 0.5em; font-style: italic; }
.dedication em, .epigraph em, .part p em { font-style: normal; }
.dedication p.poetry, .epigraph p.poetry, .part p.poetry { margin: 0 0 0.2em; }
p.attr { font-style: normal; font-size: 0.85em; letter-spacing: 0.05em; margin-top: 1em; }
.part h1 { margin: 0 0 2em; }
.part .pl { display: block; }
.part .pt { display: block; margin-top: 0.8em; font-size: 1.6em; letter-spacing: 0; text-transform: none; }
.opener h1 { margin: 0; font-size: 1.8em; letter-spacing: 0.02em; text-transform: none; }
.opener .sub { text-indent: 0; margin-top: 0.6em; font-style: italic; }
.opener .byline { margin: 3em 0 0; }
nav#toc ol { list-style: none; padding-left: 0; }
nav#toc ol ol { padding-left: 1.5em; }
.titlepage { text-align: center; margin-top: 30%; }
.titlepage h2 { font-size: 2em; margin: 0; }
.titlepage .sub { font-style: italic; }
.titlepage .auth { margin-top: 4em; letter-spacing: 0.3em; text-transform: uppercase; }
.coverimg { text-align: center; margin: 0; padding: 0; }
.coverimg img { max-width: 100%; max-height: 100%; })";
    const QString coverXhtml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head><title>"
        + escXml(t("Cover")) + "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>\n<body><div class=\"coverimg\"><img src=\""
        + coverName + "\" alt=\"" + escXml(d.title) + "\"/></div></body></html>";
    const QString titleXhtml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head><title>"
        + escXml(d.title) + "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>\n<body><div class=\"titlepage\"><h2>"
        + escXml(d.title) + "</h2>\n" + (d.subtitle.isEmpty() ? QString() : "<p class=\"sub\">" + escXml(d.subtitle) + "</p>")
        + "\n<p class=\"auth\">" + escXml(d.author) + "</p></div></body></html>";
    QList<ZipEntry> entries{
        {"mimetype", "application/epub+zip", true},
        {"META-INF/container.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>)"},
        {"OEBPS/content.opf", opf.toUtf8()},
        {"OEBPS/nav.xhtml", nav.toUtf8()},
        {"OEBPS/toc.ncx", ncx.toUtf8()},
        {"OEBPS/style.css", css},
        {"OEBPS/cover.xhtml", coverXhtml.toUtf8()},
        {"OEBPS/title.xhtml", titleXhtml.toUtf8()},
        {"OEBPS/" + coverName, cover.bytes},
    };
    for (const Section &ch : d.sections)
        entries << ZipEntry{QStringLiteral("OEBPS/ch%1.xhtml").arg(ch.num), chapterXhtml(ch, d).toUtf8()};
    return buildZip(entries);
}

} // namespace neosea
