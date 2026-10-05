#include "app/writingmodes.h"

#include "app/app.h"
#include "app/chapteredit.h"
#include "app/editorview.h"
#include "core/i18n.h"
#include "core/pagelayout.h"
#include "core/sentences.h"
#include "core/textdoc.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>

namespace neosea {

WritingModes::WritingModes(EditorView *view) : QObject(view), m_view(view)
{
    const QJsonObject &lib = view->app()->library();
    m_focus = lib.value("focus").toString("off");
    if (m_focus != "paragraph" && m_focus != "sentence") m_focus = "off";
    m_typewriter = lib.value("typewriter").toBool();
    m_soon.setSingleShot(true);
    m_soon.setInterval(0);
    connect(&m_soon, &QTimer::timeout, this, &WritingModes::updateFocus);
}

void WritingModes::watch(ChapterEdit *e)
{
    connect(e, &QTextEdit::cursorPositionChanged, this, [this, e] {
        m_last = e;
        m_soon.start();
        if (m_typewriter && m_byKeyboard) QTimer::singleShot(0, this, [this, e = QPointer<ChapterEdit>(e)] { if (e) follow(e); });
    });
    connect(e, &QTextEdit::textChanged, &m_soon, [this] { m_soon.start(); });
    e->installEventFilter(this);
    e->viewport()->installEventFilter(this);
}

bool WritingModes::eventFilter(QObject *, QEvent *e)
{
    if (e->type() == QEvent::KeyPress) {
        const auto *k = static_cast<QKeyEvent *>(e);
        if (!(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) m_byKeyboard = true;
    } else if (e->type() == QEvent::MouseButtonPress) {
        m_byKeyboard = false;
    }
    return false;
}

void WritingModes::setFocusLevel(const QString &level)
{
    if (level != "off" && level != "paragraph" && level != "sentence") return;
    m_focus = level;
    m_view->app()->library().insert("focus", level);
    m_view->app()->writeLibrary();
    updateFocus();
    emit m_view->app()->toast(level == "off" ? t("Focus mode off") : level == "sentence" ? t("Focus: sentence") : t("Focus: paragraph"));
}

void WritingModes::cycleFocus()
{
    setFocusLevel(m_focus == "off" ? "paragraph" : m_focus == "paragraph" ? "sentence" : "off");
}

void WritingModes::refresh()
{
    updateFocus();
    typewriterRoom();
}

void WritingModes::updateFocus()
{
    BookSession *s = m_view->app()->session();
    if (!s) return;
    const bool on = m_focus != "off";
    // the caret's page: the focused one, else the last that had it (title, panels: keep the last focus)
    ChapterEdit *here = qobject_cast<ChapterEdit *>(QApplication::focusWidget());
    if (!here) here = m_last;
    for (const QString &chId : s->order()) {
        ChapterEdit *e = m_view->editorFor(chId);
        if (!e) continue;
        if (!on || e != here) {
            e->pageLayout()->setFocus(on);
            continue;
        }
        const QTextCursor c = e->textCursor();
        const QTextBlock b = c.block();
        if (doc::hasClass(b, "scene-break")) e->pageLayout()->setFocus(true);
        else if (m_focus == "paragraph") e->pageLayout()->setFocus(true, b.blockNumber());
        else {
            const auto [from, to] = sentenceAt(b.text(), c.positionInBlock(), m_view->app()->writingLanguage());
            e->pageLayout()->setFocus(true, b.blockNumber(), from, to - from);
        }
    }
}

void WritingModes::toggleTypewriter()
{
    m_typewriter = !m_typewriter;
    m_view->app()->library().insert("typewriter", m_typewriter);
    m_view->app()->writeLibrary();
    typewriterRoom();
    emit m_view->app()->toast(m_typewriter ? t("Typewriter scrolling ON — your line stays centered") : t("Typewriter scrolling off"));
}

// The page needs empty room beneath its last line, or the caret can't rise to
// the writing height once the end of the draft scrolls into view.
void WritingModes::typewriterRoom()
{
    QScrollArea *sa = m_view->scrollArea();
    QLayout *outer = sa->widget()->layout();
    const QMargins m = outer->contentsMargins();
    const int room = m_typewriter ? std::max(120, int(sa->viewport()->height() * 0.55)) : 120;
    if (m.bottom() != room) outer->setContentsMargins(m.left(), m.top(), m.right(), room);
}

void WritingModes::follow(ChapterEdit *e)
{
    if (m_view->tab() != "manuscript" || e->textCursor().hasSelection()) return;
    QScrollArea *sa = m_view->scrollArea();
    const QRect r = e->cursorRect();
    const int top = e->viewport()->mapTo(sa->viewport(), r.topLeft()).y();
    const int diff = top - int(sa->viewport()->height() * 0.45);
    // a band of about three lines around the writing height
    if (std::abs(diff) <= int(r.height() * 1.5)) return;
    QScrollBar *bar = sa->verticalScrollBar();
    if (!m_glide) m_glide = new QPropertyAnimation(bar, "value", this);
    m_glide->stop();
    m_glide->setDuration(180);
    m_glide->setStartValue(bar->value());
    m_glide->setEndValue(std::clamp(bar->value() + diff, bar->minimum(), bar->maximum()));
    m_glide->setEasingCurve(QEasingCurve::OutCubic);
    m_glide->start();
}

} // namespace neosea

#include "moc_writingmodes.cpp"
