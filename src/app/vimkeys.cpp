#include "app/vimkeys.h"

#include "app/app.h"
#include "app/auxpage.h"
#include "app/chapteredit.h"
#include "app/editorview.h"
#include "app/searchbar.h"
#include "app/theme.h"
#include "core/i18n.h"
#include "core/pagelayout.h"
#include "core/search.h"
#include "core/sentences.h"
#include "core/vim.h"

#include <QApplication>
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>

namespace neosea {

VimKeys::VimKeys(EditorView *view) : QObject(view), m_view(view)
{
    m_enabled = view->app()->library().value("vimKeys").toBool();
    // leaving the page for a title, a card or a field lets moving mode go
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (m_nav && now && !qobject_cast<QTextEdit *>(now)) {
            setNav(false);
        }
    });
}

void VimKeys::watch(QTextEdit *e)
{
    e->installEventFilter(this);
    paint();
}

void VimKeys::toggle()
{
    m_enabled = !m_enabled;
    m_view->app()->library().insert("vimKeys", m_enabled);
    m_view->app()->writeLibrary();
    if (!m_enabled) setNav(false);
    else rest();
    emit m_view->app()->toast(m_enabled ? t("Vim keys on — Esc to move, i to write") : t("Vim keys off"));
}

void VimKeys::rest()
{
    if (!m_enabled) return;
    setNav(true, qobject_cast<QTextEdit *>(QApplication::focusWidget()));
}

void VimKeys::setNav(bool on, QTextEdit *e)
{
    m_nav = on && m_enabled;
    m_visual = false;
    m_count.clear();
    m_pending.clear();
    if (e) m_page = e;
    if (!m_nav)
        if (QTextEdit *p = m_page) {
            QTextCursor c = p->textCursor();
            if (c.hasSelection()) {
                c.setPosition(c.selectionEnd());
                p->setTextCursor(c);
            }
        }
    paint();
}

void VimKeys::paint()
{
    BookSession *s = m_view->app()->session();
    if (!s) return;
    for (const QString &chId : s->order())
        if (ChapterEdit *e = m_view->editorFor(chId)) e->pageLayout()->setCaretColor(m_nav ? theme().accent : QColor());
    if (QTextEdit *n = m_view->auxPage() ? m_view->auxPage()->notesEdit() : nullptr) n->setCursorWidth(m_nav ? 3 : 1);
}

bool VimKeys::eventFilter(QObject *o, QEvent *ev)
{
    auto *e = qobject_cast<QTextEdit *>(o);
    if (!m_enabled || !e || (ev->type() != QEvent::KeyPress && ev->type() != QEvent::ShortcutOverride)) return false;
    auto *k = static_cast<QKeyEvent *>(ev);
    const Qt::KeyboardModifiers mods = k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool esc = k->key() == Qt::Key_Escape;
    if (ev->type() == QEvent::ShortcutOverride) {
        // moving, the page's letters and Esc are vim's, not a menu's
        if (esc || (m_nav && !mods && !k->text().isEmpty())) ev->accept();
        return false;
    }
    m_page = e;
    if (!m_nav) {
        // Esc while writing: start moving
        if (esc && !(k->modifiers() & ~Qt::KeypadModifier)) {
            setNav(true, e);
            return true;
        }
        return false;
    }
    if (esc) {
        // only lets go of a selection or a half-typed command; never closes the book
        if (m_visual) {
            QTextCursor c = e->textCursor();
            c.setPosition(c.position());
            e->setTextCursor(c);
        }
        m_visual = false;
        m_count.clear();
        m_pending.clear();
        return true;
    }
    if (mods == Qt::ControlModifier && (k->key() == Qt::Key_D || k->key() == Qt::Key_U)) {
        halfPage(e, k->key() == Qt::Key_D ? 1 : -1);
        return true;
    }
    if (mods) return false;
    // arrows, Home, End, Page Up and Down still move as they always do
    const bool named = k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter || k->key() == Qt::Key_Backspace || k->key() == Qt::Key_Delete || k->key() == Qt::Key_Tab;
    if (k->text().isEmpty() && !named) return false;
    key(e, k);
    return true; // nothing a keyboard types lands in the text while moving
}

bool VimKeys::move(QTextEdit *e, QTextCursor::MoveOperation op, int times)
{
    QTextCursor c = e->textCursor();
    const int was = c.position();
    c.movePosition(op, m_visual ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor, times);
    e->setTextCursor(c);
    return c.position() != was;
}

void VimKeys::line(QTextEdit *e, bool down, bool paragraph)
{
    QTextCursor c = e->textCursor();
    bool moved;
    if (!paragraph) moved = move(e, down ? QTextCursor::Down : QTextCursor::Up);
    else if (down) moved = move(e, QTextCursor::NextBlock);
    else moved = move(e, c.positionInBlock() > 0 ? QTextCursor::StartOfBlock : QTextCursor::PreviousBlock);
    // a move that can't go further steps into the next chapter
    auto *ce = qobject_cast<ChapterEdit *>(e);
    BookSession *s = m_view->app()->session();
    if (moved || m_visual || !ce || !s) return;
    const QStringList order = s->order();
    const int i = int(order.indexOf(ce->chId())) + (down ? 1 : -1);
    if (i < 0 || i >= order.size()) return;
    ChapterEdit *next = m_view->editorFor(order[i]);
    if (!next || next->isHidden()) return;
    m_view->focusChapter(order[i], !down);
    m_page = next;
}

