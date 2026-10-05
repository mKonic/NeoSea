// The book as a PDF: set in pages the way NEO's print stylesheet sets it. The
// cover on a page of its own, the title page, then each section from a fresh
// page; page numbers at the foot of the story's pages only; a contents page
// whose numbers come from a first layout of the book (each number has its
// own right-aligned tab, so writing them in moves nothing).

#include "core/exporter.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "core/typing.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QImage>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

namespace neosea {

namespace {

constexpr qreal kDpi = 72; // layout in points

struct Built {
    std::unique_ptr<QTextDocument> doc;
    QHash<int, int> sectionBlock;     // section num -> block number where it starts
    QList<int> frontBlocks;           // blocks that start a page carrying no number
    int contentsBlock = -1;
    QHash<int, int> tocNumberBlock;   // section num -> block of its contents line
};

QTextCharFormat charFmt(const QString &family, qreal pt, bool italic = false, bool bold = false)
{
    QTextCharFormat f;
    f.setFontFamilies({family, "Gelasio", "serif"});
    f.setFontPointSize(pt);
    f.setFontItalic(italic);
    f.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    f.setForeground(QColor("#1c1c1c"));
    return f;
}

void insertRuns(QTextCursor &c, const QList<Run> &runs, const QTextCharFormat &base, bool flipItalic = false)
{
    for (const Run &r : runs) {
        QTextCharFormat f = base;
        f.setFontItalic(flipItalic ? !r.i : (base.fontItalic() || r.i));
        if (r.b) f.setFontWeight(QFont::Bold);
        if (r.u) f.setFontUnderline(true);
        if (r.s) f.setFontStrikeOut(true);
        c.insertText(r.text, f);
    }
}

struct Ctx {
    QTextCursor c;
    QSizeF page; // in points, margins included
    bool firstBlock = true;
    QString body;
    QString cap;
};

QTextBlockFormat blockFmt(Qt::Alignment a = Qt::AlignLeft | Qt::AlignAbsolute)
{
    QTextBlockFormat b;
    b.setAlignment(a);
    b.setLineHeight(170, QTextBlockFormat::ProportionalHeight);
    return b;
}

// a new block; the document's first block is reused
void block(Ctx &x, QTextBlockFormat bf, const QTextCharFormat &cf = {})
{
    if (x.firstBlock) {
        x.c.setBlockFormat(bf);
        x.c.setBlockCharFormat(cf);
        x.firstBlock = false;
    } else {
        x.c.insertBlock(bf, cf);
    }
}

int currentBlock(Ctx &x) { return x.c.block().blockNumber(); }

void newPage(QTextBlockFormat &bf) { bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore); }

Built build(const ExportData &d, const HtmlOptions &o, const QSizeF &page, const QImage &cover,
            const QHash<int, int> &pageOf)
{
    Built out;
    out.doc = std::make_unique<QTextDocument>();
    QTextDocument &doc = *out.doc;
    doc.setPageSize(page);
    doc.setDocumentMargin(kDpi); // one inch all round
    doc.setDefaultFont(QFont(bodyFontFamily(o.bodyFont), 13));
    Ctx x{QTextCursor(&doc), page, true, bodyFontFamily(o.bodyFont), o.dropCapFont};
    const qreal usable = page.height() - 2 * kDpi;
    const qreal em = 13;
    const QTextCharFormat body = charFmt(x.body, 13);

    if (!cover.isNull()) {
        QTextBlockFormat bf = blockFmt(Qt::AlignHCenter);
        block(x, bf);
        out.frontBlocks << currentBlock(x);
        const QSizeF room(page.width() - 2 * kDpi, usable - 4);
        const QSizeF size = QSizeF(cover.size()).scaled(room, Qt::KeepAspectRatio);
        doc.addResource(QTextDocument::ImageResource, QUrl("neosea://cover"), cover);
        QTextImageFormat img;
        img.setName("neosea://cover");
        img.setWidth(size.width());
        img.setHeight(size.height());
        x.c.insertImage(img);
    }
    // the title page
    {
        QTextBlockFormat bf = blockFmt(Qt::AlignHCenter);
        bf.setTopMargin(usable * 0.3);
        if (!cover.isNull()) newPage(bf);
        block(x, bf);
        out.frontBlocks << currentBlock(x);
        x.c.insertText(d.title, charFmt(x.body, 30, false, true));
        if (!d.subtitle.isEmpty()) {
            QTextBlockFormat sb = blockFmt(Qt::AlignHCenter);
            sb.setTopMargin(em);
            QTextCharFormat sf = charFmt(x.body, 13, true);
            sf.setForeground(QColor("#555"));
            x.c.insertBlock(sb);
            x.c.insertText(d.subtitle, sf);
        }
        QTextBlockFormat ab = blockFmt(Qt::AlignHCenter);
        ab.setTopMargin(40);
        QTextCharFormat af = charFmt(x.body, 11);
        af.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        af.setFontLetterSpacing(3);
        af.setFontCapitalization(QFont::AllUppercase);
        x.c.insertBlock(ab);
        x.c.insertText(d.author, af);
    }

    auto headingFmt = [&] {
        QTextCharFormat f = charFmt(x.body, 17);
        f.setFontCapitalization(QFont::SmallCaps);
        f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        f.setFontLetterSpacing(4);
        f.setForeground(QColor("#555"));
        return f;
    };
    auto heading = [&](const QString &text, bool pageBreak, qreal top) {
        QTextBlockFormat hb = blockFmt(Qt::AlignHCenter);
        if (pageBreak) newPage(hb);
        hb.setTopMargin(top);
        hb.setBottomMargin(36);
        hb.setHeadingLevel(1);
        block(x, hb);
        x.c.insertText(text, headingFmt());
    };
    auto brk = [&](qreal margin) {
        QTextBlockFormat bb = blockFmt(Qt::AlignHCenter);
        bb.setTopMargin(margin);
        bb.setBottomMargin(margin);
        QTextCharFormat f = charFmt(x.body, 13);
        f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        f.setFontLetterSpacing(8);
        f.setForeground(QColor("#888"));
        block(x, bb);
        x.c.insertText("***", f);
    };
    auto alignOf = [](const QString &a, Qt::Alignment def) -> Qt::Alignment {
        if (a == "center") return Qt::AlignHCenter;
        if (a == "right") return Qt::AlignRight | Qt::AlignAbsolute;
        if (a == "justify") return Qt::AlignJustify;
        return def;
    };
    // a page's lines, each set on its own, centered; italic pages flip their italics
    auto pageLines = [&](const QList<ExportPara> &paras, bool italic, bool attrs, qreal firstTop, bool pageBreak, int num) {
        bool first = true;
        for (const ExportPara &p : paras) {
            QTextBlockFormat bf = blockFmt(alignOf(p.align, Qt::AlignHCenter));
            bf.setBottomMargin(p.poetry ? 0.2 * em : 0.5 * em);
            if (first) {
                bf.setTopMargin(firstTop);
                if (pageBreak) newPage(bf);
            }
            if (p.sceneBreak) {
                if (first) { block(x, bf); x.c.insertText("***", body); }
                else brk(1.2 * em);
            } else {
                const bool attr = attrs && typing::isAttribution(p.text);
                if (attr) bf.setTopMargin(std::max(bf.topMargin(), 1.2 * em));
                block(x, bf);
                QTextCharFormat base = charFmt(x.body, attr ? 10 : 13, italic && !attr);
                insertRuns(x.c, p.runs, base, italic && !attr);
            }
            if (first) {
                out.sectionBlock.insert(num, currentBlock(x));
                out.frontBlocks << currentBlock(x);
            }
            first = false;
        }
    };

    // the contents page
    auto contents = [&] {
        QTextBlockFormat hb = blockFmt(Qt::AlignHCenter);
        newPage(hb);
        hb.setTopMargin(54);
        hb.setBottomMargin(36);
        block(x, hb);
        out.contentsBlock = currentBlock(x);
        out.frontBlocks << currentBlock(x);
        x.c.insertText(t("Contents"), headingFmt());
        const qreal width = page.width() - 2 * kDpi;
        bool prevPage = false;
        for (const TocEntry &e : d.toc) {
            if (!d.contentsChapters && e.type == "chapter") continue;
            QTextBlockFormat lb = blockFmt();
            lb.setLeftMargin(1.5 * em + 1.6 * em * e.level);
            lb.setRightMargin(1.5 * em);
            lb.setTopMargin(e.type == "part" ? 1.1 * em : (e.type == "page" && !prevPage ? 1.2 * em : 0.3 * em));
            lb.setTabPositions({QTextOption::Tab(width - lb.leftMargin() - lb.rightMargin(), QTextOption::RightTab)});
            block(x, lb);
            QTextCharFormat f = body;
            if (e.type == "part") {
                f.setFontPointSize(15);
                f.setFontCapitalization(QFont::SmallCaps);
                f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                f.setFontLetterSpacing(2);
            }
            const int pg = pageOf.value(e.num, 0);
            x.c.insertText(e.label + '\t' + (pg ? QString::number(pg) : QString()), f);
            out.tocNumberBlock.insert(e.num, currentBlock(x));
            prevPage = e.type == "page";
        }
    };

    bool placed = !(d.contents && !d.toc.isEmpty());
    for (const Section &ch : d.sections) {
        if (!placed && !ch.front) { contents(); placed = true; }
        if (ch.kind == "copyright") {
            // set low on its page, small
            bool first = true;
            for (const ExportPara &p : ch.paras) {
                QTextBlockFormat bf = blockFmt();
                bf.setBottomMargin(0.9 * 9);
                if (first) {
                    newPage(bf);
                    bf.setTopMargin(usable * 0.55);
                }
                block(x, bf);
                if (first) {
                    out.sectionBlock.insert(ch.num, currentBlock(x));
                    out.frontBlocks << currentBlock(x);
                }
                QTextCharFormat f = charFmt(x.body, 9);
                f.setForeground(QColor("#333"));
                if (!p.sceneBreak) insertRuns(x.c, p.runs, f);
                first = false;
            }
            continue;
        }
        if (ch.kind == "dedication" || ch.kind == "epigraph") {
            pageLines(ch.paras, true, true, usable * (ch.kind == "dedication" ? 0.26 : 0.24), true, ch.num);
            continue;
        }
        if (ch.kind == "part") {
            QTextBlockFormat hb = blockFmt(Qt::AlignHCenter);
            newPage(hb);
            hb.setTopMargin(usable * 0.28);
            hb.setHeadingLevel(1);
            block(x, hb);
            out.sectionBlock.insert(ch.num, currentBlock(x));
            out.frontBlocks << currentBlock(x);
            QTextCharFormat pl = headingFmt();
            pl.setFontPointSize(15.5);
            pl.setFontLetterSpacing(5);
            x.c.insertText(ch.heading, pl);
            if (!ch.partTitle.isEmpty()) {
                QTextBlockFormat tb = blockFmt(Qt::AlignHCenter);
                tb.setTopMargin(14);
                tb.setBottomMargin(2.4 * em);
                x.c.insertBlock(tb);
                x.c.insertText(ch.partTitle, charFmt(x.body, 24));
            }
            pageLines(ch.paras, true, true, ch.partTitle.isEmpty() ? 2.4 * em : 0, false, -1);
            continue;
        }
        if (ch.kind == "opener") {
            QTextBlockFormat hb = blockFmt(Qt::AlignHCenter);
            newPage(hb);
            hb.setTopMargin(usable * 0.28);
            hb.setHeadingLevel(1);
            block(x, hb);
            out.sectionBlock.insert(ch.num, currentBlock(x));
            out.frontBlocks << currentBlock(x);
            x.c.insertText(ch.heading, charFmt(x.body, 26));
            if (!ch.subtitle.isEmpty()) {
                QTextBlockFormat sb = blockFmt(Qt::AlignHCenter);
                sb.setTopMargin(12);
                x.c.insertBlock(sb);
                QTextCharFormat sf = charFmt(x.body, 13, true);
                sf.setForeground(QColor("#555"));
                x.c.insertText(ch.subtitle, sf);
            }
            if (!ch.byline.isEmpty()) {
                QTextBlockFormat bb = blockFmt(Qt::AlignHCenter);
                bb.setTopMargin(40);
                x.c.insertBlock(bb);
                QTextCharFormat bf = charFmt(x.body, 10);
                bf.setFontCapitalization(QFont::AllUppercase);
                bf.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                bf.setFontLetterSpacing(3);
                x.c.insertText(ch.byline, bf);
            }
            continue;
        }
        // a chapter, a prologue, an epilogue, the back pages
        const bool back = ch.kind == "acknowledgments" || ch.kind == "about";
        if (!ch.heading.isEmpty()) {
            heading(ch.heading, true, 54);
        } else {
            QTextBlockFormat sb = blockFmt();
            newPage(sb);
            block(x, sb);
        }
        out.sectionBlock.insert(ch.num, currentBlock(x));
        if (!ch.byline.isEmpty()) {
            QTextBlockFormat bb = blockFmt(Qt::AlignHCenter);
            bb.setBottomMargin(40);
            block(x, bb);
            QTextCharFormat bf = charFmt(x.body, 10);
            bf.setFontCapitalization(QFont::AllUppercase);
            bf.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            bf.setFontLetterSpacing(3);
            bf.setForeground(QColor("#555"));
            x.c.insertText(ch.byline, bf);
        }
        bool first = !back, afterBreak = false, prevPoetry = false, startOfSection = true;
        bool blankStart = ch.heading.isEmpty(); // the empty block made for the page break takes the first paragraph
        for (const ExportPara &p : ch.paras) {
            if (p.sceneBreak) {
                brk(2.5 * em);
                afterBreak = !back;
                prevPoetry = false;
                startOfSection = false;
                blankStart = false;
                continue;
            }
            QTextBlockFormat bf = blockFmt(alignOf(p.align, Qt::AlignJustify));
            if (p.poetry) {
                bf.setLeftMargin(2.5 * em);
                bf.setRightMargin(2.5 * em);
                if (!prevPoetry && !startOfSection) bf.setTopMargin(0.9 * em);
            } else {
                if (prevPoetry) bf.setTopMargin(0.9 * em);
                const bool noIndent = p.flush || ((first || afterBreak || startOfSection) && !typing::opensWithDash(p.text))
                    || p.align == "center" || p.align == "right";
                if (!noIndent) bf.setTextIndent(2 * em);
            }
            if (blankStart) {
                x.c.setBlockFormat(bf);
                blankStart = false;
            } else {
                block(x, bf);
            }
            QList<Run> runs = p.runs;
            // the drop cap: a raised initial in its own face
            if (first && !p.poetry && !o.dropCapFont.isEmpty() && !typing::opensWithDash(p.text) && !runs.isEmpty()
                && !runs.first().text.isEmpty()) {
                QTextCharFormat cf = charFmt(o.dropCapFont, 13 * 1.8);
                x.c.insertText(runs.first().text.left(1), cf);
                runs.first().text.remove(0, 1);
            }
            insertRuns(x.c, runs, body);
            if (!p.poetry) first = false;
            afterBreak = false;
            prevPoetry = p.poetry;
            startOfSection = false;
        }
    }
    if (!placed) contents();
    return out;
}

int pageOfBlock(QTextDocument &doc, int blockNumber, qreal pageHeight)
{
    const QTextBlock b = doc.findBlockByNumber(blockNumber);
    if (!b.isValid()) return 0;
    const QRectF r = doc.documentLayout()->blockBoundingRect(b);
    // a page break puts the block at the top of the next page; its top margin
    // may push the rect's top a little, so the line's top decides
    qreal top = r.top();
    if (b.layout() && b.layout()->lineCount() > 0) top = r.top() + b.layout()->lineAt(0).y();
    return int(top / pageHeight);
}

} // namespace

