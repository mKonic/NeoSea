#include "app/chapteredit.h"

#include "core/chapter.h"
#include "core/pagelayout.h"
#include "core/textdoc.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextDocumentFragment>

namespace neosea {

namespace {

bool isEnter(int k) { return k == Qt::Key_Return || k == Qt::Key_Enter; }

// the ⚑ beside the cursor, if there is one: its sticky id
QString markAt(QTextDocument *d, int pos)
{
    if (pos < 0 || pos >= d->characterCount() - 1) return {};
    // a cursor's format is the character's to its left: stand just after it
    QTextCursor at(d);
    at.setPosition(pos + 1);
    const QTextCharFormat cf = at.charFormat();
    return cf.hasProperty(doc::MarkSid) ? cf.property(doc::MarkSid).toString() : QString();
}

void insertRuns(QTextCursor &c, const QList<Run> &runs)
{
    for (const Run &r : runs) {
        if (r.mark) {
            c.insertText(QStringLiteral("⚑"), doc::markFormat(r.sid));
            continue;
        }
        QTextCharFormat f;
        f.setFontWeight(r.b ? QFont::Bold : QFont::Normal);
        f.setFontItalic(r.i);
        f.setFontUnderline(r.u);
        f.setFontStrikeOut(r.s);
        QString text = r.text;
        text.replace(QChar(0x2028), QChar::LineSeparator);
        c.insertText(text, f);
    }
}

} // namespace

ChapterEdit::ChapterEdit(ChapterHost *host, QString chId, QWidget *parent)
    : QTextEdit(parent), m_host(host), m_chId(std::move(chId))
{
    auto *d = new QTextDocument(this);
    m_layout = new PageLayout(d);
    d->setDocumentLayout(m_layout);
    d->setDocumentMargin(0);
    d->setUndoRedoEnabled(true);
    setDocument(d);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::NoWrap); // the layout wraps; QTextEdit mustn't set widths of its own
    setAcceptRichText(true);
    setTabChangesFocus(false);
    viewport()->setAutoFillBackground(false);
    setStyleSheet("QTextEdit { background: transparent; border: none; }");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_layout, &QAbstractTextDocumentLayout::documentSizeChanged, this, [this](const QSizeF &s) {
        const int h = int(std::ceil(s.height())) + 2;
        if (h != height()) {
            setFixedHeight(h);
            emit heightChanged();
        }
    });
    connect(d, &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (m_loading || (removed == 0 && added == 0)) return;
        m_host->chapterEdited(this);
    });
}

void ChapterEdit::loadHtml(const QString &html)
{
    m_loading = true;
    doc::loadHtml(*document(), html);
    // prose that got merged into a break's line is prose again
    if (edit::healBreaks(*document())) document()->clearUndoRedoStacks();
    m_loading = false;
    m_layout->relayout();
}

QString ChapterEdit::currentHtml() const { return doc::html(*document()); }

void ChapterEdit::resetUndo() { document()->clearUndoRedoStacks(); }

QSize ChapterEdit::sizeHint() const
{
    return QSize(400, int(std::ceil(m_layout->documentSize().height())) + 2);
}

void ChapterEdit::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    if (document()->textWidth() != viewport()->width()) document()->setTextWidth(viewport()->width());
}

void ChapterEdit::focusStart()
{
    setFocus();
    QTextCursor c(document());
    c.movePosition(QTextCursor::Start);
    setTextCursor(c);
}

void ChapterEdit::focusEnd()
{
    setFocus();
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
}

void ChapterEdit::placeCaret(int block, int offset)
{
    QTextBlock b = document()->findBlockByNumber(block);
    if (!b.isValid()) b = document()->lastBlock();
    QTextCursor c(b);
    c.setPosition(b.position() + std::clamp(offset, 0, b.length() - 1));
    setTextCursor(c);
}

QPair<int, int> ChapterEdit::caretAddress() const
{
    const QTextCursor c = textCursor();
    return {c.blockNumber(), c.positionInBlock()};
}

edit::Context ChapterEdit::context()
{
    edit::Context ctx;
    ctx.enterRun = m_enterRun;
    ctx.story = m_host->isStoryChapter(m_chId);
    ctx.snapshot = [this](const QString &label, bool rejoin) { m_host->snapshot(label, rejoin); };
    return ctx;
}

void ChapterEdit::afterStructural(edit::Result r)
{
    if (r != edit::Result::Structural) return;
    resetUndo();
    m_breakRun++;
}

void ChapterEdit::ghostToProse()
{
    // the moment writing hits a ghost, it becomes prose (its id stays, so the
    // outline knows the section has been written)
    QTextBlock b = textCursor().block();
    QStringList cls = doc::classes(b);
    if (!cls.removeAll("ghost")) return;
    doc::setClasses(b, cls);
}