void VimKeys::word(QTextEdit *e, QChar k)
{
    const QTextCursor c = e->textCursor();
    const QString text = c.block().text();
    const int at = c.positionInBlock();
    const int n = vim::wordSteps(k, text.left(at), text.mid(at));
    if (n >= 0) {
        move(e, k == 'b' ? QTextCursor::Left : QTextCursor::Right, n);
        return;
    }
    if (k == 'w') move(e, QTextCursor::NextBlock);
    else if (k == 'e') move(e, QTextCursor::EndOfBlock);
    else move(e, QTextCursor::Left); // to the end of the paragraph before
}

void VimKeys::sentence(QTextEdit *e, bool forward)
{
    QTextCursor c = e->textCursor();
    const QString text = c.block().text();
    const int at = c.positionInBlock();
    const QString lang = m_view->app()->writingLanguage();
    const auto [from, to] = sentenceAt(text, at, lang);
    int target;
    if (forward) {
        int n = to;
        while (n < text.size() && text[n].isSpace()) n++;
        if (n >= text.size() || n <= at) {
            move(e, QTextCursor::NextBlock);
            return;
        }
        target = n;
    } else if (at > from) {
        target = from;
    } else if (from > 0) {
        target = sentenceAt(text, from - 1, lang).first;
    } else {
        if (!move(e, QTextCursor::PreviousBlock)) return;
        const QTextCursor p = e->textCursor();
        const QString pt = p.block().text();
        target = sentenceAt(pt, int(pt.size()), lang).first;
        QTextCursor q = e->textCursor();
        q.setPosition(p.block().position() + target, m_visual ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        e->setTextCursor(q);
        return;
    }
    c.setPosition(c.block().position() + target, m_visual ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    e->setTextCursor(c);
}

void VimKeys::halfPage(QTextEdit *e, int dir)
{
    if (!qobject_cast<ChapterEdit *>(e)) {
        QScrollBar *b = e->verticalScrollBar();
        b->setValue(b->value() + dir * e->viewport()->height() / 2);
        e->setTextCursor(e->cursorForPosition(e->viewport()->rect().center()));
        return;
    }
    QScrollArea *sa = m_view->scrollArea();
    sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->value() + dir * sa->viewport()->height() / 2);
    // the caret follows to the same place on the screen
    const QPoint mid = sa->viewport()->rect().center();
    BookSession *s = m_view->app()->session();
    if (!s) return;
    for (const QString &chId : s->order()) {
        ChapterEdit *ce = m_view->editorFor(chId);
        if (!ce || ce->isHidden()) continue;
        const QPoint local = ce->viewport()->mapFrom(sa->viewport(), mid);
        if (!ce->viewport()->rect().contains(QPoint(ce->viewport()->width() / 2, local.y()))) continue;
        ce->setFocus();
        ce->setTextCursor(ce->cursorForPosition(QPoint(local.x(), local.y())));
        m_page = ce;
        return;
    }
}

void VimKeys::searchAgain(QTextEdit *e, int dir, int times)
{
    const QString q = m_view->searchBar()->query();
    if (q.isEmpty()) return;
    // every match in the tab, in reading order
    QList<QPair<QTextEdit *, search::Match>> found;
    BookSession *s = m_view->app()->session();
    QList<QTextEdit *> pages;
    if (qobject_cast<ChapterEdit *>(e) && s) {
        for (const QString &chId : s->order())
            if (ChapterEdit *ce = m_view->editorFor(chId); ce && !ce->isHidden()) pages << ce;
    } else {
        pages << e;
    }
    for (QTextEdit *p : pages)
        for (const search::Match &m : search::find(*p->document(), q)) found << qMakePair(p, m);
    if (found.isEmpty()) return;
    const int here = int(pages.indexOf(e));
    const int pos = e->textCursor().position();
    auto after = [&](const QPair<QTextEdit *, search::Match> &f) {
        const int pi = int(pages.indexOf(f.first));
        return pi > here || (pi == here && f.second.pos > pos);
    };
    auto before = [&](const QPair<QTextEdit *, search::Match> &f) {
        const int pi = int(pages.indexOf(f.first));
        return pi < here || (pi == here && f.second.pos < pos);
    };
    int i = -1;
    if (dir > 0) {
        for (int k = 0; k < found.size(); ++k)
            if (after(found[k])) { i = k; break; }
    } else {
        for (int k = int(found.size()) - 1; k >= 0; --k)
            if (before(found[k])) { i = k; break; }
    }
    if (i < 0) i = dir > 0 ? 0 : int(found.size()) - 1; // round to the top (or bottom), as vim does
    const int n = int(found.size());
    i = (((i + dir * (times - 1)) % n) + n) % n;
    QTextEdit *to = found[i].first;
    QTextCursor c = to->textCursor();
    if (m_visual && to == e) c.setPosition(found[i].second.pos, QTextCursor::KeepAnchor);
    else c.setPosition(found[i].second.pos);
    if (to != e) to->setFocus();
    to->setTextCursor(c);
    m_page = to;
}

void VimKeys::inclusive(QTextEdit *e)
{
    // vim's selections take in the letter under the caret, too
    QTextCursor c = e->textCursor();
    if (c.hasSelection() && c.anchor() < c.position()) {
        c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
    }
}

void VimKeys::key(QTextEdit *e, QKeyEvent *ke)
{
    QString k = ke->text();
    switch (ke->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: k = "Enter"; break;
    case Qt::Key_Backspace: k = "Backspace"; break;
    case Qt::Key_Delete: k = "Delete"; break;
    case Qt::Key_Tab: return;
    default: break;
    }
    // counts: 3w, 12j (a 0 on its own is the start of the line)
    if (k.size() == 1 && k[0].isDigit() && (k != "0" || !m_count.isEmpty())) {
        m_count += k;
        return;
    }
    const int times = std::clamp(m_count.isEmpty() ? 1 : m_count.toInt(), 1, 999);
    m_count.clear();
    const QString pending = std::exchange(m_pending, QString());
    auto reveal = [&] {
        QTextEdit *p = m_page ? m_page.data() : e;
        if (auto *ce = qobject_cast<ChapterEdit *>(p)) m_view->revealCaret(ce->chId());
        else p->ensureCursorVisible();
    };
    if (pending == "g") {
        if (k == "g") move(e, QTextCursor::Start);
        reveal();
        return;
    }
    if (pending == "[" || pending == "]") {
        if (k == pending) m_view->gotoChapter(k == "]" ? times : -times);
        return;
    }
    if (k == "h" || k == "Backspace") move(e, QTextCursor::Left, times);
    else if (k == "l" || k == " ") move(e, QTextCursor::Right, times);
    else if (k == "j" || k == "Enter") for (int i = 0; i < times; ++i) line(m_page ? m_page.data() : e, true, false);
    else if (k == "k") for (int i = 0; i < times; ++i) line(m_page ? m_page.data() : e, false, false);
    else if (k == "w" || k == "b" || k == "e") for (int i = 0; i < times; ++i) word(e, k[0]);
    else if (k == "0" || k == "^") move(e, QTextCursor::StartOfLine);
    else if (k == "$") move(e, QTextCursor::EndOfLine);
    else if (k == "(" || k == ")") for (int i = 0; i < times; ++i) sentence(e, k == ")");
    else if (k == "{" || k == "}") for (int i = 0; i < times; ++i) line(m_page ? m_page.data() : e, k == "}", true);
    else if (k == "G") move(e, QTextCursor::End);
    else if (k == "g" || k == "[" || k == "]") {
        m_pending = k;
        return;
    } else if (k == "v") {
        m_visual = !m_visual;
        if (!m_visual) {
            QTextCursor c = e->textCursor();
            c.setPosition(c.position());
            e->setTextCursor(c);
        }
    } else if (k == "y") {
        if (m_visual) {
            inclusive(e);
            e->copy();
            m_visual = false;
            QTextCursor c = e->textCursor();
            c.setPosition(c.selectionStart());
            e->setTextCursor(c);
        }
    } else if (k == "d" || k == "x" || k == "Delete") {
        if (m_visual) {
            inclusive(e);
            e->cut();
            m_visual = false;
        } else if (k != "d") {
            QTextCursor c = e->textCursor();
            c.beginEditBlock();
            for (int i = 0; i < times; ++i)
                if (c.block().position() + c.block().length() - 1 > c.position()) c.deleteChar();
            c.endEditBlock();
            e->setTextCursor(c);
        }
    } else if (k == "i") {
        setNav(false);
    } else if (k == "a") {
        setNav(false);
        move(e, QTextCursor::Right);
    } else if (k == "I") {
        setNav(false);
        move(e, QTextCursor::StartOfLine);
    } else if (k == "A") {
        setNav(false);
        move(e, QTextCursor::EndOfLine);
    } else if (k == "o" || k == "O") {
        setNav(false);
        QTextCursor c = e->textCursor();
        c.movePosition(k == "o" ? QTextCursor::EndOfBlock : QTextCursor::StartOfBlock);
        c.insertBlock();
        if (k == "O") c.movePosition(QTextCursor::Left);
        e->setTextCursor(c);
    } else if (k == "/") {
        setNav(false);
        m_view->searchBar()->openFromVim();
        return;
    } else if (k == "n" || k == "N") {
        searchAgain(e, k == "n" ? 1 : -1, times);
    } else {
        return;
    }
    reveal();
}

} // namespace neosea

#include "moc_vimkeys.cpp"
