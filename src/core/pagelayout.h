#pragma once
// How a chapter's paragraphs sit on the page, the way NEO's stylesheet sets
// them: a first-line indent, none on the opening paragraph or after a scene
// break; the opening paragraph's first letter as a drop cap the text wraps
// around; *** centered with air around it; poetry pulled in from both margins;
// outline ghosts in grey italic. A script's elements sit at their margins in
// Courier, with page breaks drawn between the pages.
//
// Everything here is drawn, not stored: the document holds only NEO's
// paragraphs. Extra formats (search hits, spelling, focus dimming) come in
// through the paint context's selections, as QTextEdit's extra selections.

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFont>
#include <QList>
#include <QRectF>
#include <QStringList>

class QTextBlock;
class QTextLayout;

namespace neosea {

struct PageStyle {
    qreal fontPx = 17;             // the body's size, zoom included
    qreal lineHeight = 1.75;       // in em
    QString dropCapFamily;         // empty: no drop cap
    bool story = true;             // indents and the drop cap; a page a book carries has neither
    QString pageKind;              // copyright, dedication, epigraph, part, acknowledgments, about
    bool script = false;           // a screenplay: elements at their margins
    QColor ink = QColor("#1c1c1c");
    QColor muted = QColor("#888888");
    QColor ghost = QColor("#a9a294");
    QColor caret = QColor("#1c1c1c");
    QColor paper = QColor("#fbfaf7");
    // a script's page breaks: block number -> blank lines kept at the foot
    QHash<int, int> scriptBreaks;
};

class PageLayout : public QAbstractTextDocumentLayout {
    Q_OBJECT
public:
    explicit PageLayout(QTextDocument *doc);

    void setStyle(const PageStyle &s);
    const PageStyle &style() const { return m_style; }

    void draw(QPainter *painter, const PaintContext &context) override;
    int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const override;
    int pageCount() const override { return 1; }
    QSizeF documentSize() const override;
    QRectF frameBoundingRect(QTextFrame *frame) const override;
    QRectF blockBoundingRect(const QTextBlock &block) const override;

    // the paragraph that opens the chapter (the drop cap's), or -1
    int openingBlock() const { return m_opening; }
    // the drop cap's box, in document coordinates, when there is one
    QRectF dropCapRect() const { return m_capRect; }

    // room under one paragraph (the walking note's), in place of its own
    // bottom margin; block -1 takes it away
    void setSpaceAfter(int block, qreal px);
    int spaceBlock() const { return m_spaceBlock; }
    qreal spaceAfter() const { return m_spaceBlock >= 0 ? m_space : 0; }

    // Focus mode: everything faint but one paragraph, or a stretch of it.
    // block -1 dims the whole page (the caret is elsewhere); length -1 is the
    // whole paragraph.
    void setFocus(bool on, int block = -1, int start = 0, int length = -1);
    // vim's moving mode turns the caret gold
    void setCaretColor(const QColor &c); // invalid: the style's own
    bool focusOn() const { return m_focusOn; }
    QColor faint() const; // the ink focus mode fades the rest to

    // Relayout everything now (fonts or style changed)
    void relayout();

protected:
    void documentChanged(int from, int charsRemoved, int charsAdded) override;

private:
    struct BlockGeom {
        qreal top = 0, height = 0;
    };
    void layoutAll();
    qreal layoutBlock(const QTextBlock &b, qreal top, int index);
    QFont bodyFont() const;
    QString capText(const QTextBlock &b) const;

    PageStyle m_style;
    QList<BlockGeom> m_geom;
    qreal m_height = 0;
    int m_opening = -1;
    QColor m_caretOverride; // kept over style changes
    bool m_focusOn = false;
    int m_focusBlock = -1, m_focusStart = 0, m_focusLength = -1;
    int m_spaceBlock = -1;
    qreal m_space = 0;
    QRectF m_capRect;
    QString m_cap;
    bool m_laying = false;
};

} // namespace neosea
