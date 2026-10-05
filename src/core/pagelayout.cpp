#include "core/pagelayout.h"

#include "core/textdoc.h"
#include "core/typing.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>

namespace neosea {

namespace {

bool isOpeningCandidate(const QTextBlock &b)
{
    const QStringList c = doc::classes(b);
    return !c.contains("poetry") && !c.contains("scene-break") && !c.contains("ghost");
}

// a script's element margins, in em of its type: indent and width
std::pair<qreal, qreal> scriptGeometry(const QStringList &classes)
{
    if (classes.contains("sp-character")) return {13.2, 23.1};
    if (classes.contains("sp-paren")) return {9.6, 15.3};
    if (classes.contains("sp-dialogue")) return {6, 21.3};
    return {0, 36.3};
}

int scriptBefore(const QStringList &classes)
{
    if (classes.contains("sp-heading")) return 2;
    if (classes.contains("sp-paren") || classes.contains("sp-dialogue")) return 0;
    return 1;
}

} // namespace

PageLayout::PageLayout(QTextDocument *d) : QAbstractTextDocumentLayout(d) {}

void PageLayout::setStyle(const PageStyle &s)
{
    m_style = s;
    relayout();
}

QFont PageLayout::bodyFont() const
{
    QFont f = document()->defaultFont();
    f.setPixelSize(std::max(1, int(std::lround(m_style.fontPx))));
    return f;
}

QString PageLayout::capText(const QTextBlock &b) const
{
    const QString text = doc::blockText(b);
    if (text.isEmpty() || text.front().isSpace()) return {};
    // an opening quote or bracket rides with the letter it opens, as ::first-letter takes it
    static const QString openers = QStringLiteral("\"'“‘„«([");
    qsizetype n = 0;
    while (n < text.size() && openers.contains(text[n])) ++n;
    if (n >= text.size()) return {};
    n += text[n].isHighSurrogate() && n + 1 < text.size() ? 2 : 1;
    return text.left(n);
}

void PageLayout::relayout()
{
    layoutAll();
}

void PageLayout::documentChanged(int, int, int)
{
    layoutAll();
}

void PageLayout::layoutAll()
{
    if (m_laying) return;
    m_laying = true;
    QTextDocument *d = document();
    // the opening paragraph: the first with words that isn't poetry, a break
    // or a ghost; in an empty chapter, the one waiting for the first word
    m_opening = -1;
    int firstCandidate = -1;
    int index = 0;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next(), ++index) {
        if (!isOpeningCandidate(b)) continue;
        if (firstCandidate < 0) firstCandidate = index;
        if (!doc::blockText(b).trimmed().isEmpty()) {
            m_opening = index;
            break;
        }
    }
    if (m_opening < 0) m_opening = firstCandidate;
    m_capRect = QRectF();
    m_cap.clear();

    m_geom.clear();
    qreal y = 0;
    index = 0;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next(), ++index) {
        const qreal h = layoutBlock(b, y, index);
        m_geom.append({y, h});
        y += h;
    }
    m_height = y;
    m_laying = false;
    const qreal width = d->textWidth() > 0 ? d->textWidth() : 600;
    emit documentSizeChanged(QSizeF(width, m_height));
    emit update(QRectF(0, 0, width, m_height + 1));
}

