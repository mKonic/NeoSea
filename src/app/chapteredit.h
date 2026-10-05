#pragma once
// One chapter's text on its sheet: a QTextEdit over the chapter's document,
// laid out by PageLayout, with NEO's keys. It grows to its text (the page
// scrolls, not the chapter) and hands structural moves (a new chapter, a merge,
// a deletion) to its host, which owns the book.

#include "core/editops.h"

#include <QTextEdit>

namespace neosea {

class PageLayout;
class ChapterEdit;

class ChapterHost {
public:
    virtual ~ChapterHost() = default;
    virtual bool isStoryChapter(const QString &chId) const = 0;
    virtual bool isScriptBook() const = 0;
    virtual edit::TypingContext typingContext(ChapterEdit *e) const = 0;
    virtual typing::QuoteStyle bookQuoteStyle(ChapterEdit *e) const = 0;
    virtual void snapshot(const QString &label, bool rejoin) = 0;
    virtual void splitChapter(ChapterEdit *e, int blockNumber) = 0;
    virtual void removeEmptyChapter(ChapterEdit *e) = 0;
    virtual void backspaceAtStart(ChapterEdit *e) = 0;
    virtual bool structuralUndo() = 0;
    virtual void markResolved(const QString &sid) = 0;
    virtual void chapterEdited(ChapterEdit *e) = 0;
    virtual void chapterFocused(ChapterEdit *e) = 0;
    virtual bool scriptKey(ChapterEdit *e, QKeyEvent *k) = 0;
};

class ChapterEdit : public QTextEdit {
    Q_OBJECT
public:
    ChapterEdit(ChapterHost *host, QString chId, QWidget *parent = nullptr);

    const QString &chId() const { return m_chId; }
    PageLayout *pageLayout() const { return m_layout; }
    void loadHtml(const QString &html);
    QString currentHtml() const;
    // a typing run Enters counted for the double and triple Enter; a click resets it
    void resetEnterRun() { m_enterRun = 0; }
    // the rearrangement just made isn't the document's own undo to replay
    void resetUndo();
    int &breakRun() { return m_breakRun; }

    void focusStart();
    void focusEnd();
    void placeCaret(int block, int offset);
    QPair<int, int> caretAddress() const; // block number, offset in it
    // the text a selection is being dragged out of, while it is
    static ChapterEdit *draggingFrom();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void heightChanged();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void ghostToProse();
    edit::Context context();
    void afterStructural(edit::Result r);

    ChapterHost *m_host;
    QString m_chId;
    PageLayout *m_layout;
    int m_enterRun = 0;
    bool m_dragArmed = false; // pressed in the selection: a drag, or a click
    QPoint m_dragFrom;
    bool inSelection(QPointF viewportPos) const;
    int m_breakRun = 0;
    bool m_loading = false;
    edit::Undoable m_just;
};

} // namespace neosea
