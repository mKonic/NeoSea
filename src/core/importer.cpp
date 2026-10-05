#include "core/importer.h"

#include "core/htmldom.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <zip.h>

namespace neosea {

namespace {

QRegularExpression rx(const QString &p, bool caseless = false)
{
    auto o = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::DotMatchesEverythingOption;
    if (caseless) o |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(p, o);
}

QString decodeXml(const QString &s)
{
    QString out = s;
    out.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'").replace("&amp;", "&");
    return out;
}

// Is a formatting tag present, and is it on? 1 on, 0 present but switched
// off (Word writes <w:i w:val="0"/> to cancel a style's italics), -1 silent.
int formatOn(const QString &rpr, const QString &tag)
{
    auto hit = rx("<" + QRegularExpression::escape(tag) + "(?:\\s[^>]*)?/?>").match(rpr);
    if (!hit.hasMatch()) return -1;
    auto val = rx("w:val=\"([^\"]*)\"").match(hit.captured(0));
    if (!val.hasMatch()) return 1;
    return rx("^(true|1|on)$", true).match(val.captured(1)).hasMatch() ? 1 : 0;
}

struct StyleFmt {
    bool bold = false, italic = false;
};

// Italics and bold don't always sit on the run: a manuscript may carry them in
// a character or paragraph style. basedOn is followed, so a style built on an
// italic one stays italic.
QHash<QString, StyleFmt> styleFormats(const QString &stylesXml)
{
    struct Raw {
        QString basedOn;
        int bold, italic;
    };
    QHash<QString, Raw> raw;
    auto it = rx("<w:style\\s[^>]*w:styleId=\"([^\"]+)\"[^>]*>(.*?)</w:style>").globalMatch(stylesXml);
    while (it.hasNext()) {
        auto m = it.next();
        const QString body = m.captured(2);
        const QString rpr = rx("<w:rPr>.*?</w:rPr>").match(body).captured(0);
        raw.insert(m.captured(1), {rx("<w:basedOn\\s+w:val=\"([^\"]+)\"").match(body).captured(1),
                                   formatOn(rpr, "w:b"), formatOn(rpr, "w:i")});
    }
    QHash<QString, StyleFmt> out;
    std::function<StyleFmt(const QString &, int)> resolve = [&](const QString &id, int depth) -> StyleFmt {
        if (out.contains(id)) return out.value(id);
        if (!raw.contains(id) || depth > 8) return {};
        const Raw st = raw.value(id);
        const StyleFmt base = st.basedOn.isEmpty() ? StyleFmt{} : resolve(st.basedOn, depth + 1);
        StyleFmt f{st.bold < 0 ? base.bold : st.bold == 1, st.italic < 0 ? base.italic : st.italic == 1};
        out.insert(id, f);
        return f;
    };
    for (const QString &id : raw.keys()) resolve(id, 0);
    return out;
}

DocxPara docxParagraph(const QString &p, const QHash<QString, StyleFmt> &styles)
{
    DocxPara out;
    out.pageBreak = rx("<w:br [^>]*w:type=\"page\"").match(p).hasMatch() || p.contains("<w:pageBreakBefore");
    // any heading style starts a chapter; the style id is "Heading*" whatever the locale
    const QString pStyle = rx("<w:pStyle\\s+w:val=\"([^\"]*)\"").match(p).captured(1);
    out.heading = rx("^heading\\s*\\d*$", true).match(pStyle).hasMatch();
    // Google Docs exports each tab under a "Title" line, and a title page uses it too
    out.title = rx("^title$", true).match(pStyle).hasMatch();
    const StyleFmt pBase = styles.value(pStyle);
    struct R {
        QString text;
        bool b, i;
    };
    QList<R> merged;
    auto runs = rx("<w:r[ >].*?</w:r>").globalMatch(p);
    while (runs.hasNext()) {
        const QString r = runs.next().captured(0);
        const QString rpr = rx("<w:rPr>.*?</w:rPr>").match(r).captured(0);
        QString text;
        auto ts = rx("<w:t(?:\\s[^>]*)?>(.*?)</w:t>").globalMatch(r);
        while (ts.hasNext()) text += decodeXml(ts.next().captured(1));
        const QString rStyle = rx("<w:rStyle\\s+w:val=\"([^\"]*)\"").match(rpr).captured(1);
        const StyleFmt rBase = (!rStyle.isEmpty() && styles.contains(rStyle)) ? styles.value(rStyle) : pBase;
        const int b = formatOn(rpr, "w:b"), i = formatOn(rpr, "w:i");
        R run{text, b < 0 ? rBase.bold : b == 1, i < 0 ? rBase.italic : i == 1};
        // **one** + **two** is one **onetwo**, not **one****two**
        if (!merged.isEmpty() && merged.last().b == run.b && merged.last().i == run.i) merged.last().text += run.text;
        else merged << run;
    }
    for (const R &r : merged) {
        QString t = r.text;
        if (r.b) t = "**" + t + "**";
        if (r.i) t = "*" + t + "*";
        out.text += t;
    }
    out.text = out.text.trimmed();
    return out;
}

// Headings that are only NEO's own numbering, in the languages it speaks
const QRegularExpression &chapterWords()
{
    static const QRegularExpression r = rx(
        "^(chapter|prologue|epilogue|part|chapitre|épilogue|partie|capítulo|capitulo|prólogo|prologo|epílogo|"
        "epilogo|parte|capitolo|kapitel|prolog|epilog|teil|hoofdstuk|proloog|epiloog|deel|rozdział|rozdzial|"
        "część|czesc|capitol(?=\\s+\\d)|capitolul|partea|глава|пролог|эпилог|часть|κεφάλαιο|κεφαλαιο|πρόλογος|"
        "προλογος|επίλογος|επιλογος|μέρος|μερος|ραψωδία|ραψωδια)(?![\\p{L}\\d])",
        true);
    return r;
}

} // namespace

QList<DocxPara> docxParagraphs(const QString &documentXml, const QString &stylesXml)
{
    const auto styles = styleFormats(stylesXml);
    QList<DocxPara> out;
    auto it = rx("<w:p[ >].*?</w:p>").globalMatch(documentXml);
    while (it.hasNext()) out << docxParagraph(it.next().captured(0), styles);
    return out;
}

QList<DocxPara> textParagraphs(const QString &text)
{
    QList<DocxPara> out;
    for (QString b : text.split(rx("\\r?\\n\\s*\\r?\\n"))) {
        b.replace(rx("\\s*\\r?\\n\\s*"), " ");
        b = b.trimmed();
        if (!b.isEmpty()) out << DocxPara{b};
    }
    return out;
}

ImportResult chapterize(const QString &name, const QList<DocxPara> &paras)
{
    static const QRegularExpression spelled = rx(
        "^(one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|thirteen|fourteen|fifteen|sixteen|"
        "seventeen|eighteen|nineteen|twenty)\\.?$",
        true);
    auto isNumeralish = [](const QString &t) {
        return rx("^\\d{1,3}\\.?$").match(t).hasMatch() || rx("^[IVXLC]{1,7}\\.?$").match(t).hasMatch()
            || spelled.match(t).hasMatch();
    };
    // bare numbers mark chapters only when there's a ladder of them
    int numerals = 0;
    for (const auto &p : paras)
        if (!p.text.isEmpty() && isNumeralish(p.text.trimmed())) numerals++;
    const bool numeralMode = numerals >= 2;
    auto isMdHeading = [](const QString &t) { return rx("^#{1,6}\\s+\\S").match(t).hasMatch(); };
    auto readsAsSentence = [](const QString &t) {
        return rx("[.!?…][”’\"'»)]*$").match(t).hasMatch() && t.trimmed().split(rx("\\s+")).size() > 2;
    };
    auto isNumberedHeading = [&](const QString &t) {
        return (chapterWords().match(t).hasMatch() && t.size() < 60 && !readsAsSentence(t))
            || (numeralMode && isNumeralish(t));
    };
    auto isHeading = [&](const QString &t) { return !t.isEmpty() && (isMdHeading(t) || isNumberedHeading(t)); };
    auto titleOf = [&](QString t) {
        if (isMdHeading(t)) t = t.remove(rx("^#{1,6}\\s*")).trimmed();
        t.replace(rx("\\*\\*([^*]+)\\*\\*"), "\\1").replace(rx("\\*([^*]+)\\*"), "\\1").replace(rx("_([^_]+)_"), "\\1");
        return isNumberedHeading(t) ? QString() : t;
    };
    auto isBreak = [](const QString &t) { return rx("^\\s*([*#•~⁂—–-]\\s*){1,7}$").match(t).hasMatch(); };
    static const QRegularExpression prologueWords = rx("^(prologue|prólogo|prologo|prolog|proloog)(?![\\p{L}\\d])", true);
    static const QRegularExpression epilogueWords = rx("^(epilogue|épilogue|epílogo|epilogo|epilog|epiloog)(?![\\p{L}\\d])", true);

    QString styledTitle;
    bool haveStyledTitle = false;
    auto run = [&](bool usePageBreaks) {
        QList<ImportChapter> chapters;
        ImportChapter cur;
        bool seenProse = false, lastWasHeading = false;
        haveStyledTitle = false;
        styledTitle.clear();
        auto close = [&] {
            if (!cur.paras.isEmpty()) chapters << cur;
            cur = ImportChapter{};
        };
        for (const DocxPara &p : paras) {
            const bool brk = usePageBreaks && p.pageBreak;
            if (p.text.isEmpty() && !brk && !p.heading && !p.title) continue;
            // a Title line before any prose is the book's title, not a chapter's
            if (p.title && !seenProse && !haveStyledTitle && !p.text.isEmpty()) {
                styledTitle = titleOf(p.text);
                haveStyledTitle = true;
                continue;
            }
            const bool isH = p.heading || p.title || isHeading(p.text);
            if (brk || isH) {
                // a heading right after another refines the chapter's title
                if (isH && lastWasHeading && cur.paras.isEmpty() && !brk) {
                    const QString tt = titleOf(p.text);
                    if (!tt.isEmpty()) cur.title = cur.title.isEmpty() ? tt : cur.title + " — " + tt;
                    continue;
                }
                close();
            }
            if (isH) {
                QString h = p.text;
                h = h.remove(rx("^#{1,6}\\s*")).trimmed();
                cur.role = prologueWords.match(h).hasMatch() ? "prologue" : epilogueWords.match(h).hasMatch() ? "epilogue" : QString();
                cur.title = titleOf(p.text);
                lastWasHeading = true;
                continue;
            }
            lastWasHeading = false;
            if (isBreak(p.text)) { cur.paras << ImportPara{{}, true}; continue; }
            if (!p.text.isEmpty()) {
                cur.paras << ImportPara{p.text, false};
                seenProse = true;
            }
        }
        close();
        return chapters;
    };
    auto countAll = [](const QList<ImportChapter> &list) {
        int n = 0;
        for (const auto &ch : list)
            for (const auto &p : ch.paras)
                if (!p.text.isEmpty()) n += p.text.trimmed().split(rx("\\s+")).size();
        return n;
    };

    ImportResult r;
    r.name = name;
    // first pass trusts page breaks; confetti (lots of tiny chapters) means
    // the word processor sprinkled them everywhere, so headings only
    QList<ImportChapter> chapters = run(true);
    if (chapters.size() > 6 && countAll(chapters) / chapters.size() < 250) chapters = run(false);
    if (chapters.isEmpty()) chapters << ImportChapter{{}, {ImportPara{}}, {}};

    // front matter: a short title line and a "by Author" line go to the title page
    QString title = haveStyledTitle ? styledTitle : QString();
    QString author;
    auto norm = [](const QString &s) {
        QString out;
        for (QChar c : s.normalized(QString::NormalizationForm_C).toLower())
            if (c.isLetterOrNumber()) out += c;
        return out;
    };
    auto bylineOf = [](const QString &s) -> QString {
        auto en = rx("^by\\s+(.{2,60})$", true).match(s);
        if (en.hasMatch()) return en.captured(1);
        auto m = rx("^(?:par|por|von|di|door|autor:?|автор:?)\\s+(.{2,60})$", true).match(s);
        if (!m.hasMatch()) m = rx("^de\\s+(.{2,60})$").match(s);
        if (!m.hasMatch() || rx("[.!?,;…]").match(m.captured(1)).hasMatch()) return {};
        const QStringList words = m.captured(1).trimmed().split(rx("\\s+"));
        static const QRegularExpression particle = rx("^(de|da|di|do|dos|das|du|des|del|della|la|le|van|von|der|den|ten|ter|y|e)$");
        if (words.size() > 5) return {};
        for (const QString &w : words)
            if (!(w.size() && w[0].isUpper()) && !particle.match(w).hasMatch()) return {};
        return m.captured(1);
    };
    ImportChapter &first = chapters.first();
    if (!first.paras.isEmpty()) {
        const QString t0 = first.paras[0].text.trimmed();
        const QString t1 = first.paras.size() > 1 ? first.paras[1].text.trimmed() : QString();
        const bool titleish = !t0.isEmpty() && t0.size() < 90 && !rx("[.!?]$").match(t0).hasMatch()
            && ((norm(t0).size() > 3 && norm(name).contains(norm(t0))) || !bylineOf(t1).isEmpty()
                || (t0 == t0.toUpper() && rx("\\p{Lu}.*\\p{Lu}").match(t0).hasMatch() && t0.size() < 60));
        if (titleish) {
            title = t0;
            first.paras.removeFirst();
        }
        const QString bl = first.paras.isEmpty() ? QString() : bylineOf(first.paras[0].text.trimmed());
        if (!bl.isEmpty()) {
            author = bl.trimmed();
            first.paras.removeFirst();
        }
        if (first.paras.isEmpty()) chapters.removeFirst();
        if (chapters.isEmpty()) chapters << ImportChapter{{}, {ImportPara{}}, {}};
    }
    // a role only holds in its place: the prologue first, the epilogue last
    for (qsizetype i = 0; i < chapters.size(); ++i) {
        auto &ch = chapters[i];
        if ((ch.role == "prologue" && i != 0) || (ch.role == "epilogue" && i != chapters.size() - 1) || chapters.size() < 2)
            ch.role.clear();
    }
    r.title = title;
    r.author = author;
    r.chapters = chapters;
    return r;
}

QByteArray zipEntry(const QString &zipPath, const QString &entry, bool *ok)
{
    if (ok) *ok = false;
    int err = 0;
    zip_t *z = zip_open(QFile::encodeName(zipPath).constData(), ZIP_RDONLY, &err);
    if (!z) return {};
    QByteArray out;
    zip_stat_t st;
    if (zip_stat(z, entry.toUtf8().constData(), 0, &st) == 0 && (st.valid & ZIP_STAT_SIZE)) {
        if (zip_file_t *f = zip_fopen(z, entry.toUtf8().constData(), 0)) {
            out.resize(qsizetype(st.size));
            const zip_int64_t n = zip_fread(f, out.data(), st.size);
            zip_fclose(f);
            if (n == zip_int64_t(st.size) && ok) *ok = true;
        }
    }
    zip_close(z);
    return out;
}

ImportResult importFile(const QString &path)
{
    const QFileInfo fi(path);
    const QString name = fi.completeBaseName();
    const QString ext = fi.suffix().toLower();
    ImportResult bad;
    bad.name = fi.fileName();
    QFile f(path);
    if (ext == "fountain" || ext == "fdx") {
        if (!f.open(QIODevice::ReadOnly)) { bad.error = f.errorString(); return bad; }
        ImportResult r;
        r.name = name;
        r.script = ext;
        r.source = QString::fromUtf8(f.readAll());
        if (r.source.startsWith(QChar(0xFEFF))) r.source.remove(0, 1);
        return r;
    }
    QList<DocxPara> paras;
    if (ext == "docx") {
        bool ok = false;
        const QByteArray doc = zipEntry(path, "word/document.xml", &ok);
        if (!ok) { bad.error = "Not a valid .docx: " + path; return bad; }
        const QByteArray styles = zipEntry(path, "word/styles.xml");
        paras = docxParagraphs(QString::fromUtf8(doc), QString::fromUtf8(styles));
    } else if (ext == "txt" || ext == "md") {
        if (!f.open(QIODevice::ReadOnly)) { bad.error = f.errorString(); return bad; }
        paras = textParagraphs(QString::fromUtf8(f.readAll()));
    } else {
        bad.error = "not a manuscript";
        return bad;
    }
    return chapterize(name, paras);
}

QString importedChapterHtml(const ImportChapter &ch, const typing::DashStyle &dashes)
{
    QString out;
    for (const ImportPara &p : ch.paras) {
        if (p.scene) { out += "<p class=\"scene-break\">***</p>"; continue; }
        QString text = html::escHtml(typing::dialogueDashes(p.text, dashes));
        text.replace(rx("\\*\\*([^*]+)\\*\\*"), "<b>\\1</b>")
            .replace(rx("\\*([^*]+)\\*"), "<i>\\1</i>")
            .replace(rx("_([^_]+)_"), "<i>\\1</i>");
        out += "<p>" + text + "</p>";
    }
    return out.isEmpty() ? QStringLiteral("<p><br></p>") : out;
}

} // namespace neosea