qreal PageLayout::layoutBlock(const QTextBlock &b, qreal top, int index)
{
    QTextDocument *d = document();
    const qreal width = d->textWidth() > 0 ? d->textWidth() : 600;
    const qreal em = m_style.fontPx;
    const QStringList cls = doc::classes(b);
    const QTextBlock prev = b.previous();
    const QStringList prevCls = prev.isValid() ? doc::classes(prev) : QStringList{};
    QTextLayout *l = b.layout();
    const QString text = doc::blockText(b);

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(b.textDirection());
    Qt::Alignment align = b.blockFormat().alignment();
    QList<QTextLayout::FormatRange> extra;

    qreal topMargin = 0, bottomMargin = 0, left = 0, right = 0, indent = 0;
    qreal lh = em * m_style.lineHeight;
    bool cap = false;

    if (m_style.script) {
        // the script's own type: one line is one em, and the page's margins are in em
        const auto [ind, w] = scriptGeometry(cls);
        left = ind * em;
        right = std::max<qreal>(0, width - left - w * em);
        lh = em;
        topMargin = (prev.isValid() ? scriptBefore(cls) : 0) * em + m_style.scriptBreaks.value(index, 0);
        if (cls.contains("sp-transition")) align = Qt::AlignRight | Qt::AlignAbsolute;
        if (cls.contains("sp-heading")) {
            QTextLayout::FormatRange fr{0, int(b.length()), {}};
            fr.format.setFontWeight(QFont::Bold);
            extra << fr;
        }
        const bool caps = cls.contains("sp-heading") || cls.contains("sp-character") || cls.contains("sp-transition")
            || cls.contains("sp-shot");
        if (caps) {
            QTextLayout::FormatRange fr{0, int(b.length()), {}};
            fr.format.setFontCapitalization(QFont::AllUppercase);
            extra << fr;
        }
    } else if (cls.contains("scene-break")) {
        align = Qt::AlignHCenter;
        topMargin = bottomMargin = 1.6 * em;
        QTextLayout::FormatRange fr{0, int(b.length()), {}};
        fr.format.setForeground(m_style.muted);
        fr.format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        fr.format.setFontLetterSpacing(8);
        extra << fr;
    } else {
        const bool poetry = cls.contains("poetry");
        const bool prevPoetry = prevCls.contains("poetry");
        if (prev.isValid() && poetry != prevPoetry && !prevCls.contains("scene-break") && !cls.contains("scene-break"))
            topMargin = 0.9 * em;
        if (poetry) left = right = 2.5 * em;
        const bool centered = (align & Qt::AlignHCenter) || ((align & Qt::AlignRight) && !(align & Qt::AlignLeft));
        const bool ghost = cls.contains("ghost");
        if (m_style.story && !poetry && !ghost && !cls.contains("flush") && !centered) {
            const bool speech = typing::opensWithDash(text);
            const bool opening = index == m_opening;
            const bool afterBreak = prevCls.contains("scene-break");
            if ((!opening && !afterBreak) || speech) indent = 2 * em;
        }
        if (!m_style.story) {
            // a page a book carries, set the way it prints
            const QString k = m_style.pageKind;
            if (k == "dedication" || k == "epigraph" || k == "part") align = Qt::AlignHCenter;
            if ((k == "acknowledgments" || k == "about") && prev.isValid()) indent = 2 * em;
            if (k == "dedication" || k == "epigraph" || k == "part") {
                QTextLayout::FormatRange fr{0, int(b.length()), {}};
                fr.format.setFontItalic(true);
                extra << fr;
            }
        }
        if (ghost) {
            QTextLayout::FormatRange fr{0, int(b.length()), {}};
            fr.format.setFontItalic(true);
            fr.format.setForeground(m_style.ghost);
            extra << fr;
        }
        if (m_style.story && index == m_opening && !m_style.dropCapFamily.isEmpty() && !poetry && !ghost
            && !typing::opensWithDash(text)) {
            m_cap = capText(b);
            cap = !m_cap.isEmpty();
        }
    }
    opt.setAlignment(align);

    QFont capFont;
    qreal capW = 0, capBottom = 0;
    if (cap) {
        capFont = QFont(m_style.dropCapFamily);
        capFont.setPixelSize(int(std::lround(3.4 * em)));
        const QFontMetricsF cm(capFont);
        capW = cm.horizontalAdvance(m_cap) + 8;
        // the letter stays in the text, set at no width and no ink: the cap
        // is drawn over it, so selection, search and copy still see "The"
        const QFontMetricsF bm(bodyFont());
        QTextLayout::FormatRange fr{0, int(m_cap.size()), {}};
        fr.format.setForeground(Qt::transparent);
        fr.format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        fr.format.setFontLetterSpacing(-bm.horizontalAdvance(m_cap) / m_cap.size());
        extra << fr;
        capBottom = topMargin + 0.8 * 3.4 * em + 4;
    }

    l->setTextOption(opt);
    l->setFormats(extra);
    l->setPosition(QPointF(0, top));
    l->beginLayout();
    qreal y = topMargin;
    int n = 0;
    QList<qreal> baselines;
    for (;;) {
        QTextLine line = l->createLine();
        if (!line.isValid()) break;
        const bool rtl = b.textDirection() == Qt::RightToLeft;
        qreal x = left, w = width - left - right;
        if (n == 0) {
            w -= indent;
            if (!rtl) x += indent;
        }
        if (cap && y < capBottom) {
            x += capW;
            w -= capW;
        }
        line.setLineWidth(std::max<qreal>(w, em));
        // the line box is the line height, as CSS sets it; the glyphs sit centred in it
        const qreal glyph = line.ascent() + line.descent();
        const qreal lineH = lh;
        line.setPosition(QPointF(x, y + (lineH - glyph) / 2));
        baselines << y + (lineH - glyph) / 2 + line.ascent();
        y += lineH;
        n++;
    }
    l->endLayout();
    if (cap) {
        // the cap's foot sits on the second line's baseline
        const QFontMetricsF cm(capFont);
        const qreal baseline = baselines.size() > 1 ? baselines[1] : baselines.value(0) + lh;
        m_capRect = QRectF(0, top + baseline - cm.ascent(), capW, cm.ascent() + cm.descent());
        // a short opening paragraph still leaves the cap room
        y = std::max(y, capBottom);
    }
    return y + (index == m_spaceBlock ? m_space : bottomMargin);
}

