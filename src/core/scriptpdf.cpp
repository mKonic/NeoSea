#include "core/scriptpdf.h"

#include "core/fonts.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextLayout>

namespace neosea::sp {

namespace {

constexpr qreal kPt = 12;          // the type, and one line
constexpr qreal kInch = 72;
constexpr qreal kLeft = 1.5 * kInch;
constexpr qreal kTop = 1 * kInch;

struct Geometry {
    qreal indent, width; // in em of the type
};

Geometry geometryOf(const QString &type)
{
    if (type == "character") return {13.2, 23.1};
    if (type == "paren") return {9.6, 15.3};
    if (type == "dialogue") return {6, 21.3};
    return {0, 36.3};
}

const QStringList kCaps{"heading", "character", "transition", "shot"};

QFont scriptFont(bool bold = false, bool italic = false)
{
    QFont f(kScriptFamily);
    f.setPointSizeF(kPt);
    f.setBold(bold);
    f.setItalic(italic);
    f.setStyleHint(QFont::Monospace);
    return f;
}

// a line's text and its formats, capitals where the element is set in them
QString lineText(const PrintLine &l, QList<QTextLayout::FormatRange> *formats)
{
    const bool caps = kCaps.contains(l.type);
    QString text;
    for (const Run &r : l.runs) {
        if (r.mark) continue;
        const QString piece = caps ? r.text.toUpper() : r.text;
        if (formats && (r.b || r.i || r.u || r.s || l.type == "heading")) {
            QTextLayout::FormatRange fr;
            fr.start = int(text.size());
            fr.length = int(piece.size());
            QFont f(scriptFont(r.b || l.type == "heading", r.i), pointDevice());
            f.setUnderline(r.u);
            f.setStrikeOut(r.s);
            fr.format.setFont(f);
            formats->append(fr);
        }
        text += piece;
    }
    text.replace(QChar(0x2028), '\n');
    if (l.contd) text += " (CONT'D)";
    return text;
}

void layoutLine(QTextLayout &layout, const PrintLine &l, int *lineCount)
{
    QList<QTextLayout::FormatRange> formats;
    QString text = lineText(l, &formats);
    text.replace('\n', QChar::LineSeparator);
    layout.setText(text);
    layout.setFont(QFont(scriptFont(l.type == "heading"), pointDevice()));
    layout.setFormats(formats);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setAlignment(l.type == "transition" ? Qt::AlignRight : Qt::AlignLeft);
    layout.setTextOption(opt);
    const Geometry g = geometryOf(l.type);
    const qreal width = g.width * kPt;
    layout.beginLayout();
    int n = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, n * kPt));
        n++;
    }
    layout.endLayout();
    if (lineCount) *lineCount = std::max(1, n);
}

} // namespace

QList<PrintLine> printLines(const QList<Para> &paras)
{
    QList<PrintLine> out;
    QList<Line> plain;
    for (const Para &p : paras) {
        if (p.hasClass("ghost")) continue;
        PrintLine l;
        l.type = typeOf(p);
        for (const Run &r : p.runs)
            if (!r.mark && !r.text.isEmpty()) l.runs << r;
        // trailing spaces don't print
        while (!l.runs.isEmpty()) {
            QString &t = l.runs.last().text;
            while (!t.isEmpty() && t.back().isSpace()) t.chop(1);
            if (!t.isEmpty()) break;
            l.runs.removeLast();
        }
        out << l;
        plain << Line{l.type, p.text()};
    }
    for (qsizetype i = 0; i < out.size(); ++i) out[i].contd = out[i].type == "character" && contd(plain, i);
    // blank lines only count between the first and the last line
    QList<PrintLine> kept;
    for (qsizetype i = 0; i < out.size(); ++i) {
        QString text;
        for (const Run &r : out[i].runs) text += r.text;
        if (!text.trimmed().isEmpty() || (i > 0 && i < out.size() - 1)) kept << out[i];
    }
    return kept;
}

QList<int> measureLines(const QList<PrintLine> &lines)
{
    registerBundledFonts();
    QList<int> out;
    for (const PrintLine &l : lines) {
        QTextLayout layout;
        int n = 1;
        layoutLine(layout, l, &n);
        out << n;
    }
    return out;
}

QByteArray buildScriptPdf(const QList<PrintLine> &lines, const TitlePage &title)
{
    registerBundledFonts();
    const QList<int> counts = measureLines(lines);
    QList<Item> items;
    for (qsizetype i = 0; i < lines.size(); ++i) items << Item{lines[i].type, counts[i]};
    const Pages pg = paginate(items);

    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QPdfWriter writer(&buf);
    writer.setResolution(int(kInch));
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF()));
    writer.setTitle(title.title);
    writer.setCreator(title.author);
    QPainter p;
    if (!p.begin(&writer)) return {};
    p.setPen(Qt::black);

    // the title page
    {
        const QRectF main(kLeft, 3.5 * kInch, 6 * kInch, 3 * kInch);
        p.setFont(scriptFont());
        qreal y = main.top();
        auto centered = [&](const QString &s) {
            p.drawText(QRectF(main.left(), y, main.width(), kPt * 1.2), Qt::AlignHCenter | Qt::AlignTop, s);
            y += kPt;
        };
        centered(title.title.toUpper());
        if (!title.credit.isEmpty()) {
            y += 2 * kPt;
            centered(title.credit);
            y += kPt;
        } else {
            y += 2 * kPt;
        }
        centered(title.author);
        auto block = [&](const QString &s, qreal x, qreal width, Qt::Alignment a) {
            const QStringList rows = s.split('\n', Qt::SkipEmptyParts);
            qreal top = 11 * kInch - kInch - rows.size() * kPt;
            for (const QString &r : rows) {
                p.drawText(QRectF(x, top, width, kPt * 1.2), a | Qt::AlignTop, r.trimmed());
                top += kPt;
            }
        };
        block(title.contact, kLeft, 3.5 * kInch, Qt::AlignLeft);
        block(title.draft, 8.5 * kInch - kInch - 2.5 * kInch, 2.5 * kInch, Qt::AlignRight);
    }

    int page = 0;
    qreal y = kTop;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const Placed &a = pg.at[i];
        if (a.page != page) {
            writer.newPage();
            page = a.page;
            y = kTop;
            if (page > 1) {
                p.setFont(scriptFont());
                p.drawText(QRectF(0, 0.5 * kInch, 8.5 * kInch - kInch, kPt * 1.2), Qt::AlignRight | Qt::AlignTop,
                           QString::number(page) + ".");
            }
        }
        y += a.before * kPt;
        QTextLayout layout;
        int n = 1;
        layoutLine(layout, lines[i], &n);
        const Geometry g = geometryOf(lines[i].type);
        // a paragraph longer than the page runs on: draw it line by line
        for (int k = 0; k < layout.lineCount(); ++k) {
            if (y + kPt > kTop + kLinesPerPage * kPt + 0.1) {
                writer.newPage();
                page++;
                y = kTop;
                p.setFont(scriptFont());
                p.drawText(QRectF(0, 0.5 * kInch, 8.5 * kInch - kInch, kPt * 1.2), Qt::AlignRight | Qt::AlignTop,
                           QString::number(page) + ".");
            }
            QTextLine line = layout.lineAt(k);
            line.draw(&p, QPointF(kLeft + g.indent * kPt, y - line.y()));
            y += kPt;
        }
    }
    p.end();
    return bytes;
}

} // namespace neosea::sp