QByteArray buildPdf(const ExportData &d, const HtmlOptions &o, QString *error)
{
    registerBundledFonts();
    // Letter is a North American habit; most of the world prints A4
    const QString country = QLocale::territoryToCode(QLocale::system().territory());
    const bool letter = QStringList{"US", "CA", "MX", "PH"}.contains(country);
    const QPageSize ps(letter ? QPageSize::Letter : QPageSize::A4);
    const QSizeF page = ps.size(QPageSize::Point);

    QImage cover;
    if (!o.coverBytes.isEmpty()) cover.loadFromData(o.coverBytes);

    // first pass: where everything lands; second: the contents' numbers in
    Built first = build(d, o, page, cover, {});
    QHash<int, int> pageOf;
    if (first.contentsBlock >= 0) {
        for (auto it = first.sectionBlock.begin(); it != first.sectionBlock.end(); ++it)
            pageOf.insert(it.key(), pageOfBlock(*first.doc, it.value(), page.height()) + 1);
    }
    Built b = first.contentsBlock >= 0 ? build(d, o, page, cover, pageOf) : std::move(first);
    QTextDocument &doc = *b.doc;

    // pages that carry no number: the cover, the title page, the pages that
    // aren't chapters, the contents (and whatever runs on from them)
    const int pages = doc.pageCount();
    QList<bool> numbered(pages, true);
    for (int blockNo : b.frontBlocks) {
        const int p = pageOfBlock(doc, blockNo, page.height());
        if (p >= 0 && p < pages) numbered[p] = false;
    }
    // a front page that runs over keeps the next page unnumbered too, until a
    // story section starts
    QSet<int> storyStarts;
    for (auto it = b.sectionBlock.begin(); it != b.sectionBlock.end(); ++it) {
        bool front = false;
        for (const Section &s : d.sections)
            if (s.num == it.key()) front = s.kind != "chapter";
        if (!front) storyStarts.insert(pageOfBlock(doc, it.value(), page.height()));
    }
    for (int p = 1; p < pages; ++p)
        if (!numbered[p - 1] && numbered[p] && !storyStarts.contains(p)) numbered[p] = false;

    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QPdfWriter writer(&buf);
    writer.setResolution(int(kDpi));
    writer.setPageLayout(QPageLayout(ps, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    writer.setTitle(d.title);
    writer.setCreator(d.author);
    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error) *error = t("Could not write the file");
        return {};
    }
    const QFont numFont(bodyFontFamily(o.bodyFont), 9);
    for (int p = 0; p < pages; ++p) {
        if (p) writer.newPage();
        painter.save();
        painter.translate(0, -p * page.height());
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.clip = QRectF(0, p * page.height(), page.width(), page.height());
        painter.setClipRect(ctx.clip);
        doc.documentLayout()->draw(&painter, ctx);
        painter.restore();
        if (numbered[p]) {
            painter.setFont(numFont);
            painter.setPen(QColor("#777"));
            painter.drawText(QRectF(0, page.height() - kDpi * 0.75, page.width(), kDpi * 0.4), Qt::AlignCenter,
                             QString::number(p + 1));
        }
    }
    painter.end();
    return bytes;
}

} // namespace neosea