void PageLayout::setSpaceAfter(int block, qreal px)
{
    if (block == m_spaceBlock && (block < 0 || qFuzzyCompare(px, m_space))) return;
    m_spaceBlock = block;
    m_space = px;
    relayout();
}

void PageLayout::draw(QPainter *painter, const PaintContext &ctx)
{
    QTextDocument *d = document();
    const QRectF clip = ctx.clip.isValid() ? ctx.clip : QRectF(0, 0, 1e9, 1e9);
    int index = 0;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next(), ++index) {
        if (index >= m_geom.size()) break;
        const BlockGeom &g = m_geom[index];
        if (g.top > clip.bottom()) break;
        if (g.top + g.height < clip.top()) continue;
        QTextLayout *l = b.layout();
        const int bpos = b.position();
        const int blen = b.length();
        QList<QTextLayout::FormatRange> sels;
        for (const Selection &s : ctx.selections) {
            const int a = std::max(s.cursor.selectionStart(), bpos);
            const int e = std::min(s.cursor.selectionEnd(), bpos + blen);
            if (e > a) sels << QTextLayout::FormatRange{a - bpos, e - a, s.format};
            else if (s.format.boolProperty(QTextFormat::FullWidthSelection) && s.cursor.block() == b)
                sels << QTextLayout::FormatRange{0, blen, s.format};
        }
        painter->setPen(m_style.ink);
        l->draw(painter, QPointF(0, 0), sels, clip);
        if (index == m_opening && !m_cap.isEmpty() && m_capRect.isValid()) {
            QFont capFont(m_style.dropCapFamily);
            capFont.setPixelSize(int(std::lround(3.4 * m_style.fontPx)));
            const QFontMetricsF cm(capFont);
            painter->setFont(capFont);
            painter->setPen(m_style.ink);
            painter->drawText(QPointF(m_capRect.left(), m_capRect.top() + cm.ascent()), m_cap);
        }
        if (ctx.cursorPosition >= bpos && ctx.cursorPosition < bpos + blen) {
            painter->setPen(m_style.caret);
            l->drawCursor(painter, QPointF(0, 0), ctx.cursorPosition - bpos, 1);
        }
    }
}

int PageLayout::hitTest(const QPointF &point, Qt::HitTestAccuracy) const
{
    QTextDocument *d = document();
    if (m_geom.isEmpty()) return 0;
    int index = 0;
    QTextBlock target = d->begin();
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next(), ++index) {
        if (index >= m_geom.size()) break;
        target = b;
        if (point.y() < m_geom[index].top + m_geom[index].height) break;
    }
    if (index == m_opening && m_capRect.contains(point)) return target.position();
    QTextLayout *l = target.layout();
    const qreal ly = point.y() - l->position().y();
    QTextLine hit;
    for (int i = 0; i < l->lineCount(); ++i) {
        QTextLine line = l->lineAt(i);
        hit = line;
        const qreal nextTop = i + 1 < l->lineCount() ? l->lineAt(i + 1).y() : 1e9;
        if (ly < nextTop) break;
    }
    if (!hit.isValid()) return target.position();
    int pos = hit.xToCursor(point.x() - l->position().x());
    return target.position() + std::clamp(pos, 0, target.length() - 1);
}

QSizeF PageLayout::documentSize() const
{
    const qreal width = document()->textWidth() > 0 ? document()->textWidth() : 600;
    return QSizeF(width, m_height);
}

QRectF PageLayout::frameBoundingRect(QTextFrame *) const { return QRectF(QPointF(0, 0), documentSize()); }

QRectF PageLayout::blockBoundingRect(const QTextBlock &block) const
{
    const int i = block.blockNumber();
    if (i < 0 || i >= m_geom.size()) return {};
    const qreal width = document()->textWidth() > 0 ? document()->textWidth() : 600;
    return QRectF(0, m_geom[i].top, width, m_geom[i].height);
}

} // namespace neosea

#include "moc_pagelayout.cpp"