void ChapterEdit::keyPressEvent(QKeyEvent *e)
{
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool cmd = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    const int key = e->key();
    const bool modifierOnly = key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta;
    if (modifierOnly) {
        QTextEdit::keyPressEvent(e);
        return;
    }

    // ⌘Z: what a smart key just did comes back as typed; right after a break,
    // the structure goes back; otherwise the document's own undo
    if (cmd && !shift && key == Qt::Key_Z) {
        if (m_just.kind != edit::Undoable::None) {
            QTextCursor c = textCursor();
            edit::revert(c, m_just);
            setTextCursor(c);
            m_just = {};
            return;
        }
        if (m_breakRun > 0 && m_host->structuralUndo()) {
            m_breakRun--;
            return;
        }
        // nothing of the chapter's own to take back: the structure's last change is
        if (!document()->isUndoAvailable() && m_host->structuralUndo()) return;
        QTextEdit::keyPressEvent(e);
        return;
    }
    m_just = {};
    if (isEnter(key) && !shift && !cmd) m_enterRun++;
    else m_enterRun = 0;
    if (!cmd || !(key == Qt::Key_Z || key == Qt::Key_Y)) m_breakRun = 0;

    if (m_host->isScriptBook() && m_host->scriptKey(this, e)) return;

    QTextCursor c = textCursor();
    if (isEnter(key)) {
        edit::Context ctx = context();
        edit::Result r = edit::Result::NotHandled;
        if (m_host->isScriptBook()) {
            // a script's lines are its elements: Enter is only a new line here
            c.insertBlock();
            setTextCursor(c);
            return;
        }
        if (cmd && shift) r = edit::poetryEnter(c, ctx);
        else if (shift) {
            r = doc::hasClass(c.block(), "poetry") ? edit::poetryEnter(c, ctx) : edit::shiftEnter(c, ctx);
        } else if (cmd) {
            QTextEdit::keyPressEvent(e); // ⌘Enter: the window's (full screen)
            return;
        } else {
            edit::dashBeforeEnter(c, m_host->typingContext(this));
            r = edit::enter(c, ctx);
        }
        if (r == edit::Result::NotHandled) {
            c.insertBlock();
            setTextCursor(c);
            ensureCursorVisible();
            return;
        }
        setTextCursor(c);
        if (r == edit::Result::SplitChapter) {
            m_host->splitChapter(this, c.blockNumber());
            return;
        }
        afterStructural(r);
        return;
    }
    if (key == Qt::Key_Backspace && !cmd && !alt) {
        const edit::Result r = edit::backspace(c, context());
        if (r == edit::Result::EmptyChapter) {
            m_host->removeEmptyChapter(this);
            return;
        }
        if (r == edit::Result::ChapterStart) {
            m_host->backspaceAtStart(this);
            return;
        }
        if (r != edit::Result::NotHandled) {
            setTextCursor(c);
            afterStructural(r);
            return;
        }
        // a placeholder's ⚑ goes with its note
        if (!c.hasSelection()) {
            const QString sid = markAt(document(), c.position() - 1);
            if (!sid.isEmpty()) {
                m_host->markResolved(sid);
                return;
            }
        }
        QTextEdit::keyPressEvent(e);
        return;
    }
    if (key == Qt::Key_Delete && !cmd && !alt) {
        const edit::Result r = edit::forwardDelete(c, context());
        if (r != edit::Result::NotHandled) {
            setTextCursor(c);
            afterStructural(r);
            return;
        }
        if (!c.hasSelection()) {
            const QString sid = markAt(document(), c.position());
            if (!sid.isEmpty()) {
                m_host->markResolved(sid);
                return;
            }
        }
        QTextEdit::keyPressEvent(e);
        return;
    }
    if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && !cmd && !alt) {
        if (key == Qt::Key_Tab && !shift) {
            c.insertText(QStringLiteral("  "));
        } else {
            // up to two em spaces before the caret come out
            int n = 0;
            const QString before = edit::textBefore(c);
            while (n < 2 && before.size() > n && before[before.size() - 1 - n] == QChar(0x2003)) n++;
            if (n) {
                c.setPosition(c.position() - n, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        }
        setTextCursor(c);
        return;
    }
    if (cmd && !alt && (key == Qt::Key_B || key == Qt::Key_I || key == Qt::Key_U || (shift && key == Qt::Key_S))) {
        QTextCharFormat f;
        const QTextCharFormat cur = c.charFormat();
        if (key == Qt::Key_B && !shift) f.setFontWeight(cur.fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        else if (key == Qt::Key_I && !shift) f.setFontItalic(!cur.fontItalic());
        else if (key == Qt::Key_U && !shift) f.setFontUnderline(!cur.fontUnderline());
        else if (key == Qt::Key_S && shift) f.setFontStrikeOut(!cur.fontStrikeOut());
        else {
            QTextEdit::keyPressEvent(e);
            return;
        }
        c.mergeCharFormat(f);
        mergeCurrentCharFormat(f);
        setTextCursor(c);
        return;
    }
    const QString text = e->text();
    if (!cmd && !text.isEmpty() && text[0].isPrint()) {
        ghostToProse();
        c = textCursor();
        // a *** line takes no words: they go on a line of their own
        if (!c.hasSelection() && edit::stepOffBreak(c)) setTextCursor(c);
        edit::TypingContext tc = m_host->typingContext(this);
        if (text == "\"") tc.doubleQuotes = m_host->bookQuoteStyle(this);
        if (edit::typeKey(c, text, tc, &m_just)) {
            setTextCursor(c);
            ensureCursorVisible();
            return;
        }
        // a dash set before the key went in: the key itself goes in as usual
        if (m_just.kind != edit::Undoable::None) setTextCursor(c);
    }
    QTextEdit::keyPressEvent(e);
}

void ChapterEdit::inputMethodEvent(QInputMethodEvent *e)
{
    ghostToProse();
    QTextEdit::inputMethodEvent(e);
}

void ChapterEdit::mousePressEvent(QMouseEvent *e)
{
    m_enterRun = 0;
    QTextEdit::mousePressEvent(e);
    // clicking a ghost outline note selects it, ready to be written over
    QTextCursor c = textCursor();
    if (!c.hasSelection() && doc::hasClass(c.block(), "ghost")) {
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        setTextCursor(c);
    }
}

void ChapterEdit::focusInEvent(QFocusEvent *e)
{
    QTextEdit::focusInEvent(e);
    m_host->chapterFocused(this);
}

void ChapterEdit::wheelEvent(QWheelEvent *e)
{
    // the page scrolls, not the chapter
    e->ignore();
}

bool ChapterEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasText() || source->hasHtml();
}

void ChapterEdit::insertFromMimeData(const QMimeData *source)
{
    const edit::TypingContext tc = m_host->typingContext(this);
    const typing::DashStyle dashes = typing::dashStyle(tc.language);
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.hasSelection()) c.removeSelectedText();
    ghostToProse();
    const QString before = edit::textBefore(c);
    QTextCursor post = c;
    post.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const typing::Edges edges{before.trimmed().isEmpty(), post.selectedText().trimmed().isEmpty(),
                              !before.isEmpty() && before.back().isSpace()};
    QList<QList<Run>> paras;
    if (source->hasFormat("application/x-neo-html")) {
        for (const Para &p : parseChapter(QString::fromUtf8(source->data("application/x-neo-html")))) paras << p.runs;
    } else if (source->hasHtml()) {
        const QList<Para> clean = cleanPaste(source->html());
        for (qsizetype i = 0; i < clean.size(); ++i) {
            QList<Run> runs = clean[i].runs;
            typing::dashRuns(runs, dashes, {i > 0 || edges.start, i < clean.size() - 1 || edges.end, i == 0 && edges.spaced});
            paras << runs;
        }
    } else {
        QStringList lines = source->text().remove('\r').split(QRegularExpression("\\n+"));
        lines.removeIf([](const QString &l) { return l.trimmed().isEmpty(); });
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString line = typing::dialogueDashes(lines[i].trimmed(), dashes,
                                                        {i > 0 || edges.start, i < lines.size() - 1 || edges.end, i == 0 && edges.spaced});
            // plain text written in Markdown keeps its *italics* and **bold**
            if (tc.markdown)
                if (auto runs = typing::markdownInline(line)) {
                    paras << *runs;
                    continue;
                }
            Run r;
            r.text = line;
            paras << QList<Run>{r};
        }
    }
    for (qsizetype i = 0; i < paras.size(); ++i) {
        if (i > 0) c.insertBlock();
        insertRuns(c, paras[i]);
    }
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
}

QMimeData *ChapterEdit::createMimeDataFromSelection() const
{
    auto *mime = new QMimeData;
    const QTextCursor c = textCursor();
    QTextDocument tmp;
    QTextCursor tc(&tmp);
    tc.insertFragment(c.selection());
    const QString html = doc::html(tmp);
    QString plain = c.selection().toPlainText();
    plain.replace(QChar::ParagraphSeparator, '\n').replace(QChar::LineSeparator, '\n');
    plain.remove(QStringLiteral("⚑"));
    mime->setText(plain);
    mime->setHtml(html);
    mime->setData("application/x-neo-html", html.toUtf8());
    return mime;
}

} // namespace neosea

#include "moc_chapteredit.cpp"
